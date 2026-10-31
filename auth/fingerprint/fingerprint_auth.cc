#include "fingerprint_auth.h"

#include <gio/gio.h>
#include <spdlog/spdlog.h>

#include <thread>
#include <vector>

#include "status_reporter.h"

namespace biopass {

namespace {

const char* FPRINT_SERVICE = "net.reactivated.Fprint";
const char* FPRINT_MANAGER_PATH = "/net/reactivated/Fprint/Manager";
const char* FPRINT_MANAGER_INTERFACE = "net.reactivated.Fprint.Manager";
const char* FPRINT_DEVICE_INTERFACE = "net.reactivated.Fprint.Device";

struct AuthContext {
  GMainLoop* loop = nullptr;
  AuthResult result = AuthResult::Failure;
  std::string error_msg;
  bool debug = false;
  std::atomic<bool>* cancel_signal = nullptr;
  int verify_timeout_ms = 0;
  bool verify_timeout_fired = false;
};

void on_verify_status(GDBusConnection* connection, const gchar* sender_name,
                      const gchar* object_path, const gchar* interface_name,
                      const gchar* signal_name, GVariant* parameters, gpointer user_data) {
  AuthContext* ctx = static_cast<AuthContext*>(user_data);
  const gchar* result;
  gboolean done;

  g_variant_get(parameters, "(&sb)", &result, &done);
  std::string res_str = result;

  spdlog::debug("Fingerprint status: {}, done: {}", res_str, done);

  if (res_str == "verify-match") {
    ctx->result = AuthResult::Success;
    g_main_loop_quit(ctx->loop);
  } else if (res_str == "verify-no-match") {
    if (done) {
      ctx->result = AuthResult::Failure;
      g_main_loop_quit(ctx->loop);
    }
  } else if (res_str == "verify-unknown-error" || res_str == "verify-disconnected") {
    ctx->result = AuthResult::Unavailable;
    ctx->error_msg = res_str;
    g_main_loop_quit(ctx->loop);
  } else {
    // Retry scan, swipe too short, etc.
    if (done) {
      ctx->result = AuthResult::Retry;
      g_main_loop_quit(ctx->loop);
    }
  }
}

gboolean on_cancel_timeout(gpointer user_data) {
  AuthContext* ctx = static_cast<AuthContext*>(user_data);
  if (ctx->cancel_signal && ctx->cancel_signal->load()) {
    spdlog::debug("FingerprintAuth: Cancelled by another method");
    ctx->result = AuthResult::Failure;
    g_main_loop_quit(ctx->loop);
  }
  return G_SOURCE_CONTINUE;
}

gboolean on_verify_timeout(gpointer user_data) {
  AuthContext* ctx = static_cast<AuthContext*>(user_data);
  ctx->verify_timeout_fired = true;
  ctx->result = AuthResult::Retry;

  if (ctx->verify_timeout_ms > 0) {
    spdlog::debug("FingerprintAuth: Verification timed out after {} ms", ctx->verify_timeout_ms);
  } else {
    spdlog::debug("FingerprintAuth: Verification timed out");
  }

  g_main_loop_quit(ctx->loop);
  return G_SOURCE_REMOVE;
}

}  // namespace

bool FingerprintAuth::isAvailable() const {
  GError* error = nullptr;
  GDBusProxy* manager = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SYSTEM, G_DBUS_PROXY_FLAGS_NONE,
                                                      nullptr, FPRINT_SERVICE, FPRINT_MANAGER_PATH,
                                                      FPRINT_MANAGER_INTERFACE, nullptr, &error);
  if (error) {
    spdlog::error("FingerprintAuth: Failed to connect to fprintd manager: {}", error->message);
    g_error_free(error);
    return false;
  }

  GVariant* ret = g_dbus_proxy_call_sync(manager, "GetDefaultDevice", nullptr,
                                         G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);

