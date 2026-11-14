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
            int accentPixels = 0;
            for (int y = 0; y < image.getHeight(); ++y)
                for (int x = 0; x < image.getWidth(); ++x) {
                    const auto pixel = image.getPixelAt(x, y);
                    require(pixel.getAlpha() == 255, "Fluid background left transparent pixels");
                    colours.insert(pixel.getARGB());
                    if (std::max({std::abs(int(pixel.getRed()) - base[0]),
                                  std::abs(int(pixel.getGreen()) - base[1]),
                                  std::abs(int(pixel.getBlue()) - base[2])}) > 32) ++accentPixels;
                    const std::array<int, 3> channels{pixel.getRed(), pixel.getGreen(), pixel.getBlue()};
                    for (size_t channel = 0; channel < channels.size(); ++channel) {
                        minimum[channel] = std::min(minimum[channel], channels[channel]);
                        maximum[channel] = std::max(maximum[channel], channels[channel]);
                        require(std::abs(channels[channel] - base[channel]) <= 144,
                                "Rare ore glint exceeded bounded highlight range");
                    }
                }
            require(accentPixels < image.getWidth() * image.getHeight() / 100, "Bright accents spread beyond rare texels");
            require(colours.size() >= 4, "Fluid background is effectively uniform");
            require(colours.size() <= 6400, "Fluid background exceeded its bounded coarse grid");
            for (size_t channel = 0; channel < minimum.size(); ++channel)
                require(maximum[channel] - minimum[channel] <= 160,
                        "Rare ore glint exceeded tonal bounds");
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
        // Only the deliberately sparse glints may leave the neutral palette.
        int highlights = 0;
        for (int y = 0; y < purple.getHeight(); ++y) for (int x = 0; x < purple.getWidth(); ++x) {
            const auto pixel = purple.getPixelAt(x, y);
            const auto other = green.getPixelAt(x, y);
            const int spread = std::max({int(pixel.getRed()), int(pixel.getGreen()), int(pixel.getBlue())})
                             - std::min({int(pixel.getRed()), int(pixel.getGreen()), int(pixel.getBlue())});
            const int otherSpread = std::max({int(other.getRed()), int(other.getGreen()), int(other.getBlue())})
                                  - std::min({int(other.getRed()), int(other.getGreen()), int(other.getBlue())});
            if (spread > 3 || otherSpread > 3) ++highlights;
            require(spread <= 144 && otherSpread <= 144, "Glint exceeded bounded accent saturation");
        }
        require(highlights < purple.getWidth() * purple.getHeight() / 100,
                "Theme highlights are not sparse");
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
        for (double phase : {0.0, 6.0, 33.3, 46.0}) {
            editor->getProperties().set(vocalstackAnimationTimeOverrideProperty, phase);
            toneUiTests::screenshot(*editor, "approval-1910-stack-" + juce::String(theme)
                + "-phase-" + juce::String(phase, 1) + ".png");
        }
        for (const auto* id : {"level", "double", "human", "width", "formant", "outputGain"}) {
            auto& knob = toneUiTests::slider(*editor, id);
            auto* valueLabel = toneUiTests::find<juce::Label>(knob, [](auto&) { return true; });
            require(valueLabel != nullptr, "Knob readout missing");
            require(valueLabel->getFont().getTypefacePtr() == pixelTypeface(),
                    "Main knob readout is not the embedded Pixelify typeface");
            require(valueLabel->getFont().getHeight() >= 14.f, "Main knob readout is too small");
        }
        require(identical(first, capture(0.0)), "Stack freeze did not restore deterministic pixels");
        // Compare actual empty card pixels to the explicit coarse renderer, not
        // just the helper: catches a missing opt-in in any theme or plate.
        const auto palette = themePalette(static_cast<Theme>(theme));
        const auto base = theme == 0 ? juce::Colour(0xff252826) : palette.panel;
        for (const auto plate : {juce::Rectangle<int>(18, 76, 104, 214),
                                 juce::Rectangle<int>(132, 76, 306, 214),
                                 juce::Rectangle<int>(448, 76, 104, 214),
                                 juce::Rectangle<int>(562, 76, 104, 214)}) {
            FluidBackground coarse, ordinary;
            juce::Image expected(juce::Image::RGB, plate.getWidth(), plate.getHeight(), true);
            { juce::Graphics g(expected); coarse.paint(g, expected.getBounds(), base, palette.accent, 0, 1.f, 24); }
            const auto fine = render(ordinary, plate.getWidth(), plate.getHeight(), base, palette.accent, 0);
            int differences = 0, transitions = 0;
            for (int y = 261; y < 267; ++y) for (int x = plate.getX() + 12; x < plate.getX() + 30; ++x) {
                const auto pixel = expected.getPixelAt(x - plate.getX(), y - plate.getY());
                if (first.getPixelAt(x, y) != pixel)
                                    throw std::runtime_error(("Card pixel mismatch theme=" + juce::String(theme) + " x=" + juce::String(x) + " y=" + juce::String(y) + " actual=" + first.getPixelAt(x,y).toString() + " expected=" + pixel.toString()).toStdString());
                // Flat spans are intentional in the clustered material; inspect
                // grid diversity over the whole reference, not this tiny empty strip.
            }
            for (int y = 0; y < expected.getHeight(); ++y) for (int x = 1; x < expected.getWidth(); ++x) {
                            differences += expected.getPixelAt(x, y) != fine.getPixelAt(x, y);
                            transitions += expected.getPixelAt(x, y) != expected.getPixelAt(x - 1, y);
                        }
                        require(differences > 0 && transitions > 0, "Card grid comparison lacks visible block boundaries");
        }
        // Empty strips inside all four control plates, away from labels/knobs.
        bool stripMotion = false;
                for (const auto x : {30, 150, 460, 580}) {
            const juce::Rectangle<int> strip(x, 250, 25, 8);
            stripMotion |= !identical(first.getClippedImage(strip), later.getClippedImage(strip));
        }
        require(stripMotion, "Top Stack control cards did not inherit animated material");
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
inline void backgroundAttenuation() {
    static_assert(FluidBackground::backgroundVariation == .8f);
    for (auto theme : {Theme::device, Theme::neon, Theme::spectral}) {
        const auto p = themePalette(theme);
        for (auto base : {p.background, p.panel, p.inset})
            for (int grid : {24, 80})
                for (double seconds : {0.0, 6.0, 33.3, 46.0}) {
                    FluidBackground background, original{FluidBackground::Material::control};
                    juce::Image actual(juce::Image::RGB, 240, 120, true), before(actual.createCopy());
                    { juce::Graphics g(actual); background.paint(g, actual.getBounds(), base, p.accent, seconds, 1.f, grid); }
                    { juce::Graphics g(before); original.paint(g, before.getBounds(), base, p.accent, seconds, 1.f, grid); }
                    int changed = 0;
                    for (int y = 0; y < 120; ++y) for (int x = 0; x < 240; ++x) {
                        const auto old = before.getPixelAt(x, y);
                        require(actual.getPixelAt(x, y) == base.interpolatedWith(old, .8f),
                                "Background field/glint variation is not 0.8 of original (8-bit interpolation)");
                        changed += actual.getPixelAt(x, y) != old;
                    }
                    require(changed > 0, "Background attenuation had no effect");
                }
        juce::TextButton button;
        button.getProperties().set(vocalstackAnimationTimeOverrideProperty, 33.3);
        ButtonFluidBackground buttons;
        FluidBackground original{FluidBackground::Material::control};
        juce::Image actual(juce::Image::RGB, 240, 120, true), expected(actual.createCopy());
        { juce::Graphics g(actual); buttons.paint(g, button, actual.getBounds(), p.panel, p.accent); }
        { juce::Graphics g(expected); original.paint(g, expected.getBounds(), p.panel, p.accent, 33.3, .55f); }
        require(identical(actual, expected), "Button material was softened with backgrounds");
    }
}

