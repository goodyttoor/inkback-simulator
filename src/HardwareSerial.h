#pragma once
#include <cstdio>
#include <iostream>

#include <cstring>

#include "SimWaitGate.h"

#include "Arduino.h"
#include "Print.h"
#include "Stream.h"
#include "WString.h"
class HWCDC : public Stream {
public:
  void begin(unsigned long baud) {}
  void setTxTimeoutMs(uint32_t timeoutMs) {}
  size_t write(uint8_t c) override {
    const char ch = (char)c;
    sim_wait::noteOutput(&ch, 1);
    std::cerr << ch;
    return 1;
  }
  size_t write(const uint8_t *buffer, size_t size) override {
    // Every byte the firmware prints also goes to the wait gate, so an input
    // script can block on something the firmware SAID rather than on a
    // millisecond guess. See SimWaitGate.h.
    sim_wait::noteOutput((const char *)buffer, size);
    std::cerr.write((const char *)buffer, size);
    return size;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  // BOTH branches feed the wait gate. This is the path the LOG_* macros take —
  // it writes to std::cerr directly rather than through write(), so hooking
  // only write() left the gate seeing nothing at all and every WAIT hanging
  // forever. The bug looked exactly like a broken scheduler.
  template <typename... Args> void printf(const char *format, Args... args) {
    if constexpr (sizeof...(Args) == 0) {
      sim_wait::noteOutput(format, std::strlen(format));
      std::cerr << format;
    } else {
      char buf[256];
      const int n = snprintf(buf, sizeof(buf), format, args...);
      if (n > 0) {
        sim_wait::noteOutput(buf, (size_t)n < sizeof(buf) - 1 ? (size_t)n : sizeof(buf) - 1);
      }
      std::cerr << buf;
    }
  }
  operator bool() const { return true; }
};

// CrossPoint uses HardwareSerial when ARDUINO_USB_CDC_ON_BOOT is not defined.
// The simulator has a single stderr-backed serial endpoint, so both Arduino
// serial types intentionally resolve to the same host implementation.
using HardwareSerial = HWCDC;

extern HWCDC Serial;
