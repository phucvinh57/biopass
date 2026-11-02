#include <fcntl.h>
#include <grp.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/spdlog.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/resource.h>

#include <CLI/CLI.hpp>
#include <algorithm>
#include <condition_variable>
#include <ctime>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include "auth_config.h"
#include "auth_deadline.h"
#include "auth_manager.h"
#include "authtok_channel.h"
#include "common/camera_capture.h"
#include "detection/face_detection.h"
#include "face_auth.h"
#include "fingerprint_auth.h"
#include "fs_user.h"
#include "helper_exit.h"
#include "image_io.h"
#include "keyring_services.h"
#include "sealed_file.h"
#include "sealed_store.h"
#include "secret_buffer.h"
#include "shadow_check.h"
#include "status_reporter.h"
#include "tpm_seal.h"
#include "user_paths.h"

using biopass::Detection;
using biopass::FaceDetection;

static_assert(biopass::kMaxAuthtokLen == biopass::kMaxSecretLen);

namespace {

std::string todayDateString() {
  std::time_t now = std::time(nullptr);
  std::tm local{};
  localtime_r(&now, &local);
  char buf[sizeof("yyyy-mm-dd")];
  std::strftime(buf, sizeof(buf), "%Y-%m-%d", &local);
  return std::string(buf);
}

// Never write to stdout/stderr here: they are inherited from the PAM caller,
// which may parse them as a line protocol (e.g. GNOME Shell's polkit agent
// reads polkit-agent-helper-1's output). Any stray line breaks its state
// machine and makes it retry auth in a tight loop (`pkexec id` hangs).
// Logs go only to a per-day file, and only in Debug Mode.
void setupBiopassLogger(const std::string& username, bool debug) {
  std::vector<spdlog::sink_ptr> sinks;

  const std::optional<biopass::UserPaths> user =
      debug ? biopass::resolveUser(username) : std::nullopt;
  if (user) {
    // The log lives in a user-writable dir: open it as the user so a planted
    // symlink can't make root append to (or create) a file elsewhere. The
    // sink keeps the fd, so later writes need no guard.
    biopass::ScopedFsUser as_user(user->uid, user->gid);
    if (as_user.ok() && biopass::setupDataDirs(user->data_dir) == 0) {
      const std::string log_path = user->logsDir() + "/" + todayDateString() + ".log";
      try {
        sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_path,
                                                                            /*truncate=*/false));
      } catch (const spdlog::spdlog_ex&) {
        // Nowhere safe to report this; run with no sinks.
      }
    }
  }

  auto logger = std::make_shared<spdlog::logger>("biopass", sinks.begin(), sinks.end());
  spdlog::set_default_logger(logger);
  spdlog::set_level(debug ? spdlog::level::debug : spdlog::level::off);
}

// An unseal running on its own thread (see unsealWithDeadline).
struct UnsealJob {
  std::mutex mutex;
  std::condition_variable done_cv;
  bool done = false;
  biopass::TpmStatus status = biopass::TpmStatus::Error;
  biopass::SecretBuffer password;
};

// Unseals `blob` with the TPM, giving up at `deadline`. ESYS's synchronous
// calls ignore Esys_SetTimeout (they wait for the TPM forever), so the TPM
// work runs on a thread that is abandoned when it is late. Returns nullptr
// when abandoned: the thread is still blocked on the TPM device, so the
// caller must end the process with _exit() rather than return from main.
std::shared_ptr<UnsealJob> unsealWithDeadline(const biopass::SealedBlob& blob,
                                              std::chrono::steady_clock::time_point deadline) {
  auto job = std::make_shared<UnsealJob>();
  std::thread worker([job, blob]() {
    biopass::TpmStatus status = biopass::TpmStatus::NoTpm;
    // An exception escaping this thread would abort the helper and turn the
    // match into PAM_AUTH_ERR.
    try {
      std::unique_ptr<biopass::TpmContext> tpm = biopass::TpmContext::openDevice();
      if (tpm) {
        status = tpm->unseal(blob, job->password);
      }
    } catch (...) {
      job->password.clear();
      status = biopass::TpmStatus::Error;
    }
    std::lock_guard<std::mutex> lock(job->mutex);
    job->status = status;
    job->done = true;
    job->done_cv.notify_all();
  });
  std::unique_lock<std::mutex> lock(job->mutex);
  if (!job->done_cv.wait_until(lock, deadline, [&job] { return job->done; })) {
    lock.unlock();
    worker.detach();
    return nullptr;
  }
  lock.unlock();
  worker.join();
  return job;
}

