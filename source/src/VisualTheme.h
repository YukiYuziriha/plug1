#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <BinaryData.h>
#include "FluidBackground.h"

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
// Stable logical-pixel metrics, rounded once from the pre-preview baselines.
// Never scale the current viewport width during layout or theme changes.
namespace scrollbarMetrics {
constexpr int scaled(int baseline) noexcept { return (baseline * 18 + 5) / 10; }
constexpr int standard = scaled(8), voiceStrip = scaled(12);
}
class PixelLookAndFeel : public juce::LookAndFeel_V4 {
public:
    int getDefaultScrollbarWidth() override { return scrollbarMetrics::standard; }
    void setDecorativePhase(double seconds) noexcept { decorativePhase = seconds; }
    juce::Typeface::Ptr getTypefaceForFont(const juce::Font&) override { return pixelTypeface(); }
    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& background,
                              bool hover, bool down) override {
        auto colour = background.withMultipliedSaturation(button.hasKeyboardFocus(true) ? 1.3f : 0.9f)
                                .withMultipliedAlpha(button.isEnabled() ? 1.f : 0.5f);
        if (hover || down) colour = colour.contrasting(down ? 0.2f : 0.05f);
        const auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
        { juce::Graphics::ScopedSaveState state(g);
                  g.setOpacity(colour.getFloatAlpha());
                  controlMaterials.paint(g, button, bounds.toNearestInt(), colour.withAlpha(1.f),
                                         button.findColour(juce::TextButton::buttonOnColourId)); }
        g.setColour(button.findColour(juce::ComboBox::outlineColourId)); g.drawRect(bounds, 1.f);
    }
    void drawPopupMenuBackgroundWithOptions(juce::Graphics&, int, int, const juce::PopupMenu::Options&) override;
    void drawPopupMenuItemWithOptions(juce::Graphics&, const juce::Rectangle<int>&, bool,
                                      const juce::PopupMenu::Item&, const juce::PopupMenu::Options&) override;
    void drawPopupMenuSectionHeaderWithOptions(juce::Graphics&, const juce::Rectangle<int>&,
                                               const juce::String&, const juce::PopupMenu::Options&) override;
    juce::Font getPopupMenuFont() override { return juce::Font(pixelFontOptions(14.f)); }
    void drawPresetSelector(juce::Graphics&, juce::Rectangle<int>, juce::ComboBox&);
    void drawComboBoxTextWhenNothingSelected(juce::Graphics& g, juce::ComboBox& box, juce::Label& label) override {

        g.setColour(box.findColour(juce::ComboBox::textColourId).withAlpha(.75f));
        g.setFont(label.getFont());
        g.drawFittedText(box.getTextWhenNothingSelected(), label.getBounds().reduced(2, 0),
                         label.getJustificationType(), 1);
    }
    void drawComboBox(juce::Graphics& g, int width, int height, bool,
                      int, int, int, int, juce::ComboBox& box) override {
        const juce::Rectangle<float> bounds(0.f, 0.f, float(width), float(height));
        if (bool(box.getProperties()["vocalstackPresetSelector"])) drawPresetSelector(g, bounds.toNearestInt(), box);
        else controlMaterials.paint(g, box, bounds.toNearestInt(),
                    box.findColour(juce::ComboBox::backgroundColourId), box.findColour(juce::ComboBox::arrowColourId));
        g.setColour(box.findColour(juce::ComboBox::outlineColourId)); g.drawRect(bounds.reduced(0.5f), 1.f);
        juce::Path arrow;
        arrow.startNewSubPath(float(width - 27), float(height) * 0.5f - 2.f);
        arrow.lineTo(float(width - 20), float(height) * 0.5f + 3.f);
        arrow.lineTo(float(width - 13), float(height) * 0.5f - 2.f);
        g.setColour(box.findColour(juce::ComboBox::arrowColourId).withAlpha(box.isEnabled() ? 0.9f : 0.2f));
        g.strokePath(arrow, juce::PathStrokeType(2.f));
    }
    void drawScrollbar(juce::Graphics& g, juce::ScrollBar& bar, int x, int y, int width, int height,
                       bool vertical, int start, int size, bool hover, bool) override {
        const juce::Rectangle<int> thumb = vertical ? juce::Rectangle<int>(x, start, width, size)
                                                   : juce::Rectangle<int>(start, y, size, height);
        auto colour = bar.findColour(juce::ScrollBar::thumbColourId);
        const auto trough = bar.findColour(juce::ScrollBar::backgroundColourId);
        if (!trough.isTransparent()) {
            juce::Graphics::ScopedSaveState state(g); g.setOpacity(trough.getFloatAlpha());
            scrollTracks.paint(g, bar, {x, y, width, height}, trough.withAlpha(1.f), colour);
        }
        juce::Graphics::ScopedSaveState state(g); g.setOpacity(colour.getFloatAlpha());
        colour = colour.withAlpha(1.f);
        controlMaterials.paint(g, bar, thumb.reduced(1),
                    hover ? colour.brighter(0.25f) : colour, colour);
    }
    void drawTooltip(juce::Graphics& g, const juce::String& text, int width, int height) override {
        tooltipMaterial.paint(g, {0, 0, width, height}, findColour(juce::TooltipWindow::backgroundColourId),
                                      findColour(juce::TooltipWindow::textColourId), decorativePhase, .35f);
                g.setColour(findColour(juce::TooltipWindow::outlineColourId)); g.drawRect(0, 0, width, height);
                g.setColour(findColour(juce::TooltipWindow::textColourId));
                g.setFont(juce::Font(pixelFontOptions(13.f)));
                g.drawFittedText(text, juce::Rectangle<int>(width, height).reduced(7, 4), juce::Justification::centredLeft, 20);
    }
    void drawBubble(juce::Graphics& g, juce::BubbleComponent& component,
                    const juce::Point<float>& tip, const juce::Rectangle<float>& body) override {
        juce::Path path;
        path.addBubble(body.reduced(0.5f), body.getUnion({tip.x, tip.y, 1.f, 1.f}), tip, 0.f,
                       juce::jmin(15.f, body.getWidth() * 0.2f, body.getHeight() * 0.2f));
        g.setColour(component.findColour(juce::BubbleComponent::backgroundColourId)); g.fillPath(path);
        { juce::Graphics::ScopedSaveState state(g); g.reduceClipRegion(path);
                  controlMaterials.paint(g, component, body.toNearestInt(),
                      component.findColour(juce::BubbleComponent::backgroundColourId),
                      component.findColour(juce::BubbleComponent::outlineColourId)); }
        g.setColour(component.findColour(juce::BubbleComponent::outlineColourId));
        g.strokePath(path, juce::PathStrokeType(1.f));
    }
    void fillTextEditorBackground(juce::Graphics& g, int width, int height, juce::TextEditor& editor) override {
        if (editor.findColour(juce::TextEditor::backgroundColourId).isTransparent()) return;
                juce::Graphics::ScopedSaveState state(g);
                g.setOpacity(editor.findColour(juce::TextEditor::backgroundColourId).getFloatAlpha());
                controlMaterials.paint(g, editor, {0, 0, width, height},
            editor.findColour(juce::TextEditor::backgroundColourId), editor.findColour(juce::TextEditor::highlightColourId));
    }
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool, bool) override;
private:
    double decorativePhase = 0;
    ButtonFluidBackground controlMaterials, scrollTracks;
    FluidBackground selectorMaterial, menuMaterial;
    FluidBackground tooltipMaterial{FluidBackground::Material::control};
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

