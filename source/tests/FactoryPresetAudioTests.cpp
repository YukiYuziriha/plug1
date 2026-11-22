// Processor integration test registered as eight per-preset CTest shards.
//
// stdout is CSV (48 rows); diagnostics/summary go to stderr. No user preferences.
// Optional single argument: factory index 0..7, for six-case bounded shards.
// With no arguments all 48 cases run. Two fresh processors per case verify reproducibility.
// Fixture: five seconds of voiced 173/1091 Hz decaying pulses every 370 ms,
// 3 ms-ish attack, fixed amplitude .2, NOT peak/RMS normalized. Analytic input
// bound .26 (-11.70 dBFS); right stereo channel uses .8 amplitude and .23-radian
// upper-partial offset. Mono uses left. This is synthetic, not recorded speech.
// .25 s silent preroll settles controls; 15 s silence follows excitation.
// Windows follow reported processor latency: active [latency, latency+5s),
// tail [latency+5s, latency+20s), final [latency+19s, latency+20s).
// RMS pools channel energy; peaks are sample peaks, NOT true peaks. All output
// samples (including preroll, latency and tails) are checked for finiteness.
// Gates: no sample clipping, non-silent active audio and tail, final-second
// energy below first tail second. No arbitrary equal-loudness/preset matching,
// automatic trim, LUFS, RT60, universal input headroom or listening claims.
// Old whole-preset output cannot be recovered by dividing by four: dry/wet
// blend is linear and only the reverb wet branch received the calibration.
//
// 2026-11-12 MSVC Release review, calibrated current library: 48 cases / 96
// renders passed. Measured input peak -12.880505..-12.880441 dBFS, pooled RMS
// -30.195836..-29.333972 dBFS. Worst output sample peaks across rates/layouts:
// Dark Lead -16.312783; Grit Adlibs -16.326572; Deep Double -19.931046;
// Night Echo -19.066225; Cloud Choir -24.478502; Clean Vocal Utility -18.180509;
// Vocal Filter Motion -24.089833; Shimmer Bloom -24.169750 dBFS.
// No output-trim adjustment evidenced by this modest-input fixture. Preserve
// the existing -3 dB trims / Shimmer Bloom -6 dB and intentional level/tail
// differences. These are observations, not brittle goldens or guarantees for
// arbitrary recordings. Full per-case CSV is generated on stdout; retain it
// in ignored build storage. No production source/preset changes were made.
#include "PresetManager.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <stdexcept>

