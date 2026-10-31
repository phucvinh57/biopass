// Checks clampStrategyLimits (core/auth_config.h): retry counts, delays and
// timeouts from the user's config are kept in ranges that can't stall the
// helper. Exits non-zero on any failure.

#include <cstdio>

#include "auth_config.h"

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
    // Worst case of the sleeps alone stays under the PAM module's 60 s
    // deadline (pam/helper_wait.h kHelperTimeout).
    const unsigned face_sleep_ms = (biopass::kMaxFaceRetries - 1) * biopass::kMaxRetryDelayMs;
    const unsigned fp_wait_ms = biopass::kMaxFingerprintRetries * biopass::kMaxFingerprintTimeoutMs;
    expect(face_sleep_ms < 60000 && fp_wait_ms < 60000, "worst-case waits fit the PAM deadline");
  }

  if (g_failures != 0) {
    std::printf("%d FAILED\n", g_failures);
    return 1;
  }
  std::printf("ALL PASS\n");
  return 0;
}
