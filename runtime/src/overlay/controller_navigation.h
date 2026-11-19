#pragma once
#include <algorithm>
#include <cmath>

namespace overlay {
struct NavigationInput { bool down; float value; };
// ImGui reads AnalogValue for scrolling even when Down is false. Apply a menu dead zone to
// stick magnitudes before sending either field; raw host values remain available for remapping.
inline NavigationInput navigation_input(float raw, bool stick, float configured_deadzone) {
    if (!std::isfinite(raw)) return {false, 0};
    float value = std::clamp(raw, 0.0f, 1.0f);
    if (stick) {
        const float deadzone = std::clamp(configured_deadzone, 0.15f, 0.9f);
        value = std::max(0.0f, (value - deadzone) / (1.0f - deadzone));
    }
    // Retain the existing deliberate half-travel navigation threshold. ImGui owns key repeat.
    return {raw > 0.5f && value > 0, value};
}
}