using namespace vocalstack;
namespace {
constexpr double pi = 3.14159265358979323846;
constexpr int maxBlock = 257;
constexpr std::array<int, 7> blocks{1, 17, 64, 137, 257, 31, 113};
void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
void success(const juce::Result& result) {
    if (result.failed()) throw std::runtime_error(result.getErrorMessage().toStdString());
}
struct TemporaryFolder {
    juce::File root = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("VocalStackFactoryAudio-" + juce::Uuid().toString());
    TemporaryFolder() { success(root.createDirectory()); }
    ~TemporaryFolder() { root.deleteRecursively(); }
};
struct Meter {
    double energy = 0, peak = 0;
    uint64_t samples = 0, clipped = 0;
    void add(float sample) {
        require(std::isfinite(sample), "Nonfinite sample");
        const double x = sample;
        energy += x * x;
        peak = std::max(peak, std::abs(x));
        ++samples;
        if (std::abs(x) >= 1) ++clipped;
    }
    double rms() const { return std::sqrt(energy / double(std::max(uint64_t{1}, samples))); }
};
double db(double amplitude) { return 20 * std::log10(std::max(1.e-30, amplitude)); }
float excitation(int frame, double rate, int channel) {
    const double time = frame / rate, age = std::fmod(time, .37);
    const double envelope = std::exp(-24 * age) * (1 - std::exp(-800 * age));
    return float(.2 * (channel ? .8 : 1) * envelope
        * (std::sin(2 * pi * 173 * time) + .3 * std::sin(2 * pi * 1091 * time + (channel ? .23 : 0))));
}
struct Measurement {
    Meter input, all, active, tail, firstTail, finalTail;
    uint64_t hash = 14695981039346656037ull;
    float trim = 0;
    int latency = 0;
};
Measurement render(int preset, double rate, int channels, const juce::File& preferences) {
    auto owner = std::make_unique<Processor>();
    auto layout = owner->getBusesLayout();
    const auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
    layout.inputBuses.set(0, set); layout.outputBuses.set(0, set);
    require(owner->setBusesLayout(layout), "Requested mono/stereo layout rejected");
    PresetManager manager(*owner, preferences);
    success(manager.load({PresetManager::factoryNames()[preset], {}, preset}));
    Measurement result;
    result.trim = owner->parameters.getRawParameterValue("outputGain")->load();
    owner->prepareToPlay(rate, maxBlock);
    result.latency = owner->getLatencySamples();
    require(result.latency >= 0 && result.latency < int(rate), "Unexpected processor latency");
    juce::AudioBuffer<float> storage(channels, maxBlock);
    juce::MidiBuffer midi;
    for (int frame = 0; frame < int(rate * .25);) {
        const int count = std::min(maxBlock, int(rate * .25) - frame);
        juce::AudioBuffer<float> audio(storage.getArrayOfWritePointers(), channels, count);
        audio.clear(); owner->processBlock(audio, midi);
        for (int c = 0; c < channels; ++c) for (int i = 0; i < count; ++i) {
            result.all.add(audio.getSample(c, i));
            require(std::abs(audio.getSample(c, i)) < 1.e-8f, "Fresh preset generated audio from silence");
        }
        frame += count;
    }
    const int activeEnd = int(rate * 5), tailBegin = activeEnd + result.latency;
    const int end = int(rate * 20) + result.latency;
    size_t block = 0;
    for (int frame = 0; frame < end;) {
        const int count = std::min(blocks[block++ % blocks.size()], end - frame);
        juce::AudioBuffer<float> audio(storage.getArrayOfWritePointers(), channels, count);
        for (int c = 0; c < channels; ++c) for (int i = 0; i < count; ++i) {
            const float x = frame + i < activeEnd ? excitation(frame + i, rate, c) : 0;
            audio.setSample(c, i, x);
            if (frame + i < activeEnd) result.input.add(x);
        }
        owner->processBlock(audio, midi);
        for (int i = 0; i < count; ++i) for (int c = 0; c < channels; ++c) {
            const float y = audio.getSample(c, i);
            result.all.add(y);
            result.hash = (result.hash ^ std::bit_cast<uint32_t>(y)) * 1099511628211ull;
            const int position = frame + i;
            if (position >= result.latency && position < tailBegin) result.active.add(y);
            if (position >= tailBegin) result.tail.add(y);
            if (position >= tailBegin && position < tailBegin + int(rate)) result.firstTail.add(y);
            if (position >= end - int(rate)) result.finalTail.add(y);
        }
        frame += count;
    }
    owner->releaseResources();
    require(result.all.clipped == 0, "Factory sample peak reaches/exceeds 0 dBFS at documented input");
    require(result.active.rms() > 1.e-5, "Factory unexpectedly muted active audio");
    if (preset < PresetManager::originalFactoryCount)
        require(result.tail.rms() > 1.e-9, "Factory unexpectedly lost its delay/reverb tail");
    if (result.firstTail.rms() > 1.e-9)
        require(result.finalTail.rms() < result.firstTail.rms(), "Factory tail did not decay over 15 seconds");
    return result;
}
}
int main(int argc, char** argv) {
    try {
        require(argc <= 2, "Usage: FactoryPresetAudioTests [factory-index | --validate]");
        const bool validateOnly = argc == 2 && std::string_view(argv[1]) == "--validate";
        int selected = -1;
        if (argc == 2 && !validateOnly) {
            char* end = nullptr;
            const long value = std::strtol(argv[1], &end, 10);
            require(end != argv[1] && *end == '\0' && value >= 0 && value < PresetManager::factoryCount, "Invalid factory index");
            selected = int(value);
        }
        juce::ScopedJuceInitialiser_GUI init;
        juce::ScopedNoDenormals noDenormals;
        TemporaryFolder files;
        const auto names = PresetManager::factoryNames();
        require(names.size() == PresetManager::factoryCount, "Factory name/recipe count differs");
        if (validateOnly) {
            auto processor = std::make_unique<Processor>();
            for (const auto* id : {"levelOn", "doubleOn", "formantOn", "choraleOn", "advancedOn", "bypass"})
                require(processor->parameters.getRawParameterValue(id)->load() == 0.0f, "Fresh instance enabled a feature or bypass");
            processor->prepareToPlay(48000, 64);
            juce::MidiBuffer midi;
            juce::AudioBuffer<float> audio(2, 64);
            const int latency = processor->getLatencySamples();
            for (int frame = 0; frame < latency + 128; frame += 64) {
                audio.clear();
                if (frame == 0) { audio.setSample(0, 0, .25f); audio.setSample(1, 0, -.125f); }
                processor->processBlock(audio, midi);
                for (int i = 0; i < 64; ++i) {
                    require(audio.getSample(0, i) == (frame + i == latency ? .25f : 0.f), "Fresh left channel is not latency-aligned dry");
                    require(audio.getSample(1, i) == (frame + i == latency ? -.125f : 0.f), "Fresh right channel is not latency-aligned dry");
                }
            }
            processor->releaseResources();
            PresetManager manager(*processor, files.root.getChildFile("isolated.settings"));
            for (int preset = 0; preset < names.size(); ++preset) {
                const auto result = manager.load({names[preset], {}, preset});
                if (result.failed()) std::fprintf(stderr, "%s: %s\n", names[preset].toRawUTF8(), result.getErrorMessage().toRawUTF8());
                success(result);
            }
            std::puts("PASS: fresh non-bypassed stereo is latency-aligned dry; all factory patches load");
            return 0;
        }
        std::puts("preset,rate,channels,trim_db,latency,input_peak_dbfs,input_rms_dbfs,output_peak_dbfs,active_rms_dbfs,tail_peak_dbfs,tail_rms_dbfs,first_tail_rms_dbfs,final_tail_rms_dbfs,clipped_samples,hash");
        int cases = 0;
        for (int preset = 0; preset < names.size(); ++preset)
            for (double rate : {44100., 48000., 96000.}) for (int channels : {1, 2}) {
                if (selected >= 0 && preset != selected) continue;
                std::fprintf(stderr, "Measuring %s %.0f Hz %dch\n", names[preset].toRawUTF8(), rate, channels);
                const auto a = render(preset, rate, channels, files.root.getChildFile("isolated.settings"));
                const auto b = render(preset, rate, channels, files.root.getChildFile("isolated.settings"));
                require(a.hash == b.hash && a.all.energy == b.all.energy && a.all.peak == b.all.peak,
                    "Fresh identical preset renders are not deterministic");
                std::printf("%s,%.0f,%d,%.3f,%d,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%llu,%016llx\n",
                    names[preset].toRawUTF8(), rate, channels, a.trim, a.latency,
                    db(a.input.peak), db(a.input.rms()), db(a.all.peak), db(a.active.rms()),
                    db(a.tail.peak), db(a.tail.rms()), db(a.firstTail.rms()), db(a.finalTail.rms()),
                    static_cast<unsigned long long>(a.all.clipped), static_cast<unsigned long long>(a.hash));
                std::fflush(stdout); ++cases;
            }
        std::fprintf(stderr, "PASS: %d factory/rate/layout cases, %d renders, finite unclipped output and decaying tails; deterministic fresh repeats. No listening check.\n", cases, cases * 2);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what()); return 1;
    }
}
