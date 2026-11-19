// Windows-only differential probe of selected TP HD arithmetic. No game loop or renderer.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "ppc.h"

extern "C" {
int g_ppc_trace = 0;
volatile int g_core_preempt[3] = {};
void ppc_trace_enter(uint32_t) { std::abort(); }
void ppc_preempt(Cpu*) { std::abort(); }
}

// 1 - sqrt(1 - gain), written to avoid cancellation for small gains.
static float half_gain(double gain) {
    return static_cast<float>(gain / (1.0 + std::sqrt(1.0 - gain)));
}

#include "guest_timing.inc"

namespace {
constexpr uint32_t kLink = 0x10000;
constexpr uint32_t kCamera = 0x14000;
constexpr uint32_t kFrameCtrl = 0x15000;
constexpr uint32_t kVelocityStep = 0x15100;
constexpr uint32_t kStack = 0x16000;
constexpr uint32_t kClockProbe = 0x20000;
constexpr uint32_t kPosition = kLink + 0x4D0;
constexpr uint32_t kYaw = kLink + 0x4DE;
constexpr uint32_t kSpeed = kLink + 0x3458;
constexpr uint32_t kFilteredDistance = kCamera + 0x2B0;
constexpr uint32_t kFrame = kFrameCtrl + 0xC;
constexpr double kAcceleration = 1.5;

// Keep the original world units per 30 Hz step. The correction matches the
// original end-velocity integration across linear acceleration steps.
void step_movement(Cpu& cpu, double step, double top_speed) {
    const double old_speed = ldf32(kSpeed);
    cpu.r[30] = kLink;
    cpu.f[13].ps0 = old_speed;
    cpu.f[27].ps0 = top_speed;
    cpu.f[28].ps0 = kAcceleration * step;
    advance_speed(&cpu);

    const double speed = ldf32(kSpeed);
    stf32(kVelocityStep + 8, step * speed + (1.0 - step) * (speed - old_speed) / 2.0);
    cpu.r[3] = kPosition;
    cpu.r[4] = kVelocityStep;
    cpu.r[5] = kPosition;
    f_02C379DC(&cpu);
}

void step_turn(Cpu& cpu, int subdivisions, int target) {
    cpu.r[3] = kYaw;
    cpu.r[4] = static_cast<uint32_t>(target);
    // Integer divisor and caps from the Link call at 020411FC.
    cpu.r[5] = 6 * subdivisions;
    cpu.r[6] = 3000 / subdivisions;
    cpu.r[7] = 2000 / subdivisions;
    f_02BEFF08(&cpu);
}

struct Sample {
    double speed, distance;
    int yaw;
    double frame, camera;
};

std::array<Sample, 60> sample(int subdivisions, int start_yaw, int target_yaw, double top_speed = 12) {
    Cpu cpu{};
    cpu.r[1] = kStack;
    stf32(kSpeed, 0);
    stf32(kPosition + 8, 0);
    st16(kYaw, static_cast<uint16_t>(start_yaw));
    st8(kFrameCtrl, 2); // Repeat
    st8(kFrameCtrl + 1, 0);
    st16(kFrameCtrl + 2, 0);
    st16(kFrameCtrl + 4, 24);
    st16(kFrameCtrl + 6, 0);
    stf32(kFrameCtrl + 8, 1.25 / subdivisions);
    stf32(kFrame, 0);
    stf32(kCamera + 0x2AC, 100);
    stf32(kFilteredDistance, 100);

    std::array<Sample, 60> samples{};
    for (int i = 0; i < 30 * subdivisions; ++i) {
        step_movement(cpu, 1.0 / subdivisions, top_speed);
        step_turn(cpu, subdivisions, target_yaw);
        cpu.r[3] = kFrameCtrl;
        f_02B520C4_orig(&cpu);
        cpu.r[31] = kCamera;
        cpu.f[1].ps0 = 250;  // Synthetic replacement for the distance computed upstream.
        filter_camera_distance(&cpu, subdivisions == 2);

        samples[i] = {ldf32(kSpeed), ldf32(kPosition + 8),
                      static_cast<int16_t>(ld16(kYaw)), ldf32(kFrame),
                      ldf32(kFilteredDistance)};
    }
    return samples;
}

int angle_error(int a, int b) {
    return std::abs(static_cast<int>(static_cast<int16_t>(a - b)));
}

void map_guest(uint32_t address, size_t bytes) {
    if (VirtualAlloc(ppc_ptr(address), bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)
        != ppc_ptr(address)) {
        std::fprintf(stderr, "Could not map probe guest memory at %08X (Windows error %lu)\n",
                     address, static_cast<unsigned long>(GetLastError()));
        std::exit(1);
    }
}

constexpr std::array<uint32_t, 5> kCameraClockOffsets{0x160, 0x170, 0x174, 0xA8, 0xAC};
using CameraClocks = std::array<uint32_t, kCameraClockOffsets.size()>;

CameraClocks read_camera_clocks(uint32_t camera) {
    CameraClocks clocks{};
    for (size_t i = 0; i < clocks.size(); ++i)
        clocks[i] = ld32(camera + kCameraClockOffsets[i]);
    return clocks;
}

bool step_camera_clocks(uint32_t camera, int path, bool legacy_tick) {
    Cpu cpu{};
    cpu.r[31] = camera;
    cpu.r[3] = 7;
    cpu.r[9] = 0xFFFFFFFFu;

    if (path == 2) {
        // This path also clears a camera flag and sets CR0 for the caller.
        st32(camera + 0x60C, 0xFFFFFFFFu);
        camera_clock_028DFD64(&cpu, legacy_tick);
        return ld32(camera + 0x60C) == 0xFFDFFFFFu && cpu.cr[2] == 1;
    }

    if (path == 0) camera_clock_028DFD04(&cpu, legacy_tick);
    else camera_clock_028DFDBC(&cpu, legacy_tick);
    camera_clock_028DFDE4(&cpu, legacy_tick);
    return cpu.r[28] == 7 && cpu.cr[2] == 0;
}

bool probe_camera_clocks() {
    constexpr uint32_t stock = kClockProbe, scoped = kClockProbe + 0x1000, doubled = kClockProbe + 0x2000;
    map_guest(kClockProbe, 0x3000);
    // Start near wrap and exercise all three post-dispatch paths.
    for (uint32_t camera : {stock, scoped, doubled}) {
        for (uint32_t offset : kCameraClockOffsets)
            st32(camera + offset, 0xFFFFFFFAu);
    }
    for (int tick = 0; tick < 30; ++tick) {
        const int path = tick % 3;
        const auto before = read_camera_clocks(scoped);
        if (!step_camera_clocks(scoped, path, false) || read_camera_clocks(scoped) != before ||
            !step_camera_clocks(scoped, path, true) || !step_camera_clocks(stock, path, true) ||
            read_camera_clocks(scoped) != read_camera_clocks(stock) ||
            !step_camera_clocks(doubled, path, true) || !step_camera_clocks(doubled, path, true)) {
            std::fprintf(stderr, "Camera clock or branch state mismatch at legacy tick %d\n", tick + 1);
            return false;
        }
    }
    const auto legacy = read_camera_clocks(stock);
    const auto unscaled = read_camera_clocks(doubled);
    std::printf("Camera counter increments, 30 Hz / gated 60 Hz / ungated 60 Hz: %u / %u / %u on +0xA8\n",
                legacy[3] - 0xFFFFFFFAu, read_camera_clocks(scoped)[3] - 0xFFFFFFFAu,
                unscaled[3] - 0xFFFFFFFAu);
    return legacy == CameraClocks{14, 14, 14, 24, 24} &&
           unscaled == CameraClocks{34, 34, 34, 54, 54};
}
} // namespace