// After a biometric match on a login screen: unseals the password the user
// saved in the app and writes it to `fd`, from where pam.cc sets PAM_AUTHTOK
// so pam_gnome_keyring / pam_kwallet unlock the keyring (docs/Keyring.md).
// Never changes the authentication result: on any problem the keyring just
// stays locked. `kill_deadline` is when pam.cc gives up on the helper.
// Returns false when a stalled TPM had to be abandoned; the caller must then
// leave with _exit() (see unsealWithDeadline).
bool releaseKeyringPassword(const std::string& username, const std::string& service, int fd,
                            std::chrono::steady_clock::time_point kill_deadline) {
  // The sealed files are root-only, so a screen locker that runs PAM as the
  // user can't read them. Lock screens that authenticate through a login
  // service as root (GNOME's, via gdm-password) do get the password.
  if (fd < 0 || geteuid() != 0 || !biopass::isKeyringService(service)) {
    return true;
  }
  const std::optional<biopass::UserPaths> user = biopass::resolveUser(username);
  if (!user) {
    return true;
  }
  std::vector<uint8_t> data;
  const biopass::StoreResult read = biopass::readSealed(biopass::SealedStore{}, user->uid, data);
  if (read != biopass::StoreResult::Ok) {
    if (read != biopass::StoreResult::NotFound) {
      spdlog::warn("Biopass: saved keyring password unreadable ({})", static_cast<int>(read));
    }
    return true;
  }
  const std::optional<biopass::SealedBlob> blob = biopass::parseSealed(data.data(), data.size());
  if (!blob || blob->uid != user->uid) {
    spdlog::warn("Biopass: saved keyring password is malformed");
    return true;
  }

  const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
      kill_deadline - std::chrono::steady_clock::now());
  if (left < biopass::kKeyringMinBudget) {
    spdlog::warn("Biopass: no time left to unlock the keyring");
    return true;
  }

  // The password is about to be in memory: no core dump, no ptrace.
  prctl(PR_SET_DUMPABLE, 0);
  const struct rlimit no_core = {0, 0};
  setrlimit(RLIMIT_CORE, &no_core);

  // Leaves time for the shadow check and exit after the TPM work.
  const std::shared_ptr<UnsealJob> job =
      unsealWithDeadline(*blob, kill_deadline - biopass::kKeyringMinBudget / 2);
  if (!job) {
    spdlog::warn("Biopass: TPM too slow, keyring not unlocked");
    return false;
  }
  const biopass::SecretBuffer& password = job->password;
  switch (job->status) {
    case biopass::TpmStatus::Ok:
      break;
    case biopass::TpmStatus::NoTpm:
      spdlog::warn("Biopass: no TPM to unlock the keyring");
      return true;
    case biopass::TpmStatus::PolicyFailed:
      biopass::reportInfo(
          "Secure Boot settings changed, so your keyring stays locked. Save your password again "
          "in Biopass.");
      return true;
    case biopass::TpmStatus::LoadFailed:
      biopass::reportInfo(
          "The TPM was reset, so your keyring stays locked. Save your password again in Biopass.");
      return true;
    default:
      spdlog::warn("Biopass: TPM unseal failed");
      return true;
  }

  // Catches a changed password, and a uid that now belongs to someone else.
  const biopass::PasswordCheck check = biopass::verifyLocalPassword(username, password.c_str());
  if (check == biopass::PasswordCheck::Mismatch) {
    biopass::reportInfo(
        "Your password changed, so your keyring stays locked. Save it again in Biopass.");
    return true;
  }
  if (check != biopass::PasswordCheck::Match) {
    spdlog::warn("Biopass: cannot check the saved keyring password ({})", static_cast<int>(check));
    return true;
  }
  if (!biopass::writeAuthtok(fd, password.c_str(), password.size())) {
    spdlog::warn("Biopass: could not hand the keyring password to PAM");
  }
  return true;
}

}  // namespace

