#include "VocalEngine.h"
#include "baseline/VocalEngine.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <vector>

// The runner extracts the pinned pre-routing DSP into build/backend-tests/baseline,
// renaming only its namespace. Nothing in the tracked source is overwritten.
int main() {
    try {
        for (int rate : {44100, 48000, 96000}) for (int channels : {1, 2}) for (bool chorale : {false, true}) {
            vocalstack::Settings s;
            baseline::Settings old;
            s.choraleOn = old.choraleOn = chorale;
            vocalstack::VocalEngine engine; baseline::VocalEngine reference;
            engine.prepare(rate, channels, 2, s); reference.prepare(rate, channels, 2, old);
            constexpr int size = 997;
            float inLeft[size], inRight[size], left[size], right[size], oldLeft[size], oldRight[size];
            float maxDifference = 0;
            for (int position = 0; position < rate * 2;) {
                const int count = std::min(size - position % 127, rate * 2 - position);
                for (int i = 0; i < count; ++i) {
                    const double phase = 2 * std::numbers::pi * 137 * (position + i) / rate;
                    inLeft[i] = 0.1f * float(std::sin(phase) + 0.3 * std::sin(phase * 7));
                    inRight[i] = 0.07f * float(std::sin(phase * 1.1));
                }
                const float* input[] = {inLeft, inRight};
                float* output[] = {left, right}; float* oldOutput[] = {oldLeft, oldRight};
                engine.process(input, output, count, s); reference.process(input, oldOutput, count, old);
                for (int i = 0; i < count; ++i) maxDifference = std::max({maxDifference, std::abs(left[i] - oldLeft[i]), std::abs(right[i] - oldRight[i])});
                position += count;
            }
            if (engine.latency() != reference.latency() || maxDifference > 1e-6f)
                throw std::runtime_error("default sound/latency changed against pre-routing engine");
            std::cout << "Baseline " << rate << " Hz " << channels << " channels Chorale=" << chorale
                      << " max difference=" << maxDifference << '\n';
        }
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
