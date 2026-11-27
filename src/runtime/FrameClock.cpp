#include "runtime/FrameClock.hpp"

namespace launcher::runtime {

unsigned FrameClock::due(std::uint64_t now, std::uint64_t period, unsigned cap) noexcept {
  if (period == 0)
    period = 1;
  if (cap == 0)
    cap = 1;
  unsigned frames = 0;
  while (now >= deadline_ && frames < cap) {
    deadline_ += period;
    ++frames;
  }
  if (frames == 0)
    return 0;
  // Still behind after spending the whole cap? Drop the backlog instead of
  // paying it back frame by frame: a stall must cost one batch, not every
  // update from here on.
  if (now > deadline_ + period)
    deadline_ = now + period;
  return frames;
}

} // namespace launcher::runtime