// Streams JPEG frames over stdout, reads commands on stdin:
//   FRAME          -> OK <byte_count>\n<bytes> | ERR <msg>\n
//   CAPTURE <path> -> detect + crop face to <path>: OK\n | NO_FACE\n | ERR <msg>\n
//   QUIT           -> exit
// The camera stays open across commands so preview and capture share one
// handle (cameras are usually single-consumer).
int previewSession(const std::string& cameraPath, const std::string& modelPath, int jpegQuality) {
  std::optional<std::string> deviceOpt;
  if (!cameraPath.empty()) {
    deviceOpt = cameraPath;
  }

  // stdout is the protocol channel; one stray log line desyncs it.
  spdlog::set_default_logger(std::make_shared<spdlog::logger>(
      "biopass", std::make_shared<spdlog::sinks::stderr_sink_mt>()));

  // Silence libcamera's info-level setup noise.
  spdlog::set_level(spdlog::level::err);
  auto session = biopass::openCameraSession(deviceOpt);
  spdlog::set_level(spdlog::level::info);
  if (!session || !session->isOpen()) {
    std::cout << "ERR failed to open camera\n" << std::flush;
    return 1;
  }

  std::unique_ptr<FaceDetection> faceDetector;
  if (!modelPath.empty()) {
    try {
      faceDetector = std::make_unique<FaceDetection>(biopass::loadModelFile(modelPath));
    } catch (const std::exception& e) {
      std::cout << "ERR failed to load detection model: " << e.what() << "\n" << std::flush;
      return 1;
    }
  }

  std::cout << "READY\n" << std::flush;

  std::vector<uint8_t> jpegBuf;
  std::string line;
  while (std::getline(std::cin, line)) {
    if (line == "QUIT") {
      break;
    }

    if (line == "FRAME") {
      ImageRGB img = session->capture();
      if (img.empty()) {
        std::cout << "ERR capture failed\n" << std::flush;
        continue;
      }
      if (!encodeJpeg(img, jpegQuality, jpegBuf)) {
        std::cout << "ERR jpeg encode failed\n" << std::flush;
        continue;
      }
      std::cout << "OK " << jpegBuf.size() << "\n";
      std::cout.write(reinterpret_cast<const char*>(jpegBuf.data()),
                      static_cast<std::streamsize>(jpegBuf.size()));
      std::cout.flush();
      continue;
    }

    if (line.rfind("CAPTURE ", 0) == 0) {
      if (!faceDetector) {
        std::cout << "ERR detection model not loaded\n" << std::flush;
        continue;
      }
      std::string outPath = line.substr(8);
      ImageRGB img = session->capture();
      if (img.empty()) {
        std::cout << "ERR capture failed\n" << std::flush;
        continue;
      }
      auto faces = faceDetector->inference(img);
      if (faces.empty()) {
        std::cout << "NO_FACE\n" << std::flush;
        continue;
      }
      if (!saveImage(outPath, faces[0].image)) {
        std::cout << "ERR save failed\n" << std::flush;
        continue;
      }
      std::cout << "OK\n" << std::flush;
      continue;
    }

    std::cout << "ERR unknown command\n" << std::flush;
  }

  return 0;
}

biopass::HelperExit authenticate(const std::string& username, const std::string& service) {
  using biopass::HelperExit;
  const char* pUsername = username.c_str();
  // Counted from here so config and model loading come out of the budget too.
  const auto deadline = std::chrono::steady_clock::now() + biopass::kAuthBudget;

  const std::optional<biopass::BiopassConfig> loaded = biopass::readConfig(pUsername);
  if (!loaded) {
    return HelperExit::Ignore;  // no usable config.yaml
  }
  const biopass::BiopassConfig& config = *loaded;

  if (!service.empty() &&
      std::find(config.strategy.ignore_services.begin(), config.strategy.ignore_services.end(),
                service) != config.strategy.ignore_services.end()) {
    return HelperExit::Ignore;
  }

  setupBiopassLogger(pUsername, config.strategy.debug);

  biopass::AuthConfig runtime_config;
  runtime_config.debug = config.strategy.debug;

  biopass::AuthManager manager;
  manager.setMode(config.strategy.execution_mode);
  manager.setConfig(runtime_config);
  manager.setDeadline(deadline);

  int numOfMethods = 0;
  for (const biopass::MethodKind method : config.strategy.order) {
    switch (method) {
      case biopass::MethodKind::Face:
        if (config.methods.face.enable) {
          manager.addMethod(std::make_unique<biopass::FaceAuth>(config.methods.face, username));
          numOfMethods++;
        }
        break;
      case biopass::MethodKind::Fingerprint:
        if (config.methods.fingerprint.enable) {
          manager.addMethod(std::make_unique<biopass::FingerprintAuth>(config.methods.fingerprint));
          numOfMethods++;
        }
        break;
    }
  }

  if (numOfMethods == 0) {
    return HelperExit::Ignore;
  }

  return biopass::toHelperExit(manager.authenticate(pUsername));
}

