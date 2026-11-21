#pragma once

#include "ParameterKnob.h"
#include "ToneUiTests.h"
#include "DoublerPanel.h"
#include "CreativeEffectPanel.h"
#include "UtilityEffectPanel.h"
#include "ModulationPanel.h"
#include <set>
#include <vector>

namespace vocalstack::sliderRenderingTests {
inline bool identical(const juce::Image& a, const juce::Image& b) {
    for (int y = 0; y < a.getHeight(); ++y)
        for (int x = 0; x < a.getWidth(); ++x)
            if (a.getPixelAt(x, y) != b.getPixelAt(x, y)) return false;
    return true;
}
inline juce::Image render(ModulationSlider& slider, float scale = 1) {
    juce::Image image(juce::Image::ARGB, juce::roundToInt(slider.getWidth() * scale),
                      juce::roundToInt(slider.getHeight() * scale), true);
    juce::Graphics graphics(image);
    graphics.addTransform(juce::AffineTransform::scale(scale));
    slider.paintEntireComponent(graphics, true);
    return image;
}
inline void selectionAndFeedback() {
    auto owner = std::make_unique<Processor>();
    for (int theme : {0, 1, 2}) for (int width : {110, 340}) for (float scale : {.75f, 1.f, 1.5f}) {
        ParameterKnob::Style style;
        ModulationSlider slider;
        slider.setLookAndFeel(&style);
        slider.bindModulation(*owner, "fx1Mix");
        slider.getProperties().set(vocalstackThemeProperty, theme);
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        slider.setRange(0, 1); slider.setValue(.5, juce::dontSendNotification); slider.setSize(width, 36);
        ModulationState state; state.modulators.push_back({1});
        ModulationSnapshot snapshot;
        slider.refreshModulation(snapshot, state, 0);
        const auto baseline = render(slider, scale);
        slider.refreshModulation(snapshot, state, 1);
        toneUiTests::require(identical(baseline, render(slider, scale)),
            "Selecting an unassigned source added a duplicate slider rail");
        state.assignments.push_back({1, "fx1Mix", .2f});
        for (bool bipolar : {false, true}) for (float depth : {-.2f, .2f}) for (float amount : {.5f, 1.f}) {
            state.modulators[0].bipolar = bipolar;
            state.modulators[0].depth = amount;
            state.assignments[0].depth = depth;
            slider.refreshModulation(snapshot, state, 1);
            const auto assigned = render(slider, scale);
            toneUiTests::require(!identical(baseline, assigned), "Assigned slider depth feedback disappeared");
            // Every changed range pixel must stay on the primary rail, not six pixels above it.
            const auto bounds = style.getSliderLayout(slider).sliderBounds.toFloat();
            const float centreY = (bounds.getY() + bounds.getHeight() * .5f + 1) * scale;
            for (int y = 0; y < assigned.getHeight(); ++y) for (int x = 0; x < assigned.getWidth(); ++x)
                if (assigned.getPixelAt(x, y) != baseline.getPixelAt(x, y)) {
                    toneUiTests::require(std::abs(float(y) + .5f - centreY) <= 2 * scale + 1,
                        "Assigned slider feedback formed a second rail");
                    const float lo = .5f + (bipolar ? -std::abs(depth) : std::min(0.f, depth)) * amount;
                    const float hi = .5f + (bipolar ? std::abs(depth) : std::max(0.f, depth)) * amount;
                    const float left = (bounds.getX() + lo * bounds.getWidth()) * scale;
                    const float right = (bounds.getX() + hi * bounds.getWidth()) * scale;
                    toneUiTests::require(float(x) + .5f >= left - 2 * scale - 1
                        && float(x) + .5f <= right + 2 * scale + 1,
                        "Slider range ignored assignment polarity, sign or source amount");
                }
        }
        state.assignments[0].enabled = false;
        slider.refreshModulation(snapshot, state, 1);
        toneUiTests::require(identical(baseline, render(slider, scale)), "Disabled assignment retained its range");
        state.assignments[0].enabled = true; state.assignments[0].depth = 0;
        slider.refreshModulation(snapshot, state, 1);
        toneUiTests::require(identical(baseline, render(slider, scale)), "Zero depth added an eligibility rail");
        snapshot.targets.push_back({"fx1Mix", .5f, .8f, .3f});
        slider.refreshModulation(snapshot, state, 0);
        const auto effective = render(slider, scale);
        toneUiTests::require(!identical(baseline, effective), "Effective-value dot disappeared without source selection");
        snapshot.consistent = false; snapshot.targets.clear();
        slider.refreshModulation(snapshot, state, 0);
        toneUiTests::require(identical(effective, render(slider, scale)), "Inconsistent snapshot erased effective feedback");
        slider.setLookAndFeel(nullptr);
    }
}
inline void alignmentAndSensitivity() {
    auto owner = std::make_unique<Processor>();
    for (int width : {110, 190, 340}) for (double skew : {1., 2.}) {
        ParameterKnob::Style style;
        ModulationSlider slider;
        slider.setLookAndFeel(&style); slider.bindModulation(*owner, "fx1Mix");
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 58, 21);
        slider.setRange(0, 1); slider.setSkewFactor(skew); slider.setSize(width, 36);
        const auto bounds = style.getSliderLayout(slider).sliderBounds.toFloat();
        // Effective feedback uses the same x coordinate as JUCE's base thumb,
        // but a separate upper lane, including at the endpoints.
        for (float proportion : {0.f, .25f, .5f, .75f, 1.f}) {
            slider.setValue(slider.proportionOfLengthToValue(proportion), juce::dontSendNotification);
            ModulationSnapshot snapshot; ModulationState state;
            slider.refreshModulation(snapshot, state, 0);
            const auto base = render(slider);
            snapshot.targets.push_back({"fx1Mix", proportion, proportion, 0});
            slider.refreshModulation(snapshot, state, 0);
            const auto marked = render(slider);
            const float expectedX = bounds.getX() + proportion * bounds.getWidth();
            int changed = 0;
            for (int y = 0; y < marked.getHeight(); ++y) for (int x = 0; x < marked.getWidth(); ++x)
                if (marked.getPixelAt(x, y) != base.getPixelAt(x, y)) {
                    ++changed;
                    toneUiTests::require(std::abs(float(x) + .5f - expectedX) <= 3.5f,
                        "Effective marker and slider thumb use different tracks");
                }
            toneUiTests::require(changed > 0, "Aligned effective marker was not rendered");
                        const float railY = bounds.getCentreY() + 1;
                        for (int y = juce::jmax(0, int(std::floor(railY - 5))); y < juce::jmin(marked.getHeight(), int(std::ceil(railY + 5))); ++y)
                            for (int x = 0; x < marked.getWidth(); ++x)
                                toneUiTests::require(marked.getPixelAt(x, y) == base.getPixelAt(x, y),
                                    "Coincident effective feedback changed/split the base thumb");
        }
        slider.setValue(slider.proportionOfLengthToValue(.5), juce::dontSendNotification);
        const auto start = bounds.getCentre().translated(bounds.getWidth() * .2f, 0);
        const auto end = start.translated(bounds.getWidth() * .2f, 0);
        const int sensitivity = slider.getMouseDragSensitivity();
        const bool snaps = slider.getSliderSnapsToMousePosition();
        slider.mouseDown(toneUiTests::mouse(slider, start, start));
        toneUiTests::near(slider.valueToProportionOfLength(slider.getValue()), .5, "Relative click jumped");
        slider.mouseDrag(toneUiTests::mouse(slider, end, start, true));
        toneUiTests::near(slider.valueToProportionOfLength(slider.getValue()), .6, "Relative drag is not half speed");
        slider.mouseUp(toneUiTests::mouse(slider, end, start, true, 0));
        toneUiTests::require(slider.getMouseDragSensitivity() == sensitivity && slider.getSliderSnapsToMousePosition() == snaps,
            "Linear gesture leaked sensitivity/snap configuration");
        slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider.mouseDown(toneUiTests::mouse(slider, start, start));
        toneUiTests::require(slider.getMouseDragSensitivity() == sensitivity, "Rotary sensitivity was halved");
        slider.mouseUp(toneUiTests::mouse(slider, start, start, false, 0));
        slider.setLookAndFeel(nullptr);
    }
}
inline void pointerSeparation() {
    auto owner = std::make_unique<Processor>();
    for (int theme : {0, 1, 2}) for (float scale : {.75f, 1.f, 1.5f, 2.f}) {
        ParameterKnob knob(*owner, "voice1Pan", "Pan");
        knob.getProperties().set(vocalstackThemeProperty, theme);
        knob.setLinear(true); knob.setSize(79, 54); knob.setNeon(theme == 1);
        auto& slider = knob.slider;
        const auto bounds = slider.getLookAndFeel().getSliderLayout(slider).sliderBounds.toFloat();
        const float railY = bounds.getCentreY() + 1;
        ModulationState state;
        for (float base : {0.f, .25f, .5f, .75f, 1.f}) {
            slider.setValue(slider.proportionOfLengthToValue(base), juce::dontSendNotification);
            slider.refreshModulation({}, state, 0);
            const auto plain = render(slider, scale);
            for (float offset : {-.01f, 0.f, .01f, .2f}) {
                const float effective = juce::jlimit(0.f, 1.f, base + offset);
                ModulationSnapshot snapshot; snapshot.targets.push_back({"voice1Pan", base, effective, offset});
                slider.refreshModulation(snapshot, state, 0);
                const auto marked = render(slider, scale);
                int changed = 0;
                const float expectedX = (bounds.getX() + effective * bounds.getWidth()) * scale;
                for (int y = 0; y < marked.getHeight(); ++y) for (int x = 0; x < marked.getWidth(); ++x)
                    if (marked.getPixelAt(x, y) != plain.getPixelAt(x, y)) {
                        ++changed;
                        toneUiTests::require(float(y) + .5f <= (railY - 6) * scale + 1,
                            "Effective marker overlaps the Stack base pointer");
                        toneUiTests::require(std::abs(float(x) + .5f - expectedX) <= 3 * scale + 1,
                            "Effective marker is not on JUCE's value coordinate");
                    }
                toneUiTests::require(changed > 0, "Effective feedback clipped in a compact Stack control");
            }
        }
    }
}
inline void controlModes() {
    auto owner = std::make_unique<Processor>();
    const auto check = [](auto&& self, juce::Component& component, bool linear) -> int {
        int count = 0;
        if (auto* slider = dynamic_cast<juce::Slider*>(&component); slider && slider->isVisible()) {
            const bool horizontal = linear || slider->getComponentID() == "fx1Mix"
                            || slider->getComponentID() == utilityParameterId(1, size_t(UtilityParameter::gainDb));
            toneUiTests::require(slider->getSliderStyle() == (horizontal ? juce::Slider::LinearHorizontal
                : juce::Slider::RotaryHorizontalVerticalDrag), "Wrong parameter control mode: " + slider->getComponentID());
            toneUiTests::require(!slider->getWantsKeyboardFocus() && !slider->getMouseClickGrabsKeyboardFocus(),
                "Parameter control captured host keys");
            toneUiTests::require(slider->getWidth() > 0 && slider->getHeight() > 0, "Parameter control has empty bounds");
            ++count;
        }
        for (auto* child : component.getChildren()) count += self(self, *child, linear);
        return count;
    };
    for (bool compact : {false, true}) {
        VoiceControls routing(*owner, 0);
        routing.setSize(340, compact ? 500 : 390); routing.setCompact(compact);
        toneUiTests::require(check(check, routing, false) == 8, "Routing voice lost a knob");
        VoiceControls stack(*owner, 0, VoiceControls::ControlMode::stackSliders);
        stack.setSize(174, 278); stack.setCompact(compact);
        toneUiTests::require(check(check, stack, true) == 8, "Stack voice lost a slider");
    }
    const auto checkPanelLayout = [](juce::Component& panel) {
        auto& mix = toneUiTests::slider(panel, "fx1Mix");
        toneUiTests::require(mix.getSliderStyle() == juce::Slider::LinearHorizontal,
            "Dry/wet Mix must remain linear");
        const auto visit = [](auto&& self, juce::Component& component) -> void {
            if (auto* slider = dynamic_cast<juce::Slider*>(&component); slider && slider->isVisible()) {
                toneUiTests::require(slider->getTextBoxPosition() == juce::Slider::TextBoxAbove,
                    "FX numeric value must be above its control");
                const auto layout = slider->getLookAndFeel().getSliderLayout(*slider);
                const bool merged = bool(slider->getProperties()["vocalstackTexturedBar"]);
                const auto bar = ParameterKnob::Style::texturedBarBounds(layout.sliderBounds.toFloat()).toNearestInt();
                toneUiTests::require(!layout.textBoxBounds.isEmpty()
                        && (merged ? bar.contains(layout.textBoxBounds)
                                   : layout.textBoxBounds.getBottom() <= layout.sliderBounds.getY()),
                    "FX value must be inside Mix or above its rotary control");
                auto* row = slider->getParentComponent();
                auto* title = toneUiTests::find<juce::Label>(*row, [&](auto& label) {
                    return label.getParentComponent() == row && label.isVisible();
                });
                toneUiTests::require(merged ? title == nullptr : (title && title->getY() >= slider->getBottom()
                        && row->getLocalBounds().contains(title->getBounds())),
                    "Only rotary FX controls should have an external caption");
                auto* value = toneUiTests::find<juce::Label>(*slider, [](auto&) { return true; });
                toneUiTests::require(value && std::abs(value->getFont().getHeight() - (ParameterKnob::controlFontSize + 2.f)) < .01f
                        && (merged || std::abs(title->getFont().getHeight() - value->getFont().getHeight()) < .01f),
                    "FX text must use the consistent 14.5px height (12.5px baseline +2)");
            }
            if (auto* button = dynamic_cast<juce::Button*>(&component);
                button && button->isVisible() && button->getComponentID().startsWith("fx1")) {
                toneUiTests::require(button->getWidth() >= 24 && button->getWidth() <= 64
                        && button->getHeight() >= 24 && button->getHeight() <= 28
                        && button->getParentComponent()->getLocalBounds().contains(button->getBounds()),
                    "FX toggle must be compact, usable and unclipped");
            }
            for (auto* child : component.getChildren()) self(self, *child);
        };
        visit(visit, panel);
    };
    for (int width : {280, 560, 940}) {
        for (auto kind : {CreativeKind::delay, CreativeKind::reverb, CreativeKind::compressor,
                          CreativeKind::bitcrusher, CreativeKind::granular}) {
            CreativeEffectPanel creative(*owner, 1, kind);
            creative.setSize(width, 600);
            toneUiTests::require(check(check, creative, false) > 1, "Creative controls missing");
            checkPanelLayout(creative);
        }
        for (auto kind : {UtilityKind::gain, UtilityKind::stereo, UtilityKind::gate,
                          UtilityKind::xpFilter, UtilityKind::synthFilter}) {
            UtilityEffectPanel utility(*owner, 1, kind);
            utility.setSize(width, 600);
            toneUiTests::require(check(check, utility, false) > 1, "Utility controls missing");
            checkPanelLayout(utility);
        }
        ModulationPanel modulation(*owner);
        modulation.setSize(width, 600);
        toneUiTests::require(check(check, modulation, false) >= 6, "Modulation knobs missing");
        ToneEffectPanel eq(*owner, 1, NodeType::equalizer);
        eq.setSize(width, 600);
        toneUiTests::require(check(check, eq, false) > 0, "EQ controls missing");
    }
}
inline void texturedFxBars() {
    auto owner = std::make_unique<Processor>();
    for (int theme : {0, 1, 2}) for (int width : {190, 340, 800}) {
        ParameterKnob control(*owner, "fx1Mix", "Mix", "%");
        control.getProperties().set(vocalstackThemeProperty, theme);
        control.setLinear(true); control.setSize(width, 44); control.setNeon(theme == 1);
        auto& slider = control.slider;
        toneUiTests::require(bool(slider.getProperties()["vocalstackTexturedBar"]), "FX Mix did not opt into its textured bar");
        const auto layout = slider.getLookAndFeel().getSliderLayout(slider);
        const auto bar = ParameterKnob::Style::texturedBarBounds(layout.sliderBounds.toFloat()).toNearestInt().toFloat();
        toneUiTests::require(std::abs(bar.getHeight() - 25.f) < .01f
            && bar.getY() - 7 >= 0 && bar.toNearestInt().contains(layout.textBoxBounds),
            "25px FX bar must contain its caption and keep modulation above");
        slider.setValue(25, juce::dontSendNotification);
        const auto baseline = render(slider);
        toneUiTests::require(identical(baseline, render(slider)), "Procedural slider texture flickers between repaints");
        std::set<juce::uint32> shades;
        const auto sample = bar.toNearestInt().withTrimmedLeft(int(bar.getWidth() * .30f)).withTrimmedRight(int(bar.getWidth() * .55f)).withHeight(7);
        for (int y = sample.getY() + 2; y < sample.getBottom() - 2; ++y)
            for (int x = sample.getX(); x < sample.getRight(); ++x)
                            shades.insert(baseline.getPixelAt(x, y).getARGB());
        // The midpoint fill's shared overlap is only 15% of the track; a
        // narrow patch may be one mineral tone. Wider fixtures must still vary.
        toneUiTests::require(shades.size() >= (width >= 340 ? 2u : 1u) && shades.size() <= 6,
            "FX texture must have a small blocky palette, not a gradient or flat fill; theme=" + juce::String(theme) + " width=" + juce::String(width) + " tones=" + juce::String(int(shades.size())));
        slider.setValue(0, juce::dontSendNotification);
        const auto extended = render(slider);
        for (int y = sample.getY() + 2; y < sample.getBottom() - 2; ++y)
            for (int x = sample.getX(); x < sample.getRight(); ++x)
                toneUiTests::require(baseline.getPixelAt(x, y) == extended.getPixelAt(x, y),
                        "Texture swims/rescales when the value changes");
        slider.setValue(25, juce::dontSendNotification);
        control.getProperties().set(vocalstackAnimationTimeOverrideProperty, 4.0);
        const auto animated = render(slider);
        toneUiTests::require(!identical(baseline, animated), "FX slider material did not animate at an explicit later phase");
        toneUiTests::require(identical(animated, render(slider)), "FX texture changes within a fixed animation frame");
        for (int y = 0; y < animated.getHeight(); ++y) for (int x = 0; x < animated.getWidth(); ++x)
            if (animated.getPixelAt(x, y) != baseline.getPixelAt(x, y))
                toneUiTests::require(bar.contains(float(x) + .5f, float(y) + .5f),
                    "Texture animation changed the label, marker lane or outside geometry");
        control.getProperties().set(vocalstackAnimationTimeOverrideProperty, 0.0);
        toneUiTests::require(identical(baseline, render(slider)), "Returning to an explicit phase changed the material");
        for (float effective : {0.f, .5f, 1.f}) {
            ModulationSnapshot snapshot; snapshot.targets.push_back({"fx1Mix", .5f, effective, effective - .5f});
            ModulationState state;
            slider.refreshModulation(snapshot, state, 0);
            const auto marked = render(slider);
            int changed = 0;
            for (int y = 0; y < marked.getHeight(); ++y) for (int x = 0; x < marked.getWidth(); ++x)
                if (marked.getPixelAt(x, y) != baseline.getPixelAt(x, y)) {
                    ++changed;
                    toneUiTests::require(y < bar.getY(), "Effective marker splits the thick bar/base thumb");
                }
            toneUiTests::require(changed > 0, "Thick-bar effective marker is clipped at an endpoint");
        }
    }
    ParameterKnob voice(*owner, "voice1Pan", "Pan"); voice.setLinear(true);
    toneUiTests::require(!bool(voice.slider.getProperties()["vocalstackTexturedBar"]),
        "Compact voice slider was changed by FX styling");
}
inline void fatMaterialPixels() {
    for (auto theme : {Theme::device, Theme::neon, Theme::spectral}) {
        ParameterKnob::Style style;
        ModulationSlider slider;
        slider.setLookAndFeel(&style);
        slider.getProperties().set(vocalstackThemeProperty, int(theme));
        slider.getProperties().set("vocalstackTexturedBar", true);
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setRange(0, 1);
        slider.setColour(juce::Slider::trackColourId, themePalette(theme).accent);
        const auto accent = slider.findColour(juce::Slider::trackColourId);
        int lifted = 0, unchanged = 0, upperToneGain = 0;
        for (double seconds : {0., 6., 33.3, 46.}) {
            FluidBackground oldMaterial{FluidBackground::Material::control};
            FluidBackground fatMaterial{FluidBackground::Material::fatBar};
            juce::Image old(juce::Image::RGB, 128, 26, true), enlarged(juce::Image::RGB, 192, 39, true);
            { juce::Graphics g(old); oldMaterial.paint(g, old.getBounds(), accent, accent, seconds, .35f, 64); }
            { juce::Graphics g(enlarged); fatMaterial.paint(g, enlarged.getBounds(), accent, accent, seconds, .35f, 64); }
            std::vector<int> oldTones, newTones;
            int boundaries = 0;
            for (int y = 0; y < 39; ++y) for (int x = 0; x < 192; ++x) {
                const auto a = old.getPixelAt(x / 3 * 2, y / 3 * 2);
                const auto b = enlarged.getPixelAt(x, y);
                toneUiTests::require(a == old.getPixelAt(x / 3 * 2 + 1, y / 3 * 2 + 1),
                    "Legacy reference is not exact 2x2 texels");
                toneUiTests::require(b == enlarged.getPixelAt(x / 3 * 3, y / 3 * 3),
                    "Fat material is not exact 3x3 nearest-neighbour texels");
                boundaries += x > 0 && b != enlarged.getPixelAt(x - 1, y);
                for (int delta : {int(b.getRed()) - a.getRed(), int(b.getGreen()) - a.getGreen(), int(b.getBlue()) - a.getBlue()})
                    toneUiTests::require(delta <= 0 && delta >= -64, "Fat material must deepen lows without a brightness lift");
                lifted += a != b; unchanged += a == b;
                oldTones.push_back(int(a.getRed()) + a.getGreen() + a.getBlue());
                newTones.push_back(int(b.getRed()) + b.getGreen() + b.getBlue());
            }
            toneUiTests::require(boundaries > 0, "Fat material has no visible texel boundaries");
            std::sort(oldTones.begin(), oldTones.end()); std::sort(newTones.begin(), newTones.end());
            for (size_t i = oldTones.size() * 3 / 4; i < oldTones.size(); ++i)
                upperToneGain += newTones[i] - oldTones[i];
            toneUiTests::require(oldTones[oldTones.size() / 2] > newTones[newTones.size() / 2],
                "Fat material did not deepen its median tone");
            slider.getProperties().set(vocalstackAnimationTimeOverrideProperty, seconds);
            const juce::Rectangle<float> area(0, 0, 400, 44);
            const auto track = ParameterKnob::Style::texturedBarBounds(area).toNearestInt();
            const auto draw = [&](float position) {
                juce::Image image(juce::Image::RGB, 400, 44, true);
                juce::Graphics g(image);
                style.drawLinearSlider(g, 0, 0, 400, 44, position, 0, 1, juce::Slider::LinearHorizontal, slider);
                return image;
            };
            const auto full = draw(400), partial = draw(200);
            for (int y = track.getY() + 1; y < track.getBottom() - 1; ++y)
                for (int x = track.getX() + 1; x < track.getRight() - 3; ++x) {
                    toneUiTests::require(full.getPixelAt(x, y) == enlarged.getPixelAt((x - track.getX()) % 192, y - track.getY()),
                        "Actual fat bar did not tile the 3px/highlight material at a fixed origin");
                    if (x < 197) toneUiTests::require(full.getPixelAt(x, y) == partial.getPixelAt(x, y),
                        "Numeric Mix value resized or reseeded fat material");
                }
            // A fat-bar render cannot change another instance's cached control material.
            juce::Image restored(juce::Image::RGB, 128, 26, true);
            { juce::Graphics g(restored); oldMaterial.paint(g, restored.getBounds(), accent, accent, seconds, .35f, 64); }
            toneUiTests::require(identical(old, restored), "Fat material leaked into ordinary control caches");
        }
        toneUiTests::require(lifted > 0 && upperToneGain < 0,
                    "Fat material needs deeper tones, not a lifted palette");
        slider.setLookAndFeel(nullptr);
    }
}
inline void squareMarkersAndPreview() {
    auto owner = std::make_unique<Processor>();
    for (int theme : {0, 1, 2}) for (int width : {110, 340}) {
        ParameterKnob::Style style;
        ModulationSlider slider;
        slider.setLookAndFeel(&style);
        slider.getProperties().set(vocalstackThemeProperty, theme);
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        slider.setRange(0, 1); slider.setValue(.5); slider.setSize(width, 36);
        juce::Image image(juce::Image::ARGB, width, 36, true);
        { juce::Graphics g(image);
        style.drawLinearSlider(g, 10, 0, width - 20, 34, float(width / 2), 0, 1,
                               juce::Slider::LinearHorizontal, slider); }
        const auto text = themePalette(slider).text;
        for (int y : {13, 21}) for (int x : {width / 2 - 2, width / 2 + 1})
            if (image.getPixelAt(x, y) != text)
                throw std::runtime_error(("Square marker x=" + juce::String(x) + " y=" + juce::String(y)
                    + " actual=" + image.getPixelAt(x,y).toString() + " expected=" + text.toString()).toStdString());
        toneUiTests::screenshot(slider, "approval-1910-slider-" + juce::String(theme) + "-" + juce::String(width) + ".png");
        slider.setLookAndFeel(nullptr);
        ParameterKnob knob(*owner, "voice1Pan", "Pan");
        knob.getProperties().set(vocalstackThemeProperty, theme);
        knob.setSize(90, 100);
        auto* label = toneUiTests::find<juce::Label>(knob.slider, [](auto&) { return true; });
        toneUiTests::require(label && label->getFont().getTypefacePtr() == pixelTypeface(), "Compact knob font fell back");
        toneUiTests::screenshot(knob, "approval-1910-compact-knob-" + juce::String(theme) + ".png");
        ParameterKnob mix(*owner, "fx1Mix", "Mix", "%");
        mix.getProperties().set(vocalstackThemeProperty, theme);
        mix.setLinear(true); mix.setSize(width, 44); mix.slider.setValue(50);
        toneUiTests::screenshot(mix, "approval-1910-mix-" + juce::String(theme) + "-" + juce::String(width) + ".png");
    }
}
inline void run() {
    squareMarkersAndPreview();
    texturedFxBars();
    fatMaterialPixels();
    selectionAndFeedback();
    alignmentAndSensitivity();
    pointerSeparation();
    controlModes();
}
} // namespace vocalstack::sliderRenderingTests
