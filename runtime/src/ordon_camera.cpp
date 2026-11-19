#include "ordon60.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

#include "input.h"
#include "mods/builtin.h"
#include "runtime.h"
#include "savestate.h"

extern "C" void f_0208AB30_orig(Cpu* c);
extern "C" void f_02088508_orig(Cpu* c);
extern "C" void f_0208858C_orig(Cpu* c);
extern "C" void f_02088608_orig(Cpu* c);

namespace {
constexpr uint32_t kLinkPtr = 0x1014B5B0;
constexpr uint32_t kLinkPos = 0x4D0, kMoveYaw = 0x4DE, kShapeYaw = 0x4E6;
constexpr uint32_t kLinkProcedure = 0x305C, kBody = 0x654;
constexpr uint32_t kBaseMatrix = 0x20, kMatrixBuffer = 0x80;
constexpr uint32_t kCamera = 0x248, kViewEye = 0xD8, kViewCenter = 0xE4;
constexpr uint32_t kCameraCenter = 0x30, kCameraEye = 0x3C;
constexpr uint32_t kChaseCenter = 0x64, kChaseEye = 0x70;
constexpr uint32_t kLineCheck = 0x028DD28C, kSetView = 0x028D7614;
constexpr uint32_t kCameraLineFlags = 0x40B7;
constexpr uint32_t kStickX = 0x1D8, kCameraYaw = 0x2E, kChaseYaw = 0x62;
constexpr uint32_t kOrbitRate = 0xAC, kOrbitLatch = 0x9A;
constexpr uint32_t kOrbitDeadZone = 0x100897D4, kOrbitScale = 0x10089ECC;
constexpr uint32_t kOrbitCurve = 0x028FEA40;
constexpr uint32_t kAngleUnitsPerDegree = 0x100CB1C8;
using Vec = std::array<float, 3>;

struct Orbit {
    uint32_t chase = 0;
    float gain = 0, curve = 0, rounding = 0;
    float host_x = 0, native_x = 0;
};

struct Follow {
    uint32_t camera = 0, process = 0, link = 0;
    uint64_t tick = 0, load = 0;
    Vec position{}, target{}, gain{};
    Orbit orbit{};
};

struct Pose {
    uint32_t link = 0;
    uint64_t tick = 0, load = 0;
    Vec position{};
    uint16_t yaw = 0;
    std::array<uint32_t, 24> models{};
    size_t count = 0;
};

thread_local Follow follow;
thread_local Pose pose;
thread_local uint32_t drawing_link;

bool allocated(uint32_t address, uint32_t bytes) {
    return (address & 3) == 0 &&
           ((address >= mem::kMem2Start && address <= mem::kMem2End - bytes) ||
            (address >= mem::kMem1 && address <= mem::kMem1 + mem::kMem1Size - bytes));
}

bool walking(uint32_t link) {
    if (!allocated(link, kLinkProcedure + 2) || (ld32(link + 0x574) & 0x1000)) return false;
    const auto proc = ld16(link + kLinkProcedure);
    return proc == 3 || proc == 4;
}

Vec read_vec(uint32_t address) {
    return {float(ldf32(address)), float(ldf32(address + 4)), float(ldf32(address + 8))};
}

void write_vec(uint32_t address, const Vec& value) {
    for (size_t i = 0; i < value.size(); ++i) stf32(address + uint32_t(i) * 4, value[i]);
}

Vec plus(const Vec& a, const Vec& b) {
    return {a[0] + b[0], a[1] + b[1], a[2] + b[2]};
}

Vec minus(const Vec& a, const Vec& b) {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}

float half_gain(float gain) {
    return gain / (1.0f + std::sqrt(1.0f - gain));
}

Vec orbit_eye(const Vec& eye, const Vec& center, float angle) {
    const auto offset = minus(eye, center);
    const float cs = std::cos(angle), sn = std::sin(angle);
    return plus(center, {cs * offset[0] + sn * offset[2], offset[1],
                         cs * offset[2] - sn * offset[0]});
}

bool bounded(const Vec& point, float limit) {
    return std::ranges::all_of(point, [=](float axis) { return std::abs(axis) <= limit; });
}

Vec translation(uint32_t matrix) {
    return {float(ldf32(matrix + 12)), float(ldf32(matrix + 28)), float(ldf32(matrix + 44))};
}

void rebase_matrix(uint32_t matrix, const Vec& before, const Vec& after, float cs, float sn) {
    for (uint32_t column = 0; column != 12; column += 4) {
        const float x = float(ldf32(matrix + column));
        const float z = float(ldf32(matrix + column + 32));
        stf32(matrix + column, cs * x + sn * z);
        stf32(matrix + column + 32, cs * z - sn * x);
    }
    const auto offset = minus(translation(matrix), before);
    stf32(matrix + 12, after[0] + cs * offset[0] + sn * offset[2]);
    stf32(matrix + 28, after[1] + offset[1]);
    stf32(matrix + 44, after[2] + cs * offset[2] - sn * offset[0]);
}

void add_model(uint32_t model) {
    if (!allocated(model, 0xDC) || pose.count == pose.models.size()) return;
    const auto end = pose.models.begin() + pose.count;
    if (std::find(pose.models.begin(), end, model) == end) pose.models[pose.count++] = model;
}

bool advance_pose(Cpu* c) {
    const Pose previous = std::exchange(pose, {});
    const uint32_t link = ld32(kLinkPtr);
    if (!walking(link) || link != previous.link || previous.tick != ordon60::pair_id() ||
        previous.load != ss::last_load_frame()) return false;
    const auto position = read_vec(link + kLinkPos);
    const auto displacement = minus(position, previous.position);
    const int16_t yaw = int16_t(ld16(link + kMoveYaw) - previous.yaw);
    if (!bounded(displacement, 32.0f) || std::abs(int(yaw)) > 0x2000) return false;
    if (displacement == Vec{} && !yaw) return ordon60::sample_pose(c, link);

    const float radians = float(yaw) * (6.28318530718f / 65536.0f);
    const float cs = std::cos(radians), sn = std::sin(radians);
    std::array<uint32_t, 72> matrices{};
    size_t count = 0;
    auto transform = [&](uint32_t address, uint32_t n) {
        if (!n || !allocated(address, n * 48)) return;
        const auto end = matrices.begin() + count;
        if (std::find(matrices.begin(), end, address) != end) return;
        matrices[count++] = address;
        for (uint32_t i = 0; i < n; ++i)
            rebase_matrix(address + i * 48, previous.position, position, cs, sn);
    };

    // The previous native Link Draw supplies the body and attachment models.
    // Their world-space matrices share Link's root, including the already
    // evaluated joint and envelope matrices used by the next native Draw.
    for (size_t i = 0; i < previous.count; ++i) {
        const auto model = previous.models[i];
        if (!bounded(minus(translation(model + kBaseMatrix), previous.position), 250.0f)) continue;
        const auto data = ld32(model), buffer = ld32(model + kMatrixBuffer);
        if (!allocated(data, 0x28) || !allocated(buffer, 0x14)) continue;
        const uint32_t joints = ld16(data + 0x24), envelopes = ld16(data + 0x26);
        if (!joints || joints > 512 || envelopes > 1024) continue;
        transform(model + kBaseMatrix, 1);
        transform(ld32(buffer + 0x0C), joints);
        transform(ld32(buffer + 0x10), envelopes);
    }
    if (count) st16(link + kShapeYaw, uint16_t(ld16(link + kShapeYaw) + yaw));
    if (count) ordon60::sample_pose(c, link);
    return count != 0;
}

bool clear_view(Cpu* c, uint32_t camera, const Vec& center, const Vec& eye) {
    // Reserve a PPC frame with room for outgoing args and two cXyz values.
    const Cpu saved = *c;
    c->r[1] -= 0x40;
    st32(c->r[1], saved.r[1]);
    const auto start = c->r[1] + 0x20, end = c->r[1] + 0x2C;
    write_vec(start, center);
    write_vec(end, eye);
    const bool clear = !guest_call(c, kLineCheck, {camera, start, end, kCameraLineFlags});
    *c = saved;
    return clear;
}

void record_camera(uint32_t process) {
    const auto eye = read_vec(process + kViewEye);
    ordon60::record(ordon60::Event::Camera, process, eye[0], eye[1], eye[2]);
    const auto offset = minus(eye, read_vec(process + kViewCenter));
    constexpr float degrees = 57.295779513f;
    ordon60::record(ordon60::Event::ViewAngle, process, degrees * std::atan2(offset[0], offset[2]),
                    degrees * std::atan2(offset[1], std::hypot(offset[0], offset[2])), 0);
}
} // namespace

