#pragma once

#include "SpatialEq.h"
#include <array>
#include <cstddef>

namespace juce { class AudioProcessor; class AudioProcessorParameter; }

namespace vocalstack {

struct FxSettings;
class Processor;

// One audio callback's borrowed host queues. The host owns the queue data until
// process returns; neither Processor nor VocalEngine retains it across blocks.
class Vst3SpatialEqAutomation {
public:
    struct Point { int sample; double value; };
    using ReadPoint = bool (*)(void*, int index, Point&) noexcept;
    struct Target { size_t slot, band; bool depth; float minimum, maximum; };

    void add(Target, void* queue, ReadPoint, int count, int samples, double previous, Point last) noexcept;
    bool empty() const noexcept { return size == 0; }
    void apply(int sample, std::array<FxSettings, 16>&) noexcept;

private:
    struct Lane {
        Target target;
        void* queue;
        ReadPoint read;
        int count, index;
        Point from, to, last;
        double slope;
        void updateSlope() noexcept;
        float valueAt(int sample) noexcept;
    };
    std::array<Lane, 15 * SpatialEqSettings::bandCount * 2> lanes;
    size_t size = 0;
};

// Wrapper calls this before its normal final-value notification. Invalid or
// unsupported input leaves that existing block-rate behavior in place.
void captureVst3SpatialEqAutomation(Vst3SpatialEqAutomation&, juce::AudioProcessorParameter&,
    void* queue, Vst3SpatialEqAutomation::ReadPoint, int count, int samples,
    Vst3SpatialEqAutomation::Point last) noexcept;

class ScopedVst3SpatialEqAutomation {
public:
    ScopedVst3SpatialEqAutomation(juce::AudioProcessor&, Vst3SpatialEqAutomation&) noexcept;
    ~ScopedVst3SpatialEqAutomation();
    ScopedVst3SpatialEqAutomation(const ScopedVst3SpatialEqAutomation&) = delete;
    ScopedVst3SpatialEqAutomation& operator=(const ScopedVst3SpatialEqAutomation&) = delete;

private:
    Processor* processor = nullptr;
};

} // namespace vocalstack
