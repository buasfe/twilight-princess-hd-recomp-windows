#pragma once
#include "input_map.h"

namespace overlay {
// Wind Waker HD issue #153: release each device's captured inputs independently.
// A drifting axis or held trigger must never block unrelated buttons or another pad.
class ReleaseGate {
public:
    void filter(float values[input_map::kPadCount], bool captured) {
        for (int p = 0; p < input_map::kPadCount; ++p) {
            const bool held = values[p] > 0.5f;
            held_back_[p] = captured ? held_back_[p] || held : held_back_[p] && held;
            if (captured || held_back_[p]) values[p] = 0;
        }
    }
private:
    bool held_back_[input_map::kPadCount] = {};
};
}
