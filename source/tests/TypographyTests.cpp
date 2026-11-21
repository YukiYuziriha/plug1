#include "VisualTheme.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <type_traits>

namespace {
using namespace vocalstack;
// These overloads and accessors are present in the pinned JUCE 8.0.12 headers.
static_assert(std::is_default_constructible_v<juce::FontOptions>);
static_assert(std::is_constructible_v<juce::FontOptions, float>);
static_assert(std::is_constructible_v<juce::FontOptions, float, int>);
static_assert(std::is_constructible_v<juce::FontOptions, const juce::String&, float, int>);
static_assert(std::is_copy_constructible_v<juce::FontOptions>);
static_assert(std::is_constructible_v<juce::Font, juce::FontOptions>);
static_assert(std::is_same_v<decltype(juce::FontOptions().getHeight()), float>);
static_assert(std::is_same_v<decltype(juce::FontOptions().getPointHeight()), float>);
static_assert(std::is_same_v<decltype(juce::FontOptions().getStyle()), juce::String>);
void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
void near(float actual, float expected, const char* message) {
    check(std::isfinite(actual) && std::abs(actual - expected) < .001f, message);
}
void heightPolicy() {
    for (const float baseline : {9.f, 9.5f, 10.f, 10.5f, 11.f, 11.5f, 12.f, 12.5f, 13.f, 14.f, 18.f, 24.f}) {
        const auto system = readableFontOptions(baseline);
        const auto pixel = pixelFontOptions(baseline);
        near(system.getHeight(), baseline + 2.f, "System font options must add exactly two, not clamp");
        near(pixel.getHeight(), baseline + 2.f, "Pixel font options must add exactly two, not clamp");
        near(juce::Font(system).getHeight(), baseline + 2.f, "Actual system font height did not increase by two");
        near(juce::Font(pixel).getHeight(), baseline + 2.f, "Actual pixel font height did not increase by two");
        check(pixel.getTypeface() == pixelTypeface(), "Embedded pixel face was lost");
        check(system.getTypeface() == nullptr, "System-font path unexpectedly acquired the pixel face");
        near(pixel.getHorizontalScale(), 1.f, "Readability must not be undone by horizontal squeezing");
        // Repeated construction during layout/theme updates must not accumulate size.
        near(juce::Font(pixelFontOptions(baseline)).getHeight(), baseline + 2.f, "Repeated construction changed height");
    }
    near(juce::Font(pixelFontOptions(12.5f)).getHeight()
        - juce::Font(pixelFontOptions(9.5f)).getHeight(), 3.f, "Font hierarchy was flattened");
    near(readableFontOptions().getHeight(), juce::FontOptions().getHeight() + 2.f, "Default options missed the policy");
}
void overloads() {
    const auto baseline = juce::FontOptions(11.f, juce::Font::bold).withKerningFactor(.05f);
    const auto enlarged = readableFontOptions(baseline);
    near(enlarged.getHeight(), 13.f, "Existing FontOptions overload lost the increase");
    near(enlarged.getKerningFactor(), baseline.getKerningFactor(), "Readability changed kerning");
    check(enlarged.getStyle() == baseline.getStyle(), "Readability changed system font style");
    const auto family = juce::Font::getDefaultMonospacedFontName();
    const auto named = readableFontOptions(family, 10.f, juce::Font::plain);
    near(named.getHeight(), 12.f, "Named-font overload missed the policy");
    check(named.getName() == family, "System font family was replaced");
    near(pixelFontOptions(family, 10.f, juce::Font::plain).getHeight(), 12.f,
         "Modulation's named pixel-font path missed the policy");
    near(pixelFontOptions(12.f, juce::Font::bold).getHeight(), 14.f,
         "Styled pixel-font path missed the policy");
    for (int flags : {int(juce::Font::plain), int(juce::Font::bold), int(juce::Font::italic),
                      juce::Font::bold | juce::Font::italic | juce::Font::underlined}) {
        const auto source = juce::FontOptions(family, 12.f, flags).withKerningFactor(.05f);
        const auto pixel = pixelFontOptions(source);
        check(pixel.getTypeface() == pixelTypeface(), "Styled/named pixel options lost the embedded face");
        check(pixel.getName() == pixelTypeface()->getName() && pixel.getStyle() == pixelTypeface()->getStyle(),
              "Direct typeface assignment changed its previously authoritative face/style");
        check(pixel.getUnderline() == source.getUnderline(), "Direct typeface assignment lost underline");
        near(pixel.getKerningFactor(), source.getKerningFactor(), "Direct typeface assignment changed kerning");
        near(juce::Font(pixel).getHeight(), 14.f, "Styled/named pixel options missed exact +2 height");
    }
    const auto points = readableFontOptions(juce::FontOptions().withPointHeight(10.f));
    near(points.getPointHeight(), 12.f, "Explicit point-height options missed the policy");
    near(points.getHeight(), -1.f, "Point-height options were converted to JUCE height units");
}
void fits(const juce::Font& font, const juce::String& text, float width, float height) {
    check(juce::GlyphArrangement::getStringWidth(font, text) <= width, "Enlarged technical text overflows its reserved width");
    check(font.getAscent() + font.getDescent() <= height + .001f, "Enlarged technical text overflows its reserved height");
}
void reservedLanes() {
    const juce::Font value(pixelFontOptions(12.5f));
    fits(value, "100.0 %", 80, 21);
    fits(value, "100.00 st", 88, 25);
    const juce::Font plot(pixelFontOptions(12.f));
    fits(plot, "dBFS / RMS", 106, 15);
    fits(plot, "GR dB", 48, 15);
    fits(plot, "Crv", 30.5f, 15);
    fits(plot, "-72", 31, 14);
    const juce::Font meter(readableFontOptions(11.f, juce::Font::bold));
    for (const auto* text : {"IN / STEREO", "OUT / STEREO", "OUT / MONO", "MAX -160.0", "MAX +6.0"})
        fits(meter, text, 96, 18);
    fits(juce::Font(readableFontOptions(9.5f, juce::Font::bold)), "SAMPLE", 44, 16);
    fits(juce::Font(readableFontOptions(9.f)), "36", 24, 12);
}
}

int main() {
    // Font/metrics only: no plugin, Components, native windows or screenshots.
    juce::ScopedJuceInitialiser_GUI initialise;
    try {
        heightPolicy();
        overloads();
        reservedLanes();
        std::cout << "Typography: exact +2 font heights and reserved text lanes passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Typography: " << error.what() << '\n';
        return 1;
    }
}
