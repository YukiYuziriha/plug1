// Link juce::juce_core; no plugin, processor, GUI, or vocalstack_dsp required.
#include "Modulation.h"
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
#include <vector>
#include <cstdlib>
#include <new>

namespace {
thread_local bool inAudio = false;
thread_local int audioAllocations = 0, audioDestructions = 0;
}
void* operator new(std::size_t size) {
    if (inAudio) ++audioAllocations;
    if (auto* data = std::malloc(size ? size : 1)) return data;
    throw std::bad_alloc();
}
void operator delete(void* data) noexcept {
    if (inAudio && data) ++audioDestructions;
    std::free(data);
}
void operator delete(void* data, std::size_t) noexcept { ::operator delete(data); }
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete[](void* data) noexcept { ::operator delete(data); }
void operator delete[](void* data, std::size_t) noexcept { ::operator delete(data); }

using namespace vocalstack;
namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
ModulationBackend::Plan plan() {
    ModulationBackend::Plan p;
    p.modulatorCount = 2;
    p.modulators[0].id = 41;
    p.modulators[0].rateHz = 3;
    p.modulators[1].id = 97;
    p.modulators[1].type = ModulatorType::random;
    p.modulators[1].rateHz = 7;
    p.modulators[1].smoothing = 0.7f;
    p.assignmentCount = 3;
    p.assignments[0] = {0, 0, 0.4f};
    p.assignments[1] = {1, 0, -0.2f};
    p.assignments[2] = {1, 1, 0.7f};
    return p;
}
std::vector<std::array<float, 2>> values(const std::vector<int>& blocks, double rate,
                                        bool sync = false, bool notes = false) {
    ModulationBackend backend;
    backend.prepare(rate);
    auto p = plan();
    for (size_t i = 0; i < p.modulatorCount; ++i) {
        p.modulators[i].sync = sync;
        p.modulators[i].division = i == 0 ? 0.5f : 1.5f;
    }
    if (notes) {
        p.modulatorCount = 3;
        p.modulators[2].id = 501;
        p.modulators[2].retrigger = ModulationRetrigger::note;
    }
    check(backend.publish(p), "publish failed");
    std::array<float, ModulationBackend::maxTargets> contribution{};
    std::vector<std::array<float, 2>> result;
    int block = 0, offset = 0, nextNote = 123;
    const int total = int(rate * 3);
    while (offset < total) {
        int remaining = std::min(blocks[size_t(block++ % blocks.size())], total - offset);
        while (remaining) {
            if (notes && offset == nextNote) {
                backend.retrigger(ModulationRetrigger::note);
                nextNote += 701;
            }
            if (backend.samplesUntilTick() == 0) backend.tick(contribution);
            int count = std::min(remaining, backend.samplesUntilTick());
            if (notes) count = std::min(count, nextNote - offset);
            for (int i = 0; i < count; ++i) result.push_back({contribution[0], contribution[1]});
            backend.advance(count);
            remaining -= count;
            offset += count;
        }
    }
    return result;
}
void clockAndContinuity() {
    for (double rate : {44100.0, 48000.0, 96000.0}) for (bool sync : {false, true}) {
        const auto regular = values({512}, rate, sync);
        check(regular == values({1, 7, 137, 3, 511, 64}, rate, sync),
            "free/synced LFO/Random clock depends on host block size");
        check(regular == values({1, 7, 137, 3, 511, 64}, rate, sync, true),
            "note-retriggered source changed unrelated free-running sources");
    }
    ModulationBackend backend;
    backend.prepare(48000);
    auto p = plan();
    check(backend.publish(p), "initial plan rejected");
    std::array<float, ModulationBackend::maxTargets> contribution{};
    backend.tick(contribution); backend.advance(32); backend.tick(contribution);
    const float previousPhase = backend.snapshots()[0].phase;
    p.assignments[0].depth = -0.6f;
    check(backend.publish(p), "assignment edit rejected");
    backend.advance(32); backend.tick(contribution);
    check(backend.snapshots()[0].phase > previousPhase, "assignment edit restarted LFO");
    p.modulators[0].retrigger = ModulationRetrigger::note;
    p.modulators[0].phase = 0.25f;
    p.modulators[0].sync = true; p.modulators[0].division = 1;
    check(backend.publish(p), "sync plan rejected");
    backend.advance(32); backend.tick(contribution);
    backend.retrigger(ModulationRetrigger::note); backend.tick(contribution);
    check(std::abs(backend.snapshots()[0].output - 1) < 1e-6f, "note phase reset failed");
    backend.setTempo(120); backend.advance(32); backend.tick(contribution);
    check(std::abs(backend.snapshots()[0].phase - (0.25f + 64.0f / 48000)) < 1e-6f,
        "quarter-note tempo clock failed");
}
void publicationStress() {
    ModulationBackend backend;
    backend.prepare(48000);
    std::atomic<bool> done{false}, finite{true};
    std::thread consumer([&] {
        std::array<float, ModulationBackend::maxTargets> c{};
        while (!done.load()) {
            backend.tick(c);
            if (!std::isfinite(c[0]) || !std::isfinite(c[1])) finite.store(false);
            backend.advance(32);
        }
    });
    auto p = plan();
    bool okay = true;
    for (int i = 0; i < 10000; ++i) {
        p.assignments[0].depth = float(i % 20) / 20;
        std::swap(p.modulators[0], p.modulators[1]);
        for (size_t a = 0; a < p.assignmentCount; ++a) p.assignments[a].modulator ^= 1;
        okay = backend.publish(p) && okay;
        bool consistent = false;
        const auto snapshot = backend.snapshots(&consistent);
        if (consistent) for (const auto& source : snapshot) {
            okay = okay && std::isfinite(source.output) && source.phase >= 0 && source.phase < 1;
            if (source.id == 41)
                okay = okay && std::abs(source.output - std::sin(6.28318530718f * source.phase)) < 1e-5f;
            else okay = okay && source.id == 97;
        }
    }
    done.store(true); consumer.join();
    check(okay && finite.load(), "plan publication or coherent source snapshot failed");
}
void audioThreadStorage() {
    ModulationBackend backend;
    backend.prepare(48000);
    auto p = plan();
    std::array<float, ModulationBackend::maxTargets> contributions{};
    for (int edit = 0; edit < 30; ++edit) {
        p.assignments[0].depth = float(edit) / 30;
        check(backend.publish(p), "allocation test publication failed");
        inAudio = true;
        for (int tick = 0; tick < 100; ++tick) {
            backend.tick(contributions);
            backend.advance(32);
        }
        backend.retrigger(ModulationRetrigger::note);
        inAudio = false;
    }
    check(audioAllocations == 0 && audioDestructions == 0, "modulation processing allocated or destroyed heap objects");
}
void resetAndIdentity() {
    ModulationBackend backend;
    backend.prepare(48000);
    auto p = plan();
    std::array<float, ModulationBackend::maxTargets> c{};
    check(backend.publish(p), "identity initial plan");
    backend.tick(c);
    for (int i = 0; i < 99; ++i) { backend.advance(32); backend.tick(c); }
    auto removed = p;
    removed.modulatorCount = 1; removed.assignmentCount = 1;
    check(backend.publish(removed), "remove Random plan");
    backend.advance(32); backend.tick(c);
    check(backend.publish(p), "re-add Random plan");
    backend.advance(32); backend.tick(c);
    check(backend.snapshots()[1].phase == 0, "removed runtime was resurrected from an unused slot");
    p.modulators[0].phase = 0.37f; p.modulators[1].phase = 0.17f;
    check(backend.publish(p), "pending reset plan");
    backend.prepare(96000);
    backend.tick(c);
    ModulationBackend fresh;
    fresh.prepare(96000); check(fresh.publish(p), "fresh reset comparison plan");
    std::array<float, ModulationBackend::maxTargets> reference{};
    fresh.tick(reference);
    for (int i = 0; i < 1000; ++i) {
        check(c == reference, "reset did not reproduce fresh LFO/Random state at the new sample rate");
        backend.advance(32); fresh.advance(32); backend.tick(c); fresh.tick(reference);
    }
    auto invalid = p; invalid.modulators[1].id = invalid.modulators[0].id;
    check(!backend.publish(invalid), "duplicate runtime IDs accepted");
    invalid = p; invalid.modulators[1].shape = std::numeric_limits<float>::quiet_NaN();
    check(!backend.publish(invalid), "nonfinite runtime shape accepted");
    check(backend.publish({}), "empty plan rejected");
    backend.advance(32); backend.tick(c);
    check(backend.isIdle() && backend.snapshots().empty() && backend.targets().empty(), "empty plan left stale sources/targets");
}

}
int main() {
    try {
        clockAndContinuity(); publicationStress(); audioThreadStorage(); resetAndIdentity();
        std::cout << "Modulation backend tests passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
