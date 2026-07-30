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

// GPIO_NUM_0..47 — the firmware names pins by constant, so the shim has to carry
// the names even though nothing here has pins. Generated as an enum rather than
// 48 #defines so a typo is a compile error and not a silently-zero macro.
enum {
  GPIO_NUM_0 = 0, GPIO_NUM_1, GPIO_NUM_2, GPIO_NUM_3, GPIO_NUM_4, GPIO_NUM_5,
  GPIO_NUM_6, GPIO_NUM_7, GPIO_NUM_8, GPIO_NUM_9, GPIO_NUM_10, GPIO_NUM_11,
  GPIO_NUM_12, GPIO_NUM_13, GPIO_NUM_14, GPIO_NUM_15, GPIO_NUM_16, GPIO_NUM_17,
  GPIO_NUM_18, GPIO_NUM_19, GPIO_NUM_20, GPIO_NUM_21, GPIO_NUM_22, GPIO_NUM_23,
  GPIO_NUM_24, GPIO_NUM_25, GPIO_NUM_26, GPIO_NUM_27, GPIO_NUM_28, GPIO_NUM_29,
  GPIO_NUM_30, GPIO_NUM_31, GPIO_NUM_32, GPIO_NUM_33, GPIO_NUM_34, GPIO_NUM_35,
  GPIO_NUM_36, GPIO_NUM_37, GPIO_NUM_38, GPIO_NUM_39, GPIO_NUM_40, GPIO_NUM_41,
  GPIO_NUM_42, GPIO_NUM_43, GPIO_NUM_44, GPIO_NUM_45, GPIO_NUM_46, GPIO_NUM_47,
};

inline int gpio_set_direction(gpio_num_t, int) { return 0; }
inline int gpio_set_level(gpio_num_t, uint32_t) { return 0; }
inline int gpio_hold_dis(gpio_num_t) { return 0; }
inline int gpio_hold_en(gpio_num_t) { return 0; }
inline int gpio_deep_sleep_hold_dis() { return 0; }
