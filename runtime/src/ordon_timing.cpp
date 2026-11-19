#include "ordon60.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <numbers>
#include <utility>

#include "input.h"
#include "runtime.h"
#include "savestate.h"

extern "C" void f_028C56B8_orig(Cpu* c);
extern "C" void f_028C42B4_orig(Cpu* c);

namespace {
constexpr uint32_t kStage = 0x1014A5E8, kLinkPtr = 0x1014B5B0;
constexpr uint32_t kPosition = 0x4D0, kOldPosition = 0x4BC, kMoveYaw = 0x4DE;
constexpr uint32_t kAcch = 0x1980, kAcchFlags = 0x2C, kGround = 0x20;
constexpr uint32_t kProcedure = 0x305C, kNormalSpeed = 0x3458, kSpeedLimit = 0x598;
constexpr unsigned kWalk = 4;
constexpr float kAngleUnit = 2.0f * std::numbers::pi_v<float> / 65536.0f;
using Position = std::array<float, 3>;

struct Stick {
    float x = 0, y = 0;
};

struct WalkStart {
    uint32_t link = 0;
    uint64_t tick = 0;
    Position position{};
};

struct StickSample {
    uint32_t link = 0;
    uint64_t tick = 0;
    Stick host{};
};

struct WalkHalf {
    uint32_t link = 0, background = 0;
    uint64_t tick = 0, load_frame = 0;
    Position displacement{};
    StickSample sample{};
};

thread_local WalkStart start;
thread_local StickSample legacy_sample;
thread_local WalkHalf pending;
thread_local bool supplemental_collision = false;

bool is_ordon_link(uint32_t link) {
    return link >= mem::kMem2Start && link <= mem::kMem2End - (kNormalSpeed + 4) &&
           link == ld32(kLinkPtr) && std::memcmp(mem::ptr(kStage), "F_SP103", 8) == 0;
}

Position position(uint32_t address) {
    return {float(ldf32(address)), float(ldf32(address + 4)), float(ldf32(address + 8))};
}

void store_position(uint32_t address, const Position& point) {
    for (unsigned i = 0; i < point.size(); ++i) stf32(address + i * 4, point[i]);
}

void record_position(ordon60::Event event, uint32_t link) {
    if (!ordon60::tracing()) return;
    const auto [x, y, z] = position(link + kPosition);
    ordon60::record(event, link, x, y, z);
}

// MoveBgCrrPos uses a 20-byte dBgS table entry and slot +0x2C is ChkMoveBg.
bool static_ground(Cpu* c, uint32_t acch, uint32_t background) {
    const uint32_t index = ld16(acch + 0xF2);
    if (index >= 256 || background < mem::kMem2Start || background > mem::kMem2End - 256 * 20)
        return false;
    const uint32_t entry = background + index * 20;
    if (!ld8(entry + 4)) return false;
    const uint32_t bg = ld32(entry);
    if (bg < mem::kMem2Start || bg > mem::kMem2End - 8) return false;
    const uint32_t vt = ld32(bg + 4);
    if (vt < mem::kMem2Start || vt > mem::kMem2End - 0x30) return false;
    const uint32_t target = ld32(vt + 0x2C);
    if (!dispatch::lookup(target)) return false;
    const Cpu saved = *c;
    const bool moving = guest_call(c, target, {bg}) != 0;
    *c = saved;
    return !moving;
}

float magnitude(const Stick& stick) {
    return std::min(std::hypot(stick.x, stick.y), 1.0f);
}

// Link's ordinary setStickData branch at 02063B50 applies this x-axis convention.
Stick cooked_stick(Stick stick) {
    if (!ld8(0x1012667C) || ld8(0x1012667D)) stick.x = -stick.x;
    return stick;
}

Position steer(const WalkHalf& half, const Stick& fresh) {
    const uint32_t link = half.link;
    const float dead_zone = ldf32(0x100073E4);
    const auto previous = cooked_stick(half.sample.host);
    const float old_amount = magnitude(previous), new_amount = magnitude(fresh);
    Position displacement = half.displacement;
    if (!std::isfinite(new_amount) || !std::isfinite(old_amount) ||
        (previous.x == fresh.x && previous.y == fresh.y))
        return displacement;

    // 020411FC steers with a divisor of six and a 3000-unit cap. Apply half
    // of that proportional response to the change in requested heading.
    int turn = 0;
    if (old_amount > dead_zone && new_amount > dead_zone) {
        const float cross = fresh.x * previous.y - fresh.y * previous.x;
        const float dot = fresh.x * previous.x + fresh.y * previous.y;
        turn = std::clamp(int(std::lround(std::atan2(cross, dot) / (12 * kAngleUnit))), -1500, 1500);
    }

    // Ordinary walk uses a squared-stick speed target. Bound a changed target
    // by half of the native acceleration/deceleration rates at 02041138/54.
    const bool alternate = ld8(link + 0x3008) == 1;
    const float acceleration = std::abs(ldf32(alternate ? 0x1000780C : 0x100072EC)) * 0.5f;
    const float deceleration = std::abs(ldf32(alternate ? 0x100072F4 : 0x1000758C)) * 0.5f;
    const float speed = ldf32(link + kNormalSpeed), limit = ldf32(link + kSpeedLimit);
    float scale = 1.0f;
    if (speed > 0 && limit > 0 && std::isfinite(speed) && std::isfinite(limit)) {
        const float old_target = old_amount > dead_zone ? limit * old_amount * old_amount : 0;
        const float new_target = new_amount > dead_zone ? limit * new_amount * new_amount : 0;
        const float toward_target = new_target - speed;
        const float change = std::clamp(new_target - old_target, -deceleration, acceleration);
        const float applied = change < 0 ? std::clamp(change, std::min(toward_target, 0.0f), 0.0f)
                                         : std::clamp(change, 0.0f, std::max(toward_target, 0.0f));
        if (applied != 0.0f) {
            const float updated = speed + applied;
            scale = (0.5f * speed + 0.5f * updated) / speed;
            stf32(link + kNormalSpeed, updated);
        }
    }
    if (!turn && scale == 1.0f) return displacement;
    st16(link + kMoveYaw, static_cast<uint16_t>(ld16(link + kMoveYaw) + turn));
    const float sine = std::sin(turn * kAngleUnit), cosine = std::cos(turn * kAngleUnit);
    displacement[0] = scale * (half.displacement[0] * cosine + half.displacement[2] * sine);
    displacement[2] = scale * (half.displacement[2] * cosine - half.displacement[0] * sine);
    return displacement;
}
} // namespace

