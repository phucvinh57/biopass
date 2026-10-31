// Checks clampStrategyLimits (core/auth_config.h): retry counts, delays and
// timeouts from the user's config are kept in ranges that can't stall the
// helper. Exits non-zero on any failure.

#include <chrono>
#include <cstdio>

#include "auth_config.h"
#include "auth_deadline.h"

namespace {

int g_failures = 0;

void expect(bool cond, const char* what) {
  std::printf("[%s] %s\n", cond ? "PASS" : "FAIL", what);
  if (!cond) {
    g_failures++;
  }
}

}  // namespace

int main() {
  using biopass::MethodsConfig;

  {
    MethodsConfig defaults;
    MethodsConfig m = defaults;
    biopass::clampStrategyLimits(m);
    expect(m.face.retries == defaults.face.retries &&
               m.face.retry_delay == defaults.face.retry_delay &&
               m.face.anti_spoofing.ir_warmup_delay_ms ==
                   defaults.face.anti_spoofing.ir_warmup_delay_ms &&
               m.face.anti_spoofing.ir_presence_timeout_ms ==
                   defaults.face.anti_spoofing.ir_presence_timeout_ms &&
               m.fingerprint.retries == defaults.fingerprint.retries &&
               m.fingerprint.timeout == defaults.fingerprint.timeout,
           "defaults are unchanged");
  }

  {
    MethodsConfig m;
    m.face.retries = 4000000000u;
    m.face.retry_delay = 4000000000u;
    m.face.anti_spoofing.ir_warmup_delay_ms = 2000000000;
    m.face.anti_spoofing.ir_presence_timeout_ms = 2000000000;
    m.fingerprint.retries = 4000000000u;
    m.fingerprint.timeout = 4000000000u;
    biopass::clampStrategyLimits(m);
    expect(m.face.retries == biopass::kMaxFaceRetries, "face.retries capped");
    expect(m.face.retry_delay == biopass::kMaxRetryDelayMs, "face.retry_delay capped");
    expect(m.face.anti_spoofing.ir_warmup_delay_ms == biopass::kMaxIrWarmupDelayMs,
           "ir_warmup_delay_ms capped");
    expect(m.face.anti_spoofing.ir_presence_timeout_ms == biopass::kMaxIrPresenceTimeoutMs,
           "ir_presence_timeout_ms capped");
    expect(m.fingerprint.retries == biopass::kMaxFingerprintRetries, "fingerprint.retries capped");
    expect(m.fingerprint.timeout == biopass::kMaxFingerprintTimeoutMs,
           "fingerprint.timeout capped");
  }

  {
    MethodsConfig m;
    m.fingerprint.timeout = 0;  // used to mean "wait forever"
    m.face.anti_spoofing.ir_warmup_delay_ms = -5;
    m.face.anti_spoofing.ir_presence_timeout_ms = -5;
    biopass::clampStrategyLimits(m);
    expect(m.fingerprint.timeout == biopass::kMaxFingerprintTimeoutMs,
           "fingerprint.timeout 0 -> the maximum, not forever");
    expect(m.face.anti_spoofing.ir_warmup_delay_ms == 0, "negative ir_warmup_delay_ms -> 0");
    expect(m.face.anti_spoofing.ir_presence_timeout_ms == 0,
           "negative ir_presence_timeout_ms -> 0");
  }

  {
    MethodsConfig m;
    m.face.retries = 3;
    m.face.retry_delay = 0;
    m.fingerprint.retries = 2;
    m.fingerprint.timeout = 1234;
    biopass::clampStrategyLimits(m);
    expect(m.face.retries == 3 && m.face.retry_delay == 0 && m.fingerprint.retries == 2 &&
               m.fingerprint.timeout == 1234,
           "in-range values are kept");
  }

  {
    // The clamps alone can't keep a run under the PAM kill. Worst case of
    // the config-driven waits in sequential [face, fingerprint], each face
    // attempt also waiting on a stuck colour capture (10 s), the IR warmup
    // and the IR presence retries:
    using std::chrono::milliseconds;
    const long long capture_ms = 10000;  // kColorCaptureTimeoutMs, face/common
    const long long face_ms =
        static_cast<long long>(biopass::kMaxFaceRetries) *
            (capture_ms + biopass::kMaxIrWarmupDelayMs + biopass::kMaxIrPresenceTimeoutMs) +
        static_cast<long long>(biopass::kMaxFaceRetries - 1) * biopass::kMaxRetryDelayMs;
    const long long fp_ms =
        static_cast<long long>(biopass::kMaxFingerprintRetries) * biopass::kMaxFingerprintTimeoutMs;
    const long long sum_ms = face_ms + fp_ms;
    const long long budget_ms = milliseconds(biopass::kAuthBudget).count();
    std::printf("worst-case sum %lld ms, budget %lld ms, PAM kill %lld ms\n", sum_ms, budget_ms,
                static_cast<long long>(milliseconds(biopass::kHelperTimeout).count()));
    expect(sum_ms > budget_ms, "worst-case sum exceeds the budget, so the budget must cut it");
    // So AuthManager stops at kAuthBudget (test/auth_manager), and what can't
    // be cancelled after that must still fit before the kill.
    expect(budget_ms + capture_ms < milliseconds(biopass::kHelperTimeout).count(),
           "budget + one uncancellable capture fits before the PAM kill");
    // Fingerprint's part that can't be cancelled: the fprintd call in flight,
    // then VerifyStop and Release in cleanup, each with its own timeout.
    const long long fp_tail_ms =
        biopass::kFprintdCallTimeoutMs + 2 * biopass::kFprintdStopTimeoutMs;
    const long long margin_ms = milliseconds(biopass::kTeardownMargin).count();
    const long long kill_ms = milliseconds(biopass::kHelperTimeout).count();
    std::printf("uncancellable tail: face %lld ms, fingerprint %lld ms, margin %lld ms\n",
                capture_ms, fp_tail_ms, margin_ms);
    expect(budget_ms + capture_ms + margin_ms <= kill_ms,
           "budget + face tail + teardown margin fits before the PAM kill");
    expect(budget_ms + fp_tail_ms + margin_ms <= kill_ms,
           "budget + fingerprint tail + teardown margin fits before the PAM kill");
    expect(biopass::kFprintdCallTimeoutMs > 0 && biopass::kFprintdStopTimeoutMs > 0,
           "fprintd calls have explicit, finite timeouts");
    // Every single clamped wait fits in the budget, so each can run once.
    expect(biopass::kMaxRetryDelayMs < budget_ms && biopass::kMaxFingerprintTimeoutMs < budget_ms &&
               biopass::kMaxIrWarmupDelayMs + biopass::kMaxIrPresenceTimeoutMs + capture_ms <
                   budget_ms,
           "each clamped wait fits in the budget");
  }

  if (g_failures != 0) {
    std::printf("%d FAILED\n", g_failures);
    return 1;
  }
  std::printf("ALL PASS\n");
  return 0;
}
