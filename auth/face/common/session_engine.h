#pragma once

#include <memory>
#include <utility>

namespace biopass {

// An engine kept for one auth session: the first get() that loads it keeps
// it, and later attempts reuse it instead of loading the model again. A load
// that fails (returns nullptr or throws) keeps nothing, so the next get()
// loads again. Not thread-safe: one attempt at a time.
template <typename Engine>
class SessionEngine {
 public:
  // load: () -> std::unique_ptr<Engine>; called only while nothing is kept.
  // Its exceptions propagate.
  template <typename Load>
  Engine* get(Load&& load) {
    if (!engine_) {
      engine_ = std::forward<Load>(load)();
    }
    return engine_.get();
  }

  bool loaded() const { return engine_ != nullptr; }

 private:
  std::unique_ptr<Engine> engine_;
};

}  // namespace biopass
