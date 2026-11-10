#pragma once

#include "ParameterKnob.h"
#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <vector>

namespace vocalstack::modulationui {

using RotaryStyle = ParameterKnob::Style;

inline void styleRotary(juce::Slider& slider, bool neon) {
    slider.setColour(juce::Slider::trackColourId, juce::Colour(neon ? 0xffff4ca0 : 0xfff5a24b));
    slider.setColour(juce::Slider::thumbColourId, juce::Colour(neon ? 0xff64f5e3 : 0xffe4e4df));
    slider.setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xffe4e9ee));
    slider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
}

// Selection is UI state, deliberately separate from the audio/gesture Assign ID.
inline uint32_t selectedModulator(const Processor& owner) {
    const auto id = uint32_t(int(owner.getRoutingLayout().getProperty("selectedModulationId", 0)));
    for (const auto& m : owner.getModulationState().modulators) if (m.id == id) return id;
    return 0;
}
inline void selectModulator(Processor& owner, uint32_t id) {
    auto layout = owner.getRoutingLayout().createCopy();
    if (uint32_t(int(layout.getProperty("selectedModulationId", 0))) == id) return;
    layout.setProperty("selectedModulationId", int(id), nullptr);
    owner.setRoutingLayout(layout);
}

// Invalid UI snapshots leave the previous complete feedback frame visible.
inline void refreshWithSelection(juce::Component& root, const Processor& owner,
                                 const ModulationSnapshot& snapshot, const ModulationState& state,
                                 uint32_t assigning) {
    // Edit mode is UI state, independent of whether this audio snapshot is
    // readable. Individual controls retain their previous effective marker.
    const uint32_t feedback = assigning ? assigning : selectedModulator(owner);
    refreshParameterModulation(root, snapshot, state, feedback);
}

inline void controlError(const juce::Component::SafePointer<juce::Component>& control, const juce::String& error) {
    if (control) juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Modulation", error);
}

// Call on right-click, or on ordinary click while assigning. No host value is
// touched: choice/switch depth is explicit signed normalized displacement.
inline void showDiscreteAssignmentMenu(Processor& owner, juce::Component& control) {
    const auto parameterId = control.getComponentID();
    auto* parameter = owner.parameters.getParameter(parameterId);
    if (!parameter || parameterId == "bypass" || parameterId == "advancedOn" || parameterId.endsWith("Present")) return;
    const auto state = owner.getModulationState();
    const auto assigning = owner.modulationAssignmentId();
    const auto selected = assigning ? assigning : selectedModulator(owner);
    juce::PopupMenu menu;
    menu.addSectionHeader(parameter->getName(80) + " / stepped modulation");
    struct Action { uint32_t source; float depth; bool erase; };
    std::vector<Action> actions;
    const auto addDepths = [&](juce::PopupMenu& destination, const Modulator& m) {
        const float step = 1.0f / float(juce::jmax(1, parameter->getNumSteps() - 1));
        std::vector<float> amounts{-1.0f, -step, 0.0f, step, 1.0f};
                amounts.erase(std::unique(amounts.begin(), amounts.end()), amounts.end());
                for (const float amount : amounts) {
            const auto text = amount == 0 ? juce::String("Zero depth")
                : (amount == step ? juce::String("+1 step / ") : amount == -step ? juce::String("-1 step / ") : juce::String())
                    + (amount > 0 ? "+" : "") + juce::String(amount * 100, 2) + "% range";
            bool checked = false;
            for (const auto& a : state.assignments)
                if (a.modulatorId == m.id && a.targetParameterId == parameterId) checked = std::abs(a.depth - amount) < .00001f;
            actions.push_back({m.id, amount, false});
            destination.addItem(int(actions.size()), text, true, checked);
        }
        actions.push_back({m.id, 0, true}); destination.addItem(int(actions.size()), "Remove assignment");
    };
    for (const auto& m : state.modulators) {
        const auto name = (m.type == ModulatorType::lfo ? "LFO " : "Random ") + juce::String(m.id);
        juce::PopupMenu sourceMenu; addDepths(sourceMenu, m);
        menu.addSubMenu(name + (m.id == selected ? " / selected" : ""), sourceMenu);
    }
    if (state.modulators.empty()) menu.addItem(1, "Add a modulator first", false);
    juce::Component::SafePointer<juce::Component> safe(&control);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(control),
        [safe, &owner, parameterId, actions](int result) {
            if (!safe || result <= 0 || result > int(actions.size())) return;
            const auto action = actions[size_t(result - 1)];
            selectModulator(owner, action.source);
            if (action.erase) {
                auto updated = owner.getModulationState();
                updated.assignments.erase(std::remove_if(updated.assignments.begin(), updated.assignments.end(), [&](const auto& a) {
                    return a.modulatorId == action.source && a.targetParameterId == parameterId;
                }), updated.assignments.end());
                juce::String error;
                if (!owner.setModulationState(updated, error)) controlError(safe, error);
            } else if (!owner.setModulationDepth(action.source, parameterId, action.depth)) {
                controlError(safe, "Modulation assignment rejected; retry the edit");
            }
        });
}

// Used for sound switches/choices only; structural buttons keep native behavior.
template <typename Base> class Assignable : public Base {
public:
    using Base::Base;
    void setModulationOwner(Processor& processor) {
        owner = &processor; this->setWantsKeyboardFocus(false); this->setMouseClickGrabsKeyboardFocus(false);
    }
    void mouseDown(const juce::MouseEvent& e) override {
        suppressUp = eligible() && (e.mods.isPopupMenu() || owner->modulationAssignmentId() != 0);
        if (suppressUp) { showDiscreteAssignmentMenu(*owner, *this); return; }
        Base::mouseDown(e);
    }
    void mouseDrag(const juce::MouseEvent& e) override { if (!suppressUp) Base::mouseDrag(e); }
    void mouseUp(const juce::MouseEvent& e) override {
        if (suppressUp) { suppressUp = false; return; }
        Base::mouseUp(e);
    }
    void mouseDoubleClick(const juce::MouseEvent& e) override {
        if (eligible() && owner->modulationAssignmentId() != 0) return;
        Base::mouseDoubleClick(e);
    }
    void paintOverChildren(juce::Graphics& g) override {
        Base::paintOverChildren(g);
        if (!eligible()) return;
        bool assigned = false;
        const auto source = selectedModulator(*owner);
        for (const auto& a : owner->getModulationState().assignments)
            if (a.modulatorId == source && a.targetParameterId == this->getComponentID() && a.enabled) assigned = true;
        if (owner->modulationAssignmentId() || assigned) {
            g.setColour(juce::Colour(0xffb798ff).withAlpha(assigned ? .9f : .5f));
            g.drawRoundedRectangle(this->getLocalBounds().toFloat().reduced(1), 3, assigned ? 2.0f : 1.0f);
        }
    }
private:
    bool eligible() const {
        if (!owner || !this->isEnabled()) return false;
        const auto id = this->getComponentID();
        return id.isNotEmpty() && id != "bypass" && id != "advancedOn" && !id.endsWith("Present") && owner->parameters.getParameter(id);
    }
    Processor* owner = nullptr;
    bool suppressUp = false;
};
using Button = Assignable<juce::TextButton>;
using ComboBox = Assignable<juce::ComboBox>;

} // namespace vocalstack::modulationui
