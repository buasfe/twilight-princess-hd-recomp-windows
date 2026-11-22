// Attract movies play from a local video file or the user's GameCube ISO.
// The disc path uses a locally supplied FFmpeg decoder.
#if defined(_WIN32) && defined(WWHD_HAS_VULKAN)
#include "title_movie.h"
#include "audio_out.h"
#include "gfx/renderer.h"
#include "gx2/gx2.h"
#include "input.h"
#include "overlay/hostui.h"
#include "runtime.h"

#include <SDL3/SDL.h>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <cstring>
#include <deque>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

namespace title_movie {
namespace {
using Clock = std::chrono::steady_clock;
constexpr int kAudioRate = 32000;
constexpr int kFrameBytes = width * height * 3;
constexpr double kFramePeriod = 1001.0 / 30000.0;
constexpr size_t kQueueFrames = 4;
constexpr size_t kPipePackets = 16;
constexpr int kBytesPerStereoFrame = 4;
std::atomic<bool> playing{false};
std::atomic<uint32_t> consumed_buttons{0};
constexpr uint32_t kSkipButtons = input::kA | input::kB | input::kPlus;
std::mutex frame_mutex;
std::shared_ptr<const Frame> current;
bool handled = false;  // guest thread; reset on returning to the opening
uint64_t serial = 0;

uint32_t be32(const uint8_t* b) {
    return uint32_t(b[0]) << 24 | uint32_t(b[1]) << 16 | uint32_t(b[2]) << 8 | b[3];
}
uint32_t le32(const uint8_t* b) {
    return uint32_t(b[3]) << 24 | uint32_t(b[2]) << 16 | uint32_t(b[1]) << 8 | b[0];
}
bool tag(std::span<const uint8_t> b, const char* magic) {
    return b.size() >= 4 && std::memcmp(b.data(), magic, 4) == 0;
}
bool movie_stage() {
    return std::memcmp(mem::ptr(0x1014A5E8), "S_MV000", 8) == 0;
}

struct DiscMovie { uint64_t offset = 0, end = 0; unsigned frames = 0; };
DiscMovie locate_movie(const std::filesystem::path& path) {
    std::ifstream image(path, std::ios::binary | std::ios::ate);
    if (!image) throw std::runtime_error("cannot open the configured GameCube ISO");
    const uint64_t size = uint64_t(image.tellg());
    auto read = [&](uint64_t offset, std::span<uint8_t> out) {
        if (offset > size || out.size() > size - offset) throw std::runtime_error("ISO range is invalid");
        image.seekg(std::streamoff(offset));
        if (!image.read(reinterpret_cast<char*>(out.data()), std::streamsize(out.size())))
            throw std::runtime_error("cannot read the configured GameCube ISO");
    };
    std::array<uint8_t, 0x440> h{};
    read(0, h);
    if (std::memcmp(h.data(), "GZ2E01", 6) != 0 || be32(h.data() + 0x1c) != 0xc2339f3d)
        throw std::runtime_error("the experiment expects a USA GameCube ISO (GZ2E01)");
    const uint32_t table_size = be32(h.data() + 0x428);
    if (table_size < 12 || table_size > (16u << 20)) throw std::runtime_error("invalid GameCube file table");
    std::vector<uint8_t> table(table_size);
    read(be32(h.data() + 0x424), table);
    const uint32_t count = be32(table.data() + 8);
    if (count < 2 || count > table.size() / 12) throw std::runtime_error("invalid GameCube file count");
    auto name = [&](const uint8_t* entry) -> std::string_view {
        const size_t offset = size_t(count) * 12 + (be32(entry) & 0xffffffu);
        if (offset >= table.size()) throw std::runtime_error("invalid GameCube filename");
        const char* first = reinterpret_cast<const char*>(table.data() + offset);
        const auto* end = static_cast<const char*>(std::memchr(first, 0, table.size() - offset));
        if (!end) throw std::runtime_error("unterminated GameCube filename");
        return {first, size_t(end - first)};
    };
    for (uint32_t i = 1; i < count;) {
        const uint8_t* dir = table.data() + 12 * i;
        const uint32_t end = dir[0] ? be32(dir + 8) : i + 1;
        if (end <= i || end > count) throw std::runtime_error("invalid GameCube directory");
        if (dir[0] && name(dir) == "Movie") {
            for (uint32_t j = i + 1; j < end;) {
                const uint8_t* file = table.data() + 12 * j;
                const uint32_t next = file[0] ? be32(file + 8) : j + 1;
                if (next <= j || next > end) throw std::runtime_error("invalid Movie directory");
                if (!file[0] && name(file) == "demo_movie98_00.thp") {
                    const uint64_t start = be32(file + 4), bytes = be32(file + 8);
                    if (bytes < 48 || start > size || bytes > size - start)
                        throw std::runtime_error("invalid THP file range");
                    std::array<uint8_t, 48> thp{};
                    read(start, thp);
                    const uint32_t frames = be32(thp.data() + 20);
                    if (!tag(thp, "THP") || be32(thp.data() + 4) != 0x11000 || frames != 3778)
                        throw std::runtime_error("unrecognized GameCube attract movie");
                    return {start, start + bytes, frames};
                }
                j = next;
            }
        }
        i = end;
    }
    throw std::runtime_error("the GameCube ISO does not contain the attract movie");
}

struct Packet {
    std::vector<uint8_t> data;
    bool picture = false;
    double seconds = 0, duration = 0;
    uint32_t width = 0, height = 0;
    float pixel_aspect = 1;
};

class Decoder {
    using Process = std::unique_ptr<SDL_Process, decltype(&SDL_DestroyProcess)>;
    Process process_{nullptr, SDL_DestroyProcess};
    using Handle = std::unique_ptr<void, decltype(&CloseHandle)>;
    Handle local_pipe_{nullptr, CloseHandle};
    HANDLE pipe_ = INVALID_HANDLE_VALUE;
    bool local_ = false, eof_ = false;
    size_t buffered_ = 0;
    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<Packet> packets_;
    std::exception_ptr failure_;
    bool stopping_ = false;
    std::thread reader_;

