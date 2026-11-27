#pragma once

#include <cstdint>

namespace launcher::runtime {

// Turns a monotonic counter into the frames a caller owes it.
//
// A front end that presents at most once per iteration cannot run one frame per
// update and stay on the clock: fast forward needs a whole batch to reach its
// multiple of real time, and normal speed needs the frames a hitch swallowed.
// The batch is capped, and what is still owed past the cap is dropped instead
// of chased, so a suspend does not turn into a burst of caught-up frames.
class FrameClock {
 public:
  // The counter value the next frame is due at.
  [[nodiscard]] std::uint64_t deadline() const noexcept { return deadline_; }

  [[nodiscard]] std::uint64_t period() const noexcept { return period_; }

  // A zero period is a core claiming an impossible frame rate: treat it as one
  // tick rather than spinning through an infinite batch.
  void setPeriod(std::uint64_t period) noexcept {
    period_ = period == 0 ? 1 : period;
  }

  // Restarts the cadence at `now`, owing nothing and banking nothing.
  void restart(std::uint64_t now) noexcept { deadline_ = now; }

  // Frames due at `now`. `period` overrides period() for this call, which is
  // how fast forward and slow motion scale the clock; at most `cap` frames are
  // handed out, and the deadline moves with them.
  [[nodiscard]] unsigned due(std::uint64_t now, std::uint64_t period, unsigned cap) noexcept;

 private:
  std::uint64_t period_ = 1;
  std::uint64_t deadline_ = 0;
};

} // namespace launcher::runtime
