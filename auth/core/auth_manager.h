#pragma once

#include <future>
#include <memory>
#include <thread>
#include <vector>

#include "auth_method.h"
#include "helper_exit.h"

namespace biopass {

enum class ExecutionMode {
  Sequential,  // Try methods in order, fallback on failure
  Parallel     // Run all methods concurrently, succeed on first success
};

class AuthManager {
 public:
  AuthManager() = default;
  ~AuthManager() = default;

  AuthManager(const AuthManager&) = delete;
  AuthManager& operator=(const AuthManager&) = delete;
  AuthManager(AuthManager&&) = default;
  AuthManager& operator=(AuthManager&&) = default;

  void addMethod(std::unique_ptr<IAuthMethod> method);
  void setMode(ExecutionMode mode);
  void setConfig(const AuthConfig& config);
  AuthOutcome authenticate(const std::string& username);

 private:
  AuthOutcome runSequential(const std::string& username);
  AuthOutcome runParallel(const std::string& username);

  std::vector<std::unique_ptr<IAuthMethod>> methods_;
  ExecutionMode mode_ = ExecutionMode::Parallel;
  AuthConfig config_;
};

}  // namespace biopass