    struct Element {
        uint64_t id = 0, size = 0;
        bool unknown = false;
    };
    uint64_t position_ = 0;
    uint64_t cluster_ = 0;
    double tick_seconds_ = 0.001;
    double frame_period_ = 0;
    uint32_t width_ = 0, height_ = 0, display_width_ = 0, display_height_ = 0;

    bool read(std::span<uint8_t> out, bool allow_eof = false) {
        bool fresh = true;
        while (!out.empty()) {
            DWORD n = 0;
            if (!ReadFile(pipe_, out.data(), DWORD(out.size()), &n, nullptr) || n == 0) {
                if (allow_eof && fresh && (!n && GetLastError() == ERROR_BROKEN_PIPE)) return false;
                throw std::runtime_error("the local decoder stopped producing data");
            }
            position_ += n;
            out = out.subspan(n);
            fresh = false;
        }
        return true;
    }

    std::optional<uint64_t> vint(bool size, bool allow_eof = false, bool* unknown = nullptr) {
        uint8_t first = 0;
        if (!read(std::span(&first, 1), allow_eof)) return {};
        if (!first) throw std::runtime_error("invalid timestamped movie header");
        uint8_t marker = 0x80;
        unsigned extra = 0;
        while (!(first & marker)) { marker >>= 1; ++extra; }
        if (!size && extra > 3) throw std::runtime_error("invalid movie element identifier");
        uint64_t value = size ? first & (marker - 1) : first;
        std::array<uint8_t, 7> rest{};
        read(std::span(rest).first(extra));
        for (auto byte : std::span(rest).first(extra)) value = (value << 8) | byte;
        if (unknown) *unknown = value == (uint64_t(1) << (7 * (extra + 1))) - 1;
        return value;
    }

    Element element(bool allow_eof = false) {
        const auto id = vint(false, allow_eof);
        if (!id) return {};
        Element field{*id};
        field.size = *vint(true, false, &field.unknown);
        return field;
    }

    uint64_t integer(uint64_t size) {
        if (!size || size > 8) throw std::runtime_error("invalid movie timing value");
        std::array<uint8_t, 8> buf{};
        const auto bytes = std::span(buf).first(size);
        read(bytes);
        uint64_t result = 0;
        for (auto byte : bytes) result = (result << 8) | byte;
        return result;
    }

    void discard(const Element& field) {
        if (field.unknown || field.size > (2u << 20)) throw std::runtime_error("invalid movie metadata size");
        std::array<uint8_t, 4096> buf{};
        for (uint64_t left = field.size; left != 0;) {
            const auto count = std::min(left, uint64_t(buf.size()));
            read(std::span(buf).first(count));
            left -= count;
        }
    }