int main() {
    map_guest(kLink, 0x7000);
    map_guest(0x10080000, 0x10000);
    map_guest(0x100A0000, 0x10000);
    st32(0x100897C4, kCameraGainWord);
    for (unsigned i = 0; i < std::size(kFrameControllerWords); ++i)
        st32(0x100ABBA8 + 4 * i, kFrameControllerWords[i]);

    const double gain = ldf32(0x100897C4);
    if (!(gain > 0.0 && gain < 1.0)) {
        std::fprintf(stderr, "Unexpected camera gain %.9g\n", gain);
        return 1;
    }

    const auto original = sample(1, 0, 0x3000);
    const auto experiment = sample(2, 0, 0x3000);
    const auto wrap_original = sample(1, 32000, -29000);
    const auto wrap_experiment = sample(2, 32000, -29000);
    // A speed cap hit inside the second half step exposes integration error
    // hidden when acceleration hits the cap exactly at a 30 Hz boundary.
    const auto capped_original = sample(1, 0, 0, 11.5);
    const auto capped_experiment = sample(2, 0, 0, 11.5);
    const double cap_drift = capped_experiment[59].distance - capped_original[29].distance;
    double speed_error = 0, distance_error = 0, animation_error = 0, camera_error = 0;
    int turn_error = 0, wrap_error = 0;
    for (int i = 0; i < 30; ++i) {
        const Sample& a = original[i];
        const Sample& b = experiment[2 * i + 1];
        speed_error = std::max(speed_error, std::abs(a.speed - b.speed));
        distance_error = std::max(distance_error, std::abs(a.distance - b.distance));
        animation_error = std::max(animation_error, std::abs(a.frame - b.frame));
        camera_error = std::max(camera_error, std::abs(a.camera - b.camera));
        turn_error = std::max(turn_error, angle_error(a.yaw, b.yaw));
        wrap_error = std::max(wrap_error, angle_error(wrap_original[i].yaw, wrap_experiment[2 * i + 1].yaw));
    }

    std::puts("USA Rev 2 differential probe, 1 nominal second, synthetic inputs");
    std::printf("30/60 updates: 30 / 60; terminal distance: %.6f / %.6f; speed: %.3f / %.3f\n",
                original[29].distance, experiment[59].distance, original[29].speed, experiment[59].speed);
    std::printf("max error at shared times: speed %.6f, distance %.6f, animation %.6f frames, camera %.6f units\n",
                speed_error, distance_error, animation_error, camera_error);
    std::printf("yaw max deviation: %d normal, %d across wrap (65536 units per turn)\n", turn_error, wrap_error);
    std::printf("off-grid speed cap: %.6f / %.6f distance, difference %+.6f units\n",
                capped_original[29].distance, capped_experiment[59].distance, cap_drift);
    std::printf("first 60 Hz sample (16.67 ms): distance %.6f, yaw %d, frame %.3f, camera %.6f\n",
                experiment[0].distance, experiment[0].yaw, experiment[0].frame, experiment[0].camera);
    std::printf("first 30 Hz sample (33.33 ms): distance %.6f, yaw %d, frame %.3f, camera %.6f; camera gain %.6f\n",
                original[0].distance, original[0].yaw, original[0].frame, original[0].camera, gain);

    const bool clocks_passed = probe_camera_clocks();
    const bool passed = clocks_passed && speed_error < 1e-5 && distance_error < 1e-4 &&
                        animation_error < 1e-5 && camera_error < 2e-4 &&
                        original[29].distance == 318 && original[29].frame == 13.5 &&
                        experiment[59].yaw == 0x3000 && wrap_experiment[59].yaw == -29000 &&
                        turn_error <= 32 && wrap_error <= 32 &&
                        capped_original[29].speed == 11.5 && capped_experiment[59].speed == 11.5 &&
                        std::abs(cap_drift) <= 0.125 &&
                        experiment[0].distance > 0 && experiment[0].yaw > 0 &&
                        experiment[0].frame > 0 && experiment[0].camera > 100;
    if (!passed) std::fputs("Timing probe outside expected limits.\n", stderr);
    return passed ? 0 : 1;
}
