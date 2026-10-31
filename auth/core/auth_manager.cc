#include "auth_manager.h"

#include <spdlog/spdlog.h>

#include <atomic>
#include <condition_variable>
#include <exception>
#include <iostream>
#include <mutex>

#include "auth_deadline.h"

namespace biopass {

namespace {

// Ends the method's session. end() lets a throwing teardown reach the caller;
// if end() was never reached (an exception is unwinding) the destructor ends
// the session and swallows anything it throws.
class MethodSessionGuard {
 public:
  explicit MethodSessionGuard(IAuthMethod& method) : method_(method) {}
  ~MethodSessionGuard() {
    if (ended_) {
      return;
    }
    try {
      method_.endAuthenticationSession();
    } catch (...) {
      spdlog::error("AuthManager: ending the {} session threw", method_.name());
    }
  }

  void end() {
    ended_ = true;
    method_.endAuthenticationSession();
  }

  MethodSessionGuard(const MethodSessionGuard&) = delete;
  MethodSessionGuard& operator=(const MethodSessionGuard&) = delete;

 private:
  IAuthMethod& method_;
  bool ended_ = false;
};

// A method that got past its first attempt was tried, so going unavailable
// later (e.g. camera unplugged mid-scan) is a failure, not "nothing ran".
AuthResult finalResult(AuthResult last, uint32_t attempts) {
  if (last == AuthResult::Unavailable && attempts > 1) {
    return AuthResult::Failure;
  }
  return last;
}

// Sets `cancel` once `deadline` passes, until destroyed. No thread when there
// is no deadline.
class DeadlineWatch {
 public:
  DeadlineWatch(std::chrono::steady_clock::time_point deadline, std::atomic<bool>& cancel) {
    if (deadline == std::chrono::steady_clock::time_point::max()) {
      return;
    }
    thread_ = std::thread([this, deadline, &cancel] {
      std::unique_lock<std::mutex> lock(mutex_);
      if (!stopped_cv_.wait_until(lock, deadline, [this] { return stopped_; })) {
        spdlog::debug("AuthManager: time budget spent, cancelling");
        cancel.store(true);
      }
    });
  }
  ~DeadlineWatch() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stopped_ = true;
    }
    stopped_cv_.notify_all();
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  DeadlineWatch(const DeadlineWatch&) = delete;
  DeadlineWatch& operator=(const DeadlineWatch&) = delete;

 private:
  std::mutex mutex_;
  std::condition_variable stopped_cv_;
  bool stopped_ = false;
  std::thread thread_;
};

// Runs `method`'s attempts until one is final, its retries run out or
// `cancel` is set (retry sleeps wake for it too). Cancelled before the first
// attempt = Failure.
AuthResult runAttempts(IAuthMethod& method, const std::string& username, const AuthConfig& config,
                       std::atomic<bool>& cancel) {
  RetryStrategy retry_strategy(method.getRetries());
  uint32_t attempts = 0;
  AuthResult result = AuthResult::Failure;

  do {
    if (attempts > 0) {
      spdlog::debug("AuthManager: Retrying {} (attempt {}/{})", method.name(), attempts + 1,
                    method.getRetries());
      if (!sleepUnlessCancelled(std::chrono::milliseconds(method.getRetryDelayMs()), &cancel)) {
        break;
      }
    } else if (cancel.load()) {
      break;
    } else {
      spdlog::debug("AuthManager: Trying {} authentication", method.name());
    }

    result = method.authenticate(username, config, &cancel);
    attempts++;
  } while (retry_strategy.shouldRetry(result, attempts) && !cancel.load());

  return finalResult(result, attempts);
}

// A method that throws while checking availability is treated as not
// available: nothing was attempted.
bool isAvailableNoThrow(const IAuthMethod& method) {
  try {
    return method.isAvailable();
  } catch (const std::exception& e) {
    spdlog::error("AuthManager: {} availability check threw: {}", method.name(), e.what());
  } catch (...) {
    spdlog::error("AuthManager: {} availability check threw", method.name());
  }
  return false;
}

// Runs one method's whole session. Any exception (ONNX Runtime, bad_alloc,
// a non-std throw) ends it as a Failure, never Success, and never reaches a
// std::async future or main, where it would terminate the helper.
AuthResult runMethod(IAuthMethod& method, const std::string& username, const AuthConfig& config,
                     std::atomic<bool>& cancel) {
  try {
    // Guard first: a begin that throws halfway (camera open, models not)
    // still gets its session ended.
    MethodSessionGuard session_guard(method);
    method.beginAuthenticationSession();
    const AuthResult result = runAttempts(method, username, config, cancel);
    session_guard.end();
    return result;
  } catch (const std::exception& e) {
    spdlog::error("AuthManager: {} threw, counting it as a failure: {}", method.name(), e.what());
  } catch (...) {
    spdlog::error("AuthManager: {} threw, counting it as a failure", method.name());
  }
  return AuthResult::Failure;
}

}  // namespace