    template<typename Visitor>
    void children(const Element& parent, Visitor visit) {
        if (parent.unknown || parent.size > (64u << 20)) throw std::runtime_error("invalid movie container size");
        const uint64_t end = position_ + parent.size;
        while (position_ < end) {
            const auto f = element();
            if (f.unknown || position_ > end || f.size > end - position_)
                throw std::runtime_error("invalid nested movie field");
            visit(f);
        }
    }

    void tracks(const Element& group) {
        children(group, [&](const Element& track) {
            if (track.id != 0xae) { discard(track); return; }
            double duration = 0;
            bool video = false;
            children(track, [&](const Element& field) {
                if (field.id == 0x23e383) duration = integer(field.size) / 1e9;
                else if (field.id == 0xe0) {
                    video = true;
                    children(field, [&](const Element& attr) {
                        auto size = [&](uint32_t& dst) {
                            const auto value = integer(attr.size);
                            if (value > 65536) throw std::runtime_error("invalid decoded movie dimensions");
                            dst = uint32_t(value);
                        };
                        if (attr.id == 0xb0) size(width_);
                        else if (attr.id == 0xba) size(height_);
                        else if (attr.id == 0x54b0) size(display_width_);
                        else if (attr.id == 0x54ba) size(display_height_);
                        else discard(attr);
                    });
                } else discard(field);
            });
            if (video) frame_period_ = duration;
        });
    }

    Packet block(const Element& field) {
        const auto begin = position_;
        const auto track = *vint(true);
        const auto encoded_offset = uint16_t(integer(2));
        const int offset = encoded_offset < 0x8000 ? encoded_offset : int(encoded_offset) - 0x10000;
        const auto flags = integer(1);
        if (flags & 0x06 || position_ - begin >= field.size || (track != 1 && track != 2))
            throw std::runtime_error("unexpected decoded movie block");
        const auto bytes = field.size - (position_ - begin);
        Packet out{};
        out.picture = track == 1;
        out.seconds = (double(cluster_) + offset) * tick_seconds_;
        if (out.picture) {
            if (!width_ || !height_ || width_ > 4096 || height_ > 4096 || bytes != size_t(width_) * height_ * 3)
                throw std::runtime_error("invalid decoded movie picture");
            out.width = width_;
            out.height = height_;
            out.pixel_aspect = display_width_ && display_height_
                ? float(display_width_) * height_ / (float(display_height_) * width_) : 1;
            out.duration = frame_period_;
        } else if (bytes > (1u << 20) || bytes % kBytesPerStereoFrame) {
            throw std::runtime_error("invalid decoded movie audio");
        }
        out.data.resize(bytes);
        read(out.data);
        return out;
    }

