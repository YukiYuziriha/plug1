#include "Chorale.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <numbers>
#include <stdexcept>

#include <vector>

namespace {

using vocalstack::Chorale;
constexpr std::array rates{44100, 48000, 96000, 192000};

void expect(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

int samples(int rate, double seconds) {
    return static_cast<int>(std::ceil(rate * seconds));
}

bool identical(float a, float b) {
    return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
}

struct Frame {
    float left{}, right{};
};

Frame inputAt(int index, int rate, bool mono = false) {
    const double t = static_cast<double>(index) / rate;
    const float left = static_cast<float>(0.22 * std::sin(2 * std::numbers::pi * 137 * t)
        + 0.08 * std::sin(2 * std::numbers::pi * 613 * t));
    const float right = mono ? left : static_cast<float>(
        0.19 * std::sin(2 * std::numbers::pi * 193 * t + 0.4)
        + 0.09 * std::sin(2 * std::numbers::pi * 877 * t));
    return {left, right};
}

void process(Chorale& effect, Frame& frame, bool enabled, float mix, float motion) {
    effect.process(frame.left, frame.right, enabled, mix, motion);
    expect(std::isfinite(frame.left) && std::isfinite(frame.right), "Nonfinite output");
}

void expectIdentical(Frame a, Frame b, const char* reason) {
    expect(identical(a.left, b.left) && identical(a.right, b.right), reason);
}

void prepareAndReset(int rate) {
    Chorale effect, fresh;
    effect.prepare(rate);
    fresh.prepare(rate);
    for (int i = 0; i < samples(rate, 0.12); ++i) {
        Frame silence{};
        process(effect, silence, true, 1.0f, 1.0f);
        expect(silence.left == 0 && silence.right == 0, "Prepared silence is not silent");
    }
    for (int i = 0; i < samples(rate, 0.10); ++i) {
        auto frame = inputAt(i, rate);
        process(effect, frame, true, 0.8f, 0.9f);
    }
    effect.reset();
    for (int i = 0; i < samples(rate, 0.09); ++i) {
        auto actual = inputAt(i, rate);
        auto expected = actual;
        process(effect, actual, true, 0.7f, 0.2f);
        process(fresh, expected, true, 0.7f, 0.2f);
        expectIdentical(actual, expected, "Reset did not restore fresh state");
    }
    effect.reset();
    for (int i = 0; i < samples(rate, 0.065); ++i) {
        Frame silence{};
        process(effect, silence, true, 1.0f, 0.5f);
        expect(silence.left == 0 && silence.right == 0, "Reset retained audio history");
    }
    // Reprepare the same instance at a different rate, including buffer resizing.
    effect.prepare(rate == 192000 ? 44100 : 192000);
    effect.prepare(rate);
    fresh.reset();
    for (int i = 0; i < samples(rate, 0.06); ++i) {
        auto actual = inputAt(i, rate);
        auto expected = actual;
        process(effect, actual, true, 1.0f, 1.0f);
        process(fresh, expected, true, 1.0f, 1.0f);
        expectIdentical(actual, expected, "Reprepare did not restore fresh state");
    }
}

void transparency(int rate) {
    constexpr std::array probes{0.0f, -0.0f, 0.125f, -0.75f, 1.0f, -1.0f, 0.000001f};
    for (bool enabled : {false, true}) {
        Chorale effect;
        effect.prepare(rate);
        for (int i = 0; i < samples(rate, 0.08); ++i) {
            Frame frame{probes[static_cast<std::size_t>(i) % probes.size()],
                        probes[static_cast<std::size_t>(i + 3) % probes.size()]};
            const auto dry = frame;
            process(effect, frame, enabled, enabled ? 0.0f : 1.0f, 1.0f);
            expectIdentical(frame, dry, "Disabled/Mix 0 is not bit-exact transparent");
        }
        // After active processing, transparency is required once the fade settles,
        // not immediately on the automation edge.
        for (int i = 0; i < samples(rate, 0.10); ++i) {
            auto frame = inputAt(i, rate);
            process(effect, frame, true, 1.0f, 0.8f);
        }
        for (int i = 0; i < samples(rate, 0.46); ++i) {
            auto frame = inputAt(i, rate);
            const auto dry = frame;
            process(effect, frame, enabled, enabled ? 0.0f : 1.0f, 0.1f);
            if (i >= samples(rate, 0.40))
                expectIdentical(frame, dry, "Settled bypass/Mix 0 is not exact");
        }
    }
}

void stereoAndMono(int rate) {
    for (bool mono : {false, true}) {
        for (float mix : {0.5f, 1.0f}) {
            Chorale effect;
            effect.prepare(rate);
            double leftPower = 0, rightPower = 0, foldPower = 0, difference = 0, width = 0;
            int count = 0;
            for (int i = 0; i < samples(rate, 0.65); ++i) {
                const auto dry = inputAt(i, rate, mono);
                auto frame = dry;
                process(effect, frame, true, mix, 0.8f);
                expect(std::max(std::abs(frame.left), std::abs(frame.right)) <= 0.451f,
                       "Signal exceeded the conservative 1.5x peak bound");
                if (i < samples(rate, 0.15))
                    continue;
                const double fold = (frame.left + frame.right) * 0.5;
                leftPower += frame.left * frame.left;
                rightPower += frame.right * frame.right;
                foldPower += fold * fold;
                difference += std::pow(frame.left - dry.left, 2)
                    + std::pow(frame.right - dry.right, 2);
                width += std::pow(frame.left - frame.right, 2);
                ++count;
            }
            expect(leftPower / count > 1.0e-5 && rightPower / count > 1.0e-5,
                   "Wet channel is silent");
            expect(foldPower / count > 1.0e-5, "Mono fold collapsed to silence");
            expect(difference / count > 1.0e-4, "Wet output did not differ from dry");
            if (mono)
                expect(width / count > 1.0e-5, "Mono input did not acquire stereo width");
        }
    }
}

struct Controls {
    bool enabled;
    float mix, motion;
};

Controls controlsAt(int index, int rate) {
    // Deliberately sample-indexed, so host partitioning cannot shift events.
    const int stage = static_cast<int>(static_cast<double>(index) * 10 / rate) % 6;
    constexpr std::array<Controls, 6> controls{{
        {true, 1.0f, 0.0f}, {true, 0.2f, 1.0f}, {false, 1.0f, 0.1f},
        {true, 0.8f, 0.9f}, {true, 0.0f, 0.0f}, {true, 1.0f, 1.0f}}};
    return controls[static_cast<std::size_t>(stage)];
}

std::vector<Frame> render(int rate, const std::vector<int>& blocks) {
    Chorale effect;
    effect.prepare(rate);
    std::vector<Frame> output(static_cast<std::size_t>(samples(rate, 0.75)));
    std::size_t position = 0, blockIndex = 0;
    while (position < output.size()) {
        const auto end = std::min(output.size(), position
            + static_cast<std::size_t>(blocks[blockIndex++ % blocks.size()]));
        for (; position < end; ++position) {
            const auto index = static_cast<int>(position);
            auto frame = inputAt(index, rate);
            const auto controls = controlsAt(index, rate);
            process(effect, frame, controls.enabled, controls.mix, controls.motion);
            output[position] = frame;
        }
    }
    return output;
}

void partitionAndAutomation(int rate) {
    const auto reference = render(rate, {1});
    for (const auto& blocks : {std::vector<int>{64}, std::vector<int>{512},
                              std::vector<int>{1, 7, 137, 2, 1024, 31, 3}}) {
        const auto actual = render(rate, blocks);
        for (std::size_t i = 0; i < actual.size(); ++i)
            expectIdentical(actual[i], reference[i], "Host block partition changed output");
    }
    double power = 0;
    for (std::size_t i = 1; i < reference.size(); ++i) {
        const auto frame = reference[i];
        // The stimulus's highest partial is 877 Hz. Scale the allowed derivative
        // with rate: a fixed per-sample bound would hide high-rate discontinuities.
        const float limit = 0.065f * 44100.0f / static_cast<float>(rate);
        expect(std::abs(frame.left - reference[i - 1].left) < limit
                   && std::abs(frame.right - reference[i - 1].right) < limit,
               "Enabled/mix/motion automation produced an excessive discontinuity");
        expect(std::max(std::abs(frame.left), std::abs(frame.right)) < 0.451f,
               "Automation exceeded headroom bound");
        power += frame.left * frame.left + frame.right * frame.right;
    }
    expect(power / reference.size() > 1.0e-4, "Automated processing is silent");
}

void headroomAndTail(int rate) {
    Chorale effect;
    effect.prepare(rate);
    Frame final{};
    for (int i = 0; i < samples(rate, 0.45); ++i) {
        final = {1.0f, -1.0f};
        process(effect, final, true, 1.0f, 1.0f);
        expect(std::max(std::abs(final.left), std::abs(final.right)) <= 1.5001f,
               "Coherent taps exceeded 1.5x headroom (+3.53 dB)");
    }
    expect(final.left > 1.4f && final.right < -1.4f,
           "Steady coherent wet gain unexpectedly collapsed");
    double earlyPower = 0;
    for (int i = 0; i < samples(rate, 0.10); ++i) {
        Frame frame{};
        process(effect, frame, true, 1.0f, 1.0f);
        if (i < samples(rate, 0.005))
            earlyPower += frame.left * frame.left + frame.right * frame.right;
        // No feedback: 40 ms delay budget plus 25 ms for smoothing/filter decay.
        if (i >= samples(rate, 0.065))
            expect(std::max(std::abs(frame.left), std::abs(frame.right)) <= 1.0e-7f,
                   "Silence tail outlasted 40 ms plus smoothing allowance");
    }
    expect(earlyPower > 1.0, "Tail vanished immediately instead of draining history");

    effect.reset();
    // Prime only controls with silence, then measure wet-only impulse timing in
    // seconds at every rate; sample counts must not be hard-coded for 48 kHz.
    for (int i = 0; i < samples(rate, 0.40); ++i) {
        Frame frame{};
        process(effect, frame, true, 1.0f, 0.0f);
    }
    int first = -1;
    double impulsePower = 0;
    for (int i = 0; i < samples(rate, 0.065); ++i) {
        Frame frame{i == 0 ? 1.0f : 0.0f, 0.0f};
        process(effect, frame, true, 1.0f, 0.0f);
        expect(frame.right == 0.0f, "Independent channels leaked into each other");
        if (std::abs(frame.left) > 1.0e-7f && first < 0)
            first = i;
        impulsePower += frame.left * frame.left;
        if (i >= samples(rate, 0.045))
            expect(std::abs(frame.left) <= 1.0e-7f, "Impulse persisted beyond delay/filter budget");
    }
    expect(first >= samples(rate, 0.008) && first <= samples(rate, 0.030),
           "Wet impulse onset is not in the expected millisecond range");
    expect(impulsePower > 1.0e-5, "Wet impulse was lost");
}

} // namespace

int main() {
    int failures = 0;
    for (int rate : rates) {
        const auto run = [&](const char* name, auto test) {
            try {
                test(rate);
                std::cout << "PASS " << name << " @ " << rate << " Hz\n";
            } catch (const std::exception& error) {
                ++failures;
                std::cerr << "FAIL " << name << " @ " << rate << " Hz: " << error.what() << '\n';
            }
        };
        run("prepare/reset", prepareAndReset);
        run("exact transparency", transparency);
        run("stereo/mono fold and wet difference", stereoAndMono);
        run("block independence and automation continuity", partitionAndAutomation);
        run("headroom, tail and rate-scaled timing", headroomAndTail);
    }
    if (failures == 0)
        std::cout << "All Chorale tests passed\n";
    else
        std::cout << "Chorale tests failed: " << failures << '\n';
    return failures == 0 ? 0 : 1;
}
