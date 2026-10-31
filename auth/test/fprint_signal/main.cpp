// Checks how fprintd VerifyStatus and EnrollStatus signals are filtered and
// mapped to a result (fingerprint/fprint_signal.h). Exits non-zero on failure.

#include <cstdio>

#include "fprint_signal.h"

namespace {

using biopass::acceptVerifyStatus;
using biopass::AuthResult;
using biopass::enrollWaitExpired;
using biopass::fprintdOwnerLost;
using biopass::isNoEnrolledPrints;
using biopass::kEnrollIdleTimeout;
using biopass::kEnrollMaxDuration;
using biopass::mapEnrollStatus;
using biopass::mapVerifyStatus;

int g_failures = 0;

void expect(bool cond, const char* what) {
  std::printf("[%s] %s\n", cond ? "PASS" : "FAIL", what);
  if (!cond) {
    g_failures++;
  }
}

bool is(const char* status, bool done, AuthResult want) {
  auto got = mapVerifyStatus(status, done);
  return got.has_value() && *got == want;
}

bool waits(const char* status, bool done) { return !mapVerifyStatus(status, done).has_value(); }

void testMapVerifyStatus() {
  expect(is("verify-match", true, AuthResult::Success), "verify-match ends with Success");
  expect(is("verify-no-match", true, AuthResult::Failure), "final verify-no-match is Failure");
  expect(waits("verify-no-match", false), "non-final verify-no-match keeps waiting");
  expect(is("verify-unknown-error", true, AuthResult::Unavailable), "unknown error is Unavailable");
  expect(is("verify-unknown-error", false, AuthResult::Unavailable),
         "unknown error ends the wait even when not done");
  expect(is("verify-disconnected", true, AuthResult::Unavailable), "disconnected is Unavailable");
  expect(is("verify-retry-scan", true, AuthResult::Retry), "final retry-scan is Retry");
  expect(waits("verify-retry-scan", false), "non-final retry-scan keeps waiting");
  expect(waits("verify-swipe-too-short", false), "swipe-too-short keeps waiting");
  expect(is("something-new", true, AuthResult::Retry), "unknown final status is Retry");
  expect(waits("", false), "empty status keeps waiting");
  expect(!is("verify-match-ish", true, AuthResult::Success), "only exact verify-match succeeds");
  expect(!is("VERIFY-MATCH", true, AuthResult::Success), "match is case sensitive");
}

void testAcceptVerifyStatus() {
  const std::string owner = ":1.42";
  expect(acceptVerifyStatus(":1.42", owner, true), "signal from fprintd's unique name is accepted");
  expect(!acceptVerifyStatus(":1.666", owner, true), "signal from another unique name is ignored");
  expect(!acceptVerifyStatus("net.reactivated.Fprint", owner, true),
         "signal claiming the well-known name is ignored");
  expect(!acceptVerifyStatus(nullptr, owner, true), "signal without a sender is ignored");
  expect(!acceptVerifyStatus(":1.42", owner, false), "wrong payload type is ignored");
  expect(!acceptVerifyStatus("", "", true), "unresolved owner accepts nothing");
  expect(!acceptVerifyStatus(":1.4", owner, true), "prefix of the owner is ignored");
  expect(!acceptVerifyStatus(":1.420", owner, true), "owner as a prefix is ignored");
}

void testFprintdOwnerLost() {
  const std::string owner = ":1.42";
  expect(fprintdOwnerLost("", owner), "fprintd exiting (empty new owner) is lost");
  expect(fprintdOwnerLost(":1.99", owner), "fprintd restarting under a new name is lost");
  expect(!fprintdOwnerLost(":1.42", owner), "same owner is not lost");
  expect(fprintdOwnerLost("", ""), "unresolved owner counts as lost");
}

bool enrolls(const char* status, bool done, bool want) {
  auto got = mapEnrollStatus(status, done);
  return got.has_value() && *got == want;
}

bool enrollWaits(const char* status, bool done) {
  return !mapEnrollStatus(status, done).has_value();
}

void testMapEnrollStatus() {
  expect(enrolls("enroll-completed", true, true), "enroll-completed succeeds");
  expect(enrolls("enroll-completed", false, true), "enroll-completed succeeds even if not done");
  expect(enrollWaits("enroll-stage-passed", false), "a passed stage keeps waiting");
  expect(enrolls("enroll-stage-passed", true, false), "final stage-passed is not a completion");
  expect(enrollWaits("enroll-retry-scan", false), "retry-scan keeps waiting");
  expect(enrollWaits("enroll-swipe-too-short", false), "swipe-too-short keeps waiting");
  expect(enrollWaits("enroll-remove-and-retry", false), "remove-and-retry keeps waiting");
  expect(enrolls("enroll-unknown-error", false, false), "unknown error fails even if not done");
  expect(enrolls("enroll-disconnected", false, false), "disconnected fails even if not done");
  expect(enrolls("enroll-failed", true, false), "final enroll-failed fails");
  expect(enrolls("enroll-data-full", true, false), "final data-full fails");
  expect(enrolls("enroll-duplicate", true, false), "final duplicate fails");
  expect(enrolls("something-new", true, false), "unknown final status fails");
  expect(enrollWaits("", false), "empty status keeps waiting");
  expect(!enrolls("enroll-completed-ish", true, true), "only exact enroll-completed succeeds");
}

void testEnrollWaitExpired() {
  using std::chrono::seconds;
  expect(!enrollWaitExpired(seconds(0), seconds(0)), "fresh wait is not expired");
  expect(!enrollWaitExpired(kEnrollIdleTimeout - seconds(1), kEnrollIdleTimeout - seconds(1)),
         "just under the idle timeout keeps waiting");
  expect(enrollWaitExpired(kEnrollIdleTimeout, kEnrollIdleTimeout),
         "no status for the idle timeout expires");
  expect(!enrollWaitExpired(kEnrollIdleTimeout * 3, seconds(5)),
         "an active user past the idle timeout keeps waiting");
  expect(enrollWaitExpired(kEnrollMaxDuration, seconds(0)),
         "the absolute cap expires even while scanning");
  expect(kEnrollIdleTimeout < kEnrollMaxDuration, "idle timeout is below the cap");
}

void testIsNoEnrolledPrints() {
  expect(isNoEnrolledPrints("net.reactivated.Fprint.Error.NoEnrolledPrints"),
         "NoEnrolledPrints means an empty list");
  expect(!isNoEnrolledPrints("net.reactivated.Fprint.Error.PermissionDenied"),
         "permission denied is a failure");
  expect(!isNoEnrolledPrints(""), "a local error (timeout, no bus) is a failure");
  expect(!isNoEnrolledPrints("net.reactivated.Fprint.Error.NoEnrolledPrintsX"),
         "only the exact error name counts");
}

}  // namespace

int main() {
  testMapVerifyStatus();
  testAcceptVerifyStatus();
  testFprintdOwnerLost();
  testMapEnrollStatus();
  testEnrollWaitExpired();
  testIsNoEnrolledPrints();
  if (g_failures != 0) {
    std::printf("%d check(s) failed\n", g_failures);
    return 1;
  }
  std::printf("all checks passed\n");
  return 0;
}