    std::optional<Packet> timestamped_packet() {
        // The private decoder emits top-down raw BGR, stereo PCM, and Matroska
        // timestamps. The source container is handled by FFmpeg.
        for (;;) {
            const auto field = element(true);
            switch (field.id) {
            case 0: return {};
            case 0x18538067: break; // Segment, its children follow.
            case 0x1f43b675: cluster_ = 0; break; // Cluster
            case 0xe7: cluster_ = integer(field.size); break;
            case 0x1549a966: // Info
                children(field, [&](const Element& f) {
                    if (f.id == 0x2ad7b1) {
                        const auto scale = integer(f.size);
                        if (!scale || scale > 1'000'000'000) throw std::runtime_error("invalid movie timescale");
                        tick_seconds_ = scale / 1e9;
                    } else discard(f);
                });
                break;
            case 0x1654ae6b: tracks(field); break;
            case 0xa3: return block(field);
            case 0xa0: {
                std::optional<Packet> result;
                double duration = 0;
                children(field, [&](const Element& f) {
                    if (f.id == 0xa1) result = block(f);
                    else if (f.id == 0x9b) duration = integer(f.size) * tick_seconds_;
                    else discard(f);
                });
                if (result) {
                    if (duration > 0) result->duration = duration;
                    return result;
                }
                break;
            }
            default: discard(field); break;
            }
        }
    }

    void open_avi() {
        std::array<uint8_t, 12> h{};
        read(h);
        if (!tag(h, "RIFF") || !tag(std::span(h).subspan(8), "AVI "))
            throw std::runtime_error("the decoder did not produce AVI");
        for (unsigned i = 0; i < 16; ++i) {
            read(std::span(h).first(8));
            uint32_t n = le32(h.data() + 4);
            if (tag(h, "LIST")) {
                read(std::span(h).subspan(8));
                if (tag(std::span(h).subspan(8), "movi")) return;
                if (n < 4) break;
                n -= 4;
            }
            if (n > (128u << 10)) break;
            std::vector<uint8_t> skip(n + (n & 1u));
            read(skip);
        }
        throw std::runtime_error("AVI stream has no movie data");
    }

    Packet packet() {
        std::array<uint8_t, 8> h{};
        read(h);
        const uint32_t n = le32(h.data() + 4);
        Packet out{};
        out.picture = tag(h, "00dc") || tag(h, "00db");
        if (out.picture ? n != kFrameBytes
                        : !tag(h, "01wb") || n == 0 || n > 65536 || n % kBytesPerStereoFrame != 0)
            throw std::runtime_error("unexpected decoded THP packet");
        out.data.resize(n + (n & 1u));
        read(out.data);
        out.data.resize(n);
        return out;
    }

    void receive() {
        try {
            if (!local_) open_avi();
            for (;;) {
                auto next = local_ ? timestamped_packet() : std::optional(packet());
                std::unique_lock lock(mutex_);
                if (!next) {
                    eof_ = true;
                    ready_.notify_one();
                    return;
                }
                ready_.wait(lock, [&] {
                    return stopping_ || (packets_.size() < kPipePackets && buffered_ < (24u << 20));
                });
                if (stopping_) break;
                buffered_ += next->data.size();
                packets_.push_back(std::move(*next));
                ready_.notify_one();
            }
        } catch (...) {
            std::lock_guard lock(mutex_);
            failure_ = std::current_exception();
            ready_.notify_one();
        }
    }

public:
    Decoder(const char* executable, const std::string& input, const char* format = nullptr)
        : local_(format != nullptr) {
        std::vector<const char*> args{executable, "-hide_banner", "-nostdin", "-v", "error"};
        if (local_) {
            const char* local_args[] = {
                "-protocol_whitelist", "file", "-copyts", "-start_at_zero", "-f", format, "-i", input.c_str(),
                "-map", "0:v:0", "-map", "0:a:0", "-c:v", "rawvideo", "-pix_fmt", "bgr24",
                "-fps_mode", "passthrough", "-enc_time_base:v", "demux", "-c:a", "pcm_s16le", "-ar", "48000", "-ac", "2",
                "-f", "matroska", "-allow_raw_vfw", "1", "-live", "1", "-cluster_time_limit", "80",
                "-write_crc32", "0", "pipe:1", nullptr};
            args.insert(args.end(), std::begin(local_args), std::end(local_args));
        } else {
            const char* disc_args[] = {
                "-protocol_whitelist", "file,subfile", "-f", "thp", "-i", input.c_str(),
                "-map", "0:v:0", "-map", "0:a:0", "-c:v", "rawvideo", "-pix_fmt", "bgr24",
                "-c:a", "pcm_s16le", "-f", "avi", "pipe:1", nullptr};
            args.insert(args.end(), std::begin(disc_args), std::end(disc_args));
        }
        const auto props = SDL_CreateProperties();
        SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, args.data());
        SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);
        SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
        SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_NULL);
        if (local_) {
            // Let FFmpeg write HD frames in large chunks instead of making
            // the two processes alternate on a small default Windows pipe.
            HANDLE read_end = nullptr, write_end = nullptr;
            if (!CreatePipe(&read_end, &write_end, nullptr, 1u << 20)) {
                SDL_DestroyProperties(props);
                throw std::runtime_error("could not open the HD decoder pipe");
            }
            local_pipe_.reset(read_end);
            Handle writer(write_end, CloseHandle);
            SDL_IOStreamInterface interface;
            SDL_INIT_INTERFACE(&interface);
            std::unique_ptr<SDL_IOStream, decltype(&SDL_CloseIO)> output(SDL_OpenIO(&interface, nullptr), SDL_CloseIO);
            if (!output) {
                SDL_DestroyProperties(props);
                throw std::runtime_error("could not wrap the HD decoder pipe");
            }
            SDL_SetPointerProperty(SDL_GetIOProperties(output.get()), SDL_PROP_IOSTREAM_WINDOWS_HANDLE_POINTER, writer.get());
            SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_POINTER, output.get());
            SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_REDIRECT);
            process_.reset(SDL_CreateProcessWithProperties(props));
        } else {
            SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
            process_.reset(SDL_CreateProcessWithProperties(props));
        }
        SDL_DestroyProperties(props);
        if (!process_) throw std::runtime_error("could not start the local FFmpeg decoder");
        try {
            if (local_) pipe_ = local_pipe_.get();
            else {
                auto* output = SDL_GetProcessOutput(process_.get());
                pipe_ = SDL_GetPointerProperty(SDL_GetIOProperties(output), SDL_PROP_IOSTREAM_WINDOWS_HANDLE_POINTER,
                                              INVALID_HANDLE_VALUE);
            }
            // SDL creates this as a nonblocking pipe. A raw frame spans many pipe
            // buffers, so sleeping between short reads starves both video and audio.
            DWORD mode = PIPE_WAIT;
            if (pipe_ == INVALID_HANDLE_VALUE || !SetNamedPipeHandleState(pipe_, &mode, nullptr, nullptr))
                throw std::runtime_error("could not configure the local decoder pipe");
            reader_ = std::thread([this] { receive(); });
        } catch (...) {
            SDL_KillProcess(process_.get(), true);
            throw;
        }
    }

    ~Decoder() {
        {
            std::lock_guard lock(mutex_);
            stopping_ = true;
            ready_.notify_one();
        }
        // Killing the child closes the writer and wakes a blocked ReadFile.
        SDL_KillProcess(process_.get(), true);
        reader_.join();
    }

    std::optional<Packet> next_optional() {
        std::unique_lock lock(mutex_);
        if (!ready_.wait_for(lock, std::chrono::seconds(2), [&] { return !packets_.empty() || eof_ || failure_; }))
            throw std::runtime_error("the local decoder timed out");
        if (packets_.empty()) {
            if (failure_) std::rethrow_exception(failure_);
            return {};
        }
        auto result = std::move(packets_.front());
        packets_.pop_front();
        buffered_ -= result.data.size();
        ready_.notify_one();
        return result;
    }

    Packet next() {
        auto result = next_optional();
        if (!result) throw std::runtime_error("the disc movie ended early");
        return std::move(*result);
    }
};

