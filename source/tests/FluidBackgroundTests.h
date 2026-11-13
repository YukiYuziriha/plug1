#pragma once

#include "FluidBackground.h"
#include "VisualTheme.h"
#include "PluginProcessor.h"
#include "ToneUiTests.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>

namespace vocalstack::fluidBackgroundTests {

inline void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

inline juce::Image render(FluidBackground& background, int width, int height,
                          juce::Colour base, juce::Colour accent, double seconds) {
    juce::Image image(juce::Image::RGB, width, height, true);
    juce::Graphics graphics(image);
    background.paint(graphics, image.getBounds(), base, accent, seconds);
    return image;
}

inline bool identical(const juce::Image& a, const juce::Image& b) {
    if (a.getBounds() != b.getBounds()) return false;
    for (int y = 0; y < a.getHeight(); ++y)
        for (int x = 0; x < a.getWidth(); ++x)
            if (a.getPixelAt(x, y) != b.getPixelAt(x, y)) return false;
    return true;
}

inline void deterministicTimeAndMotion() {
    const auto palette = themePalette(Theme::spectral);
    FluidBackground cached, independent;
    const auto first = render(cached, 320, 200, palette.background, palette.accent, 12.31);
    require(identical(first, render(independent, 320, 200, palette.background, palette.accent, 12.31)),
            "Fluid background depends on instance state or wall clock");
    require(identical(first, render(cached, 320, 200, palette.background, palette.accent, 12.39)),
            "Fluid background regenerated within the same 10 Hz phase frame");
    const auto later = render(cached, 320, 200, palette.background, palette.accent, 52.31);
    require(!identical(first, later), "Fluid background did not evolve over forty seconds");
    double totalChange = 0.0;
    for (int y = 0; y < first.getHeight(); ++y)
        for (int x = 0; x < first.getWidth(); ++x) {
            const auto a = first.getPixelAt(x, y), b = later.getPixelAt(x, y);
            totalChange += std::abs(int(a.getRed()) - int(b.getRed()))
                         + std::abs(int(a.getGreen()) - int(b.getGreen()))
                         + std::abs(int(a.getBlue()) - int(b.getBlue()));
        }
    require(totalChange / (3.0 * first.getWidth() * first.getHeight()) > 0.05,
            "Fluid background motion is only an isolated pixel change");
    require(identical(first, render(cached, 320, 200, palette.background, palette.accent, 12.31)),
            "Returning to an explicit phase did not restore the same image");
    // Also exercise negative lattice coordinates and long wrapped phases.
    for (double seconds : {40.0, 400000.0})
        require(identical(render(cached, 320, 200, palette.background, palette.accent, seconds),
                          render(independent, 320, 200, palette.background, palette.accent, seconds)),
                "Negative or wrapped noise coordinates are not deterministic");
}

inline void subduedNonUniformPixels() {
    for (auto theme : {Theme::device, Theme::neon, Theme::spectral}) {
        const auto palette = themePalette(theme);
        FluidBackground background;
        for (double seconds : {0.0, 40.0, 100.0}) {
            const auto image = render(background, 320, 200, palette.background, palette.accent, seconds);
            std::array<int, 3> minimum{255, 255, 255}, maximum{0, 0, 0};
            const std::array<int, 3> base{palette.background.getRed(), palette.background.getGreen(),
                                          palette.background.getBlue()};
            std::set<juce::uint32> colours;
            for (int y = 0; y < image.getHeight(); ++y)
                for (int x = 0; x < image.getWidth(); ++x) {
                    const auto pixel = image.getPixelAt(x, y);
                    require(pixel.getAlpha() == 255, "Fluid background left transparent pixels");
                    colours.insert(pixel.getARGB());
                    const std::array<int, 3> channels{pixel.getRed(), pixel.getGreen(), pixel.getBlue()};
                    for (size_t channel = 0; channel < channels.size(); ++channel) {
                        minimum[channel] = std::min(minimum[channel], channels[channel]);
                        maximum[channel] = std::max(maximum[channel], channels[channel]);
                        require(std::abs(channels[channel] - base[channel]) <= 32,
                                "Fluid background strayed too far from its theme base");
                    }
                }
            require(colours.size() >= 4, "Fluid background is effectively uniform");
            require(colours.size() <= 6400, "Fluid background exceeded its bounded coarse grid");
            for (size_t channel = 0; channel < minimum.size(); ++channel)
                require(maximum[channel] - minimum[channel] <= 48,
                        "Fluid background has distracting high contrast");
            require(std::max({maximum[0] - minimum[0], maximum[1] - minimum[1], maximum[2] - minimum[2]}) >= 8,
                    "Background must retain visible luminance variation, not just a faint tint");
        }
    }
}

inline void themeTintStaysMuted() {
    FluidBackground background;
    for (double seconds : {0.0, 10.0, 40.0}) {
        const auto gray = juce::Colour(0xff202020);
        const auto purple = render(background, 320, 200, gray, juce::Colour(0xffb020ff), seconds);
        const auto green = render(background, 320, 200, gray, juce::Colour(0xff20ff40), seconds);
        // Even deliberately saturated source accents must yield near-neutral surfaces.
        for (int y = 0; y < purple.getHeight(); ++y) for (int x = 0; x < purple.getWidth(); ++x) {
            const auto pixel = purple.getPixelAt(x, y);
            const auto other = green.getPixelAt(x, y);
            const int spread = std::max({int(pixel.getRed()), int(pixel.getGreen()), int(pixel.getBlue())})
                             - std::min({int(pixel.getRed()), int(pixel.getGreen()), int(pixel.getBlue())});
            const int otherSpread = std::max({int(other.getRed()), int(other.getGreen()), int(other.getBlue())})
                                  - std::min({int(other.getRed()), int(other.getGreen()), int(other.getBlue())});
            require(spread <= 3 && otherSpread <= 3,
                    "Theme tint turned the dark neutral surface into a saturated gradient");
        }
    }
}

inline void proportionalCoarsePixels() {
    const auto palette = themePalette(Theme::neon);
    FluidBackground background;
    const auto small = render(background, 400, 240, palette.background, palette.accent, 40.0);
    const auto large = render(background, 800, 480, palette.background, palette.accent, 40.0);
    int transitions = 0;
    for (int y = 0; y < small.getHeight(); ++y)
        for (int x = 0; x < small.getWidth(); ++x) {
            require(small.getPixelAt(x, y) == small.getPixelAt((x / 5) * 5, (y / 5) * 5),
                    "Fluid background interpolated inside a coarse pixel");
            if (x > 0 && small.getPixelAt(x, y) != small.getPixelAt(x - 1, y)) {
                ++transitions;
                require(x % 5 == 0, "Fluid background horizontal transition missed grid boundary");
            }
        }
    require(transitions > 0, "Coarse pixel test rendered no visible boundaries");
    for (int y = 0; y < large.getHeight(); ++y)
        for (int x = 0; x < large.getWidth(); ++x)
            require(large.getPixelAt(x, y) == small.getPixelAt(x / 2, y / 2),
                    "Resizing did not proportionally enlarge nearest-neighbour pixels");
}

inline void inheritedTimeAndOverrides() {
    juce::Component root, middle, leaf, standalone;
    root.addChildComponent(middle);
    middle.addChildComponent(leaf);
    require(fluidAnimationSeconds(standalone) == 0.0 && fluidAnimationSeconds(leaf) == 0.0,
            "Unclocked components must default to deterministic time zero");
    root.getProperties().set(vocalstackAnimationTimeProperty, 80.0);
    require(fluidAnimationSeconds(leaf) == 80.0, "Descendants did not inherit editor phase");
    middle.getProperties().set(vocalstackAnimationTimeProperty, 20.0);
    require(fluidAnimationSeconds(leaf) == 20.0, "Nearest ordinary phase did not win");
    leaf.getProperties().set(vocalstackAnimationTimeProperty, 15.0);
    root.getProperties().set(vocalstackAnimationTimeOverrideProperty, 0.0);
    require(fluidAnimationSeconds(leaf) == 0.0, "Ancestor freeze lost to a descendant ordinary phase");
    root.getProperties().set(vocalstackAnimationTimeProperty, 90.0);
    require(fluidAnimationSeconds(leaf) == 0.0, "Editor clock update defeated snapshot freeze");
    middle.getProperties().set(vocalstackAnimationTimeOverrideProperty, 4.0);
    require(fluidAnimationSeconds(leaf) == 4.0, "Nearest ancestor override did not win");
    leaf.getProperties().set(vocalstackAnimationTimeOverrideProperty, 2.0);
    require(fluidAnimationSeconds(leaf) == 2.0, "Component-local override did not win");
    leaf.getProperties().remove(vocalstackAnimationTimeOverrideProperty);
    middle.getProperties().remove(vocalstackAnimationTimeOverrideProperty);
    root.getProperties().remove(vocalstackAnimationTimeOverrideProperty);
    require(fluidAnimationSeconds(leaf) == 15.0, "Removing overrides did not restore ordinary phase");
    for (double invalid : {-1.0, std::numeric_limits<double>::infinity(),
                           std::numeric_limits<double>::quiet_NaN()}) {
        standalone.getProperties().set(vocalstackAnimationTimeProperty, invalid);
        require(fluidAnimationSeconds(standalone) == 0.0, "Invalid phase was not sanitized to zero");
    }
}

inline void stackSurfaces() {
    require(proceduralAnimationSpeed == 1.2, "Procedural motion must be exactly 20 percent faster");
    for (int theme : {0, 1, 2}) {
        auto owner = std::make_unique<Processor>();
        owner->setSkin(theme);
        std::unique_ptr<juce::AudioProcessorEditor> editor(owner->createEditor());
        auto& canvas = *toneUiTests::slider(*editor, "outputGain").getParentComponent();
        const auto capture = [&](double time) {
            editor->getProperties().set(vocalstackAnimationTimeOverrideProperty, time);
            return canvas.createComponentSnapshot(canvas.getLocalBounds());
        };
        const auto first = capture(0.0), later = capture(40.0);
        toneUiTests::screenshot(*editor, "stack-material-197-" + juce::String(theme) + ".png");
        require(identical(first, capture(0.0)), "Stack freeze did not restore deterministic pixels");
        // Empty strips inside all four control plates, away from labels/knobs.
        for (const auto x : {30, 150, 460, 580}) {
            const juce::Rectangle<int> strip(x, 250, 25, 8);
            require(!identical(first.getClippedImage(strip), later.getClippedImage(strip)),
                    "Top Stack control card did not inherit animated material");
        }
        auto* button = toneUiTests::find<juce::TextButton>(canvas, [](auto&) { return true; });
        require(button != nullptr, "Stack button not found");
        const auto buttonCapture = [&](double time) {
            editor->getProperties().set(vocalstackAnimationTimeOverrideProperty, time);
            return button->createComponentSnapshot(button->getLocalBounds());
        };
        require(!identical(buttonCapture(0), buttonCapture(40)), "Stack button material did not animate");
        const auto off = buttonCapture(0);
        button->setToggleState(true, juce::dontSendNotification);
        require(!identical(off, buttonCapture(0)), "Button active state disappeared under material");
        require(!button->getWantsKeyboardFocus(), "Textured button claimed host keyboard focus");
    }
}

// Call on the GUI/message thread; no processor, DSP, timer or sleeping required.
inline void run() {
    deterministicTimeAndMotion();
    subduedNonUniformPixels();
    themeTintStaysMuted();
    proportionalCoarsePixels();
    inheritedTimeAndOverrides();
    stackSurfaces();
}

} // namespace vocalstack::fluidBackgroundTests
