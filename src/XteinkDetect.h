#pragma once

// Simulator shim for the SDK's Xteink runtime detection.
//
// The firmware calls this before display.begin() to resolve which panel
// controller a unit carries: newer X4 / X4 Pro batches ship a UC8179 where
// original ones have an SSD1677, and the real implementation reads the OEM
// factory value from NVS (hw_calib/screenType), falling back to a display-bus
// probe.
//
// Neither exists here. The simulator has no NVS partition and no display bus —
// its "panel" is a pixel buffer — so there is nothing to detect and nothing that
// could honestly be promoted. Returning false means "no UltraChip sibling
// present", which is the correct answer for a simulated device rather than a
// placeholder: the simulator composites its own greys and never runs a
// controller driver at all.
//
// If a simulated UC8179 is ever wanted, it belongs behind an explicit build flag
// or env var, not behind a probe that cannot probe.

namespace freeink {

inline bool applyXteinkDisplayController() { return false; }


// The record the ONE legitimate boot probe leaves behind, so the panel-id
// screen can report it without touching hardware.
//
// NEVER VALID HERE, and that is the honest answer rather than a limitation:
// detectXteinkDisplayController() pulses the panel's reset line, there is no
// panel, and a made-up LUT_VER would be worse than none — that screen exists
// precisely to report what the silicon said. DisplayTestActivity checks
// `valid` and prints "No display probe on this build."
struct DisplayProbeRecord {
  bool valid = false;
  uint8_t ver[5] = {0};
  uint8_t flg = 0;
  uint32_t lutVer = 0;
  bool ultraChip = false;
  bool screenTypeKnown = false;
  uint8_t screenType = 0;
};

inline const DisplayProbeRecord &lastDisplayProbe() {
  static const DisplayProbeRecord kNever;
  return kNever;
}

}  // namespace freeink