uint32_t widescreen_matte_rows(unsigned frame, std::span<const uint8_t> pixels) {
    // Stable letterboxed sections of the USA attract movie, indexed from zero.
    // Full-height shots and their artwork use the entire decoded frame.
    constexpr std::pair<unsigned, unsigned> sections[] = {
        {190, 1801}, {1855, 1915}, {2212, 2438}, {3464, 3778}
    };
    if (std::none_of(std::begin(sections), std::end(sections), [frame](const auto& segment) {
            return frame >= segment.first && frame < segment.second;
        })) return 0;

    // Leave a 608x342 picture (16:9). The removed rows must contain only dark
    // matte plus negligible THP compression noise in each RGB channel.
    constexpr uint32_t rows = 53;
    constexpr size_t band = width * rows * 3;
    auto black = [](uint8_t channel) { return channel <= 8; };
    if (!std::all_of(pixels.begin(), pixels.begin() + band, black) ||
        !std::all_of(pixels.end() - band, pixels.end(), black)) return 0;
    return rows;
}

void set_frame(std::shared_ptr<const Frame> picture) {
    std::lock_guard lock(frame_mutex);
    current = std::move(picture);
}

struct Playback {
    Playback() {
        audio::flush();
        playing.store(true, std::memory_order_release);
    }
    ~Playback() {
        playing.store(false, std::memory_order_release);
        set_frame(nullptr);
        audio::flush();
    }
};

std::filesystem::path local_path(const char* utf8) {
    return std::filesystem::absolute(std::filesystem::path(
        std::u8string_view(reinterpret_cast<const char8_t*>(utf8))));
}

using Sound = std::unique_ptr<SDL_AudioStream, decltype(&SDL_DestroyAudioStream)>;
Sound open_sound(int rate, int channels) {
    SDL_AudioSpec spec{SDL_AUDIO_S16, channels, rate};
    Sound sound(SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr),
                SDL_DestroyAudioStream);
    if (!sound) throw std::runtime_error("could not open playback for the movie soundtrack");
    return sound;
}

bool skip_movie(unsigned shown, uint32_t& previous) {
    // The original port-one A, B or Start trigger skips after frame zero.
    const auto pressed = input::read().buttons;
    const bool skip = shown > 1 && (pressed & ~previous & kSkipButtons);
    previous = pressed;
    if (skip) consumed_buttons.store(pressed & kSkipButtons, std::memory_order_release);
    return skip;
}