inline ThemePalette popupPalette(const juce::PopupMenu::Options& options) {
    if (auto* target = options.getTargetComponent()) return themePalette(*target);
    return themePalette(Theme::device);
}
inline void PixelLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button, bool hover, bool down) {
    const auto p = themePalette(button);
    const auto area = button.getLocalBounds().reduced(1);
    controlMaterials.paint(g, button, area, down ? p.panel : p.inset, p.accent);
    g.setColour(hover ? p.accent : p.border); g.drawRect(area);
    auto mark = area.withSizeKeepingCentre(12, 12).withX(area.getX() + 5);
    g.setColour(button.isEnabled() ? p.text : p.muted.withAlpha(.5f));
    g.drawRect(mark);
    if (button.getToggleState()) g.fillRect(mark.reduced(3));
    g.setFont(juce::Font(pixelFontOptions(13.f)));
    g.drawFittedText(button.getButtonText(), area.withTrimmedLeft(24), juce::Justification::centredLeft, 1);
}
inline void PixelLookAndFeel::drawPresetSelector(juce::Graphics& g, juce::Rectangle<int> bounds, juce::ComboBox& box) {
    const auto p = themePalette(box);
    selectorMaterial.paint(g, bounds, p.inset, p.accent, fluidAnimationSeconds(box), .65f);
}
inline void PixelLookAndFeel::drawPopupMenuBackgroundWithOptions(juce::Graphics& g, int width, int height,
                                                                                const juce::PopupMenu::Options& options) {
    const auto p = popupPalette(options);
    // Popups are separate peers: resolve their originating component, not global defaults.
    menuMaterial.paint(g, {0, 0, width, height}, p.panel, p.accent,
            options.getTargetComponent() ? fluidAnimationSeconds(*options.getTargetComponent()) : decorativePhase, .65f);
    g.setColour(p.border); g.drawRect(0, 0, width, height);
}
inline void PixelLookAndFeel::drawPopupMenuItemWithOptions(juce::Graphics& g, const juce::Rectangle<int>& area,
    bool highlighted, const juce::PopupMenu::Item& item, const juce::PopupMenu::Options& options) {
    const auto p = popupPalette(options);
    if (item.isSeparator) {
        g.setColour(p.border); g.fillRect(area.reduced(8, 0).withSizeKeepingCentre(juce::jmax(1, area.getWidth() - 16), 1));
        return;
    }
    if (highlighted && item.isEnabled) {
        g.setColour(p.accent.withAlpha(.18f)); g.fillRect(area.reduced(2, 0));
        g.setColour(p.accent); g.drawRect(area.reduced(2, 0));
    }
    g.setColour(item.isEnabled ? p.text : p.muted.withAlpha(.55f));
    g.setFont(getPopupMenuFont());
    auto text = area.reduced(12, 0);
    auto mark = text.removeFromLeft(18);
    if (item.isTicked) g.drawText("+", mark, juce::Justification::centred);
    if (item.image) item.image->drawWithin(g, mark.toFloat().reduced(2), juce::RectanglePlacement::centred, 1.f);
    if (item.subMenu) g.drawText(">", text.removeFromRight(16), juce::Justification::centred);
    if (item.shortcutKeyDescription.isNotEmpty())
        g.drawText(item.shortcutKeyDescription, text.removeFromRight(text.getWidth() / 3), juce::Justification::centredRight);
    g.drawFittedText(item.text, text, juce::Justification::centredLeft, 1);
}
inline void PixelLookAndFeel::drawPopupMenuSectionHeaderWithOptions(juce::Graphics& g, const juce::Rectangle<int>& area,
    const juce::String& text, const juce::PopupMenu::Options& options) {
    g.setColour(popupPalette(options).muted); g.setFont(getPopupMenuFont().boldened());
    g.drawFittedText(text, area.reduced(12, 0), juce::Justification::centredLeft, 1);
}

} // namespace vocalstack
