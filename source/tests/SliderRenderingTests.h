#pragma once

#include "ParameterKnob.h"
#include "ToneUiTests.h"
#include "DoublerPanel.h"
#include "CreativeEffectPanel.h"
#include "UtilityEffectPanel.h"
#include "ModulationPanel.h"
#include <set>

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
            const bool horizontal = linear || slider->getComponentID() == "fx1Mix";
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
                toneUiTests::require(!layout.textBoxBounds.isEmpty()
                        && layout.textBoxBounds.getBottom() <= layout.sliderBounds.getY(),
                    "FX value overlaps its slider");
                auto* row = slider->getParentComponent();
                auto* title = toneUiTests::find<juce::Label>(*row, [&](auto& label) {
                    return label.getParentComponent() == row && label.isVisible();
                });
                toneUiTests::require(title && title->getY() >= slider->getBottom()
                        && row->getLocalBounds().contains(title->getBounds()),
                    "FX parameter title must be below, without clipping");
                                    auto* value = toneUiTests::find<juce::Label>(*slider, [](auto&) { return true; });
                                    toneUiTests::require(value && std::abs(value->getFont().getHeight() - ParameterKnob::controlFontSize) < .01f
                                            && std::abs(title->getFont().getHeight() - value->getFont().getHeight()) < .01f,
                                        "FX names and values must share the intermediate 12.5px font size");
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
        control.setLinear(true); control.setSize(width, 94); control.setNeon(theme == 1);
        auto& slider = control.slider;
        toneUiTests::require(bool(slider.getProperties()["vocalstackTexturedBar"]), "FX Mix did not opt into its textured bar");
        const auto layout = slider.getLookAndFeel().getSliderLayout(slider);
        const auto bar = ParameterKnob::Style::texturedBarBounds(layout.sliderBounds.toFloat());
        toneUiTests::require(std::abs(bar.getHeight() - 25.f) < .01f
            && bar.getY() - 7 >= layout.textBoxBounds.getBottom(),
            "25px FX bar or its modulation lane overlaps numeric text");
        slider.setValue(50, juce::dontSendNotification);
        const auto baseline = render(slider);
        toneUiTests::require(identical(baseline, render(slider)), "Procedural slider texture flickers between repaints");
        std::set<juce::uint32> shades;
        const auto sample = bar.toNearestInt().withTrimmedLeft(8).withTrimmedRight(int(bar.getWidth() * .6f));
        for (int y = sample.getY() + 2; y < sample.getBottom() - 2; ++y)
            for (int x = sample.getX(); x < sample.getRight(); ++x) shades.insert(baseline.getPixelAt(x, y).getARGB());
        toneUiTests::require(shades.size() >= 2 && shades.size() <= 6,
            "FX texture must have a small blocky palette, not a gradient or flat fill");
        slider.setValue(80, juce::dontSendNotification);
        const auto extended = render(slider);
        for (int y = sample.getY() + 2; y < sample.getBottom() - 2; ++y)
            for (int x = sample.getX(); x < sample.getRight(); ++x)
                toneUiTests::require(baseline.getPixelAt(x, y) == extended.getPixelAt(x, y),
                    "Texture swims/rescales when the value changes");
        slider.setValue(50, juce::dontSendNotification);
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
inline void run() {
    texturedFxBars();
    selectionAndFeedback();
    alignmentAndSensitivity();
    pointerSeparation();
    controlModes();
}
} // namespace vocalstack::sliderRenderingTests