void play(const char* iso, const char* ffmpeg) {
    const auto path = local_path(iso);
    DiscMovie movie = locate_movie(path);
    const auto utf8 = path.generic_u8string();
    std::string name(utf8.begin(), utf8.end());
    const std::string uri = "subfile,,start," + std::to_string(movie.offset) +
        ",end," + std::to_string(movie.end) + ",,:" + name;
    const char* fit = SDL_getenv("WWHD_ATTRACT_WIDE_FIT");
    const bool wide = fit && std::string_view(fit) == "1";
    Decoder decoder(ffmpeg, uri);
    auto sound = open_sound(kAudioRate, 2);

    unsigned frames_read = 0, shown = 0, presented = 0;
    const double run_time = movie.frames * kFramePeriod;
    const auto minimum_samples = int64_t(run_time * kAudioRate) + 1;
    int64_t samples_queued = 0;
    std::deque<std::shared_ptr<Frame>> pictures;
    auto refill = [&] {
        while (pictures.size() < kQueueFrames &&
               (frames_read < movie.frames || samples_queued < minimum_samples)) {
            auto packet = decoder.next();
            if (packet.picture) {
                if (frames_read >= movie.frames) throw std::runtime_error("too many THP pictures");
                auto f = std::make_shared<Frame>();
                f->number = ++serial;
                if (wide) f->matte_rows = widescreen_matte_rows(frames_read, packet.data);
                f->bgr = std::move(packet.data);
                pictures.push_back(std::move(f));
                ++frames_read;
            } else {
                if (!SDL_PutAudioStreamData(sound.get(), packet.data.data(), int(packet.data.size())))
                    throw std::runtime_error("could not queue the movie soundtrack");
                samples_queued += int64_t(packet.data.size() / kBytesPerStereoFrame);
            }
        }
    };
    refill();
    if (pictures.empty() || !samples_queued) throw std::runtime_error("the movie could not be buffered");

    Playback playback;
    auto buttons = input::read().buttons;
    if (!SDL_ResumeAudioStreamDevice(sound.get())) throw std::runtime_error("could not start movie audio");
    const auto began = Clock::now();
    bool skipped = false;
    double last_audio_time = 0, max_frame_lateness = 0;
    auto last_progress = began;
    LOG("[attract] movie playing: %u frames at 30000/1001, local stereo soundtrack", movie.frames);
    for (;;) {
        const auto now = Clock::now();
        if ((skipped = skip_movie(shown, buttons))) break;

        const int queued = SDL_GetAudioStreamQueued(sound.get());
        if (queued < 0) throw std::runtime_error("movie audio device became unavailable");
        const double audio_time = double(samples_queued - queued / kBytesPerStereoFrame) / kAudioRate;
        if (audio_time > last_audio_time) {
            last_audio_time = audio_time;
            last_progress = now;
        }
        if (audio_time >= run_time) break;
        if (now - last_progress > std::chrono::seconds(2))
            throw std::runtime_error("movie audio stopped advancing");

        const unsigned due = unsigned(audio_time / kFramePeriod);
        if (!pictures.empty() && (shown == 0 || due >= shown)) {
            std::shared_ptr<Frame> next;
            do {
                next = std::move(pictures.front());
                pictures.pop_front();
                ++shown;
            } while (!pictures.empty() && due >= shown);
            max_frame_lateness = std::max(max_frame_lateness, audio_time - (shown - 1) * kFramePeriod);
            set_frame(std::move(next));
            gx2::present_title_movie();
            ++presented;
            refill();
        } else {
            SDL_Delay(2);
        }
    }
    SDL_PauseAudioStreamDevice(sound.get());
    SDL_ClearAudioStream(sound.get());
    const double elapsed = std::chrono::duration<double>(Clock::now() - began).count();
    LOG("[attract] movie %s after %.3f s; %u of %u pictures presented, peak clock skew %.1f ms",
        skipped ? "skipped" : "ended", elapsed, presented, movie.frames, max_frame_lateness * 1000.0);
}
void play_local(const char* file, const char* ffmpeg) {
    const auto path = local_path(file);
    const auto ext = path.extension().wstring();
    const bool mp4 = !_wcsicmp(ext.c_str(), L".mp4") || !_wcsicmp(ext.c_str(), L".mov") || !_wcsicmp(ext.c_str(), L".m4v");
    const bool mkv = !_wcsicmp(ext.c_str(), L".mkv") || !_wcsicmp(ext.c_str(), L".webm");
    if ((!mp4 && !mkv) || !std::filesystem::is_regular_file(path))
        throw std::runtime_error("select an existing local MP4, MOV, Matroska or WebM file");

    const auto utf8 = path.generic_u8string();
    Decoder decoder(ffmpeg, std::string(utf8.begin(), utf8.end()), mp4 ? "mov" : "matroska");
    constexpr int rate = 48000;
    constexpr auto stride = kBytesPerStereoFrame;
    auto sound = open_sound(rate, 2);
    struct TimedPicture { std::shared_ptr<Frame> frame; double timestamp; };
    std::deque<TimedPicture> pictures;
    std::optional<Packet> pending;
    int64_t written = 0, audio_end = 0;
    double video_end = 0, latest_video = 0, last_video = -1;
    unsigned decoded = 0;
    bool eof = false;
    const std::array<uint8_t, rate * stride / 6> silence{};

    auto queue = [&](const uint8_t* data, int64_t count) {
        if (!SDL_PutAudioStreamData(sound.get(), data, int(count * stride)))
            throw std::runtime_error("could not queue the local movie soundtrack");
        written += count;
    };
    auto pad = [&](int64_t to) {
        while (written < to) queue(silence.data(), std::min(to - written, int64_t(silence.size() / stride)));
    };
    auto end_sample = [&] { return std::max(audio_end, int64_t(std::ceil(video_end * rate))); };
    auto refill = [&](int64_t target) {
        while (!eof && pictures.size() < kQueueFrames) {
            if (!pending) pending = decoder.next_optional();
            if (!pending) { eof = true; break; }
            if (pending->picture) {
                const double stamp = pending->seconds;
                if (stamp < last_video || stamp > 24 * 60 * 60)
                    throw std::runtime_error("invalid local movie picture timestamp");
                const double duration = pending->duration > 0 ? pending->duration : (last_video >= 0 ? stamp - last_video : 0);
                video_end = std::max(video_end, stamp + duration);
                latest_video = std::max(latest_video, stamp);
                last_video = stamp;
                auto f = std::make_shared<Frame>();
                f->number = ++serial;
                f->width = pending->width;
                f->height = pending->height;
                f->pixel_aspect = pending->pixel_aspect;
                f->bgr = std::move(pending->data);
                pictures.push_back({std::move(f), stamp});
                ++decoded;
            } else {
                const auto length = int64_t(pending->data.size() / stride);
                const auto start = std::llround(pending->seconds * rate);
                if (start > 24LL * 60 * 60 * rate)
                    throw std::runtime_error("invalid local movie audio timestamp");
                // The pipe's millisecond ticks round PCM packet times. Consecutive
                // PCM packets within one tick remain sample-contiguous.
                const auto aligned = std::abs(written - start) <= rate / 1000 ? written : start;
                pad(std::min(target, aligned));
                if (written < aligned) break;
                const auto overlap = written - aligned;
                if (overlap < length) queue(pending->data.data() + overlap * stride, length - overlap);
                audio_end = std::max(audio_end, aligned + length);
            }
            pending.reset();
        }
        // The muxer progresses in time order. If the video is ahead of the
        // soundtrack, continue the common audio clock through the silent span.
        if (eof) pad(std::min(target, end_sample()));
        else if (!pending) pad(std::min(target, int64_t(std::max(0.0, latest_video - 0.025) * rate)));
    };

    refill(rate / 6);
    if (!written) throw std::runtime_error("the local movie could not be buffered");
    Playback playback;
    auto buttons = input::read().buttons;
    if (!SDL_ResumeAudioStreamDevice(sound.get())) throw std::runtime_error("could not start local movie sound");
    const auto began = Clock::now();
    auto last_progress = began;
    int64_t played = 0;
    unsigned shown = 0, presented = 0;
    bool skipped = false, flushed = false;
    double max_skew = 0;
    LOG("[attract] local cinematic uses source presentation timestamps");
    for (;;) {
        const auto now = Clock::now();
        if ((skipped = skip_movie(shown, buttons))) break;
        const int queued = SDL_GetAudioStreamQueued(sound.get());
        if (queued < 0) throw std::runtime_error("the local movie audio device became unavailable");
        const auto consumed = written - queued / stride;
        if (consumed > played) {
            played = consumed;
            last_progress = now;
        }
        const double audio_time = double(played) / rate;
        const double elapsed = std::chrono::duration<double>(now - began).count();
        const double movie_time = std::min(elapsed, audio_time + 0.020);
        std::shared_ptr<Frame> due;
        double stamp = 0;
        while (!pictures.empty() && pictures.front().timestamp <= movie_time) {
            stamp = pictures.front().timestamp;
            due = std::move(pictures.front().frame);
            pictures.pop_front();
            ++shown;
        }
        if (due) {
            max_skew = std::max(max_skew, std::abs(stamp - audio_time));
            set_frame(std::move(due));
            gx2::present_title_movie();
            ++presented;
        }
        if (eof && pictures.empty() && played >= end_sample() && elapsed >= double(end_sample()) / rate) break;
        refill(played + rate / 6);
        if (!flushed && eof && written >= end_sample()) {
            if (!SDL_FlushAudioStream(sound.get())) throw std::runtime_error("could not drain the local soundtrack");
            flushed = true;
        }
        if (now - last_progress > std::chrono::seconds(2))
            throw std::runtime_error("local movie audio stopped advancing");
        SDL_Delay(1);
    }
    SDL_PauseAudioStreamDevice(sound.get());
    SDL_ClearAudioStream(sound.get());
    LOG("[attract] local movie %s after %.3f s; %u of %u pictures presented, peak clock skew %.1f ms",
        skipped ? "skipped" : "ended", std::chrono::duration<double>(Clock::now() - began).count(),
        presented, decoded, max_skew * 1000.0);
}

}  // namespace