inline void coherentClusters() {
    for (auto theme : {Theme::device, Theme::neon, Theme::spectral})
        for (double phase : {0.0, 12.3, 46.0}) {
            const auto p = themePalette(theme);
            FluidBackground background, control{FluidBackground::Material::control};
            const auto image = render(background, 80, 80, p.panel, p.accent, phase);
            juce::Image quiet(juce::Image::RGB, 80, 80, true);
            { juce::Graphics g(quiet); control.paint(g, quiet.getBounds(), p.panel, p.accent, phase, .35f); }
            double neighbours = 0, distant = 0, bgDeviation = 0, controlDeviation = 0;
            int equalNeighbours = 0;
            const auto distance = [](juce::Colour a, juce::Colour b) {
                return std::abs(int(a.getRed()) - int(b.getRed()))
                     + std::abs(int(a.getGreen()) - int(b.getGreen()))
                     + std::abs(int(a.getBlue()) - int(b.getBlue()));
            };
            for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x) {
                const auto pixel = image.getPixelAt(x, y);
                neighbours += distance(pixel, image.getPixelAt(x + 1, y));
                distant += distance(pixel, image.getPixelAt(x + 16, y + 16));
                equalNeighbours += pixel == image.getPixelAt(x + 1, y);
                bgDeviation += distance(pixel, p.panel);
                controlDeviation += distance(quiet.getPixelAt(x, y), p.panel);
            }
            require(neighbours < distant * .35, "Material lacks coherent spatial clusters");
            require(equalNeighbours > 4096 * .65, "Quantized patches fragmented into grain");
            require(bgDeviation / (4096 * 3) < 7, "Material contrast is not muted");
            require(controlDeviation < bgDeviation, "Controls are not subtler than backgrounds");
        }
}