extern "C" void site_02063BFC(Cpu* c) {
    if (!ordon60::tracing() && !ordon60::paired_pass()) return;
    const uint32_t link = c->r[30];
    if (!is_ordon_link(link)) return;
    ordon60::record(ordon60::Event::Input, link, c->f[28].ps0, c->f[29].ps0, c->f[11].ps0);
    if (ordon60::paired_pass() && !ordon60::supplemental_pass()) {
        const auto pad = input::read();
        legacy_sample = {link, ordon60::pair_id(), {pad.lx, pad.ly}};
    }
}

extern "C" void site_0208634C(Cpu* c) {
    if (!ordon60::paired_pass() || ordon60::supplemental_pass()) return;
    const uint32_t link = c->r[3];
    if (!is_ordon_link(link)) return;
    pending = {};
    start = {};
    if (ordon60::eligible() && ld16(link + kProcedure) == kWalk &&
        (ld32(link + kAcch + kAcchFlags) & kGround))
        start = {link, ordon60::pair_id(), position(link + kPosition)};
}

extern "C" void hook_028C56B8(Cpu* c) {
    const uint32_t link = c->r[31], acch = c->r[3], background = c->r[4];
    if ((!ordon60::paired_pass() && !ordon60::tracing()) || c->lr != 0x020863CC ||
        acch != link + kAcch || !is_ordon_link(link))
        return f_028C56B8_orig(c);

    const auto first = std::exchange(start, {});
    const uint64_t tick = ordon60::pair_id();
    const uint32_t pos_addr = link + kPosition;
    if (!ordon60::supplemental_pass() && first.link == link && first.tick == tick &&
        ld16(link + kProcedure) == kWalk && ordon60::eligible() && !(ld32(link + 0x574) & 0x1000) &&
        !(ld32(acch + kAcchFlags) & 1) && ld32(acch + 0x30) == pos_addr &&
        ld32(acch + 0x34) == link + kOldPosition) {
        const auto target = position(pos_addr);
        Position half{};
        for (unsigned i = 0; i < half.size(); ++i) half[i] = (target[i] - first.position[i]) * 0.5f;
        if (std::hypot(half[0], half[2]) <= 32 && std::abs(half[1]) <= 8 &&
            (half[0] != 0 || half[2] != 0) && static_ground(c, acch, background)) {
            pending = {link, background, tick, ss::last_load_frame(), half, {}};
            if (legacy_sample.link == link && legacy_sample.tick == tick) pending.sample = legacy_sample;
            store_position(pos_addr, {first.position[0] + half[0], first.position[1] + half[1],
                                      first.position[2] + half[2]});
        }
    }
    f_028C56B8_orig(c);
    record_position(ordon60::Event::Collision, link);
}

