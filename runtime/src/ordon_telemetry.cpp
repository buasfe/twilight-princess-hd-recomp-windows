#include "ordon60.h"

#include "runtime.h"

#include <array>
#include <cstdlib>
#include <cstring>
#include <utility>

#include "gx2/gx2.h"
#include "interp.h"
#include "overlay/overlay.h"

namespace ordon60 {
namespace {
constexpr uint32_t kStage = 0x1014A5E8, kLinkPtr = 0x1014B5B0, kEvent = 0x1014A6B0;

bool selected(const char* name) {
    const char* value = std::getenv(name);
    return value && std::strcmp(value, "1") == 0;
}

bool in_ordon() {
    return std::memcmp(mem::ptr(kStage), "F_SP103", 8) == 0;
}

uint64_t timestamp_us() {
    using Clock = std::chrono::steady_clock;
    static const auto epoch = Clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - epoch).count();
}

const char* phase() {
    return supplemental_pass() ? "half" : paired_pass() ? "main" : "stock";
}

struct Revision {
    uint32_t updates = 0;
    uint32_t changes = 0;
    uint32_t object = 0;
    std::array<float, 3> value{};
    const char* pass = "stock";

    void advance(uint32_t id, const char* kind, const std::array<float, 3>& point) {
        if (!updates || id != object || point != value) ++changes;
        ++updates;
        object = id;
        value = point;
        pass = kind;
    }
};

thread_local Revision movement, camera, draw, pose, skeleton;
thread_local uint32_t picture_counter = 0, queued_picture = 0;
thread_local uint32_t present_picture = 0;
} // namespace

bool requested() {
    static const bool enabled = selected("WWHD_ORDON_TRUE60");
    return enabled;
}

bool tracing() {
    static const bool enabled = selected("WWHD_ORDON_TRACE");
    return enabled;
}

bool eligible() {
    if (!requested() || interp::mode() || gx2::turbo() || gx2::uncapped() ||
        !in_ordon() || ld8(kEvent + 0xE5) || overlay::blocks_input())
        return false;
    const uint32_t link = ld32(kLinkPtr);
    if (link < mem::kMem2Start || link > mem::kMem2End - 0x3060) return false;
    const uint16_t procedure = ld16(link + 0x305C);
    return procedure == 3 || procedure == 4;
}

void record(Event event, uint32_t object, float x, float y, float z) {
    if (!tracing() || !in_ordon()) return;
    constexpr const char* names[] = {
        "legacy", "input", "move", "collision", "camera", "draw", "pose", "orbit", "skeleton", "clip", "viewangle"
    };
    const char* pass = phase();
    if (event == Event::Movement) movement.advance(object, pass, {x, y, z});
    if (event == Event::Camera) camera.advance(object, pass, {x, y, z});
    if (event == Event::Draw) draw.advance(object, pass, {x, y, z});
    if (event == Event::Pose) pose.advance(object, pass, {x, y, z});
    if (event == Event::Skeleton) skeleton.advance(object, pass, {x, y, z});
    LOG("[ordon60] event=%s us=%llu phase=%s object=%08X x=%.6f y=%.6f z=%.6f",
        names[static_cast<unsigned>(event)], (unsigned long long)timestamp_us(), pass, object, x, y, z);
}

void begin_picture() {
    if (!tracing() || !in_ordon()) return;
    queued_picture = ++picture_counter;
    LOG("[ordon60] event=picture us=%llu ticket=%u phase=%s move=%u move_change=%u move_phase=%s "
        "camera=%u camera_change=%u camera_phase=%s draw=%u draw_change=%u pose=%u pose_change=%u "
        "skeleton=%u skeleton_change=%u skeleton_phase=%s",
        (unsigned long long)timestamp_us(), queued_picture, phase(), movement.updates, movement.changes,
        movement.pass, camera.updates, camera.changes, camera.pass, draw.updates, draw.changes,
        pose.updates, pose.changes, skeleton.updates, skeleton.changes, skeleton.pass);
}

uint32_t take_picture() { return std::exchange(queued_picture, 0); }

void set_present_picture(uint32_t picture) { present_picture = picture; }

void record_present() {
    if (!present_picture) return;
    LOG("[ordon60] event=present us=%llu ticket=%u",
        (unsigned long long)timestamp_us(), present_picture);
}
} // namespace ordon60