inline void ordinaryMaterialFingerprints() {
    // New glint strength is deliberately different; pin determinism independently
    // of cache history, while the tests above bound density and coherent geology.
    for (auto theme : {Theme::device, Theme::neon, Theme::spectral}) {
        const auto p = themePalette(theme);
        for (auto material : {FluidBackground::Material::background, FluidBackground::Material::control}) {
            FluidBackground surface{material};
            juce::Image image(juce::Image::RGB, 160, 80, true);
            { juce::Graphics g(image); surface.paint(g, image.getBounds(), p.panel, p.accent, 33.3, .55f); }
            std::uint64_t hash = 14695981039346656037ull;
            for (int y = 0; y < image.getHeight(); ++y) for (int x = 0; x < image.getWidth(); ++x) {
                const auto pixel = image.getPixelAt(x, y);
                for (auto byte : {pixel.getRed(), pixel.getGreen(), pixel.getBlue()}) {
                    hash ^= byte; hash *= 1099511628211ull;
                }
            }
            FluidBackground other{material};
            juce::Image reference(juce::Image::RGB, 160, 80, true);
            { juce::Graphics g(reference); other.paint(g, reference.getBounds(), p.panel, p.accent, 33.3, .55f); }
            require(hash != 0 && identical(image, reference), "Material cache changed deterministic ore field");
        }
    }
}
inline void run() {
    ordinaryMaterialFingerprints();
    coherentClusters();
    backgroundAttenuation();
    PixelLookAndFeel pixelStyle;
    const auto face = pixelStyle.getTypefaceForFont(juce::Font(juce::FontOptions(13.f)));
    require(face != nullptr && face->getName().containsIgnoreCase("Pixelify"),
            "Embedded licensed pixel font was not loaded");
    FluidBackground quiet, normal;
    const auto palette = themePalette(Theme::device);
    const auto controls = render(quiet, 320, 200, palette.background, palette.accent, 12.0);
    juce::Image calmer(juce::Image::RGB, 320, 200, true);
    { juce::Graphics g(calmer); normal.paint(g, calmer.getBounds(), palette.background, palette.accent, 12.0, .45f); }
    require(!identical(controls, calmer), "Control texture contrast did not change");
    deterministicTimeAndMotion();
    subduedNonUniformPixels();
    themeTintStaysMuted();
    proportionalCoarsePixels();
    inheritedTimeAndOverrides();
    // At this exact 2:1 size the card opt-in must produce 10x10 pixels,
    // while switching the same cache back to default restores 3x3 pixels.
    FluidBackground scoped;
    const auto defaultBefore = render(scoped, 240, 120, palette.panel, palette.accent, 0);
    juce::Image chunky(juce::Image::RGB, 240, 120, true);
    { juce::Graphics g(chunky); scoped.paint(g, chunky.getBounds(), palette.panel, palette.accent, 0, 1.f, 24); }
    int boundaries = 0;
    for (int y = 0; y < 120; ++y) for (int x = 0; x < 240; ++x) {
        require(chunky.getPixelAt(x, y) == chunky.getPixelAt(x / 10 * 10, y / 10 * 10),
                "Card texel is not a solid nearest-neighbour 10x10 block");
        if (x > 0 && chunky.getPixelAt(x, y) != chunky.getPixelAt(x - 1, y)) {
            ++boundaries;
            require(x % 10 == 0, "Card pixel boundary missed the coarse grid");
        }
    }
    require(boundaries > 0, "Card pixel blocks are not visibly distinct");
    require(identical(defaultBefore, render(scoped, 240, 120, palette.panel, palette.accent, 0)),
            "Card opt-in leaked into the default surface grid/cache");
    stackSurfaces();
}

} // namespace vocalstack::fluidBackgroundTests