void AuthManager::addMethod(std::unique_ptr<IAuthMethod> method) {
  this->methods_.push_back(std::move(method));
}

void AuthManager::setMode(ExecutionMode mode) { this->mode_ = mode; }
void AuthManager::setConfig(const AuthConfig& config) { this->config_ = config; }
void AuthManager::setDeadline(std::chrono::steady_clock::time_point deadline) {
  this->deadline_ = deadline;
}

AuthOutcome AuthManager::authenticate(const std::string& username) {
  if (this->methods_.empty()) {
    spdlog::error("AuthManager: No authentication methods configured");
    return AuthOutcome::Ignore;
  }

  if (this->config_.debug) {
    spdlog::set_level(spdlog::level::debug);
  } else {
    spdlog::set_level(spdlog::level::off);
  }

  // Set when the run should wind down: the deadline passed, or (parallel) a
  // method succeeded. Methods get it as their cancel signal.
  std::atomic<bool> cancel{false};
  DeadlineWatch watch(this->deadline_, cancel);

  switch (this->mode_) {
    case ExecutionMode::Sequential:
      return this->runSequential(username, cancel);
    case ExecutionMode::Parallel:
      return this->runParallel(username, cancel);
    default:
      return AuthOutcome::Failure;
  }
}

AuthOutcome AuthManager::runSequential(const std::string& username, std::atomic<bool>& cancel) {
  bool any_attempted = false;

  for (auto& method : this->methods_) {
    if (cancel.load()) {
      spdlog::debug("AuthManager: time budget spent, not starting {}", method->name());
      break;
    }
    if (!isAvailableNoThrow(*method)) {
      spdlog::debug("AuthManager: {} is not available, skipping", method->name());
      continue;
    }

    const AuthResult result = runMethod(*method, username, this->config_, cancel);

    switch (result) {
      case AuthResult::Success:
        spdlog::debug("AuthManager: {} authentication succeeded", method->name());
        return AuthOutcome::Success;
      case AuthResult::Unavailable:
        spdlog::debug("AuthManager: {} is unavailable, skipping", method->name());
        break;
      case AuthResult::Failure:
        any_attempted = true;
        spdlog::debug("AuthManager: {} authentication failed, trying next", method->name());
        break;
      case AuthResult::Retry:
        any_attempted = true;
        spdlog::debug("AuthManager: {} requested retry but max retries exceeded", method->name());
        break;
    }
  }

  if (!any_attempted) {
    spdlog::debug("AuthManager: No methods were able to run for this user, skipping module");
    return AuthOutcome::Ignore;
  }

  spdlog::error("AuthManager: All authentication methods failed");
  return AuthOutcome::Failure;
}

AuthOutcome AuthManager::runParallel(const std::string& username, std::atomic<bool>& cancel) {
  if (this->methods_.empty()) {
    spdlog::debug("AuthManager: No methods were able to run for this user, skipping module");
    return AuthOutcome::Ignore;
  }

  std::vector<std::future<AuthResult>> futures;

  for (auto& method : this->methods_) {
    if (!isAvailableNoThrow(*method)) {
      spdlog::debug("AuthManager: {} is not available, skipping", method->name());
      continue;
    }

    // A success sets `cancel`, so the other methods leave their attempt or
    // retry sleep and the joins below return promptly.
    futures.push_back(
        std::async(std::launch::async, [&method, &username, &config = this->config_, &cancel]() {
          const AuthResult result = runMethod(*method, username, config, cancel);
          if (result == AuthResult::Success) {
            cancel.store(true);
            spdlog::debug("AuthManager: {} authentication succeeded (parallel)", method->name());
          }
          return result;
        }));
  }

  bool any_success = false;
  bool any_attempted = false;
  for (auto& future : futures) {
    AuthResult result = future.get();
    if (result == AuthResult::Success) {
      any_success = true;
    } else if (result != AuthResult::Unavailable) {
      any_attempted = true;
    }
  }

  if (any_success) {
    return AuthOutcome::Success;
  }

  if (!any_attempted) {
    spdlog::debug("AuthManager: All parallel methods became unavailable, skipping module");
    return AuthOutcome::Ignore;
  }

  spdlog::error("AuthManager: All parallel authentication methods failed");
  return AuthOutcome::Failure;
}

}  // namespace biopass