bool active() { return playing.load(std::memory_order_acquire); }
uint32_t filter_skip_buttons(uint32_t held) {
    if (!consumed_buttons.load(std::memory_order_acquire)) return held;
    // Only a physical release rearms a button consumed by the movie.
    return held & ~consumed_buttons.fetch_and(held, std::memory_order_acq_rel);
}
std::shared_ptr<const Frame> frame() {
    std::lock_guard lock(frame_mutex);
    return current;
}
void at_guest_frame() {
    if (handled && !movie_stage()) handled = false;
}

void choose_file(const char* key, const char* kind, const char* extensions) {
    struct Selection { const char* key; SDL_DialogFileFilter filter; };
    hostui::post([=] {
        auto* request = new Selection{key, {kind, extensions}};
        SDL_ShowOpenFileDialog([](void* context, const char* const* files, int) {
            std::unique_ptr<Selection> selection(static_cast<Selection*>(context));
            if (files && files[0]) {
                hostui::post([key = selection->key, path = std::string(files[0])] { hostui::set(key, path); });
            }
        }, request, SDL_GetKeyboardFocus(), &request->filter, 1, nullptr, false);
    });
}

static std::string configured(const char* env, const char* key) {
    if (const char* value = SDL_getenv(env)) return value;
    std::string value;
    hostui::get(key, value);
    return value;
}

