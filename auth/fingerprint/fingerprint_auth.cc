#include "fingerprint_auth.h"

#include <gio/gio.h>
#include <spdlog/spdlog.h>

#include <optional>
#include <thread>
#include <vector>

#include "auth_deadline.h"
#include "fprint_signal.h"
#include "status_reporter.h"

namespace biopass {

// After a cancel (sibling success or spent budget), authenticate() can still
// be in one fprintd call and then runs VerifyStop and Release; the rest stops
// at the next cancel check. That tail must end before the PAM module kills
// the helper, or a result reached within the budget could be lost.
static_assert(kAuthBudget +
                      std::chrono::milliseconds(kFprintdCallTimeoutMs + 2 * kFprintdStopTimeoutMs) +
                      kTeardownMargin <=
                  kHelperTimeout,
              "kAuthBudget leaves no room for the fprintd cleanup before kHelperTimeout");

namespace {

const char* FPRINT_SERVICE = "net.reactivated.Fprint";
const char* FPRINT_MANAGER_PATH = "/net/reactivated/Fprint/Manager";
const char* FPRINT_MANAGER_INTERFACE = "net.reactivated.Fprint.Manager";
const char* FPRINT_DEVICE_INTERFACE = "net.reactivated.Fprint.Device";

const char* DBUS_SERVICE = "org.freedesktop.DBus";
const char* DBUS_PATH = "/org/freedesktop/DBus";
const char* DBUS_INTERFACE = "org.freedesktop.DBus";

struct AuthContext {
  GMainLoop* loop = nullptr;
  AuthResult result = AuthResult::Failure;
  bool finished = false;
  std::string error_msg;
  bool debug = false;
  std::atomic<bool>* cancel_signal = nullptr;
  int verify_timeout_ms = 0;
  bool verify_timeout_fired = false;
  // Unique bus name (":1.N") of the fprintd instance verification runs on.
  std::string fprintd_owner;
};

// Returns fprintd's unique bus name (":1.N"), or "" if it can't be resolved.
// The bus daemon sets the sender field, so another local process can't claim
// this name.
std::string resolveFprintdOwner(GDBusConnection* connection) {
  GError* error = nullptr;
  GVariant* owner_ret = g_dbus_connection_call_sync(
      connection, DBUS_SERVICE, DBUS_PATH, DBUS_INTERFACE, "GetNameOwner",
      g_variant_new("(s)", FPRINT_SERVICE), G_VARIANT_TYPE("(s)"), G_DBUS_CALL_FLAGS_NONE,
      kFprintdCallTimeoutMs, nullptr, &error);
  if (!owner_ret) {
    spdlog::error("FingerprintAuth: Failed to resolve the fprintd bus name: {}",
                  (error ? error->message : ""));
    if (error)
      g_error_free(error);
    return "";
  }
  const gchar* owner = nullptr;
  g_variant_get(owner_ret, "(&s)", &owner);
  std::string result = owner ? owner : "";
  g_variant_unref(owner_ret);
  if (result.empty()) {
    spdlog::error("FingerprintAuth: fprintd has no bus owner");
  }
  return result;
}

// The first decision ends the wait; later events can't overwrite it.
void finish(AuthContext* ctx, AuthResult result) {
  if (ctx->finished) {
    return;
  }
  ctx->finished = true;
  ctx->result = result;
  g_main_loop_quit(ctx->loop);
}

void on_verify_status(GDBusConnection* connection, const gchar* sender_name,
                      const gchar* object_path, const gchar* interface_name,
                      const gchar* signal_name, GVariant* parameters, gpointer user_data) {
  AuthContext* ctx = static_cast<AuthContext*>(user_data);

  // GLib < 2.80.1 (CVE-2024-34397) can deliver signals from any sender, so
  // check the sender and payload type here too.
  bool type_ok = parameters != nullptr && g_variant_is_of_type(parameters, G_VARIANT_TYPE("(sb)"));
  if (!acceptVerifyStatus(sender_name, ctx->fprintd_owner, type_ok)) {
    spdlog::warn("FingerprintAuth: Ignoring VerifyStatus from {} (payload type ok: {})",
                 sender_name ? sender_name : "(none)", type_ok);
    return;
  }

  const gchar* status = nullptr;
  gboolean done = FALSE;
  g_variant_get(parameters, "(&sb)", &status, &done);

  spdlog::debug("Fingerprint status: {}, done: {}", status, done);

  std::optional<AuthResult> result = mapVerifyStatus(status, done);
  if (!result) {
    return;
  }
  if (*result == AuthResult::Unavailable) {
    ctx->error_msg = status;
  }
  finish(ctx, *result);
}

// Ends the wait if fprintd leaves the bus or restarts, instead of hanging
// until the timeout (or forever when the timeout is 0).
void on_fprintd_owner_changed(GDBusConnection* connection, const gchar* sender_name,
                              const gchar* object_path, const gchar* interface_name,
                              const gchar* signal_name, GVariant* parameters, gpointer user_data) {
  AuthContext* ctx = static_cast<AuthContext*>(user_data);

  if (g_strcmp0(sender_name, DBUS_SERVICE) != 0 || parameters == nullptr ||
      !g_variant_is_of_type(parameters, G_VARIANT_TYPE("(sss)"))) {
    return;
  }

  const gchar* name = nullptr;
  const gchar* old_owner = nullptr;
  const gchar* new_owner = nullptr;
  g_variant_get(parameters, "(&s&s&s)", &name, &old_owner, &new_owner);
  if (g_strcmp0(name, FPRINT_SERVICE) != 0 || !fprintdOwnerLost(new_owner, ctx->fprintd_owner)) {
    return;
  }

  spdlog::error("FingerprintAuth: fprintd left the bus during verification");
  ctx->error_msg = "fprintd vanished";
  finish(ctx, AuthResult::Unavailable);
}

gboolean on_cancel_timeout(gpointer user_data) {
  AuthContext* ctx = static_cast<AuthContext*>(user_data);
  if (ctx->cancel_signal && ctx->cancel_signal->load()) {
    spdlog::debug("FingerprintAuth: Cancelled by another method");
    finish(ctx, AuthResult::Failure);
  }
  return G_SOURCE_CONTINUE;
}

gboolean on_verify_timeout(gpointer user_data) {
  AuthContext* ctx = static_cast<AuthContext*>(user_data);
  ctx->verify_timeout_fired = true;

  if (ctx->verify_timeout_ms > 0) {
    spdlog::debug("FingerprintAuth: Verification timed out after {} ms", ctx->verify_timeout_ms);
  } else {
    spdlog::debug("FingerprintAuth: Verification timed out");
  }

  finish(ctx, AuthResult::Retry);
  return G_SOURCE_REMOVE;
}

}  // namespace

bool FingerprintAuth::isAvailable() const {
  GError* error = nullptr;
  GDBusProxy* manager = g_dbus_proxy_new_for_bus_sync(
      G_BUS_TYPE_SYSTEM, G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES, nullptr, FPRINT_SERVICE,
      FPRINT_MANAGER_PATH, FPRINT_MANAGER_INTERFACE, nullptr, &error);
  if (error) {
    spdlog::error("FingerprintAuth: Failed to connect to fprintd manager: {}", error->message);
    g_error_free(error);
    return false;
  }

  GVariant* ret =
      g_dbus_proxy_call_sync(manager, "GetDefaultDevice", nullptr, G_DBUS_CALL_FLAGS_NONE,
                             kFprintdCallTimeoutMs, nullptr, &error);

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
  guint owner_changed_subscription_id = 0;
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

    if (owner_changed_subscription_id != 0 && connection) {
      g_dbus_connection_signal_unsubscribe(connection, owner_changed_subscription_id);
      owner_changed_subscription_id = 0;
    }

    if (ctx.loop != nullptr) {
      g_main_loop_unref(ctx.loop);
      ctx.loop = nullptr;
    }

    if (verification_started && device) {
      GVariant* stop_ret =
          g_dbus_proxy_call_sync(device, "VerifyStop", nullptr, G_DBUS_CALL_FLAGS_NONE,
                                 kFprintdStopTimeoutMs, nullptr, nullptr);
      if (stop_ret)
        g_variant_unref(stop_ret);
      verification_started = false;
    }

    if (device_claimed && device) {
      GVariant* release_ret =
          g_dbus_proxy_call_sync(device, "Release", nullptr, G_DBUS_CALL_FLAGS_NONE,
                                 kFprintdStopTimeoutMs, nullptr, nullptr);
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

  // Checked before every fprintd call, so a cancelled run is in at most one
  // call when it notices (see the static_assert at the top).
  auto cancelled = [&]() { return cancel_signal != nullptr && cancel_signal->load(); };

  if (!connection) {
    spdlog::error("FingerprintAuth: Failed to get system bus: {}", error->message);
    g_error_free(error);
    return AuthResult::Unavailable;
  }

  manager = g_dbus_proxy_new_sync(connection, G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES, nullptr,
                                  FPRINT_SERVICE, FPRINT_MANAGER_PATH, FPRINT_MANAGER_INTERFACE,
                                  nullptr, &error);

  if (!manager) {
    spdlog::error("FingerprintAuth: Failed to get manager proxy: {}", error->message);
    g_error_free(error);
    cleanup();
    return AuthResult::Unavailable;
  }

  if (cancelled()) {
    cleanup();
    return AuthResult::Failure;
  }
  GVariant* dev_ret =
      g_dbus_proxy_call_sync(manager, "GetDefaultDevice", nullptr, G_DBUS_CALL_FLAGS_NONE,
                             kFprintdCallTimeoutMs, nullptr, &error);
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

  device = g_dbus_proxy_new_sync(connection, G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES, nullptr,
                                 FPRINT_SERVICE, dev_path_str.c_str(), FPRINT_DEVICE_INTERFACE,
                                 nullptr, &error);

  if (!device) {
    spdlog::error("FingerprintAuth: Failed to get device proxy: {}", error->message);
    g_error_free(error);
    cleanup();
    return AuthResult::Unavailable;
  }

  if (cancelled()) {
    cleanup();
    return AuthResult::Failure;
  }
  GVariant* enrolled_ret =
      g_dbus_proxy_call_sync(device, "ListEnrolledFingers", g_variant_new("(s)", username.c_str()),
                             G_DBUS_CALL_FLAGS_NONE, kFprintdCallTimeoutMs, nullptr, &error);

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

  if (cancelled()) {
    cleanup();
    return AuthResult::Failure;
  }
  GVariant* claim_ret =
      g_dbus_proxy_call_sync(device, "Claim", g_variant_new("(s)", username.c_str()),
                             G_DBUS_CALL_FLAGS_NONE, kFprintdCallTimeoutMs, nullptr, &error);

  if (!claim_ret) {
    spdlog::error("FingerprintAuth: Failed to claim device: {}", (error ? error->message : ""));
    if (error)
      g_error_free(error);
    cleanup();
    return AuthResult::Unavailable;
  }
  g_variant_unref(claim_ret);
  device_claimed = true;

  ctx.loop = g_main_loop_new(nullptr, FALSE);
  ctx.debug = config.debug;
  ctx.cancel_signal = cancel_signal;
  ctx.verify_timeout_ms = config_.timeout;

  // Watch fprintd's name before resolving its owner, so a restart in between
  // is still seen once the loop runs.
  owner_changed_subscription_id = g_dbus_connection_signal_subscribe(
      connection, DBUS_SERVICE, DBUS_INTERFACE, "NameOwnerChanged", DBUS_PATH, FPRINT_SERVICE,
      G_DBUS_SIGNAL_FLAGS_NONE, on_fprintd_owner_changed, &ctx, nullptr);

  // Pin VerifyStatus to fprintd's unique name.
  if (cancelled()) {
    cleanup();
    return AuthResult::Failure;
  }
  ctx.fprintd_owner = resolveFprintdOwner(connection);
  if (ctx.fprintd_owner.empty()) {
    cleanup();
    return AuthResult::Unavailable;
  }

  // Subscribe before VerifyStart so no early status is missed.
  verify_status_subscription_id = g_dbus_connection_signal_subscribe(
      connection, ctx.fprintd_owner.c_str(), FPRINT_DEVICE_INTERFACE, "VerifyStatus",
      dev_path_str.c_str(), nullptr, G_DBUS_SIGNAL_FLAGS_NONE, on_verify_status, &ctx, nullptr);

  // "any" accepts any enrolled finger
  if (cancelled()) {
    cleanup();
    return AuthResult::Failure;
  }
  GVariant* verify_ret =
      g_dbus_proxy_call_sync(device, "VerifyStart", g_variant_new("(s)", "any"),
                             G_DBUS_CALL_FLAGS_NONE, kFprintdCallTimeoutMs, nullptr, &error);

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

  // Pin EnrollStatus to fprintd's unique name, as for verify.
  std::string fprintd_owner = resolveFprintdOwner(connection);
  if (fprintd_owner.empty()) {
    GVariant* rel_ret = g_dbus_proxy_call_sync(device, "Release", nullptr, G_DBUS_CALL_FLAGS_NONE,
                                               -1, nullptr, nullptr);
    if (rel_ret)
      g_variant_unref(rel_ret);
    g_object_unref(device);
    g_object_unref(manager);
    g_object_unref(connection);
    return false;
  }

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
    std::string fprintd_owner;
  };

  EnrollContext ctx;
  ctx.loop = g_main_loop_new(nullptr, FALSE);
  ctx.success = false;
  ctx.callback = callback;
  ctx.user_data = user_data;
  ctx.fprintd_owner = fprintd_owner;

  auto on_enroll_status = [](GDBusConnection* connection, const gchar* sender_name,
                             const gchar* object_path, const gchar* interface_name,
                             const gchar* signal_name, GVariant* parameters, gpointer user_data) {
    EnrollContext* ctx = static_cast<EnrollContext*>(user_data);

    // Same checks as VerifyStatus: fprintd's unique name and a "(sb)" payload.
    bool type_ok =
        parameters != nullptr && g_variant_is_of_type(parameters, G_VARIANT_TYPE("(sb)"));
    if (!acceptVerifyStatus(sender_name, ctx->fprintd_owner, type_ok)) {
      spdlog::warn("FingerprintAuth: Ignoring EnrollStatus from {} (payload type ok: {})",
                   sender_name ? sender_name : "(none)", type_ok);
      return;
    }

    const gchar* result = nullptr;
    gboolean done = FALSE;

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
      connection, ctx.fprintd_owner.c_str(), FPRINT_DEVICE_INTERFACE, "EnrollStatus",
      dev_path_str.c_str(), nullptr, G_DBUS_SIGNAL_FLAGS_NONE, on_enroll_status, &ctx, nullptr);

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
