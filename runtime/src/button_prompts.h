// Binding-aware prompt policy, adapted from ZeldaWWHDRecomp's separation of prompt style and
// physical input bindings (MPL-2.0). No SDL, game addresses or artwork in the policy.
#pragma once
#include "input_map.h"
#include <cstdint>
#include <map>
#include <string>

namespace button_prompts {
enum class Family { Unknown, Xbox, PlayStation, Nintendo };
enum class Style { Original, Xbox, PlayStation, Nintendo, Keyboard };
struct Device { uint32_t id; Family family; bool virtual_device; std::string name; };
// Physical pads are preferred over ambiguous virtual duplicates. Only meaningful new input
// changes the active device: a resting stick cannot flip prompts between two connected pads.
class Tracker {
public:
    void add(Device device);
    void remove(uint32_t id);
    void activity(uint32_t id);
    const Device* active() const;
    uint32_t generation() const { return generation_; }
private:
    void fallback();
    std::map<uint32_t,Device> devices_;
    uint32_t active_ = 0, generation_ = 0;
};
Style style(const input_map::Mapping& mapping, Family family);
struct Prompt {
    Style style = Style::Original;
    int input = input_map::kPadNone; // SDL physical-position identity; never an assumed Wii U letter
    std::string caption;
    bool original() const { return style == Style::Original; }
};
Prompt resolve(const input_map::Mapping& mapping, Family family, int action);
const char* style_name(Style style);
// Host event thread writes; game/UI threads read a snapshot. These never change PadState.
void connected(Device device);
void disconnected(uint32_t id);
void activity(uint32_t id);
Device active_device();
uint32_t device_generation();
}
