#pragma once
#include "NodeGalleryTests.h"

namespace vocalstack::typographyMotionTests {
namespace ui = toneUiTests;
inline void fits(const juce::String& text, float width, float height, float size = 12.f) {
    const juce::Font font(pixelFontOptions(size));
    ui::require(juce::GlyphArrangement::getStringWidth(font, text) <= width,
                "Technical label overflow: " + text + " / width " + juce::String(width));
    ui::require(font.getAscent() + font.getDescent() <= height, "Technical label baseline clipped: " + text);
}
inline void layout(bool capture) {
    // Offscreen layout/pixel checks; only the Motion capture below has a native peer.
    for (int theme = 0; theme < 3; ++theme) {
        auto owner = std::make_unique<Processor>(); owner->setSkin(theme);
        PixelLookAndFeel style;
        for (int width : {190, 220, 320, 480, 840, 1200}) {
            SpectralMorphPanel morph(*owner, 1); morph.setLookAndFeel(&style);
            morph.getProperties().set(vocalstackThemeProperty, theme); morph.setNeon(theme == 1);
            morph.setInspectorLayout(width < 480); morph.setSize(width, 420);
            const auto plot = morph.spectrumBounds();
            ui::require(morph.getLocalBounds().contains(plot.withTrimmedTop(-18).withTrimmedBottom(-32)), "Morph labels escaped reserved lanes");
            for (double rate : {0., 44100., 48000., 96000., 192000.}) {
                const auto axes = SpectralMorphPanel::axisLabels(plot.getWidth(), rate);
                fits(axes.first, float(plot.getWidth()), 16); fits(axes.second, float(plot.getWidth()), 16);
            }
            const float cell = float(std::min(145, plot.getWidth() / 3));
            for (const auto* text : {"MAIN", "DONOR", "RESULT"}) fits(text, cell, 16);
            if (capture) ui::screenshot(morph, "typography-morph-" + updateUiTests::themeName(theme) + "-" + juce::String(width) + ".png");
            morph.setLookAndFeel(nullptr);
        }
        for (int width : {158, 174, 190, 220, 360, 700}) {
            const juce::Rectangle<float> bounds(0, 0, float(width), 190);
            const auto plot = CompressorPlot::plotBounds(bounds);
            for (const auto* text : {"In", "Wet", "Curve", "GR"}) fits(text, width / 4.f - 9, 13);
            fits(width < 290 ? "dBFS / RMS" : "Output dBFS / RMS", float(width - 105), 13);
            fits("GR dB", 45, 13);
            fits(plot.getWidth() < 180 ? "-72  In dBFS" : "-72     Input dBFS", plot.getWidth() - 9, 13);
            fits("-4 s  History", plot.getWidth() - juce::GlyphArrangement::getStringWidth(juce::Font(pixelFontOptions(12.f)), "now") - 3, 13);
            for (const auto* text : {"-72", "-36", "0", "18", "36"}) fits(text, 25, 12);
        }
        fits("Bypassed", 80, 15);
        fits("FINAL GAIN", 88, 23); fits("TRUE MIX", 124, 23);
    }
}
inline void capture() {
    layout(true);
    guiTestDisplay::enumerate();
    // Matched 10 s timelines. This samples the production clock, not wall-clock
    // rendering performance: playback is 25 fps / 1x, never accelerated.
    for (int theme : {1, 0, 2}) {
        for (int percent : {0, 3, 100}) {
            if (theme != 1 && percent != 3) continue;
            auto owner = std::make_unique<Processor>(); owner->setSkin(theme);
            std::unique_ptr<juce::AudioProcessorEditor> editor(owner->createEditor());
            editor->setSize(960, 1000); guiTestDisplay::attach(*editor); ui::action(*editor, "Stack");
            auto& slider = ui::slider(*editor, "decorativeMotion");
            slider.setValue(percent, juce::sendNotificationSync);
            ui::near(float(owner->getMotionPercent()), float(percent), "Capture must use actual Motion callback");
            auto* viewport = ui::find<juce::Viewport>(*editor, [](auto& v) { return v.getComponentID() == "stackViewport"; });
            ui::require(viewport && viewport->getViewedComponent(), "Production Stack canvas missing");
            auto& canvas = *viewport->getViewedComponent();
            DecorativeMotion clock; clock.advance(0, true, owner->getMotionPercent());
            juce::Image baseline, last;
            const auto stem = "motion-" + updateUiTests::themeName(theme) + "-" + juce::String(percent);
            const int frames = theme == 1 ? 250 : 1;
            for (int frame = 0; frame < frames; ++frame) {
                clock.advance(frame / 25., true, owner->getMotionPercent());
                editor->getProperties().set(vocalstackAnimationTimeOverrideProperty, clock.seconds());
                ui::require(fluidAnimationSeconds(slider) == clock.seconds(), "Motion phase not inherited by controls");
                const auto image = ui::render(canvas);
                if (frame == 0) baseline = image;
                if (percent == 0) ui::require(updateUiTests::samePixels(baseline, image), "Motion 0 changed decorative pixels in silence");
                updateUiTests::saveImage(image, stem + "-" + juce::String(frame).paddedLeft('0', 3) + ".png");
                last = image;
            }
            if (percent > 0 && frames > 1) ui::require(!updateUiTests::samePixels(baseline, last), "Motion showcase did not move");
            std::cout << "MOTION " << stem << " frames=" << frames << " fps=25 seconds=10 phase=" << clock.seconds()
                      << " canvas=" << canvas.getWidth() << "x" << canvas.getHeight() << " silence; offscreen snapshots\n";
        }
    }
}
inline void run() { layout(false); }
}
