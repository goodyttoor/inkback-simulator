#include "HalGPIO.h"

#include <BoardConfig.h>
#ifdef SIMULATOR_HEADLESS
#include "SimHeadless.h"
#else
#include <SDL.h>
#endif

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <string>
#include <vector>

#include "SimulatorLifecycle.h"

// Defined in HalDisplay.cpp — set here so all SDL event polling lives in one
// place.
extern std::atomic<bool> quitRequested;

// Keyboard mapping:
//   BTN_BACK    (0) → Escape
//   BTN_CONFIRM (1) → Return
//   BTN_LEFT    (2) → Left arrow
//   BTN_RIGHT   (3) → Right arrow
//   BTN_UP      (4) → Up arrow
//   BTN_DOWN    (5) → Down arrow
//   BTN_POWER   (6) → P
//   Simulator sleep shortcut → S

static constexpr int NUM_BUTTONS = 7;
static constexpr SDL_Scancode SIMULATOR_SLEEP_SCANCODE = SDL_SCANCODE_S;

static const SDL_Scancode buttonScancode[NUM_BUTTONS] = {
    SDL_SCANCODE_ESCAPE, // BTN_BACK
    SDL_SCANCODE_RETURN, // BTN_CONFIRM
    SDL_SCANCODE_LEFT,   // BTN_LEFT
    SDL_SCANCODE_RIGHT,  // BTN_RIGHT
    SDL_SCANCODE_UP,     // BTN_UP
    SDL_SCANCODE_DOWN,   // BTN_DOWN
    SDL_SCANCODE_P,      // BTN_POWER
};

static bool pressedThisFrame[NUM_BUTTONS] = {};
static bool releasedThisFrame[NUM_BUTTONS] = {};
static unsigned long buttonPressTime[NUM_BUTTONS] = {};
static bool syntheticButtonDown[NUM_BUTTONS] = {};
static bool simulatorSleepRequested = false;

