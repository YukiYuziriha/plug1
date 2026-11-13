#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <BinaryData.h>

namespace vocalstack {

// Embedded, unmodified Pixelify Sans; SIL OFL 1.1 in ThirdParty.
inline juce::Typeface::Ptr pixelTypeface() {
    static const auto face = juce::Typeface::createSystemTypefaceFor(
        BinaryData::PixelifySans_ttf, BinaryData::PixelifySans_ttfSize);
    return face;
}
template <typename... Args> inline juce::FontOptions pixelFontOptions(Args&&... args) {
    return juce::FontOptions(std::forward<Args>(args)...).withTypeface(pixelTypeface());
}
class PixelLookAndFeel : public juce::LookAndFeel_V4 {
public:
    juce::Typeface::Ptr getTypefaceForFont(const juce::Font&) override { return pixelTypeface(); }
};

enum class Theme { device = 0, neon = 1, spectral = 2 };
inline const juce::Identifier vocalstackThemeProperty{"vocalstackTheme"};

struct ThemePalette {
    juce::Colour background, panel, inset, border, text, muted, accent, secondary;
};

inline Theme themeFromInt(int value) noexcept {
    return static_cast<Theme>(juce::jlimit(0, 2, value));
}

// The editor publishes before calling legacy setNeon/setFlash setters. Descendants
// (including controls with a private LookAndFeel) inherit without copying state.
inline Theme componentTheme(const juce::Component& component, bool legacyNeon = false) {
    for (auto* current = &component; current != nullptr; current = current->getParentComponent())
        if (current->getProperties().contains(vocalstackThemeProperty))
            return themeFromInt(int(current->getProperties()[vocalstackThemeProperty]));
    return legacyNeon ? Theme::neon : Theme::device;
}

inline ThemePalette themePalette(Theme theme) {
    if (theme == Theme::spectral)
        return {juce::Colour(0xff191a1e), juce::Colour(0xff24252b), juce::Colour(0xff1e1f24),
                juce::Colour(0xff383a43), juce::Colour(0xffe3e4ec), juce::Colour(0xff9b9daa),
                juce::Colour(0xffa390ed), juce::Colour(0xff63caba)};
    if (theme == Theme::neon)
        return {juce::Colour(0xff090d17), juce::Colour(0xff111827), juce::Colour(0xff0c1220),
                juce::Colour(0xff2b3a53), juce::Colour(0xfff1f5ff), juce::Colour(0xff9eafc8),
                juce::Colour(0xffff4ca0), juce::Colour(0xff64f5e3)};
    return {juce::Colour(0xff292b2a), juce::Colour(0xff252826), juce::Colour(0xff171b18),
            juce::Colour(0xff50564f), juce::Colour(0xffe4e4df), juce::Colour(0xffaeb1a9),
            juce::Colour(0xfff5a24b), juce::Colour(0xff9ac37d)};
}

inline ThemePalette themePalette(const juce::Component& component, bool legacyNeon = false) {
    return themePalette(componentTheme(component, legacyNeon));
}

} // namespace vocalstack