// In the settled chase path the game multiplies (target - center) by the
// three gains at r1+0x110, adds the result to +0x64, then solves the eye.
extern "C" void site_028E7084(Cpu* c) {
    if (!ordon60::paired_pass() || ordon60::supplemental_pass()) return;
    const uint32_t camera = c->r[31], link = ld32(kLinkPtr);
    if (ld32(camera + 0x180) != link || !walking(link)) return;
    const Vec target = read_vec(c->r[1] + 0xA8);
    const Vec position = read_vec(link + kLinkPos);
    const Vec gain = read_vec(c->r[1] + 0x110);
    if (!bounded(minus(target, position), 200.0f) ||
        !std::ranges::all_of(gain, [](float value) { return value >= 0.0f && value <= 1.0f; })) return;
    Vec gain_per_pass{};
    for (size_t i = 0; i < gain.size(); ++i)
        gain_per_pass[i] = half_gain(gain[i]);
    follow = {camera, 0, link, ordon60::pair_id(), ss::last_load_frame(), position, target, gain_per_pass};
    write_vec(c->r[1] + 0x110, gain_per_pass);
}

extern "C" void site_028E7948(Cpu* c) {
    // The chase style has passed its native manual-orbit permission checks.
    const auto camera = c->r[31], chase = c->r[29];
    if (follow.camera != camera || ld32(camera + 0x1A4) || (ld32(camera + 0x60C) & 0x800000) ||
        mods::builtin::on("direct-camera") || mods::builtin::on("mouse-camera")) return;
    const float gain = ldf32(chase + 0x4C), curve = float(c->f[31].ps0);
    if (!(gain > 0 && gain <= 1 && curve > 0 && curve < 1)) return;
    const float host_x = input::read().rx, native_x = ldf32(camera + kStickX);
    follow.orbit = {chase, half_gain(gain), curve, 0, host_x, native_x};
    if (ordon60::tracing())
        ordon60::record(ordon60::Event::Orbit, camera, native_x, ldf32(camera + 0x1DC), host_x);
}