  bool available = false;
  if (ret) {
    // Enrollment is checked per user in authenticate().
    available = true;
    g_variant_unref(ret);
  } else {
    if (error)
      g_error_free(error);
  }

  g_object_unref(manager);
  return available;
}

AuthResult FingerprintAuth::authenticate(const std::string& username, const AuthConfig& config,
                                         std::atomic<bool>* cancel_signal) {
  GError* error = nullptr;
  GDBusConnection* connection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &error);
  GDBusProxy* manager = nullptr;
  GDBusProxy* device = nullptr;
  bool device_claimed = false;
  bool verification_started = false;
  guint verify_status_subscription_id = 0;
  guint cancel_poll_source_id = 0;
  guint verify_timeout_source_id = 0;
  AuthContext ctx;

  auto cleanup = [&]() {
    if (cancel_poll_source_id != 0) {
      g_source_remove(cancel_poll_source_id);
      cancel_poll_source_id = 0;
    }

    if (verify_timeout_source_id != 0 && !ctx.verify_timeout_fired) {
      g_source_remove(verify_timeout_source_id);
      verify_timeout_source_id = 0;
    }

    if (verify_status_subscription_id != 0 && connection) {
      g_dbus_connection_signal_unsubscribe(connection, verify_status_subscription_id);
      verify_status_subscription_id = 0;
    }

    if (ctx.loop != nullptr) {
      g_main_loop_unref(ctx.loop);
      ctx.loop = nullptr;
    }

    if (verification_started && device) {
      GVariant* stop_ret = g_dbus_proxy_call_sync(device, "VerifyStop", nullptr,
                                                  G_DBUS_CALL_FLAGS_NONE, -1, nullptr, nullptr);
      if (stop_ret)
        g_variant_unref(stop_ret);
      verification_started = false;
    }

    if (device_claimed && device) {
      GVariant* release_ret = g_dbus_proxy_call_sync(device, "Release", nullptr,
                                                     G_DBUS_CALL_FLAGS_NONE, -1, nullptr, nullptr);
      if (release_ret)
        g_variant_unref(release_ret);
      device_claimed = false;
    }

    if (device) {
      g_object_unref(device);
      device = nullptr;
    }

    if (manager) {
      g_object_unref(manager);
      manager = nullptr;
    }

    if (connection) {
      g_object_unref(connection);
      connection = nullptr;
    }
  };

  if (!connection) {
    spdlog::error("FingerprintAuth: Failed to get system bus: {}", error->message);
    g_error_free(error);
    return AuthResult::Unavailable;
  }

  manager = g_dbus_proxy_new_sync(connection, G_DBUS_PROXY_FLAGS_NONE, nullptr, FPRINT_SERVICE,
                                  FPRINT_MANAGER_PATH, FPRINT_MANAGER_INTERFACE, nullptr, &error);

  if (!manager) {
    spdlog::error("FingerprintAuth: Failed to get manager proxy: {}", error->message);
    g_error_free(error);
    cleanup();
    return AuthResult::Unavailable;
  }

  GVariant* dev_ret = g_dbus_proxy_call_sync(manager, "GetDefaultDevice", nullptr,
                                             G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);
  if (!dev_ret) {
    spdlog::error("FingerprintAuth: No fingerprint device found: {}",
                  (error ? error->message : "Unknown"));
    if (error)
      g_error_free(error);
    cleanup();
    return AuthResult::Unavailable;
  }

  const gchar* device_path;
  g_variant_get(dev_ret, "(&o)", &device_path);
  std::string dev_path_str = device_path;
  g_variant_unref(dev_ret);

  device = g_dbus_proxy_new_sync(connection, G_DBUS_PROXY_FLAGS_NONE, nullptr, FPRINT_SERVICE,
                                 dev_path_str.c_str(), FPRINT_DEVICE_INTERFACE, nullptr, &error);

  if (!device) {
    spdlog::error("FingerprintAuth: Failed to get device proxy: {}", error->message);
    g_error_free(error);
    cleanup();
    return AuthResult::Unavailable;
  }

  GVariant* enrolled_ret =
      g_dbus_proxy_call_sync(device, "ListEnrolledFingers", g_variant_new("(s)", username.c_str()),
                             G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);

  if (!enrolled_ret) {
    spdlog::error(
        "FingerprintAuth: Failed to list enrolled fingers (user might not exist or permission "
        "denied): {}",
        (error ? error->message : ""));
    if (error)
      g_error_free(error);
    cleanup();
    return AuthResult::Unavailable;
  }

  GVariantIter* iter;
  gchar* finger_name;
  bool has_fingers = false;
  g_variant_get(enrolled_ret, "(as)", &iter);
  while (g_variant_iter_loop(iter, "s", &finger_name)) {
    has_fingers = true;
  }
  g_variant_iter_free(iter);
  g_variant_unref(enrolled_ret);

  if (!has_fingers) {
    spdlog::error("FingerprintAuth: User {} has no enrolled fingerprints.", username);
    cleanup();
    return AuthResult::Unavailable;
  }

  GVariant* claim_ret =
      g_dbus_proxy_call_sync(device, "Claim", g_variant_new("(s)", username.c_str()),
                             G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);

  if (!claim_ret) {
    spdlog::error("FingerprintAuth: Failed to claim device: {}", (error ? error->message : ""));
    if (error)
      g_error_free(error);
    cleanup();
    return AuthResult::Unavailable;
  }
  g_variant_unref(claim_ret);
  device_claimed = true;

  // "any" accepts any enrolled finger
  GVariant* verify_ret = g_dbus_proxy_call_sync(device, "VerifyStart", g_variant_new("(s)", "any"),
                                                G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);

  if (!verify_ret) {
    spdlog::error("FingerprintAuth: Failed to start verification: {}",
                  (error ? error->message : ""));
    if (error)
      g_error_free(error);
    cleanup();
    return AuthResult::Failure;
  }
  g_variant_unref(verify_ret);
  verification_started = true;

  // Announce once per session, only once the reader is actually listening.
  if (!scan_announced_) {
    scan_announced_ = true;
    reportInfo("Biopass: place your finger on the fingerprint reader");
  }

  ctx.loop = g_main_loop_new(nullptr, FALSE);
  ctx.debug = config.debug;
  ctx.cancel_signal = cancel_signal;
  ctx.verify_timeout_ms = config_.timeout;

  verify_status_subscription_id = g_dbus_connection_signal_subscribe(
      connection, FPRINT_SERVICE, FPRINT_DEVICE_INTERFACE, "VerifyStatus", dev_path_str.c_str(),
      nullptr, G_DBUS_SIGNAL_FLAGS_NONE, on_verify_status, &ctx, nullptr);

  cancel_poll_source_id = g_timeout_add(50, on_cancel_timeout, &ctx);

  if (config_.timeout > 0) {
    verify_timeout_source_id = g_timeout_add(config_.timeout, on_verify_timeout, &ctx);
    spdlog::debug("Waiting for fingerprint (timeout {} ms)...", config_.timeout);
  } else {
    spdlog::debug("Waiting for fingerprint...");
  }

  g_main_loop_run(ctx.loop);

  cleanup();
  return ctx.result;
}

