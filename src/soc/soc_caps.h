#pragma once

// Simulator shim for ESP-IDF's SoC capability macros.
//
// main.cpp compiles a wake-source branch behind SOC_PM_SUPPORT_EXT1_WAKEUP. The
// simulator has no power management at all, so the capability is absent and the
// branch compiles out — which is the honest answer: a desktop process cannot
// demonstrate anything about EXT1 wake behaviour, and stubbing it to 1 would
// build code the simulator can never exercise.
