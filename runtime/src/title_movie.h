#pragma once
#include <cstdint>
#include <memory>
#include <vector>

namespace title_movie {
#if defined(_WIN32) && defined(WWHD_HAS_VULKAN)
inline constexpr uint32_t width = 608, height = 448;
struct Frame {
    uint64_t number = 0;
    uint32_t matte_rows = 0;
    uint32_t width = title_movie::width, height = title_movie::height;
    float pixel_aspect = 1;
    std::vector<uint8_t> bgr;
};
bool active();
uint32_t filter_skip_buttons(uint32_t held);
std::shared_ptr<const Frame> frame();
void at_guest_frame();
void choose_file(const char* key, const char* kind, const char* extensions);
#else
inline bool active() { return false; }
inline uint32_t filter_skip_buttons(uint32_t held) { return held; }
inline void at_guest_frame() {}
#endif
}  // namespace title_movie
