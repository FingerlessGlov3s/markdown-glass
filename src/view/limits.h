#pragma once

// Bounds and defaults for the reading view, shared by the view, the settings
// and the settings dialog so they cannot drift apart.
namespace ViewLimits {

constexpr double MinZoom = 0.5;
constexpr double MaxZoom = 4.0;
constexpr double ZoomStep = 1.1;  // per zoom shortcut, and per notch of Ctrl+wheel
constexpr int MinTextWidth = 300; // pixels, when the width limit is on
constexpr int MaxTextWidth = 10000;
constexpr int DefaultTextWidth = 980; // GitHub's reading width

} // namespace ViewLimits
