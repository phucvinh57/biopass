#pragma once

#include <future>
#include <memory>
#include <thread>
#include <vector>

#include "auth_method.h"

namespace biopass {

enum class ExecutionMode {
  Sequential,  // Try methods in order, fallback on failure
  Parallel     // Run all methods concurrently, succeed on first success
};

/**
 * Manages multiple authentication methods and their execution.
 */
class AuthManager {
 public:
  AuthManager() = default;
  ~AuthManager() = default;

  // Non-copyable, movable
  AuthManager(const AuthManager&) = delete;
  AuthManager& operator=(const AuthManager&) = delete;
  AuthManager(AuthManager&&) = default;
  AuthManager& operator=(AuthManager&&) = default;

  void addMethod(std::unique_ptr<IAuthMethod> method);
  void setMode(ExecutionMode mode);
  void setConfig(const AuthConfig& config);
  int authenticate(const std::string& username);

 private:
  int runSequential(const std::string& username);
  int runParallel(const std::string& username);

  std::vector<std::unique_ptr<IAuthMethod>> methods_;
  ExecutionMode mode_ = ExecutionMode::Parallel;
  AuthConfig config_;
};

}  // namespace biopass
