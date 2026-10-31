#pragma once

// Time limits shared by the PAM module (pam/helper_wait.h) and the helper
// (AuthManager), and a cancel-aware sleep for the waits inside a run.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>

namespace biopass {

// The PAM module SIGKILLs biopass-helper after this, so a stuck camera or
// fprintd can't keep the user from the password prompt.
inline constexpr std::chrono::seconds kHelperTimeout{60};

// The helper's own budget. Once it is spent AuthManager starts no attempt or
// retry sleep and cancels the attempts in flight. The gap to kHelperTimeout
// covers what can't be cancelled: a camera capture that is already waiting
// (kColorCaptureTimeoutMs in face/common/camera_capture.h, checked there),
// plus teardown. A result reached within the budget is never lost to the kill.
inline constexpr std::chrono::seconds kAuthBudget{40};

// Sleeps for `duration`, waking every few ms to check `cancel` (may be
// null). Returns false if it stopped early because `cancel` was set.
inline bool sleepUnlessCancelled(std::chrono::milliseconds duration,
                                 const std::atomic<bool>* cancel) {
  using Clock = std::chrono::steady_clock;
  const auto end = Clock::now() + duration;
  for (;;) {
    if (cancel != nullptr && cancel->load()) {
      return false;
    }
    const auto now = Clock::now();
    if (now >= end) {
      return true;
    }
    std::this_thread::sleep_for(
        std::min<Clock::duration>(end - now, std::chrono::milliseconds(20)));
  }
}

}  // namespace biopass
