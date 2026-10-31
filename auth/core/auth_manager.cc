#include "auth_manager.h"

#include <spdlog/spdlog.h>

#include <atomic>
#include <iostream>

namespace biopass {

namespace {

class MethodSessionGuard {
 public:
  explicit MethodSessionGuard(IAuthMethod& method) : method_(method) {}
  ~MethodSessionGuard() { method_.endAuthenticationSession(); }

  MethodSessionGuard(const MethodSessionGuard&) = delete;
  MethodSessionGuard& operator=(const MethodSessionGuard&) = delete;

 private:
  IAuthMethod& method_;
};

}  // namespace

void AuthManager::addMethod(std::unique_ptr<IAuthMethod> method) {
  this->methods_.push_back(std::move(method));
}

void AuthManager::setMode(ExecutionMode mode) { this->mode_ = mode; }
void AuthManager::setConfig(const AuthConfig& config) { this->config_ = config; }

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

  switch (this->mode_) {
    case ExecutionMode::Sequential:
      return this->runSequential(username);
    case ExecutionMode::Parallel:
      return this->runParallel(username);
    default:
      return AuthOutcome::Failure;
  }
}

AuthOutcome AuthManager::runSequential(const std::string& username) {
  bool any_attempted = false;

  for (auto& method : this->methods_) {
    if (!method->isAvailable()) {
      spdlog::debug("AuthManager: {} is not available, skipping", method->name());
      continue;
    }

    method->beginAuthenticationSession();
    MethodSessionGuard session_guard(*method);

    RetryStrategy rs(method->getRetries());
    uint32_t attempts = 0;
    AuthResult result;

    do {
      if (attempts > 0) {
        spdlog::debug("AuthManager: Retrying {} (attempt {}/{})", method->name(), attempts + 1,
                      method->getRetries());
        std::this_thread::sleep_for(std::chrono::milliseconds(method->getRetryDelayMs()));
      } else {
        spdlog::debug("AuthManager: Trying {} authentication", method->name());
      }

      result = method->authenticate(username, this->config_);
      attempts++;

    } while (rs.shouldRetry(result, attempts));

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

AuthOutcome AuthManager::runParallel(const std::string& username) {
  if (this->methods_.empty()) {
    spdlog::debug("AuthManager: No methods were able to run for this user, skipping module");
    return AuthOutcome::Ignore;
  }

  std::atomic<bool> success_found{false};
  std::vector<std::future<AuthResult>> futures;

  for (auto& method : this->methods_) {
    if (!method->isAvailable()) {
      spdlog::debug("AuthManager: {} is not available, skipping", method->name());
      continue;
    }

    futures.push_back(std::async(
        std::launch::async, [&method, &username, &config = this->config_, &success_found]() {
          method->beginAuthenticationSession();
          MethodSessionGuard session_guard(*method);

          RetryStrategy retry_strategy(method->getRetries());
          uint32_t attempts = 0;
          AuthResult result;

          do {
            if (success_found.load()) {
              return AuthResult::Failure;
            }

            if (attempts > 0) {
              spdlog::debug("AuthManager: Retrying {} (parallel attempt {})", method->name(),
                            attempts + 1);
              std::this_thread::sleep_for(std::chrono::milliseconds(method->getRetryDelayMs()));
            } else {
              spdlog::debug("AuthManager: Starting {} authentication (parallel)", method->name());
            }

            result = method->authenticate(username, config, &success_found);
            attempts++;
          } while (retry_strategy.shouldRetry(result, attempts) && !success_found.load());

          if (result == AuthResult::Success) {
            success_found.store(true);
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