void at_movie_start() {
    if (handled || !movie_stage() || !render::vulkan()) return;
    const auto video = configured("WWHD_ATTRACT_VIDEO", "attractVideo");
    const auto iso = configured("WWHD_ATTRACT_ISO", "attractIso");
    if (video.empty() && iso.empty()) return;
    handled = true;
    const auto decoder = configured("WWHD_ATTRACT_FFMPEG", "attractFfmpeg");
    const char* ffmpeg = decoder.empty() ? "ffmpeg" : decoder.c_str();
    BlockingScope block;
    if (!video.empty()) {
        try {
            play_local(video.c_str(), ffmpeg);
            return;
        } catch (const std::exception& e) {
            LOG("[attract] local video unavailable: %s", e.what());
        }
    }
    if (iso.empty()) return;
    try {
        play(iso.c_str(), ffmpeg);
    } catch (const std::exception& e) {
        LOG("[attract] using the original HD loop: %s", e.what());
    }
}
}  // namespace title_movie
#endif

// USA Rev 2: dDemo_c::update's native "Movie Start Wait" OSReport call.
// Continue at the original instruction after the local player returns.
extern "C" void site_02910FE4(struct Cpu*) {
#if defined(_WIN32) && defined(WWHD_HAS_VULKAN)
    title_movie::at_movie_start();
#endif
}
