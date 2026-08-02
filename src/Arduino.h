#pragma once
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <string>
#include <thread>

#define PROGMEM
#define ICACHE_RODATA_ATTR
#define IRAM_ATTR
#define DRAM_ATTR
#define RTC_NOINIT_ATTR
#define PGM_P const char *
#define PSTR(s) (s)

// No pins here. Returns 0 = "not pressed" for the X4 Pro's active-low buttons,
// so the recovery-combo diagnostic in main.cpp logs a consistent released state
// rather than a floating value that would look like a stuck pin.
inline int digitalRead(int) { return 0; }
inline void digitalWrite(int, int) {}
inline void pinMode(int, int) {}

inline unsigned long millis() {
  using namespace std::chrono;
  static const auto start = steady_clock::now();
  return duration_cast<milliseconds>(steady_clock::now() - start).count();
}

inline unsigned long micros() {
  using namespace std::chrono;
  static const auto start = steady_clock::now();
  return duration_cast<microseconds>(steady_clock::now() - start).count();
}

inline void delay(unsigned long ms) {
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}
inline void yield() { std::this_thread::yield(); }

#include "HardwareSerial.h"
#include "Print.h"
#include "WString.h"

struct ESPMock {
  uint32_t getFreeHeap() { return 1024 * 1024; }
  void restart() {}
  uint32_t getHeapSize() { return 1024 * 1024; }
  uint32_t getMinFreeHeap() { return 1024 * 1024; }
  uint32_t getMaxAllocHeap() { return 1024 * 1024; }
  // PSRAM: ZERO, because this simulator has none. Firmware that reports PSRAM
  // guards on getPsramSize() > 0, so returning zero makes that guard false and
  // the block compiles here without pretending to a pool that does not exist.
  // Reporting a fake size would be worse than not having these at all: it would
  // make a PSRAM regression look fine on the desktop.
  uint32_t getPsramSize() { return 0; }
  uint32_t getFreePsram() { return 0; }
  uint32_t getMinFreePsram() { return 0; }
  uint32_t getMaxAllocPsram() { return 0; }
};
extern ESPMock ESP;

inline long random(long max) { return std::rand() % max; }

template <typename A, typename B>
constexpr auto max(A a, B b) -> decltype(a > b ? a : b) {
  return a > b ? a : b;
}
template <typename A, typename B>
constexpr auto min(A a, B b) -> decltype(a < b ? a : b) {
  return a < b ? a : b;
}
