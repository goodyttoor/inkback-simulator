#include "SimWaitGate.h"

#include <mutex>

namespace sim_wait {
namespace {

// Bounded. Long enough to hold several screens' worth of logging so a marker is
// not missed between polls, short enough that a long run does not grow without
// limit. Markers are matched against the tail, so a marker printed and then
// scrolled past before the scheduler next ticks would be missed — the scheduler
// runs every frame, which is far more often than 64 KB of logging accumulates.
constexpr size_t kMaxTail = 64 * 1024;

std::string &buffer() {
  static std::string tail;
  return tail;
}

std::mutex &lock() {
  // The serial sink is written from the firmware thread and read from the
  // input scheduler; without this the two race on the same std::string.
  static std::mutex m;
  return m;
}

}  // namespace

void noteOutput(const char *data, const size_t len) {
  if (data == nullptr || len == 0) return;
  std::lock_guard<std::mutex> guard(lock());
  std::string &tail = buffer();
  tail.append(data, len);
  if (tail.size() > kMaxTail) {
    tail.erase(0, tail.size() - kMaxTail);
  }
}

bool consumeMarker(const std::string &marker) {
  if (marker.empty()) return true;
  std::lock_guard<std::mutex> guard(lock());
  std::string &tail = buffer();
  const size_t at = tail.find(marker);
  if (at == std::string::npos) return false;
  // Erase through the match, so the same marker used again means "the next
  // one" rather than "this one, forever".
  tail.erase(0, at + marker.size());
  return true;
}

void reset() {
  std::lock_guard<std::mutex> guard(lock());
  buffer().clear();
}

}  // namespace sim_wait
