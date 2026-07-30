#pragma once

// Simulator shim for the ESP-IDF GPIO driver.
//
// PR #2675's main.cpp drives a few pins directly — releasing a deep-sleep hold
// and setting a level — outside the HAL, because those calls happen before the
// HAL exists or after it has been torn down. There are no pins here, so every
// call is a no-op that records nothing.
//
// Deliberately NOT routed to the simulator's HalGPIO: these are raw pin pokes at
// boot/shutdown boundaries, and pretending they map onto emulated buttons would
// invent behaviour the firmware never asked for.

#include <cstdint>

using gpio_num_t = int;

enum { GPIO_MODE_OUTPUT = 2 };

inline int gpio_set_direction(gpio_num_t, int) { return 0; }
inline int gpio_set_level(gpio_num_t, uint32_t) { return 0; }
inline int gpio_hold_dis(gpio_num_t) { return 0; }
inline int gpio_hold_en(gpio_num_t) { return 0; }
inline int gpio_deep_sleep_hold_dis() { return 0; }
