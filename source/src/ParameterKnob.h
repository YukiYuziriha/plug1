#pragma once

#include "PluginProcessor.h"
#include "VisualTheme.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace vocalstack {

// Refresh from a single message-thread snapshot for the complete editor tree.
// Ordinary gestures never take keyboard focus; only explicit numeric entry does.
class ModulationSlider : public juce::Slider {
public:
    void bindModulation(Processor& processor, const juce::String& parameterId) {
        owner = &processor; id = parameterId;
        setComponentID(id);
        setWantsKeyboardFocus(false);
        setMouseClickGrabsKeyboardFocus(false);
        setScrollWheelEnabled(false);
        setVelocityModeParameters(1.0, 1, 0.0, true,
                                 static_cast<juce::ModifierKeys::Flags>(juce::ModifierKeys::shiftModifier | juce::ModifierKeys::ctrlModifier
                                 | juce::ModifierKeys::commandModifier));
    }
    void setTextBoxIsEditable(bool editable) {
        juce::Slider::setTextBoxIsEditable(editable);
        restoreIdleFocusPolicy();
    }
    void lookAndFeelChanged() override {
        juce::Slider::lookAndFeelChanged();
        restoreIdleFocusPolicy();
    }
    void enablementChanged() override {
        juce::Slider::enablementChanged();
        restoreIdleFocusPolicy();
    }
    // displayedSource may be selected without being in assignment mode.
    void refreshModulation(const ModulationSnapshot& snapshot, const ModulationState& state, uint32_t displayedSource) {
        assignment = displayedSource;
        if (snapshot.consistent) {
            effective = float(valueToProportionOfLength(getValue()));
            hasEffective = false;
            for (const auto& target : snapshot.targets) if (target.parameterId == id) {
                effective = target.effectiveNormalized; hasEffective = true; break;
            }
        }
        depth = 0; bipolar = true;
        float sourceAmount = 1;
        for (const auto& modulator : state.modulators) if (modulator.id == displayedSource) {
            bipolar = modulator.bipolar;
            sourceAmount = modulator.depth;
        }
        for (const auto& a : state.assignments)
            if (a.modulatorId == displayedSource && a.targetParameterId == id && a.enabled) { depth = a.depth * sourceAmount; break; }
        const bool assigning = owner && owner->modulationAssignmentId() != 0;
        if (isTextBoxEditable() == assigning) {
            // Only entering Assign cancels deliberate base-value entry. Selection
            // and ordinary feedback ticks must not disturb the transient editor.
            if (assigning) hideTextBox(true);
            setTextBoxIsEditable(!assigning);
        }
        repaint();
    }
    void mouseDown(const juce::MouseEvent& e) override {
        dragAssignment = owner ? owner->modulationAssignmentId() : 0;
        if (dragAssignment != 0) {
            dragDepth = 0;
            for (const auto& a : owner->getModulationState().assignments)
                if (a.modulatorId == dragAssignment && a.targetParameterId == id) { dragDepth = a.depth; break; }
            return;
        }
        juce::Slider::mouseDown(e);
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (dragAssignment != 0) {
            if (owner->modulationAssignmentId() != dragAssignment) return;
            const float fine = e.mods.isShiftDown() || e.mods.isCtrlDown() || e.mods.isCommandDown() ? 0.1f : 1.0f;
            const auto amount = juce::jlimit(-1.0f, 1.0f,
                dragDepth + fine * float(e.getDistanceFromDragStartX() - e.getDistanceFromDragStartY()) / 180.0f);
            if (owner->setModulationDepth(dragAssignment, id, amount)) { depth = amount; repaint(); }
            return;
        }
        juce::Slider::mouseDrag(e);
    }
    void mouseUp(const juce::MouseEvent& e) override {
        if (dragAssignment != 0) { dragAssignment = 0; return; }
        juce::Slider::mouseUp(e);
    }
    void mouseDoubleClick(const juce::MouseEvent& e) override {
        if (!owner || owner->modulationAssignmentId() == 0) juce::Slider::mouseDoubleClick(e);
    }
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override {}
    void drawLinearModulation(juce::Graphics& g, juce::Rectangle<float> track) {
        const auto x = [&](float value) { return track.getX() + juce::jlimit(0.0f, 1.0f, value) * track.getWidth(); };
        const float base = float(valueToProportionOfLength(getValue())), y = track.getY() - 6;
        if (assignment != 0) {
            g.setColour((componentTheme(*this) == Theme::spectral ? themePalette(*this).accent
                            : juce::Colour(0xffb798ff)).withAlpha(0.4f));
            g.drawLine(track.getX(), y, track.getRight(), y, 1);
            const float lo = bipolar ? base - std::abs(depth) : base + juce::jmin(0.0f, depth);
            const float hi = bipolar ? base + std::abs(depth) : base + juce::jmax(0.0f, depth);
            g.setColour(componentTheme(*this) == Theme::spectral
                ? (depth < 0 ? themePalette(*this).secondary : themePalette(*this).accent)
                : juce::Colour(depth < 0 ? 0xffff96c8 : 0xffb798ff));
            g.drawLine(x(lo), y, x(hi), y, 2.5f);
        }
        if (hasEffective) {
            g.setColour(componentTheme(*this) == Theme::spectral ? themePalette(*this).secondary : juce::Colour(0xff64f5e3));
            g.drawLine(x(effective), y - 3, x(effective), track.getY() + 4, 1.5f);
        }
    }
    void drawModulation(juce::Graphics& g, juce::Point<float> centre, float radius, float start, float end) {
        const auto angle = [=](float value) { return start + juce::jlimit(0.0f, 1.0f, value) * (end - start); };
        const auto base = float(valueToProportionOfLength(getValue()));
        if (assignment != 0) {
            g.setColour((componentTheme(*this) == Theme::spectral ? themePalette(*this).accent
                            : juce::Colour(0xffb798ff)).withAlpha(0.45f));
            juce::Path eligible;
            eligible.addCentredArc(centre.x, centre.y, radius + 5, radius + 5, 0, start, end, true);
            g.strokePath(eligible, juce::PathStrokeType(1));
            const float lo = bipolar ? base - std::abs(depth) : base + juce::jmin(0.0f, depth);
            const float hi = bipolar ? base + std::abs(depth) : base + juce::jmax(0.0f, depth);
            juce::Path range;
            range.addCentredArc(centre.x, centre.y, radius + 5, radius + 5, 0, angle(lo), angle(hi), true);
            g.setColour(componentTheme(*this) == Theme::spectral
                ? (depth < 0 ? themePalette(*this).secondary : themePalette(*this).accent)
                : juce::Colour(depth < 0 ? 0xffff96c8 : 0xffb798ff));
            g.strokePath(range, juce::PathStrokeType(2.5f));
        }
        if (hasEffective) {
            const auto point = centre.getPointOnCircumference(radius + 1, angle(effective));
            g.setColour(componentTheme(*this) == Theme::spectral ? themePalette(*this).secondary : juce::Colour(0xff64f5e3));
            g.fillEllipse(point.x - 2, point.y - 2, 4, 4);
        }
    }
private:
    void restoreIdleFocusPolicy() {
        // JUCE Label::setEditable restores keyboard focusability. Keep the idle
        // label passive without touching the deliberate transient TextEditor.
        const auto passive = [](auto&& self, juce::Component& component) -> void {
            if (dynamic_cast<juce::TextEditor*>(&component)) return;
            component.setWantsKeyboardFocus(false);
            component.setMouseClickGrabsKeyboardFocus(false);
            component.setFocusContainerType(juce::Component::FocusContainerType::none);
            for (auto* child : component.getChildren()) self(self, *child);
        };
        passive(passive, *this);
    }
    Processor* owner = nullptr;
    juce::String id;
    uint32_t assignment = 0, dragAssignment = 0;
    float depth = 0, dragDepth = 0, effective = 0;
    bool bipolar = true, hasEffective = false;
};

