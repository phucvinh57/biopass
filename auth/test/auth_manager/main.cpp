// Outcome checks for AuthManager (core/auth_manager.h) with fake methods, in
// both execution modes. Exits non-zero on any failure.

#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
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

struct Spec {
  std::vector<AuthResult> results;
  bool available = true;
  uint32_t retries = 1;
  uint32_t retry_delay_ms = 0;
  // How long each attempt takes. Like fprintd's verify, it ends early (as
  // Failure) once the cancel signal is set.
  uint32_t attempt_ms = 0;
};

// Returns `results` in order (the last one repeats). `available` = false
// makes isAvailable() refuse before any attempt.
class FakeMethod : public IAuthMethod {
 public:
  FakeMethod(Spec spec, Calls* calls) : spec_(std::move(spec)), calls_(calls) {}

  std::string name() const override { return "fake"; }
  bool isAvailable() const override { return spec_.available; }
  uint32_t getRetries() const override { return spec_.retries; }
  uint32_t getRetryDelayMs() const override { return spec_.retry_delay_ms; }
  void beginAuthenticationSession() override { calls_->begin++; }
  void endAuthenticationSession() override { calls_->end++; }
  AuthResult authenticate(const std::string&, const biopass::AuthConfig&,
                          std::atomic<bool>* cancel) override {
    size_t i = static_cast<size_t>(calls_->authenticate++);
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(spec_.attempt_ms);
    while (std::chrono::steady_clock::now() < end) {
      if (cancel != nullptr && cancel->load()) {
        return AuthResult::Failure;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return spec_.results[i < spec_.results.size() ? i : spec_.results.size() - 1];
  }

 private:
  Spec spec_;
  Calls* calls_;
};

void setUp(AuthManager& manager, ExecutionMode mode, const std::vector<Spec>& specs,
           std::vector<Calls>& calls) {
  calls.assign(specs.size(), Calls{});
  manager.setMode(mode);
  for (size_t i = 0; i < specs.size(); ++i) {
    manager.addMethod(std::make_unique<FakeMethod>(specs[i], &calls[i]));
  }
}

Outcome run(ExecutionMode mode, const std::vector<Spec>& specs,
            std::vector<Calls>* calls = nullptr) {
  std::vector<Calls> local;
  AuthManager manager;
  setUp(manager, mode, specs, calls ? *calls : local);
  return manager.authenticate("someone");
}

long long msSince(std::chrono::steady_clock::time_point start) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                               start)
      .count();
}

// Times out the slow cases instead of letting them pass by waiting.
constexpr long long kQuickMs = 1000;
// Retries forever, waiting 5 s between attempts.
const Spec kSlowRetrier{{AuthResult::Retry}, true, 1000, 5000};
// One attempt that takes 5 s unless cancelled.
const Spec kSlowAttempt{{AuthResult::Failure}, true, 1, 0, 5000};

void checkTiming() {
  // Parallel: a success must be returned right away, not after the slower
  // sibling finishes its retry sleep (which used to overrun the PAM deadline).
  {
    const Spec quick_ok{{AuthResult::Success}, true, 1, 0, 50};
    auto start = std::chrono::steady_clock::now();
    Outcome rc = run(ExecutionMode::Parallel, {kSlowRetrier, quick_ok});
    long long took = msSince(start);
    expect(rc == Outcome::Success, "parallel: success beside a retry sleep -> success");
    expect(took < kQuickMs, "parallel: success not held by a sibling's retry sleep (" +
                                std::to_string(took) + " ms)");

    start = std::chrono::steady_clock::now();
    rc = run(ExecutionMode::Parallel, {kSlowAttempt, quick_ok});
    took = msSince(start);
    expect(rc == Outcome::Success, "parallel: success beside a long attempt -> success");
    expect(took < kQuickMs, "parallel: success not held by a sibling's long attempt (" +
                                std::to_string(took) + " ms)");
  }

  // Time budget: no new attempt or retry sleep once the deadline passed, and
  // an attempt in flight is cancelled, so the helper ends before PAM kills it.
  const Spec busy_retrier{{AuthResult::Retry}, true, 1000, 50};
  const Spec ok{{AuthResult::Success}};
  for (ExecutionMode mode : {ExecutionMode::Sequential, ExecutionMode::Parallel}) {
    const std::string label = mode == ExecutionMode::Sequential ? "sequential" : "parallel";
    struct Case {
      std::vector<Spec> specs;
      Outcome want;
      const char* what;
    };
    const std::vector<Case> cases = {
        {{busy_retrier}, Outcome::Failure, "budget stops retries"},
        {{kSlowRetrier}, Outcome::Failure, "budget cuts a retry sleep"},
        {{kSlowAttempt}, Outcome::Failure, "budget cancels an attempt in flight"},
        {{ok}, Outcome::Success, "success within budget -> success"},
    };
    for (const Case& c : cases) {
      std::vector<Calls> calls;
      AuthManager manager;
      setUp(manager, mode, c.specs, calls);
      const auto start = std::chrono::steady_clock::now();
      manager.setDeadline(start + std::chrono::milliseconds(300));
      Outcome rc = manager.authenticate("someone");
      long long took = msSince(start);
      expect(rc == c.want, label + ": " + c.what);
      expect(took < kQuickMs,
             label + ": " + c.what + ", within budget (" + std::to_string(took) + " ms)");
    }
  }

  {
    // Sequential: once the budget is spent, later methods don't start.
    std::vector<Calls> calls;
    AuthManager manager;
    setUp(manager, ExecutionMode::Sequential, {kSlowAttempt, ok}, calls);
    manager.setDeadline(std::chrono::steady_clock::now() + std::chrono::milliseconds(300));
    Outcome rc = manager.authenticate("someone");
    expect(rc == Outcome::Failure && calls[1].authenticate == 0,
           "sequential: no method starts after the budget");
  }
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

  checkTiming();

  if (g_failures != 0) {
    std::printf("%d FAILED\n", g_failures);
    return 1;
  }
  std::printf("ALL PASS\n");
  return 0;
}