std::vector<std::string> FingerprintAuth::listEnrolledFingers(const std::string& username) {
  std::vector<std::string> enrolled_fingers;
  GError* error = nullptr;
  GDBusConnection* connection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &error);
  if (!connection) {
    spdlog::error("FingerprintAuth: Failed to get system bus: {}", error->message);
    g_error_free(error);
    return enrolled_fingers;
  }

  GDBusProxy* manager =
      g_dbus_proxy_new_sync(connection, G_DBUS_PROXY_FLAGS_NONE, nullptr, FPRINT_SERVICE,
                            FPRINT_MANAGER_PATH, FPRINT_MANAGER_INTERFACE, nullptr, &error);

  if (error) {
    spdlog::error("FingerprintAuth: Failed to get manager proxy: {}", error->message);
    g_error_free(error);
    g_object_unref(connection);
    return enrolled_fingers;
  }

  GVariant* dev_ret = g_dbus_proxy_call_sync(manager, "GetDefaultDevice", nullptr,
                                             G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);
  if (!dev_ret) {
    spdlog::error("FingerprintAuth: No fingerprint device found: {}",
                  (error ? error->message : "Unknown"));
    if (error)
      g_error_free(error);
    g_object_unref(manager);
    g_object_unref(connection);
    return enrolled_fingers;
  }

  const gchar* device_path;
  g_variant_get(dev_ret, "(&o)", &device_path);
  std::string dev_path_str = device_path;
  g_variant_unref(dev_ret);

  GDBusProxy* device =
      g_dbus_proxy_new_sync(connection, G_DBUS_PROXY_FLAGS_NONE, nullptr, FPRINT_SERVICE,
                            dev_path_str.c_str(), FPRINT_DEVICE_INTERFACE, nullptr, &error);

  if (error) {
    spdlog::error("FingerprintAuth: Failed to get device proxy: {}", error->message);
    g_error_free(error);
    g_object_unref(manager);
    g_object_unref(connection);
    return enrolled_fingers;
  }

  GVariant* enrolled_ret =
      g_dbus_proxy_call_sync(device, "ListEnrolledFingers", g_variant_new("(s)", username.c_str()),
                             G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);

  if (!enrolled_ret) {
    spdlog::error("FingerprintAuth: Failed to list enrolled fingers: {}",
                  (error ? error->message : ""));
    if (error)
      g_error_free(error);
    g_object_unref(device);
    g_object_unref(manager);
    g_object_unref(connection);
    return enrolled_fingers;
  }

  GVariantIter* iter;
  gchar* finger_name;
  g_variant_get(enrolled_ret, "(as)", &iter);
  while (g_variant_iter_loop(iter, "s", &finger_name)) {
    enrolled_fingers.push_back(finger_name);
  }
  g_variant_iter_free(iter);
  g_variant_unref(enrolled_ret);

  g_object_unref(device);
  g_object_unref(manager);
  g_object_unref(connection);

  return enrolled_fingers;
}

