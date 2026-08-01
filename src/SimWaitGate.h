#pragma once

// Lets an input script WAIT for something the firmware has said, instead of
// guessing how long it will take.
//
// The scripts are scheduled in milliseconds — "3000:TAP:240,560" — which is
// fine on an idle machine and wrong on a busy one. A tap aimed at a list can
// land before the list has drawn, and the gate then reports a firmware failure
// that is really a scheduling one. It happened twice in one session, both times
// with another build running alongside.
//
// So: the simulator watches its own serial output and an event can block the
// rest of the schedule until a given substring appears. Timing stops being a
// guess about the machine and becomes a statement about the firmware — "after
// the emulator says it is playing", not "3.2 seconds in".
//
// Substring matching on the log is deliberately crude. The alternative is
// exposing internal state to the harness, which couples the two far more
// tightly; a log line is already a thing the firmware promises to print.

#include <cstddef>
#include <string>

namespace sim_wait {

// Called from the serial sink for every byte the firmware prints. Keeps a
// bounded tail — the marker only ever needs to be found once, and an unbounded
// buffer in a process that can run for minutes is a leak with extra steps.
void noteOutput(const char *data, size_t len);

// True once `marker` has appeared in anything printed so far.
bool sawMarker(const std::string &marker);

// Test seam: forget everything printed so far.
void reset();

}  // namespace sim_wait