namespace {

enum class SyntheticAction { KeyDown, KeyUp, Sleep, Quit };

struct SyntheticEvent {
  unsigned long atMs;
  SyntheticAction action;
  int button = -1;
  bool handled = false;
};

std::vector<SyntheticEvent> syntheticEvents;
bool syntheticEventsInitialized = false;

void requestSimulatorSleep() {
  simulatorSleepRequested = true;
  // Current CrossPoint firmware sleeps on a held physical power button. Keep
  // the compatibility latch above for older consumers, and also drive the
  // current public HalGPIO state so the S shortcut follows the firmware path.
  pressedThisFrame[HalGPIO::BTN_POWER] = true;
  syntheticButtonDown[HalGPIO::BTN_POWER] = true;
  buttonPressTime[HalGPIO::BTN_POWER] = SDL_GetTicks();
}

std::string uppercase(std::string value) {
  std::transform(
      value.begin(), value.end(), value.begin(),
      [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return value;
}

int namedButton(const std::string &name) {
  if (name == "ESCAPE" || name == "BACK")
    return HalGPIO::BTN_BACK;
  if (name == "RETURN" || name == "ENTER" || name == "CONFIRM")
    return HalGPIO::BTN_CONFIRM;
  if (name == "LEFT")
    return HalGPIO::BTN_LEFT;
  if (name == "RIGHT")
    return HalGPIO::BTN_RIGHT;
  if (name == "UP")
    return HalGPIO::BTN_UP;
  if (name == "DOWN")
    return HalGPIO::BTN_DOWN;
  if (name == "P" || name == "POWER")
    return HalGPIO::BTN_POWER;
  return -1;
}

void initializeSyntheticEvents() {
  if (syntheticEventsInitialized)
    return;
  syntheticEventsInitialized = true;

  const char *script = std::getenv("CROSSPOINT_SIM_INPUT_SCRIPT");
  if (!script || script[0] == '\0')
    return;

  const std::string spec(script);
  size_t start = 0;
  while (start < spec.size()) {
    const size_t end = spec.find(';', start);
    const std::string item = spec.substr(
        start, end == std::string::npos ? std::string::npos : end - start);
    const size_t firstColon = item.find(':');
    const size_t secondColon = firstColon == std::string::npos
                                   ? std::string::npos
                                   : item.find(':', firstColon + 1);
    if (firstColon != std::string::npos) {
      const unsigned long atMs =
          std::strtoul(item.substr(0, firstColon).c_str(), nullptr, 10);
      const std::string key = uppercase(
          item.substr(firstColon + 1, secondColon == std::string::npos
                                          ? std::string::npos
                                          : secondColon - firstColon - 1));
      if (key == "QUIT") {
        syntheticEvents.push_back({atMs, SyntheticAction::Quit});
      } else if (key == "S" || key == "SLEEP") {
        syntheticEvents.push_back({atMs, SyntheticAction::Sleep});
      } else {
        const int button = namedButton(key);
        if (button >= 0) {
          const unsigned long holdMs =
              secondColon == std::string::npos
                  ? 80
                  : std::strtoul(item.substr(secondColon + 1).c_str(), nullptr,
                                 10);
          syntheticEvents.push_back({atMs, SyntheticAction::KeyDown, button});
          syntheticEvents.push_back(
              {atMs + holdMs, SyntheticAction::KeyUp, button});
        }
      }
    }

    if (end == std::string::npos)
      break;
    start = end + 1;
  }

  std::sort(syntheticEvents.begin(), syntheticEvents.end(),
            [](const SyntheticEvent &a, const SyntheticEvent &b) {
              return a.atMs < b.atMs;
            });
}

void processSyntheticEvents() {
  initializeSyntheticEvents();
  const unsigned long now = millis();
  for (auto &event : syntheticEvents) {
    if (event.handled || event.atMs > now)
      continue;
    event.handled = true;
    switch (event.action) {
    case SyntheticAction::KeyDown:
      pressedThisFrame[event.button] = true;
      syntheticButtonDown[event.button] = true;
      // Synthetic presses must use the SDL clock used by real key events.
      buttonPressTime[event.button] = SDL_GetTicks();
      break;
    case SyntheticAction::KeyUp:
      releasedThisFrame[event.button] = true;
      syntheticButtonDown[event.button] = false;
      break;
    case SyntheticAction::Sleep:
      requestSimulatorSleep();
      break;
    case SyntheticAction::Quit:
      quitRequested.store(true);
      break;
    }
  }
}

} // namespace

static void clearButtonState() {
  for (int i = 0; i < NUM_BUTTONS; i++) {
    pressedThisFrame[i] = false;
    releasedThisFrame[i] = false;
    buttonPressTime[i] = 0;
    syntheticButtonDown[i] = false;
  }
}

static int scancodeToButton(SDL_Scancode sc) {
  for (int i = 0; i < NUM_BUTTONS; i++) {
    if (buttonScancode[i] == sc)
      return i;
  }
  return -1;
}

void HalGPIO::begin() {
#if defined(SIMULATOR_DEVICE_X3)
  _deviceType = DeviceType::X3;
  BoardConfig::selectDevice(BoardConfig::Board::XteinkX3);
#else
  _deviceType = DeviceType::X4;
  BoardConfig::selectDevice(BoardConfig::Board::XteinkX4);
#endif
}

bool HalGPIO::isXteinkDevice() const { return true; }

bool HalGPIO::hasEdgeSideButtons() const { return deviceIsX3(); }

void HalGPIO::beginFrame() {
  // Clear the press/release edge latches once per frame. See update() for why
  // this is deliberately separate from the SDL poll.
  for (int i = 0; i < NUM_BUTTONS; i++) {
    pressedThisFrame[i] = false;
    releasedThisFrame[i] = false;
  }
}

void HalGPIO::update() {
  // Per-frame press/release edges are intentionally NOT cleared here; that
  // happens once per frame in beginFrame(). The firmware calls update() several
  // times within a single frame (e.g. CrossPointWebServerActivity polls input
  // between handleClient() bursts, on top of the top-of-loop gpio.update() in
  // main.cpp). If edges were cleared on every update(), a key press drained by
  // an earlier update() would be wiped before a later update()'s wasPressed()
  // check could observe it — which made Back/Exit require repeated presses.
  // Latching edges for the whole frame keeps wasPressed() stable across all
  // update() calls in that frame, matching the on-device InputManager.

  // HalGPIO owns all SDL event polling so keyboard and quit events are never
  // split between two callers (HalDisplay::presentIfNeeded only renders).
  SDL_Event e;
  while (SDL_PollEvent(&e) != 0) {
    if (e.type == SDL_QUIT) {
      quitRequested.store(true);
    } else if (e.type == SDL_KEYDOWN && !e.key.repeat) {
      if (e.key.keysym.scancode == SIMULATOR_SLEEP_SCANCODE) {
        requestSimulatorSleep();
        continue;
      }
      int btn = scancodeToButton(e.key.keysym.scancode);
      if (btn >= 0) {
        pressedThisFrame[btn] = true;
        buttonPressTime[btn] = SDL_GetTicks();
      }
    } else if (e.type == SDL_KEYUP) {
      int btn = scancodeToButton(e.key.keysym.scancode);
      if (btn >= 0) {
        releasedThisFrame[btn] = true;
      }
    }
  }
  processSyntheticEvents();
}

bool HalGPIO::isPressed(uint8_t buttonIndex) const {
  if (buttonIndex >= NUM_BUTTONS)
    return false;
  const uint8_t *state = SDL_GetKeyboardState(NULL);
  return state[buttonScancode[buttonIndex]] || syntheticButtonDown[buttonIndex];
}

bool HalGPIO::wasPressed(uint8_t buttonIndex) const {
  if (buttonIndex >= NUM_BUTTONS)
    return false;
  return pressedThisFrame[buttonIndex];
}

bool HalGPIO::wasReleased(uint8_t buttonIndex) const {
  if (buttonIndex >= NUM_BUTTONS)
    return false;
  return releasedThisFrame[buttonIndex];
}

bool HalGPIO::wasAnyPressed() const {
  for (int i = 0; i < NUM_BUTTONS; i++) {
    if (pressedThisFrame[i])
      return true;
  }
  return false;
}

bool HalGPIO::wasAnyReleased() const {
  for (int i = 0; i < NUM_BUTTONS; i++) {
    if (releasedThisFrame[i])
      return true;
  }
  return false;
}

unsigned long HalGPIO::getHeldTime() const {
  // Return the longest held time among all currently pressed buttons
  unsigned long now = SDL_GetTicks();
  unsigned long maxHeld = 0;
  const uint8_t *state = SDL_GetKeyboardState(NULL);
  for (int i = 0; i < NUM_BUTTONS; i++) {
    if ((state[buttonScancode[i]] || syntheticButtonDown[i]) &&
        buttonPressTime[i] > 0) {
      unsigned long held = now - buttonPressTime[i];
      if (held > maxHeld)
        maxHeld = held;
    }
  }
  return maxHeld;
}

unsigned long HalGPIO::getPowerButtonHeldTime() const {
  const uint8_t *state = SDL_GetKeyboardState(NULL);
  if ((!state[buttonScancode[BTN_POWER]] && !syntheticButtonDown[BTN_POWER]) ||
      buttonPressTime[BTN_POWER] == 0)
    return 0;
  return SDL_GetTicks() - buttonPressTime[BTN_POWER];
}

bool HalGPIO::hasTouch() const { return false; }
bool HalGPIO::hasHomeKey() const { return false; }
bool HalGPIO::wasHomeKeyPressed() const { return false; }
bool HalGPIO::wasHomeKeyTapped() const { return false; }
bool HalGPIO::wasHomeKeyLongPressed() const { return false; }
bool HalGPIO::wasTouchTap(float & /*nx*/, float & /*ny*/) const {
  return false;
}
bool HalGPIO::wasTouchDown(float & /*nx*/, float & /*ny*/) const {
  return false;
}
bool HalGPIO::wasTouchReleased() const { return false; }
bool HalGPIO::isTouchTapCandidate(float & /*nx*/, float & /*ny*/,
                                  unsigned long &heldMs) const {
  heldMs = 0;
  return false;
}
bool HalGPIO::isTouchHeldAt(float & /*nx*/, float & /*ny*/) const {
  return false;
}
unsigned long HalGPIO::lastTouchHeldMs() const { return 0; }
bool HalGPIO::wasSwipe(float & /*nxStart*/, float & /*nyStart*/,
                       float & /*nxEnd*/, float & /*nyEnd*/) const {
  return false;
}
bool HalGPIO::wasTouchActivity() const { return false; }
void HalGPIO::setSharedConfirmPowerShortPressEmitsPower(bool /*enabled*/) {}

bool HalGPIO::consumeSimulatorSleepRequest() {
  const bool requested = simulatorSleepRequested;
  simulatorSleepRequested = false;
  return requested;
}

HalGPIO::WakeupReason HalGPIO::getWakeupReason() const {
  if (SimulatorLifecycle::consumeWakeReason() ==
      SimulatorLifecycle::WakeReason::PowerButton) {
    return WakeupReason::PowerButton;
  }
  return WakeupReason::Other;
}
bool HalGPIO::isUsbConnected() const { return true; }
bool HalGPIO::wasUsbStateChanged() const { return false; }
void HalGPIO::startDeepSleep() {
  clearButtonState();

  while (true) {
    processSyntheticEvents();
    if (quitRequested.load())
      return;
    for (int button = 0; button < NUM_BUTTONS; button++) {
      if (syntheticButtonDown[button]) {
        clearButtonState();
        SimulatorLifecycle::rebootAsPowerWake();
      }
    }

    SDL_Event e;
    while (SDL_PollEvent(&e) != 0) {
      if (e.type == SDL_QUIT) {
        quitRequested.store(true);
        return;
      }

      if (e.type == SDL_KEYDOWN && !e.key.repeat &&
          scancodeToButton(e.key.keysym.scancode) >= 0) {
        clearButtonState();
        SimulatorLifecycle::rebootAsPowerWake();
      }
    }

    SDL_Delay(10);
  }
}
bool HalGPIO::verifyPowerButtonWakeup(uint16_t /*requiredDurationMs*/,
                                      bool /*shortPressAllowed*/) {
  return true;
}

HalGPIO gpio;
