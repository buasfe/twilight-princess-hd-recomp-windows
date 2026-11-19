#include "ordon60.h"

#include <array>
#include <cmath>
#include <cstring>
#include <utility>
#include <vector>

#include "mods/builtin.h"
#include "runtime.h"
#include "savestate.h"

extern "C" void f_02B520C4_orig(Cpu* c);

namespace {
constexpr uint32_t kLink = 0x1014B5B0, kLinkSize = 0x396C;  // USA Rev 2 actor profile
constexpr uint32_t kBody = 0x654, kOldPose = 0x2070, kBase = 0x20, kBuffer = 0x80;
constexpr uint32_t kJ3D = 0x101961FC, kLinkMatrixStack = 0x10187EF8;
constexpr uint32_t kJointCalc = 0x02B5C210, kEnvelopeCalc = 0x02B5C260;
constexpr uint32_t kInverse = 0x02C36FFC, kConcat = 0x02C36F1C, kMtx = 48;
constexpr uint32_t kAnimation = 0x1F3C, kControl = 0x1FE0, kControlSize = 24;

bool valid(uint32_t address, uint32_t size) {
    return !(address & 3) &&
           ((address >= mem::kMem2Start && address <= mem::kMem2End - size) ||
            (address >= mem::kMem1 && address <= mem::kMem1 + mem::kMem1Size - size));
}

struct Region {
    uint32_t address;
    std::vector<uint8_t> data;
};

struct Saved {
    std::vector<Region> regions;

    void copy(uint32_t address, uint32_t size) {
        if (!size || !valid(address, size)) return;
        const auto address_ptr = static_cast<const uint8_t*>(mem::ptr(address));
        regions.push_back({address, {address_ptr, address_ptr + size}});
    }

    void restore() {
        for (auto it = regions.rbegin(); it != regions.rend(); ++it)
            std::memcpy(mem::ptr(it->address), it->data.data(), it->data.size());
        regions.clear();
    }
};

struct Matrices {
    uint32_t buffer = 0, joint = 0, envelope = 0, count = 0, weights = 0;
};

Matrices matrices(uint32_t model) {
    if (!valid(model, 0xDC)) return {};
    const uint32_t data = ld32(model), buffer = ld32(model + kBuffer);
    if (!valid(data, 0x54) || !valid(buffer, 0x14)) return {};
    const uint32_t n = ld16(data + 0x24), w = ld16(data + 0x26);
    const uint32_t joint = ld32(buffer + 0xC), envelope = ld32(buffer + 0x10);
    if (!n || n > 64 || w > 256 || !valid(joint, n * kMtx) || (w && !valid(envelope, w * kMtx))) return {};
    return {buffer, joint, envelope, n, w};
}

void save_matrices(Saved& saved, const Matrices& m) {
    saved.copy(m.joint, m.count * kMtx);
    saved.copy(m.envelope, m.weights * kMtx);
    saved.copy(ld32(m.buffer + 4), m.count);
    saved.copy(ld32(m.buffer + 8), m.weights);
}

struct VisualPose {
    uint32_t link = 0, model = 0;
    uint64_t load = 0;
    Saved saved;
};
thread_local VisualPose visual;

struct Clip {
    uint32_t animation = 0, control = 0;
    float phase = 0;
};

std::array<Clip, 6> locomotion_clips(uint32_t link) {
    std::array<Clip, 6> clips{};
    for (uint32_t i = 0; i < clips.size(); ++i) {
        const uint32_t animation = ld32(link + kAnimation + i * 8);
        const uint32_t control = link + kControl + i * kControlSize;
        // Upper slots can share a lower clip and use its frame controller.
        bool shared = false;
        for (uint32_t j = 0; j < i; ++j) shared |= clips[j].animation == animation;
        if (!valid(animation, 8) || shared || ld8(control) != 2) continue;
        const float phase = ldf32(animation + 4), rate = ldf32(control + 8);
        if (phase != float(ldf32(control + 0xC)) || !std::isfinite(phase) ||
            !(rate > 0 && rate <= 4) || ld16(control + 4) <= ld16(control + 6)) continue;
        clips[i] = {animation, control, phase};
    }
    return clips;
}

void adjust_attachment(Cpu* c, uint32_t model, uint32_t old_joint, uint32_t new_joint, uint32_t work) {
    const Matrices m = matrices(model);
    if (!m.joint || model == visual.model || !guest_call(c, kInverse, {old_joint, work})) return;
    guest_call(c, kConcat, {new_joint, work, work + kMtx});
    visual.saved.copy(model + kBase, kMtx);
    save_matrices(visual.saved, m);
    const uint32_t delta = work + kMtx, result = work + 2 * kMtx;
    const auto apply = [&](uint32_t address) {
        guest_call(c, kConcat, {delta, address, result});
        std::memcpy(mem::ptr(address), mem::ptr(result), kMtx);
    };
    apply(model + kBase);
    for (uint32_t i = 0; i < m.count; ++i) apply(m.joint + i * kMtx);
    for (uint32_t i = 0; i < m.weights; ++i) apply(m.envelope + i * kMtx);
}
} // namespace