class ParameterKnob final : public juce::Component {
public:
    class Style final : public juce::LookAndFeel_V4 {
    public:
        void drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h,
                              float position, float, float, juce::Slider::SliderStyle,
                              juce::Slider& slider) override {
            const auto track = juce::Rectangle<float>(float(x + 5), float(y + h / 2), float(juce::jmax(1, w - 10)), 2.0f);
            const bool bipolar = slider.getMinimum() < 0 && slider.getMaximum() > 0;
            const float origin = bipolar ? track.getX() + float(slider.valueToProportionOfLength(0)) * track.getWidth() : track.getX();
            const float thumb = juce::jlimit(track.getX(), track.getRight(), position);
            g.setColour(componentTheme(slider) == Theme::device ? juce::Colour(0xff46515b) : themePalette(slider).border);
            g.fillRect(track);
            g.setColour(slider.findColour(juce::Slider::trackColourId));
            g.fillRect(juce::Rectangle<float>(juce::jmin(origin, thumb), track.getY(), std::abs(thumb - origin), 2));
            if (bipolar) {
                g.setColour(componentTheme(slider) == Theme::device ? juce::Colour(0xffd7dfe6) : themePalette(slider).muted);
                g.drawVerticalLine(juce::roundToInt(origin), track.getY() - 3, track.getY() + 5);
            }
            g.setColour(slider.findColour(juce::Slider::thumbColourId));
            g.fillRoundedRectangle(thumb - 2, track.getY() - 4, 4, 10, 1);
            if (auto* mod = dynamic_cast<ModulationSlider*>(&slider)) mod->drawLinearModulation(g, track);
        }
        void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float position,
                              float start, float end, juce::Slider& slider) override {
            const auto centre = juce::Rectangle<float>(float(x), float(y), float(w), float(h)).getCentre();
            const float radius = float(juce::jmin(w, h)) * 0.34f;
            const float angle = start + position * (end - start);
            const bool signedRange = slider.getMinimum() < 0 && slider.getMaximum() > 0;
            const float origin = signedRange ? start + float(slider.valueToProportionOfLength(0)) * (end - start) : start;
            const auto arc = [&](float a, float b, juce::Colour colour, float width) {
                juce::Path path;
                path.addCentredArc(centre.x, centre.y, radius, radius, 0, juce::jmin(a, b), juce::jmax(a, b), true);
                g.setColour(colour); g.strokePath(path, juce::PathStrokeType(width));
            };
            arc(start, end, componentTheme(slider) == Theme::spectral ? themePalette(slider).border : juce::Colour(0xff46515b), 2.5f);
            arc(origin, angle, slider.findColour(juce::Slider::trackColourId), 3);
            if (signedRange) {
                g.setColour(componentTheme(slider) == Theme::spectral ? themePalette(slider).muted : juce::Colour(0xffd7dfe6));
                g.drawLine({centre.getPointOnCircumference(radius + 2, origin),
                            centre.getPointOnCircumference(radius + 5, origin)}, 1);
            }
            g.setColour(componentTheme(slider) == Theme::spectral ? themePalette(slider).inset : juce::Colour(0xff20262d));
            g.fillEllipse(juce::Rectangle<float>(radius * 1.55f, radius * 1.55f).withCentre(centre));
            g.setColour(slider.findColour(juce::Slider::thumbColourId));
            g.drawLine({centre.getPointOnCircumference(radius * 0.35f, angle),
                        centre.getPointOnCircumference(radius * 0.7f, angle)}, 2);
            if (auto* mod = dynamic_cast<ModulationSlider*>(&slider)) mod->drawModulation(g, centre, radius, start, end);
        }
    };
