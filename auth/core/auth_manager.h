#pragma once

#include <atomic>
#include <chrono>
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
  // After `deadline` no attempt or retry sleep starts and attempts in flight
  // get their cancel signal. Default: none. The helper sets now + kAuthBudget.
  void setDeadline(std::chrono::steady_clock::time_point deadline);
  AuthOutcome authenticate(const std::string& username);

 private:
  AuthOutcome runSequential(const std::string& username, std::atomic<bool>& cancel);
  AuthOutcome runParallel(const std::string& username, std::atomic<bool>& cancel);

  std::vector<std::unique_ptr<IAuthMethod>> methods_;
  ExecutionMode mode_ = ExecutionMode::Parallel;
  AuthConfig config_;
  std::chrono::steady_clock::time_point deadline_ = std::chrono::steady_clock::time_point::max();
};

}  // namespace biopass
