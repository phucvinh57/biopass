#pragma once

namespace biopass {

// Frames libcamera flags as failed (or that fail to convert) don't count as
// samples, so a capture keeps pulling frames after one is dropped. The drop
// cap bounds the loop when there is no capture deadline.
class FrameSampling {
 public:
  FrameSampling(int wanted, int max_dropped) : wanted_(wanted), max_dropped_(max_dropped) {}

  bool wantsMore() const { return good_ < wanted_ && dropped_ < max_dropped_; }
  bool gotAny() const { return good_ > 0; }
  void recordGood() { ++good_; }
  void recordDropped() { ++dropped_; }

 private:
  int wanted_;
  int max_dropped_;
  int good_ = 0;
  int dropped_ = 0;
};

}  // namespace biopass
