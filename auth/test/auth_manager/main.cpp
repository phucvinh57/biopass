// Outcome checks for AuthManager (core/auth_manager.h) with fake methods, in
// both execution modes. Exits non-zero on any failure.

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "auth_manager.h"

namespace {

using biopass::AuthManager;
using biopass::AuthResult;
using biopass::ExecutionMode;
using biopass::IAuthMethod;
using Outcome = biopass::AuthOutcome;

int g_failures = 0;

void expect(bool cond, const std::string& what) {
  std::printf("[%s] %s\n", cond ? "PASS" : "FAIL", what.c_str());
  if (!cond) {
    g_failures++;
  }
}

struct Calls {
  int authenticate = 0;
  int begin = 0;
  int end = 0;
};

// Returns `results` in order (the last one repeats). `available` = false
// makes isAvailable() refuse before any attempt.
class FakeMethod : public IAuthMethod {
 public:
  FakeMethod(std::vector<AuthResult> results, Calls* calls, bool available, uint32_t retries)
      : results_(std::move(results)), calls_(calls), available_(available), retries_(retries) {}

  std::string name() const override { return "fake"; }
  bool isAvailable() const override { return available_; }
  uint32_t getRetries() const override { return retries_; }
  uint32_t getRetryDelayMs() const override { return 0; }
  void beginAuthenticationSession() override { calls_->begin++; }
  void endAuthenticationSession() override { calls_->end++; }
  AuthResult authenticate(const std::string&, const biopass::AuthConfig&,
                          std::atomic<bool>*) override {
    size_t i = static_cast<size_t>(calls_->authenticate++);
    return results_[i < results_.size() ? i : results_.size() - 1];
  }

 private:
  std::vector<AuthResult> results_;
  Calls* calls_;
  bool available_;
  uint32_t retries_;
};

struct Spec {
  std::vector<AuthResult> results;
  bool available = true;
  uint32_t retries = 1;
};

Outcome run(ExecutionMode mode, const std::vector<Spec>& specs,
            std::vector<Calls>* calls = nullptr) {
  std::vector<Calls> local;
  std::vector<Calls>& c = calls ? *calls : local;
  c.assign(specs.size(), Calls{});
  AuthManager manager;
  manager.setMode(mode);
  for (size_t i = 0; i < specs.size(); ++i) {
    manager.addMethod(std::make_unique<FakeMethod>(specs[i].results, &c[i], specs[i].available,
                                                   specs[i].retries));
  }
  return manager.authenticate("someone");
}

void checkMode(ExecutionMode mode, const char* label) {
  auto name = [label](const char* what) { return std::string(label) + ": " + what; };
  const Spec ok{{AuthResult::Success}};
  const Spec fail{{AuthResult::Failure}};
  const Spec gone{{AuthResult::Unavailable}};
  const Spec refused{{AuthResult::Success}, false};

  expect(run(mode, {}) == Outcome::Ignore, name("no methods -> ignore"));
  expect(run(mode, {ok}) == Outcome::Success, name("success -> success"));
  expect(run(mode, {fail}) == Outcome::Failure, name("failure -> failure"));
  expect(run(mode, {refused}) == Outcome::Ignore, name("method not available -> ignore"));
  expect(run(mode, {gone, gone}) == Outcome::Ignore, name("all methods unavailable -> ignore"));
  expect(run(mode, {refused, gone}) == Outcome::Ignore,
         name("not available + unavailable -> ignore"));
  expect(run(mode, {gone, fail}) == Outcome::Failure, name("unavailable + failure -> failure"));
  expect(run(mode, {fail, gone}) == Outcome::Failure, name("failure + unavailable -> failure"));
  expect(run(mode, {fail, ok}) == Outcome::Success, name("failure + success -> success"));
  expect(run(mode, {gone, ok}) == Outcome::Success, name("unavailable + success -> success"));
  expect(run(mode, {refused, ok}) == Outcome::Success, name("not available + success -> success"));

  std::vector<Calls> calls;
  expect(run(mode, {Spec{{AuthResult::Retry}, true, 3}}, &calls) == Outcome::Failure,
         name("retries exhausted -> failure"));
  expect(calls[0].authenticate == 3, name("retry count honoured"));

  expect(run(mode, {Spec{{AuthResult::Retry, AuthResult::Success}, true, 3}}) == Outcome::Success,
         name("retry then success -> success"));

  // A method that started scanning was tried, even if it then went away
  // (e.g. camera unplugged mid-attempt).
  expect(
      run(mode, {Spec{{AuthResult::Retry, AuthResult::Unavailable}, true, 3}}) == Outcome::Failure,
      name("retry then unavailable -> failure"));
  expect(run(mode, {gone, Spec{{AuthResult::Retry, AuthResult::Unavailable}, true, 3}}) ==
             Outcome::Failure,
         name("unavailable + retry then unavailable -> failure"));

  run(mode, {fail, refused, ok}, &calls);
  expect(calls[0].begin == 1 && calls[0].end == 1 && calls[2].begin == 1 && calls[2].end == 1,
         name("sessions begin and end once per tried method"));
  expect(calls[1].begin == 0 && calls[1].authenticate == 0,
         name("not-available method is never started"));
}

}  // namespace

int main() {
  checkMode(ExecutionMode::Sequential, "sequential");
  checkMode(ExecutionMode::Parallel, "parallel");

  std::vector<Calls> calls;
  run(ExecutionMode::Sequential, {Spec{{AuthResult::Success}}, Spec{{AuthResult::Success}}},
      &calls);
  expect(calls[1].authenticate == 0, "sequential: stops after the first success");

  if (g_failures != 0) {
    std::printf("%d FAILED\n", g_failures);
    return 1;
  }
  std::printf("ALL PASS\n");
  return 0;
}
