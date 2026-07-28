#pragma once

// Headless replacements for the small slice of SDL the simulator's INPUT and
// TIMING paths use, so those files need no #ifdef beyond swapping this header
// in for <SDL.h>.
//
// WHY THIS EXISTS. Under AddressSanitizer, merely LINKING SDL2 hangs the process
// before main(): SDL2's dllinit is a dylib load constructor and it raises a
// modal error_dialog that never returns. A five-line program that links SDL2 and
// never calls it hangs identically. So there is no runtime flag or code path
// that rescues ASan — a headless build has to stop linking SDL2 outright, which
// means every SDL symbol must be gone at compile time, not merely unused.
//
// The display path needs real work (software rotation and a BMP writer, in
// HalDisplay.cpp). The input path does not: it wants a millisecond clock, a
// scancode enum, and an event queue that is always empty because a headless run
// is driven entirely by CROSSPOINT_SIM_INPUT_SCRIPT.

#include <chrono>
#include <cstdint>
#include <thread>

using SDL_Scancode = int;

// Values are arbitrary but must be distinct: nothing outside this header
// depends on them matching SDL's, because a headless build has no keyboard.
enum {
  SDL_SCANCODE_UNKNOWN = 0,
  SDL_SCANCODE_ESCAPE,
  SDL_SCANCODE_RETURN,
  SDL_SCANCODE_LEFT,
  SDL_SCANCODE_RIGHT,
  SDL_SCANCODE_UP,
  SDL_SCANCODE_DOWN,
  SDL_SCANCODE_P,
  SDL_SCANCODE_S,
  SDL_NUM_SCANCODES
};

enum { SDL_QUIT = 0x100, SDL_KEYDOWN = 0x300, SDL_KEYUP };

struct SDL_Keysym {
  SDL_Scancode scancode;
};

struct SDL_KeyboardEvent {
  uint8_t repeat;
  SDL_Keysym keysym;
};

struct SDL_Event {
  uint32_t type;
  SDL_KeyboardEvent key;
};

// Milliseconds since the first call, matching SDL_GetTicks()'s contract closely
// enough for the input script and long-press timing, which are the only
// consumers.
inline uint32_t SDL_GetTicks() {
  static const auto start = std::chrono::steady_clock::now();
  return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now() - start)
                                   .count());
}

inline void SDL_Delay(const uint32_t ms) {
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// Always empty: a headless run has no window and no keyboard, so every button
// press comes from the input script's synthetic-event path instead.
inline int SDL_PollEvent(SDL_Event *) { return 0; }

inline const uint8_t *SDL_GetKeyboardState(int *numkeys) {
  static const uint8_t none[SDL_NUM_SCANCODES] = {};
  if (numkeys) *numkeys = SDL_NUM_SCANCODES;
  return none;
}

inline void SDL_Quit() {}