extern "C" void site_028E7984(Cpu* c) {
    if (follow.orbit.chase != c->r[29]) return;
    // Native target minus previous rate is already in f7. Split the gain
    // before the rate store and keep the float until the native angle cast.
    c->f[1].ps0 = std::fma(float(c->f[7].ps0), follow.orbit.gain, float(c->f[8].ps0));
}

extern "C" void site_028E7988(Cpu* c) {
    if (follow.orbit.chase != c->r[29]) return;
    c->f[1].ps0 *= 0.5;
    const float units = float(c->f[1].ps0) * float(ldf32(kAngleUnitsPerDegree));
    follow.orbit.rounding = units - std::trunc(units);
}

extern "C" void site_028E12C8(Cpu*) {
    if (ordon60::paired_pass()) follow = {};
}

extern "C" void site_028E12E4(Cpu* c) {
    const uint32_t process = c->r[31];
    if (ordon60::paired_pass() && follow.camera == process + kCamera) follow.process = process;
    if (ordon60::tracing()) record_camera(process);
}

bool ordon60::advance_camera(Cpu* c) {
    if (!supplemental_pass()) return false;
    advance_pose(c);
    const Follow primary = std::exchange(follow, {});
    if (!primary.process || primary.link != ld32(kLinkPtr) || !walking(primary.link) ||
        primary.tick != pair_id() || primary.load != ss::last_load_frame() ||
        ld32(primary.process + 0x26C) != 0) return false;

    const auto position = read_vec(primary.link + kLinkPos);
    const auto displacement = minus(position, primary.position);
    if (!bounded(displacement, 32.0f)) return false;
    const auto target = plus(primary.target, displacement);
    const uint32_t camera = primary.camera, process = primary.process;
    const auto current = read_vec(camera + kChaseCenter);
    Vec change{};
    for (size_t i = 0; i < change.size(); ++i) change[i] = (target[i] - current[i]) * primary.gain[i];
    if (!bounded(change, 32.0f)) return false;
    float orbit_rate = 0;
    int16_t turn = 0;
    if (primary.orbit.chase) {
        const auto pad = input::read();
        const auto& orbit = primary.orbit;
        // A held stick reuses the native Wii U normalization exactly. A
        // changed sample uses the same measured gain and axis convention.
        float scale = ld8(0x1012667C) && !ld8(0x1012667D) ? 0.9f : -0.9f;
        if (std::abs(orbit.host_x) > 0.1f && std::abs(orbit.native_x) > 0.05f)
            scale = std::clamp(orbit.native_x / orbit.host_x, -1.5f, 1.5f);
        const float stick = pad.rx == orbit.host_x ? orbit.native_x : std::clamp(pad.rx * scale, -1.0f, 1.0f);
        if (std::abs(stick) > ldf32(kOrbitDeadZone)) {
            const Cpu saved = *c;
            c->f[1].ps0 = stick;
            c->f[2].ps0 = primary.orbit.curve;
            guest_call(c, kOrbitCurve, {});
            const float target_rate = float(c->f[1].ps0) * float(ldf32(kOrbitScale));
            *c = saved;
            const float previous_rate = ldf32(primary.orbit.chase + kOrbitRate);
            orbit_rate = std::lerp(previous_rate, target_rate, primary.orbit.gain);
            // The latter interval carries the rest of the original 30 Hz
            // angular displacement. The two gains compose to the native rate.
            const float units = (orbit_rate - previous_rate * 0.5f) * float(ldf32(kAngleUnitsPerDegree));
            if (std::isfinite(units))
                turn = static_cast<int16_t>(std::clamp(units + primary.orbit.rounding, -2048.0f, 2048.0f));
        }
        if (tracing()) record(Event::Orbit, camera, stick, pad.ry, turn);
    }
    if (!turn && change == Vec{}) return false;
    const auto center = plus(read_vec(process + kViewCenter), change);
    const float radians = float(turn) * (6.28318530718f / 65536.0f);
    const auto eye = orbit_eye(plus(read_vec(process + kViewEye), change), center, radians);
    if (!clear_view(c, camera, center, eye)) return false;

    // Publish the actual chase state and the camera process's view used by
    // native drawing. Native occlusion and mode dispatch run on the full tick.
    for (const auto [center_offset, eye_offset] :
         {std::pair{kChaseCenter, kChaseEye}, std::pair{kCameraCenter, kCameraEye}}) {
        const auto next_center = plus(read_vec(camera + center_offset), change);
        write_vec(camera + center_offset, next_center);
        write_vec(camera + eye_offset, orbit_eye(plus(read_vec(camera + eye_offset), change), next_center, radians));
    }
    if (primary.orbit.chase) stf32(primary.orbit.chase + kOrbitRate, orbit_rate);
    if (turn) {
        for (const uint32_t offset : {kCameraYaw, kChaseYaw})
            st16(camera + offset, uint16_t(ld16(camera + offset) + turn));
        st16(primary.orbit.chase + kOrbitLatch, uint16_t(ld16(camera + kChaseYaw)));
        st8(primary.orbit.chase + 0x93, 1);
    }
    write_vec(process + kViewEye, eye);
    write_vec(process + kViewCenter, center);
    const Cpu saved = *c;
    guest_call(c, kSetView, {process});
    *c = saved;
    record_camera(process);
    return true;
}

