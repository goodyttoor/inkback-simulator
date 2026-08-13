#pragma once

#include <Arduino.h>
#include <InputManager.h>
#include <Logging.h>
#include <freertos/semphr.h>

#include <cassert>

#include "HalGPIO.h"

class HalPowerManager;
extern HalPowerManager powerManager; // Singleton

class HalPowerManager {
  int normalFreq = 0; // MHz
  bool isLowPower = false;

  enum LockMode { None, NormalSpeed };
  LockMode currentLockMode = None;
  SemaphoreHandle_t modeMutex = nullptr; // Protect access to currentLockMode

public:
  static constexpr int LOW_POWER_FREQ = 10;                   // MHz
  static constexpr unsigned long IDLE_POWER_SAVING_MS = 3000; // ms

  void begin();

  // Control CPU frequency for power saving
  void setPowerSaving(bool enabled);

  // Setup wake up GPIO and enter deep sleep
  // Should be called inside main loop() to handle the currentLockMode
  void startDeepSleep(HalGPIO &gpio) const;

  // The light-sleep surface X4 Pro Beta 20 expects. NONE OF IT IS SIMULATED.
  //
  // On device these coordinate light sleep with e-ink refreshes: the driver
  // calls onEinkBusyWaitSlice() while polling the panel's BUSY pin so a 0.3-2 s
  // wait can be slept through, and the two noteRenderWait* calls tell it the
  // main loop is parked in requestUpdateAndWait() and cannot poll input
  // meanwhile. The simulator has no BUSY pin, no light sleep and an immediate
  // refresh, so there is nothing to reproduce — these exist so main.cpp and
  // ActivityManager compile unchanged, and are deliberately inert rather than
  // approximated.

  // Idle threshold before the device light-sleeps between input polls. Kept at
  // the device value so a gate asserting on timing sees the same number.
  static constexpr unsigned long IDLE_LIGHT_SLEEP_MS = 1000;

  // Returns true on device when the slice actually slept. Always false here:
  // claiming a sleep happened would make the caller skip its own polling.
  bool onEinkBusyWaitSlice(int8_t, uint8_t) { return false; }

  // Bumped once per main-loop body on device, read by the render task.
  void noteMainLoopIteration() {}

  void noteRenderWaitBegin() {}
  void noteRenderWaitEnd() {}

  // Get battery percentage (range 0-100)
  uint16_t getBatteryPercentage() const;

  // RAII helper class to manage power saving locks
  // Usage: create an instance of Lock in a scope to disable power saving, for
  // example when running a task that needs full performance. When the Lock
  // instance is destroyed (goes out of scope), power saving will be re-enabled.
  class Lock {
    friend class HalPowerManager;
    bool valid = false;

  public:
    explicit Lock();
    ~Lock();

    // Non-copyable and non-movable
    Lock(const Lock &) = delete;
    Lock &operator=(const Lock &) = delete;
    Lock(Lock &&) = delete;
    Lock &operator=(Lock &&) = delete;
  };
};
