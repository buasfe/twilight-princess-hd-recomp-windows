#pragma once

#include <cstdint>

struct Cpu;

namespace ordon60 {
bool requested();
bool tracing();
bool eligible();
bool paired_pass();
bool supplemental_pass();
uint64_t pair_id();  // game thread only
void set_pass(bool paired, bool supplemental);

bool advance_walk(Cpu*);
bool advance_camera(Cpu*);
bool sample_pose(Cpu*, uint32_t link);
void restore_sampled_pose();
void record_skeleton(uint32_t link);

enum class Event : uint8_t { Legacy, Input, Movement, Collision, Camera, Draw, Pose, Orbit, Skeleton, Clip, ViewAngle };
void record(Event event, uint32_t object = 0, float x = 0, float y = 0, float z = 0);
// The game thread tags the state available to a painter. Its ticket travels
// with the native swap through the GX2 queue to the render thread.
void begin_picture();
uint32_t take_picture();
void set_present_picture(uint32_t picture);
void record_present();
}
