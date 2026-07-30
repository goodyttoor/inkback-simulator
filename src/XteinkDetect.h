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

}  // namespace freeink