extern "C" void hook_0208AB30(Cpu* c) {
    const uint32_t link = c->r[3];
    if (!link || link != ld32(kLinkPtr) || (!ordon60::paired_pass() && !ordon60::tracing()))
        return f_0208AB30_orig(c);

    if (ordon60::paired_pass() && !ordon60::supplemental_pass()) {
        pose = {};
        if (walking(link)) {
            pose = {link, ordon60::pair_id(), ss::last_load_frame(), read_vec(link + kLinkPos), ld16(link + kMoveYaw)};
            add_model(ld32(link + kBody));
        }
    }
    const auto outer = std::exchange(drawing_link, pose.link);
    f_0208AB30_orig(c);
    drawing_link = outer;

    if (ordon60::tracing()) {
        ordon60::record_skeleton(link);
        const auto body = ld32(link + kBody);
        if (!allocated(body, kBaseMatrix + 48)) return;
        const auto root = translation(body + kBaseMatrix);
        ordon60::record(ordon60::Event::Pose, link, root[0], root[1], root[2]);
    }
}

#define CAPTURE_MODEL(address)                              \
    extern "C" void hook_##address(Cpu* c) {                 \
        if (drawing_link && c->r[3] == drawing_link) add_model(c->r[4]); \
        f_##address##_orig(c);                              \
    }
CAPTURE_MODEL(02088508)
CAPTURE_MODEL(0208858C)
CAPTURE_MODEL(02088608)
#undef CAPTURE_MODEL

// The supplemental chase bypasses mode dispatch. Keep legacy counter sites
// gated in case a native draw path also enters camera execute.
#define CAMERA_CLOCK_SITE(address, reg) \
    extern "C" void site_##address(Cpu* c) { if (ordon60::supplemental_pass()) --c->r[reg]; }
CAMERA_CLOCK_SITE(028DFD0C, 10)
CAMERA_CLOCK_SITE(028DFD14, 12)
CAMERA_CLOCK_SITE(028DFD1C, 11)
CAMERA_CLOCK_SITE(028DFDC4, 10)
CAMERA_CLOCK_SITE(028DFDCC, 12)
CAMERA_CLOCK_SITE(028DFDD4, 11)
CAMERA_CLOCK_SITE(028DFDEC, 4)
CAMERA_CLOCK_SITE(028DFDF4, 5)
CAMERA_CLOCK_SITE(028DFD70, 4)
CAMERA_CLOCK_SITE(028DFD78, 5)
#undef CAMERA_CLOCK_SITE
