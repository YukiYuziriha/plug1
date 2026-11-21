#pragma once
#include "NodeGalleryTests.h"

namespace vocalstack::typographyMotionTests {
namespace ui = toneUiTests;
inline void fits(const juce::String& text, float width, float height, float size = 12.f,
                 int styleFlags = juce::Font::plain) {
    const juce::Font font(pixelFontOptions(size, styleFlags));
    ui::require(std::abs(font.getHeight() - (size + 2.f)) < .001f,
                "Technical label missed exact +2 height: " + text);
    ui::require(font.getHorizontalScale() == 1.f, "Technical label squeezed horizontally: " + text);
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
            const int cell = std::min(145, plot.getWidth() / 3);
            fits("MAIN", float(cell), 16); fits("DONOR", float(cell), 16);
            fits(plot.getWidth() < 320 ? "RESULT" : "RESULT / pre Mix", float(plot.getWidth() - 2 * cell), 16);
            const juce::Rectangle<int> legend(plot.getX(), plot.getY() - 17, plot.getWidth(), 16);
            const juce::Rectangle<int> levelAxis(plot.getX(), plot.getBottom(), plot.getWidth(), 16);
            const auto frequencyAxis = levelAxis.translated(0, 16);
            ui::require(morph.getLocalBounds().contains(legend) && morph.getLocalBounds().contains(frequencyAxis)
                && !legend.intersects(plot) && !levelAxis.intersects(plot) && !levelAxis.intersects(frequencyAxis),
                "Morph legend/axis lanes overlap the plot or each other");
            if (capture) ui::screenshot(morph, "typography-morph-" + updateUiTests::themeName(theme) + "-" + juce::String(width) + ".png");
            morph.setLookAndFeel(nullptr);
        }
        for (int width : {158, 174, 179, 180, 190, 220, 245, 246, 289, 290, 360, 700}) {
            const juce::Rectangle<float> bounds(7, 9, float(width), 190);
            const auto plot = CompressorPlot::plotBounds(bounds);
            ui::require(plot == bounds.withTrimmedTop(32).withTrimmedBottom(36).withTrimmedLeft(34).withTrimmedRight(32),
                        "Compressor plot no longer reserves its enlarged typography lanes");
            const auto laneFits = [&](const juce::String& text, juce::Rectangle<float> lane) {
                ui::require(bounds.contains(lane) && !lane.intersects(plot),
                            "Compressor text lane clips its frame or overlaps the plot: " + text);
                fits(text, lane.getWidth(), lane.getHeight());
            };
            const juce::Rectangle<float> heading(bounds.getX(), bounds.getY(), float(width - 52), 15);
            const juce::Rectangle<float> grHeading(bounds.getRight() - 48, bounds.getY(), 48, 15);
            ui::require(!heading.intersects(grHeading), "Compressor axis headings overlap");
            laneFits(width < 290 ? "dBFS / RMS" : "Output dBFS / RMS", heading);
            laneFits("GR dB", grHeading);
            const float cell = float(width) / 4.f;
            for (int i = 0; i < 4; ++i)
                laneFits(i == 0 ? "In" : i == 1 ? "Wet" : i == 2 ? (cell < 45 ? "Crv" : "Curve") : "GR",
                         {bounds.getX() + float(i) * cell + 9, bounds.getY() + 16, cell - 9, 15});
            for (int db : {-72, -36, 0}) {
                const float y = std::clamp(CompressorPlot::levelY(float(db), plot) - 7.f, plot.getY(), plot.getBottom() - 14.f);
                laneFits(juce::String(db), {bounds.getX(), y, 31, 14});
            }
            laneFits("0", {plot.getRight() + 3, plot.getY(), 29, 14});
            laneFits("18", {plot.getRight() + 3, plot.getCentreY() - 7, 29, 14});
            laneFits("36", {plot.getRight() + 3, plot.getBottom() - 14, 29, 14});
            const juce::Font font(pixelFontOptions(12.f));
            const float zeroWidth = juce::GlyphArrangement::getStringWidth(font, "0");
            const float nowWidth = juce::GlyphArrangement::getStringWidth(font, "now");
            const juce::Rectangle<float> inputAxis(plot.getX(), plot.getBottom() + 2, plot.getWidth(), 15);
            const juce::Rectangle<float> historyAxis(plot.getX(), plot.getBottom() + 19, plot.getWidth(), 15);
            ui::require(!inputAxis.intersects(historyAxis), "Compressor input/history axis rows overlap");
            const auto inputText = plot.getWidth() < 180 ? "-72 In dBFS" : "-72     Input dBFS";
            const auto historyText = plot.getWidth() < 180 ? "-4 s" : "-4 s  History";
            laneFits(inputText, inputAxis); laneFits("0", inputAxis);
            laneFits(historyText, historyAxis); laneFits("now", historyAxis);
            fits(inputText, plot.getWidth() - zeroWidth - 3, 15);
            fits(historyText, plot.getWidth() - nowWidth - 3, 15);
        }
        fits("Bypassed", 80, 15);
        const int headingStyle = theme == 2 ? juce::Font::plain : juce::Font::bold;
        fits("FINAL GAIN", 88, 23, 12.f, headingStyle);
        fits("TRUE MIX", 124, 23, 12.f, headingStyle);
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