bool FingerprintAuth::enroll(const std::string& username, const std::string& finger_name,
                             void (*callback)(bool done, const char* status, void* user_data),
                             void* user_data) {
  GError* error = nullptr;
  GDBusConnection* connection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &error);
  if (!connection) {
    spdlog::error("FingerprintAuth: Failed to get system bus: {}", error->message);
    g_error_free(error);
    return false;
  }

  GDBusProxy* manager =
      g_dbus_proxy_new_sync(connection, G_DBUS_PROXY_FLAGS_NONE, nullptr, FPRINT_SERVICE,
                            FPRINT_MANAGER_PATH, FPRINT_MANAGER_INTERFACE, nullptr, &error);

  if (error) {
    spdlog::error("FingerprintAuth: Failed to get manager proxy: {}", error->message);
    g_error_free(error);
    g_object_unref(connection);
    return false;
  }

  GVariant* dev_ret = g_dbus_proxy_call_sync(manager, "GetDefaultDevice", nullptr,
                                             G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);
  if (!dev_ret) {
    spdlog::error("FingerprintAuth: No fingerprint device found: {}",
                  (error ? error->message : "Unknown"));
    if (error)
      g_error_free(error);
    g_object_unref(manager);
    g_object_unref(connection);
    return false;
  }

  const gchar* device_path;
  g_variant_get(dev_ret, "(&o)", &device_path);
  std::string dev_path_str = device_path;
  g_variant_unref(dev_ret);

  GDBusProxy* device =
      g_dbus_proxy_new_sync(connection, G_DBUS_PROXY_FLAGS_NONE, nullptr, FPRINT_SERVICE,
                            dev_path_str.c_str(), FPRINT_DEVICE_INTERFACE, nullptr, &error);

  if (error) {
    spdlog::error("FingerprintAuth: Failed to get device proxy: {}", error->message);
    g_error_free(error);
    g_object_unref(manager);
    g_object_unref(connection);
    return false;
  }

  GVariant* claim_ret =
      g_dbus_proxy_call_sync(device, "Claim", g_variant_new("(s)", username.c_str()),
                             G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);

  if (!claim_ret) {
    spdlog::error("FingerprintAuth: Failed to claim device: {}", (error ? error->message : ""));
    if (error)
      g_error_free(error);
    g_object_unref(device);
    g_object_unref(manager);
    g_object_unref(connection);
    return false;
  }
  g_variant_unref(claim_ret);

  GVariant* enroll_start_ret =
      g_dbus_proxy_call_sync(device, "EnrollStart", g_variant_new("(s)", finger_name.c_str()),
                             G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);

  if (!enroll_start_ret) {
    spdlog::error("FingerprintAuth: Failed to start enrollment: {}", (error ? error->message : ""));
    if (error)
      g_error_free(error);
    GVariant* rel_ret = g_dbus_proxy_call_sync(device, "Release", nullptr, G_DBUS_CALL_FLAGS_NONE,
                                               -1, nullptr, nullptr);
    if (rel_ret)
      g_variant_unref(rel_ret);
    g_object_unref(device);
    g_object_unref(manager);
    g_object_unref(connection);
    return false;
  }
  g_variant_unref(enroll_start_ret);

  struct EnrollContext {
    GMainLoop* loop;
    bool success;
    std::string error_msg;
    void (*callback)(bool done, const char* status, void* user_data);
    void* user_data;
  };

  EnrollContext ctx;
  ctx.loop = g_main_loop_new(nullptr, FALSE);
  ctx.success = false;
  ctx.callback = callback;
  ctx.user_data = user_data;

  auto on_enroll_status = [](GDBusConnection* connection, const gchar* sender_name,
                             const gchar* object_path, const gchar* interface_name,
                             const gchar* signal_name, GVariant* parameters, gpointer user_data) {
    EnrollContext* ctx = static_cast<EnrollContext*>(user_data);
    const gchar* result;
    gboolean done;

    g_variant_get(parameters, "(&sb)", &result, &done);
    std::string res_str = result;

    spdlog::info("Enrollment status: {}, done: {}", res_str, done);

    if (ctx->callback) {
      ctx->callback(done, result, ctx->user_data);
    }

    if (res_str == "enroll-completed") {
      ctx->success = true;
      g_main_loop_quit(ctx->loop);
    } else if (res_str == "enroll-stage-passed") {
      if (done) {
        ctx->success = true;
        g_main_loop_quit(ctx->loop);
      }
    } else if (res_str == "enroll-unknown-error" || res_str == "enroll-disconnected") {
      ctx->error_msg = res_str;
      g_main_loop_quit(ctx->loop);
    } else if (done) {
      g_main_loop_quit(ctx->loop);
    }
  };

  spdlog::info("Waiting for fingerprint enrollment...");
  guint sub_id = g_dbus_connection_signal_subscribe(
      connection, FPRINT_SERVICE, FPRINT_DEVICE_INTERFACE, "EnrollStatus", dev_path_str.c_str(),
      nullptr, G_DBUS_SIGNAL_FLAGS_NONE, on_enroll_status, &ctx, nullptr);

  g_main_loop_run(ctx.loop);

  g_dbus_connection_signal_unsubscribe(connection, sub_id);
  g_main_loop_unref(ctx.loop);

  GVariant* enroll_stop_ret = g_dbus_proxy_call_sync(device, "EnrollStop", nullptr,
                                                     G_DBUS_CALL_FLAGS_NONE, -1, nullptr, nullptr);
  if (enroll_stop_ret)
    g_variant_unref(enroll_stop_ret);

  GVariant* release_ret = g_dbus_proxy_call_sync(device, "Release", nullptr, G_DBUS_CALL_FLAGS_NONE,
                                                 -1, nullptr, nullptr);
  if (release_ret)
    g_variant_unref(release_ret);

  g_object_unref(device);
  g_object_unref(manager);
  g_object_unref(connection);

  return ctx.success;
}

