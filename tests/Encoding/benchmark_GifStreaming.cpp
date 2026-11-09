// Standalone POSIX msf-only memory/time benchmark (not a CTest target).
// c++ -O2 -std=c++17 -Iinclude tests/Encoding/benchmark_GifStreaming.cpp -o /tmp/gif-bench
// /tmp/gif-bench 0 120 /tmp/legacy.gif
// /tmp/gif-bench 1 120 /tmp/stream.gif
// cmp /tmp/legacy.gif /tmp/stream.gif
// Run each mode in a fresh process; repeat with 1200 frames. RSS includes input,
// libc and OS overhead; allocator counts only msf. This is not an app benchmark.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <sys/resource.h>

namespace {
size_t liveBytes = 0;
size_t peakBytes = 0;

void *allocate(size_t size)
{
    void *result = std::malloc(size);
    if (result) {
        liveBytes += size;
        peakBytes = std::max(peakBytes, liveBytes);
    }
    return result;
}

void release(void *memory, size_t size)
{
    if (memory) {
        liveBytes -= size;
        std::free(memory);
    }
}

void *resize(void *memory, size_t oldSize, size_t size)
{
    void *result = std::realloc(memory, size);
    if (result) {
        liveBytes = liveBytes - oldSize + size;
        peakBytes = std::max(peakBytes, liveBytes);
    }
    return result;
}

size_t writeFile(const void *buffer, size_t size, size_t count, void *file)
{
    return std::fwrite(buffer, size, count, static_cast<FILE *>(file));
}
}

#define MSF_GIF_MALLOC(context, size) allocate(size)
#define MSF_GIF_REALLOC(context, memory, oldSize, size) resize(memory, oldSize, size)
#define MSF_GIF_FREE(context, memory, size) release(memory, size)
#define MSF_GIF_IMPL
#include "external/msf_gif.h"

int main(int argc, char **argv)
{
    if (argc != 4) return 2;
    const bool streaming = std::atoi(argv[1]) != 0;
    const int frames = std::atoi(argv[2]);
    if (frames <= 0) return 2;
    FILE *file = std::fopen(argv[3], "wb");
    if (!file) return 3;
    const auto start = std::chrono::steady_clock::now();
    constexpr int width = 640;
    constexpr int height = 360;
    MsfGifState state = {};
    std::vector<unsigned char> pixels(width * height * 4);
    unsigned rng = 12345;
    if (!(streaming ? msf_gif_begin_to_file(&state, width, height, writeFile, file)
                    : msf_gif_begin(&state, width, height))) return 4;
    for (int i = 0; i < frames; ++i) {
        for (size_t j = 0; j < pixels.size(); j += 4) {
            for (int k = 0; k < 3; ++k) {
                rng = rng * 1664525u + 1013904223u;
                pixels[j + k] = rng >> 24;
            }
            pixels[j + 3] = 255;
        }
        if (!(streaming ? msf_gif_frame_to_file(&state, pixels.data(), 3, 16, width * 4)
                        : msf_gif_frame(&state, pixels.data(), 3, 16, width * 4))) return 5;
    }
    const size_t beforeFinish = liveBytes;
    if (streaming) {
        if (!msf_gif_end_to_file(&state)) return 6;
    } else {
        auto result = msf_gif_end(&state);
        if (!result.data || std::fwrite(result.data, result.dataSize, 1, file) != 1) return 7;
        msf_gif_free(result);
    }
    const long bytes = std::ftell(file);
    if (std::fclose(file) != 0) return 8;
    rusage usage = {};
    if (getrusage(RUSAGE_SELF, &usage) != 0) return 9;
    long long rssBytes = usage.ru_maxrss;
#ifndef __APPLE__
    rssBytes *= 1024; // Linux reports KiB; macOS reports bytes.
#endif
    std::printf("stream=%d frames=%d bytes=%ld allocator_peak=%zu before_finish=%zu "
                "live_end=%zu process_peak_rss_bytes=%lld seconds=%.3f\n",
                streaming, frames, bytes, peakBytes, beforeFinish, liveBytes, rssBytes,
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
    return liveBytes == 0 ? 0 : 10;
}