bool ordon60::sample_pose(Cpu* c, uint32_t link) {
    if (visual.link || !eligible() || !valid(link, kLinkSize) || ld8(link + 0x302F) ||
        !(ld32(link + 0x19AC) & 0x20) || mods::builtin::on("big-head")) return false;
    const auto clips = locomotion_clips(link);
    if (!clips[0].animation && !clips[1].animation) return false;

    const uint32_t body = ld32(link + kBody), old = ld32(link + kOldPose);
    const Matrices m = matrices(body);
    if (m.count < 27 || m.count > 40 || !valid(old, 0x24)) return false;
    const uint32_t transforms = ld32(old + 0x1C), quaternions = ld32(old + 0x20);
    if (!valid(transforms, m.count * 32) || !valid(quaternions, m.count * 16)) return false;

    visual.link = link;
    visual.model = body;
    visual.load = ss::last_load_frame();
    save_matrices(visual.saved, m);
    Saved gameplay;
    // The native joint callbacks write arm/foot and blend state in Link.
    // The animator also updates the old-frame arrays and morph controller.
    gameplay.copy(link, kLinkSize);
    gameplay.copy(old, 0x24);
    gameplay.copy(transforms, m.count * 32);
    gameplay.copy(quaternions, m.count * 16);
    gameplay.copy(kJ3D, 0x118);
    gameplay.copy(kLinkMatrixStack, kMtx);

    const Cpu registers = *c;
    c->r[1] -= 0x110;
    const uint32_t stack = c->r[1], control = stack + 0x20, work = stack + 0x40;
    st32(stack, registers.r[1]);
    for (const auto& clip : clips) {
        if (!clip.animation) continue;
        gameplay.copy(clip.animation + 4, 4);
        std::memcpy(mem::ptr(control), mem::ptr(clip.control), kControlSize);
        stf32(control + 8, ldf32(control + 8) * 0.5);
        c->r[3] = control;
        // This operates on a copy. The live frame controller still owns
        // duration, loop flags and animation-triggered events.
        f_02B520C4_orig(c);
        const float phase = ldf32(control + 0xC);
        stf32(clip.animation + 4, phase);
        if (tracing() && clip.control == clips[0].control)
            record(Event::Clip, clip.animation, clip.phase, phase, ldf32(control + 8));
    }

    // Use the native animation tree and weighting, without executing the
    // actor again or evaluating its material, deformation or sound paths.
    guest_call(c, kJointCalc, {body});
    guest_call(c, kEnvelopeCalc, {body});
    gameplay.restore();

    // Use the same body joints that the normal human item matrix uses.
    // The prior model transform already contains the attachment offset.
    const bool sword_in_hand = ld16(link + 0x304C) == 0x103;
    const uint32_t back = ld16(link + 0x312A);
    for (const auto [offset, anchor] : std::array<std::pair<uint32_t, uint32_t>, 5>{{
             {0x658, 4}, {0x65C, 4}, {0x6C0, back},
             {0x6BC, sword_in_hand ? uint32_t(ld16(link + 0x3134)) : back},
             {0x67C, sword_in_hand ? uint32_t(ld16(link + 0x3136)) : back}}}) {
        if (anchor >= m.count) continue;
        const uint32_t saved_joint = work + 3 * kMtx;
        std::memcpy(mem::ptr(saved_joint), visual.saved.regions.front().data.data() + anchor * kMtx, kMtx);
        adjust_attachment(c, ld32(link + offset), saved_joint, m.joint + anchor * kMtx, work);
    }
    *c = registers;
    return true;
}

void ordon60::restore_sampled_pose() {
    if (!visual.link) return;
    if (visual.load == ss::last_load_frame() && visual.link == ld32(kLink) &&
        visual.model == ld32(visual.link + kBody))
        visual.saved.restore();
    visual = {};
}

void ordon60::record_skeleton(uint32_t link) {
    const Matrices m = matrices(ld32(link + kBody));
    if (m.count < 27) return;
    const auto distance = [&](uint32_t a, uint32_t b) {
        const uint32_t left = m.joint + a * kMtx, right = m.joint + b * kMtx;
        const float x = ldf32(left + 12) - ldf32(right + 12);
        const float y = ldf32(left + 28) - ldf32(right + 28);
        const float z = ldf32(left + 44) - ldf32(right + 44);
        // A rigid translation or yaw changes none of these distances.
        return std::round(std::hypot(x, y, z) * 128) / 128;
    };
    record(Event::Skeleton, link, distance(0, 21), distance(0, 26), distance(21, 26));
}