extern "C" void site_02086404(Cpu* c) {
    if (ordon60::tracing() && is_ordon_link(c->r[31]))
        record_position(ordon60::Event::Movement, c->r[31]);
}

bool ordon60::advance_walk(Cpu* c) {
    if (!supplemental_pass()) return false;
    const auto half = std::exchange(pending, {});
    if (!half.link || half.tick != pair_id() || half.load_frame != ss::last_load_frame() ||
        !is_ordon_link(half.link)) return false;

    const uint32_t link = half.link, acch = link + kAcch;
    const uint32_t old_addr = link + kOldPosition, pos_addr = link + kPosition;
    const uint32_t flags = ld32(acch + kAcchFlags);
    if (!(flags & kGround) || (flags & 1) || (ld32(link + 0x574) & 0x1000) ||
        ld32(acch + 0x30) != pos_addr || ld32(acch + 0x34) != old_addr ||
        !static_ground(c, acch, half.background)) {
        if (tracing()) LOG("[ordon60 walk] supplemental sweep dropped, link=%08X acch_flags=%08X", link, flags);
        return false;
    }

    Position displacement = half.displacement;
    const auto pad = input::read();
    const auto stick = cooked_stick({pad.lx, pad.ly});
    record(Event::Input, link, stick.x, stick.y, magnitude(stick));
    if (half.sample.link && eligible() && ld16(link + kProcedure) == kWalk)
        displacement = steer(half, stick);

    // Sweep from the actual committed position. Legacy consumers retain their
    // previous-position field once the native wall and floor correction returns.
    const auto corrected = position(pos_addr);
    std::array<uint32_t, 3> legacy_old{};
    for (unsigned i = 0; i < legacy_old.size(); ++i) legacy_old[i] = ld32(old_addr + i * 4);
    store_position(old_addr, corrected);
    store_position(pos_addr, {corrected[0] + displacement[0], corrected[1] + displacement[1],
                              corrected[2] + displacement[2]});
    const Cpu saved = *c;
    supplemental_collision = true;
    st32(acch + kAcchFlags, flags & ~kGround);
    guest_call(c, 0x028C56B8, {acch, half.background});
    supplemental_collision = false;
    *c = saved;
    for (unsigned i = 0; i < legacy_old.size(); ++i) st32(old_addr + i * 4, legacy_old[i]);
    record_position(Event::Collision, link);
    record_position(Event::Movement, link);
    return true;
}

extern "C" void hook_028C42B4(Cpu* c) {
    // Floor, wall and roof geometry still run during the supplemental sweep.
    if (!supplemental_collision) f_028C42B4_orig(c);
}
