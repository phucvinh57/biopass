#pragma once

#include <iostream>
#include <vector>

#include "auth_config.h"
#include "auth_method.h"

namespace biopass {

// Fingerprint auth via fprintd over D-Bus.
class FingerprintAuth : public IAuthMethod {
 public:
  explicit FingerprintAuth(const FingerprintMethodConfig& config) : config_(config) {}
  ~FingerprintAuth() override = default;

  std::string name() const override { return "Fingerprint"; }
  bool isAvailable() const override;
  uint32_t getRetries() const override { return config_.retries; }
  uint32_t getRetryDelayMs() const override { return 0; }
  void beginAuthenticationSession() override { scan_announced_ = false; }
  AuthResult authenticate(const std::string& username, const AuthConfig& config,
                          std::atomic<bool>* cancel_signal = nullptr) override;
  std::vector<std::string> listEnrolledFingers(const std::string& username);
  bool enroll(const std::string& username, const std::string& finger_name,
              void (*callback)(bool done, const char* status, void* user_data) = nullptr,
              void* user_data = nullptr);
  bool removeFinger(const std::string& username, const std::string& finger_name);

 private:
  FingerprintMethodConfig config_;
  bool scan_announced_ = false;
};

}  // namespace biopass
