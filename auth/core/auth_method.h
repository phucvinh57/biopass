#pragma once

#include <security/_pam_types.h>

#include <atomic>
#include <cstdint>
#include <string>

namespace biopass {
enum AuthResult { Success, Failure, Retry, Unavailable };

struct AuthConfig {
  bool debug = false;
};

struct IAuthMethod {
  virtual ~IAuthMethod() = default;
  virtual std::string name() const = 0;
  virtual bool isAvailable() const = 0;
  virtual uint32_t getRetries() const = 0;
  virtual uint32_t getRetryDelayMs() const = 0;
  virtual void beginAuthenticationSession() {}
  virtual void endAuthenticationSession() {}
  virtual AuthResult authenticate(const std::string& username, const AuthConfig& config,
                                  std::atomic<bool>* cancelSignal = nullptr) = 0;
};

struct RetryStrategy {
  RetryStrategy(uint32_t maxRetries) : maxRetries_(maxRetries) {}

  bool shouldRetry(AuthResult result, uint32_t attempts) const {
    if (result != AuthResult::Retry) {
      return false;
    }
    return attempts < maxRetries_;
  }

 private:
  uint32_t maxRetries_;
};
}  // namespace biopass