public:
    ParameterKnob(Processor& owner, const juce::String& id, const juce::String& title, const char* unit = "") {
        setComponentID(id + "Control");
        label.setText(title, juce::dontSendNotification);
        label.setJustificationType(juce::Justification::centred);
        label.setFont(juce::Font(juce::FontOptions(11.0f)));
        label.setColour(juce::Label::textColourId, juce::Colour(0xffe4e9ee));
        label.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(label);
        slider.bindModulation(owner, id);
        slider.setName(title); slider.setTitle(title);
        slider.setLookAndFeel(&style);
        slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 68, 19);
        addAndMakeVisible(slider);
        link = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(owner.parameters, id, slider);
        if (auto* parameter = owner.parameters.getParameter(id))
            slider.setDoubleClickReturnValue(true, parameter->convertFrom0to1(parameter->getDefaultValue()));
        const juce::String suffix(unit);
        slider.textFromValueFunction = [suffix](double value) {
            return juce::String(value, suffix == "%" ? 0 : 2)
                + (suffix.isEmpty() ? juce::String() : " " + suffix);
        };
        slider.valueFromTextFunction = [](const juce::String& value) { return value.getDoubleValue(); };
        if (id == "double") {
            slider.textFromValueFunction = [](double value) {
                return juce::String(100 - value, 0) + " / " + juce::String(value, 0);
            };
            slider.valueFromTextFunction = [](const juce::String& text) {
                return text.contains("/") ? text.fromFirstOccurrenceOf("/", false, false).getDoubleValue() : text.getDoubleValue();
            };
        }
        if (id.endsWith("Pan")) {
            slider.textFromValueFunction = [](double value) {
                return std::abs(value) < 0.005 ? juce::String("C")
                    : juce::String(std::abs(value) * 100, 0) + (value < 0 ? " L" : " R");
            };
            slider.valueFromTextFunction = [](const juce::String& text) {
                if (text.trim().equalsIgnoreCase("C")) return 0.0;
                const double value = text.getDoubleValue();
                if (text.containsIgnoreCase("L")) return -std::abs(value) / 100;
                if (text.containsIgnoreCase("R")) return std::abs(value) / 100;
                return value;
            };
        }
        slider.updateText(); setNeon(false);
        setWantsKeyboardFocus(false); setMouseClickGrabsKeyboardFocus(false);
    }
    void setLinear(bool value) {
        slider.setSliderStyle(value ? juce::Slider::LinearHorizontal : juce::Slider::RotaryHorizontalVerticalDrag);
        resized();
    }
    ~ParameterKnob() override { slider.setLookAndFeel(nullptr); }
    void resized() override {
        auto area = getLocalBounds(); label.setBounds(area.removeFromTop(17)); slider.setBounds(area);
    }
    void setNeon(bool neon) {
        const auto p = themePalette(*this, neon);
        const auto selectedTheme = componentTheme(*this, neon);
        const auto text = selectedTheme == Theme::device ? juce::Colour(0xffe4e9ee) : p.text;
        label.setColour(juce::Label::textColourId, text);
        slider.setColour(juce::Slider::trackColourId, p.accent);
        slider.setColour(juce::Slider::thumbColourId, selectedTheme == Theme::device ? p.text : p.secondary);
        slider.setColour(juce::Slider::textBoxTextColourId, text);
        slider.setColour(juce::Slider::textBoxHighlightColourId, p.accent.withAlpha(0.3f));
        slider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        for (auto* child : slider.getChildren()) {
            child->setWantsKeyboardFocus(false); child->setMouseClickGrabsKeyboardFocus(false);
        }
    }
    ModulationSlider slider;
private:
    Style style;
    juce::Label label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> link;
};

inline void refreshParameterModulation(juce::Component& root, const ModulationSnapshot& snapshot,
                                       const ModulationState& state, uint32_t assigning) {
    if (auto* slider = dynamic_cast<ModulationSlider*>(&root)) slider->refreshModulation(snapshot, state, assigning);
    // Keep dormant duplicate controls in sync too: switching inspectors/views
    // must never briefly expose editable base text while Assign is active.
    for (auto* child : root.getChildren())
        refreshParameterModulation(*child, snapshot, state, assigning);
}

} // namespace vocalstack