bool FingerprintAuth::removeFinger(const std::string& username, const std::string& finger_name) {
  GError* error = nullptr;
  GDBusConnection* connection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &error);
  if (!connection) {
    spdlog::error("FingerprintAuth: Failed to get system bus: {}", error->message);
    g_error_free(error);
    return false;
  }

  GDBusProxy* manager =
      g_dbus_proxy_new_sync(connection, G_DBUS_PROXY_FLAGS_NONE, nullptr, FPRINT_SERVICE,
                            FPRINT_MANAGER_PATH, FPRINT_MANAGER_INTERFACE, nullptr, &error);

  if (error) {
    spdlog::error("FingerprintAuth: Failed to get manager proxy: {}", error->message);
    g_error_free(error);
    g_object_unref(connection);
    return false;
  }

  GVariant* dev_ret = g_dbus_proxy_call_sync(manager, "GetDefaultDevice", nullptr,
                                             G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);
  if (!dev_ret) {
    spdlog::error("FingerprintAuth: No fingerprint device found: {}",
                  (error ? error->message : "Unknown"));
    if (error)
      g_error_free(error);
    g_object_unref(manager);
    g_object_unref(connection);
    return false;
  }

  const gchar* device_path;
  g_variant_get(dev_ret, "(&o)", &device_path);
  std::string dev_path_str = device_path;
  g_variant_unref(dev_ret);

  GDBusProxy* device =
      g_dbus_proxy_new_sync(connection, G_DBUS_PROXY_FLAGS_NONE, nullptr, FPRINT_SERVICE,
                            dev_path_str.c_str(), FPRINT_DEVICE_INTERFACE, nullptr, &error);

  if (error) {
    spdlog::error("FingerprintAuth: Failed to get device proxy: {}", error->message);
    g_error_free(error);
    g_object_unref(manager);
    g_object_unref(connection);
    return false;
  }

  GVariant* claim_ret =
      g_dbus_proxy_call_sync(device, "Claim", g_variant_new("(s)", username.c_str()),
                             G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);

  if (!claim_ret) {
    spdlog::error("FingerprintAuth: Failed to claim device for deletion: {}",
                  (error ? error->message : ""));
    if (error)
      g_error_free(error);
    g_object_unref(device);
    g_object_unref(manager);
    g_object_unref(connection);
    return false;
  }
  g_variant_unref(claim_ret);

  // Takes only the finger name, not (username, finger_name)
  GVariant* delete_ret = g_dbus_proxy_call_sync(device, "DeleteEnrolledFinger",
                                                g_variant_new("(s)", finger_name.c_str()),
                                                G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);

  bool success = false;
  if (delete_ret) {
    spdlog::info("Successfully removed fingerprint: {}", finger_name);
    success = true;
    g_variant_unref(delete_ret);
  } else {
    spdlog::error("FingerprintAuth: Failed to delete enrolled finger: {}",
                  (error ? error->message : ""));
    if (error)
      g_error_free(error);
  }

  GVariant* release_ret = g_dbus_proxy_call_sync(device, "Release", nullptr, G_DBUS_CALL_FLAGS_NONE,
                                                 -1, nullptr, nullptr);
  if (release_ret)
    g_variant_unref(release_ret);

  g_object_unref(device);
  g_object_unref(manager);
  g_object_unref(connection);

  return success;
}

}  // namespace biopass
