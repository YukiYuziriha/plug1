#include "SpatialEq.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <new>
#include <stdexcept>
#include <string_view>
#include <vector>

#ifdef _WIN32
#include <malloc.h>
#endif

namespace {
thread_local bool trackAllocations = false;
thread_local std::size_t allocations = 0;

void* allocate(std::size_t size, std::size_t alignment = 0) {
    if (trackAllocations) ++allocations;
    size = std::max(size, std::size_t{1});
    void* p = nullptr;
    if (!alignment) {
        p = std::malloc(size);
    } else {
#ifdef _WIN32
        p = _aligned_malloc(size, alignment);
#else
        const auto remainder = size % alignment;
        const auto padding = remainder ? alignment - remainder : 0;
        if (size <= std::numeric_limits<std::size_t>::max() - padding)
            p = std::aligned_alloc(alignment, size + padding);
#endif
    }
    if (!p) throw std::bad_alloc{};
    return p;
}

void freeAligned(void* p) noexcept {
#ifdef _WIN32
    _aligned_free(p);
#else
    std::free(p);
#endif
}
} // namespace

void* operator new(std::size_t n) { return allocate(n); }
void* operator new[](std::size_t n) { return allocate(n); }
void* operator new(std::size_t n, std::align_val_t a) { return allocate(n, static_cast<std::size_t>(a)); }
void* operator new[](std::size_t n, std::align_val_t a) { return ::operator new(n, a); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, std::align_val_t) noexcept { freeAligned(p); }
void operator delete[](void* p, std::align_val_t) noexcept { freeAligned(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { freeAligned(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { freeAligned(p); }

namespace {
using namespace vocalstack;

void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

SpatialEqSettings boosts(std::size_t count) {
    SpatialEqSettings s;
    constexpr std::array frequencies{150.f, 330.f, 700.f, 1300.f, 2300.f, 4300.f, 7200.f, 11500.f};
    for (std::size_t i = 0; i < count; ++i)
        s.bands[i] = {true, true, SpatialEqShape::Bell, frequencies[i], 6.f, 1.f, 0.75f};
    return s;
}

void checkLatency() {
    require(SpatialEq::latencySamples == 0, "SpatialEq must report zero latency");
    constexpr std::size_t onset = 53;
    for (const auto rate : {44100., 48000., 96000.}) {
        for (const auto count : {0u, 1u, 8u}) {
            SpatialEq eq;
            eq.setSettings(boosts(count));
            eq.prepare(rate);
            std::array<float, 2048> left{}, right{};
            left[onset] = 0.125f;
            right[onset] = -0.0625f;
            eq.process(left.data(), right.data(), 67);
            eq.process(left.data() + 67, right.data() + 67, left.size() - 67);
            for (std::size_t i = 0; i < left.size(); ++i) {
                require(std::isfinite(left[i]) && std::isfinite(right[i]), "impulse output is not finite");
                if (!count || i < onset)
                    require(left[i] == (i == onset ? 0.125f : 0.f)
                            && right[i] == (i == onset ? -0.0625f : 0.f), "dry path or pre-onset changed");
            }
            require(std::abs(left[onset]) > 0.01f && std::abs(right[onset]) > 0.01f,
                    "missing immediate stereo impulse response");
        }
    }
}

void checkAllocations() {
    SpatialEq eq;
    auto s = boosts(SpatialEqSettings::bandCount);
    eq.setSettings(s);
    eq.prepare(96000.);
    std::array<float, 257> left{}, right{};
    allocations = 0;
    trackAllocations = true;
    // Explicit call: unlike a new expression, this self-check cannot be elided as a pair.
    ::operator delete(::operator new(16));
    trackAllocations = false;
    require(allocations == 1, "allocation counter self-check failed");
    allocations = 0;
    trackAllocations = true;
    for (int i = 0; i < 320; ++i) {
        left.fill(0.f);
        right.fill(0.f);
        if (i != 0) {
            s.bands[0].depth = i % 3 == 0 ? 0.f : 0.73f;
            s.bands[1].frequency = 500.f + static_cast<float>(i);
            s.bands[2].gainDb = i % 32 < 16 ? -6.f : 6.f;
            s.bands[3].enabled = i % 7 != 0;
            s.bands[4].active = i % 64 >= 32;
            s.bands[5].shape = i % 32 < 16 ? SpatialEqShape::LowShelf : SpatialEqShape::HighShelf;
            eq.setSettings(s);
            if (i % 63 == 0) eq.reset();
        }
        left[0] = 0.08f;
        right[0] = -0.03f;
        eq.process(left[0], right[0]);
        eq.process(left.data() + 1, right.data() + 1, i % 2 == 0 ? 256 : 31);
    }
    trackAllocations = false;
    require(allocations == 0, "C++ heap allocation after prepare");
}

struct Event {
    std::size_t frame;
    SpatialEqSettings settings;
    bool reset;
};

SpatialEqSettings readSettings() {
    SpatialEqSettings s;
    for (auto& b : s.bands) {
        int shape = 0;
        require(bool(std::cin >> b.active >> b.enabled >> shape >> b.frequency >> b.gainDb >> b.q >> b.depth),
                "invalid band settings");
        switch (shape) {
        case 0: b.shape = SpatialEqShape::Bell; break;
        case 1: b.shape = SpatialEqShape::LowShelf; break;
        case 2: b.shape = SpatialEqShape::HighShelf; break;
        default: throw std::runtime_error("invalid shape");
        }
    }
    return s;
}

// Text protocol driven by Test-Spatial-EQ-Cpp.py. Parsing, copies and I/O are
// outside the engine. The same input is rendered in two block partitions.
void render() {
    double rate = 0;
    std::size_t frames = 0, count = 0;
    require(bool(std::cin >> rate >> frames >> count) && frames > 0 && frames <= 1000000 && count <= frames,
            "invalid render header");
    const auto initial = readSettings();
    std::vector<Event> events;
    for (std::size_t i = 0; i < count; ++i) {
        std::size_t frame = 0;
        bool reset = false;
        require(bool(std::cin >> frame >> reset) && frame < frames && (events.empty() || frame > events.back().frame),
                "invalid event frame/reset");
        events.push_back({frame, readSettings(), reset});
    }
    std::vector<float> left(frames), right(frames);
    for (std::size_t i = 0; i < frames; ++i)
        require(bool(std::cin >> left[i] >> right[i]), "missing input frames");
    std::cin >> std::ws;
    require(std::cin.eof(), "unexpected input after final frame");

    const auto renderPartition = [&](auto& l, auto& r, bool irregular) {
        SpatialEq eq;
        eq.setSettings(initial);
        eq.prepare(rate);
        constexpr std::array<std::size_t, 7> blocks{17, 257, 1, 31, 64, 5, 131};
        std::size_t event = 0, block = 0;
        for (std::size_t pos = 0; pos < frames;) {
            if (event < events.size() && events[event].frame == pos) {
                eq.setSettings(events[event].settings);
                if (events[event].reset) eq.reset();
                ++event;
            }
            const auto end = event < events.size() ? events[event].frame : frames;
            const auto n = irregular ? std::min(end - pos, blocks[block++ % blocks.size()]) : end - pos;
            eq.process(l.data() + pos, r.data() + pos, n);
            pos += n;
        }
    };
    auto splitLeft = left, splitRight = right;
    renderPartition(left, right, false);
    renderPartition(splitLeft, splitRight, true);
    require(left == splitLeft && right == splitRight, "output depends on block partition");
    std::cout << std::setprecision(std::numeric_limits<float>::max_digits10);
    for (std::size_t i = 0; i < frames; ++i) std::cout << left[i] << ' ' << right[i] << '\n';
    require(bool(std::cout), "could not write render output");
}

void benchmark() {
    using Clock = std::chrono::steady_clock;
    constexpr auto rate = 48000.;
    constexpr std::size_t block = 256, blocksPerRun = 4096, repeats = 5;
    std::array<float, block> inputL{}, inputR{};
    for (std::size_t i = 0; i < block; ++i) {
        const double phase = 6.283185307179586 * static_cast<double>(i) / block;
        inputL[i] = static_cast<float>(0.07 * std::sin(3 * phase) + 0.04 * std::sin(11 * phase) + 0.03 * std::sin(37 * phase));
        inputR[i] = static_cast<float>(0.06 * std::cos(5 * phase) + 0.03 * std::sin(17 * phase) + 0.02 * std::cos(43 * phase));
    }
    std::cout << "SpatialEq steady-state: " << rate << " Hz, " << block << " stereo frames/block, "
              << repeats << " trials x " << blocksPerRun << " blocks\n"
              << "depth_bands,median_ns_per_stereo_frame,median_us_per_block,median_one_thread_realtime_pct,checksum\n";
    for (const auto count : {0u, 1u, 8u}) {
        SpatialEq eq;
        eq.setSettings(boosts(count));
        eq.prepare(rate);
        std::array<double, repeats> timings{};
        double checksum = 0;
        const auto run = [&](std::size_t blocks) {
            for (std::size_t j = 0; j < blocks; ++j) {
                auto left = inputL, right = inputR;
                eq.process(left.data(), right.data(), block);
                checksum += static_cast<double>(left[j % block]) + right[(j + 23) % block];
            }
        };
        run(512);
        for (auto& nsPerFrame : timings) {
            const auto start = Clock::now();
            run(blocksPerRun);
            nsPerFrame = std::chrono::duration<double, std::nano>(Clock::now() - start).count() / (block * blocksPerRun);
        }
        require(std::isfinite(checksum), "non-finite benchmark checksum");
        std::sort(timings.begin(), timings.end());
        const double ns = timings[repeats / 2];
        std::cout << std::fixed << std::setprecision(3) << count << ',' << ns << ','
                  << ns * block / 1000.0 << ',' << ns * rate / 1e7 << ',' << checksum << '\n';
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        std::ios::sync_with_stdio(false);
        std::cin.imbue(std::locale::classic());
        std::cout.imbue(std::locale::classic());
        const std::string_view mode = argc == 2 ? argv[1] : "";
        if (mode == "--render") render();
        else if (mode == "--benchmark") benchmark();
        else if (mode == "--self-test") {
            checkLatency();
            checkAllocations();
            std::cout << "SpatialEq impulse latency and post-prepare C++ allocation checks passed\n";
        } else {
            std::cerr << "usage: SpatialEqHarness --self-test | --render | --benchmark\n";
            return 2;
        }
    } catch (const std::exception& e) {
        std::cerr << "SpatialEqHarness: " << e.what() << '\n';
        return 1;
    }
}