int main(int argc, char** argv) {
  // pam.cc kills the helper kHelperTimeout after forking it.
  const auto kill_deadline =
      std::chrono::steady_clock::now() + biopass::kHelperTimeout - biopass::kTeardownMargin;

  CLI::App app{"Biopass Helper Tool"};
  app.set_version_flag("--version,-v", BIOPASS_VERSION);
  app.require_subcommand(1, 1);

  auto preview_cmd = app.add_subcommand(
      "preview-session",
      "Long-running camera session: streams JPEG frames + handles capture via stdin/stdout");
  std::string previewCameraPath, previewModelPath;
  int previewQuality = 70;
  preview_cmd->add_option("--camera,-c", previewCameraPath,
                          "Camera device path (e.g. /dev/video0). Empty = auto-select first.");
  preview_cmd->add_option("--model,-m", previewModelPath,
                          "Detection model path (required for CAPTURE command)");
  preview_cmd->add_option("--quality,-q", previewQuality,
                          "JPEG encoding quality 1-100 (default 70)");

  std::string username;
  std::string pamService;
  int statusFd = -1;
  int authtokFd = -1;
  auto auth_cmd = app.add_subcommand("auth", "Authenticate a user with Biopass");
  auth_cmd->add_option("--username,-u", username, "Username for authentication")->required();
  auth_cmd->add_option("--service,-s", pamService, "PAM service name");
  auth_cmd->add_option("--status-fd", statusFd,
                       "Inherited fd for status lines relayed to the PAM conversation");
  auth_cmd->add_option("--authtok-fd", authtokFd,
                       "Inherited fd that receives the saved keyring password after a match");

  try {
    app.parse(argc, argv);
  } catch (const CLI::ParseError& e) {
    // CLI11 exits 0 for --help/--version, which the PAM module would read as
    // a successful authentication, so every parse stop is an error here.
    // Text goes to stderr, never stdout (the PAM caller's channel), and the
    // auth path prints nothing at all.
    const bool authMode = argc > 1 && std::string(argv[1]) == "auth";
    if (!authMode) {
      app.exit(e, std::cerr, std::cerr);
    }
    return biopass::exitCode(biopass::HelperExit::AuthErr);
  }

  if (app.got_subcommand(preview_cmd)) {
    return previewSession(previewCameraPath, previewModelPath, previewQuality);
  }

  if (app.got_subcommand(auth_cmd)) {
    // spdlog's default logger writes to stdout, which belongs to the PAM
    // caller (see setupBiopassLogger). Silence it before anything can log;
    // setupBiopassLogger replaces it once the config is known.
    spdlog::set_default_logger(std::make_shared<spdlog::logger>("biopass"));
    spdlog::set_level(spdlog::level::off);
    // Same for libraries: tpm2-tss logs warnings to stderr by default, and
    // anything else that prints lands in /dev/null, not the PAM caller.
    setenv("TSS2_LOG", "all+NONE", 1);
    const int devnull = open("/dev/null", O_RDWR | O_CLOEXEC);
    if (devnull >= 0) {
      dup2(devnull, STDOUT_FILENO);
      dup2(devnull, STDERR_FILENO);
      if (devnull > STDERR_FILENO) {
        close(devnull);
      }
    }
    // Close-on-exec, so no child (e.g. libcamera's IPA proxy) inherits it.
    if (authtokFd >= 0 && (authtokFd <= STDERR_FILENO || fcntl(authtokFd, F_GETFD) < 0 ||
                           fcntl(authtokFd, F_SETFD, FD_CLOEXEC) < 0)) {
      authtokFd = -1;
    }
    if (username.empty()) {
      spdlog::info("{}", app.help());
      return biopass::exitCode(biopass::HelperExit::Ignore);
    }
    // ScopedFsUser switches only the filesystem uid/gid; root's supplementary
    // groups would still grant group access. Drop them before any thread
    // starts. Root keeps its capabilities, so nothing else changes.
    if (geteuid() == 0 && setgroups(0, nullptr) != 0) {
      return biopass::exitCode(biopass::HelperExit::AuthErr);  // fail closed
    }
    // Only methods that start scanning report, so skipped runs stay silent.
    biopass::setStatusFd(statusFd);
    // An unexpected exception (config, model loading, thread start) would
    // otherwise abort with a core dump. Fail closed with AuthErr instead.
    try {
      const biopass::HelperExit result = authenticate(username, pamService);
      if (result == biopass::HelperExit::Ok) {
        // Its own try: a keyring problem must not turn a match into AuthErr.
        bool finished = true;
        try {
          finished = releaseKeyringPassword(username, pamService, authtokFd, kill_deadline);
        } catch (...) {
          spdlog::warn("Biopass: keyring unlock threw");
        }
        if (!finished) {
          // A thread is still blocked on the TPM: skip static destructors.
          spdlog::default_logger()->flush();
          _exit(biopass::exitCode(result));
        }
      }
      if (authtokFd >= 0) {
        close(authtokFd);
      }
      return biopass::exitCode(result);
    } catch (const std::exception& e) {
      spdlog::error("Biopass: authentication threw: {}", e.what());
    } catch (...) {
      spdlog::error("Biopass: authentication threw");
    }
    return biopass::exitCode(biopass::HelperExit::AuthErr);
  }

  spdlog::error("No valid subcommand provided");
  return 1;
}
