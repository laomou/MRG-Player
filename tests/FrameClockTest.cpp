// The catch-up batch is the whole point of the frame clock, and it used to be
// checked by sleeping on a real core: an 80 ms wall clock, a stub core, and a
// range wide enough to hide whatever the machine felt like doing. These feed
// it synthetic counters instead, so a slow machine cannot fail them.

#include <cstdint>
#include <cstdio>

#include "runtime/FrameClock.hpp"

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
  if (condition)
    return;
  std::fprintf(stderr, "FAIL: %s\n", message);
  ++failures;
}

using launcher::runtime::FrameClock;

// A clock restarted at zero ticks once at the deadline it was given, then rolls
// forward one period per frame.
void firstFrameIsDueImmediately() {
  FrameClock clock;
  clock.setPeriod(10);
  clock.restart(1000);
  expect(clock.due(1000, clock.period(), 4) == 1,
         "a restarted clock owes the frame it was started with");
  expect(clock.deadline() == 1010, "the deadline moved on by one period");
  expect(clock.due(1009, clock.period(), 4) == 0,
         "nothing is due before the next deadline");
}

// Every period the caller was asleep for is a frame it owes, in order.
void everyPeriodOwedIsRun() {
  FrameClock clock;
  clock.setPeriod(10);
  clock.restart(0);
  expect(clock.due(0, clock.period(), 8) == 1, "the first frame runs");
  expect(clock.due(39, clock.period(), 8) == 3,
         "three frames came due at 10, 20 and 30");
  expect(clock.deadline() == 40, "the deadline landed on the next period");
}

// A stall longer than the cap is not repaid frame by frame: the batch stops at
// the cap and the backlog is dropped, otherwise every later update would spend
// the whole budget catching up and the game would never be on the clock again.
void aStallIsNotRepaidInFull() {
  FrameClock clock;
  clock.setPeriod(10);
  clock.restart(0);
  expect(clock.due(100000, clock.period(), 4) == 4,
         "a long stall runs exactly the cap");
  expect(clock.deadline() == 100010,
         "the deadline restarts one period after the stall instead of staying behind");
  expect(clock.due(100010, clock.period(), 4) == 1,
         "the clock is settled again after dropping the backlog");
}

// Fast forward and slow motion scale the period per call, and the clock follows
// the period it was handed rather than the one stored on it.
void thePeriodCanBeScaled() {
  FrameClock clock;
  clock.setPeriod(10);
  clock.restart(0);
  expect(clock.due(0, 5, 8) == 1, "the scaled period still owes one frame");
  expect(clock.due(4, 5, 8) == 0, "a halved period does not come due early");
  expect(clock.due(5, 5, 8) == 1, "a halved period comes due twice as often");
  expect(clock.due(9, 20, 8) == 0, "a doubled period waits longer");
  expect(clock.due(10, 20, 8) == 1, "the doubled period comes due on time");
  expect(clock.due(29, 20, 8) == 0, "and then waits a full doubled period");
}

// A core claiming a zero period must not make the loop spin or divide by zero.
void aZeroPeriodIsOneTick() {
  FrameClock clock;
  clock.setPeriod(0);
  expect(clock.period() == 1, "a zero period is clamped to one tick");
  clock.restart(0);
  expect(clock.due(5, 0, 4) == 4, "a zero period per call runs the cap");
  expect(clock.deadline() == 4, "and still advances one tick per frame");
}

// A cap of zero would silently freeze the clock, so it is treated as one.
void aZeroCapStillRunsAFrame() {
  FrameClock clock;
  clock.setPeriod(10);
  clock.restart(0);
  expect(clock.due(100, clock.period(), 0) == 1,
         "a zero cap is one frame, never none");
}

} // namespace

int main() {
  firstFrameIsDueImmediately();
  everyPeriodOwedIsRun();
  aStallIsNotRepaidInFull();
  thePeriodCanBeScaled();
  aZeroPeriodIsOneTick();
  aZeroCapStillRunsAFrame();
  if (failures != 0) {
    std::fprintf(stderr, "%d frame clock checks failed\n", failures);
    return 1;
  }
  std::printf("frame clock ok\n");
  return 0;
}
