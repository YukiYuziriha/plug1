#pragma once

#include "ChoiceDropdown.h"
#include "UtilityEffectPanel.h"
#include "TuningPanel.h"
#include "GuiTestDisplay.h"
#include <cmath>
#include <functional>
#include <memory>
#include <stdexcept>
#include <vector>

// Include at translation-unit scope; call run(dispatch) on the initialized JUCE
// message thread. dispatch must pump native messages and pending JUCE timers.
// This suite creates native menus/windows on the enumerated secondary display.
namespace vocalstack::discreteUiArchitectureTests {
inline void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
inline juce::Component* find(juce::Component& root, const juce::String& id) {
    if (root.getComponentID() == id) return &root;
    for (auto* child : root.getChildren()) if (auto* result = find(*child, id)) return result;
    return nullptr;
}
inline void settle(const std::function<void()>& dispatch) {
    for (int i = 0; i < 20; ++i) { dispatch(); juce::Thread::sleep(1); }
}
inline juce::MouseEvent mouse(juce::Component& target, int modifiers) {
    const auto now = juce::Time::getCurrentTime();
    const auto point = target.getLocalBounds().toFloat().getCentre();
    return {juce::Desktop::getInstance().getMainMouseSource(), point, juce::ModifierKeys(modifiers),
            1, 0, 0, 0, 0, &target, &target, now, point, now, 1, false};
}
class HostLog final : private juce::AudioProcessorParameter::Listener {
public:
    explicit HostLog(Processor& owner) {
        for (auto* p : owner.getParameters()) {
            parameters.push_back(p); bases.push_back(p->getValue()); p->addListener(this);
        }
    }
    ~HostLog() override { for (auto* p : parameters) p->removeListener(this); }
    void unchanged() const {
        require(values == 0 && begins == 0 && ends == 0, "Assign/right-click emitted host values or gestures");
        for (size_t i = 0; i < parameters.size(); ++i)
            require(parameters[i]->getValue() == bases[i], "Assign/right-click changed a host base");
    }
    int values = 0, begins = 0, ends = 0;
private:
    void parameterValueChanged(int, float) override { ++values; }
    void parameterGestureChanged(int, bool starting) override { if (starting) ++begins; else ++ends; }
    std::vector<juce::AudioProcessorParameter*> parameters;
    std::vector<float> bases;
};
inline void passive(juce::Component& control) {
    require(!control.getWantsKeyboardFocus() && !control.getMouseClickGrabsKeyboardFocus(),
            "Discrete control captures DAW keyboard focus");
}
inline void checkAssignment(Processor& owner, juce::Component& control, const juce::String& id,
                            bool assigning, const std::function<void()>& dispatch,
                            const std::function<void()>& intercepted = {}) {
    auto* parameter = owner.parameters.getParameter(id);
    require(parameter != nullptr, "Missing discrete host parameter");
    require(control.getComponentID() == id, "Assignable trigger lost exact host target ID");
    if (assigning) owner.beginModulationAssignment(1); else owner.endModulationAssignment();
    HostLog host(owner);
    auto* focused = juce::Component::getCurrentlyFocusedComponent();
    const auto event = mouse(control, assigning ? juce::ModifierKeys::leftButtonModifier
                                               : juce::ModifierKeys::rightButtonModifier);
    control.mouseDown(event);
    control.mouseDrag(event);
    control.mouseUp(event);
    host.unchanged();
    if (intercepted) intercepted();
    auto* menu = juce::Component::getCurrentlyModalComponent();
    require(menu != nullptr, "Assign/right-click failed to open shared discrete assignment menu");
    require(guiTestDisplay::selected().userArea.contains(menu->getScreenBounds()),
            "Assignment menu escaped selected display");
    // One source: -range, -step, zero, +step, +range; bools collapse
    // duplicate range/step entries. Complete the real modal menu callback,
    // not a direct setModulationDepth test shortcut.
    menu->exitModalState(parameter->getNumSteps() == 2 ? 3 : 4);
    settle(dispatch);
    require(juce::Component::getCurrentlyModalComponent() == nullptr, "Assignment menu remained modal");
    const auto state = owner.getModulationState();
    require(state.assignments.size() == 1, "Menu did not create exactly one assignment");
    const auto& assignment = state.assignments.front();
    const float step = 1.f / float(juce::jmax(1, parameter->getNumSteps() - 1));
    require(assignment.modulatorId == 1 && assignment.targetParameterId == id
                && std::abs(assignment.depth - step) < .00001f,
            "Menu assigned wrong owner, target, or signed step");
    require(modulationui::selectedModulator(owner) == 1, "Menu lost selected modulation source");
    host.unchanged();
    require(juce::Component::getCurrentlyFocusedComponent() == focused, "Assignment stole host keyboard focus");
    passive(control);
    owner.endModulationAssignment();
}
inline void resetAssignments(Processor& owner) {
    ModulationState state; state.modulators.push_back({1});
    juce::String error;
    require(owner.setModulationState(state, error), "Discrete test modulator setup failed");
}
inline void choices(const std::function<void()>& dispatch) {
    for (int theme : {0, 1, 2}) {
        auto owner = std::make_unique<Processor>();
        for (const char* id : {"tuningRoot", "tuningScale", "tuningRange"}) {
            resetAssignments(*owner);
            auto* parameter = dynamic_cast<juce::AudioParameterChoice*>(owner->parameters.getParameter(id));
            require(parameter != nullptr, "Tuning choice no longer uses explicit choice host mapping");
            ChoiceDropdown dropdown(*owner, id, id, parameter->choices);
            dropdown.getProperties().set(vocalstackThemeProperty, theme);
            dropdown.setNeon(theme == 1); dropdown.setSize(240, 64);
            guiTestDisplay::attach(dropdown);
            auto* trigger = dynamic_cast<modulationui::Button*>(find(dropdown, id));
            require(trigger != nullptr, "ChoiceDropdown trigger bypasses shared Assignable");
            dropdown.selectIndex(0);
            for (bool assigning : {true, false}) {
                resetAssignments(*owner);
                checkAssignment(*owner, *trigger, id, assigning, dispatch, [&] {
                    require(!dropdown.isPopupOpen(), "Assignment click also opened normal choice popup");
                    if (assigning) dropdown.selectIndex(parameter->choices.size() - 1);
                });
            }
            HostLog normal(*owner);
            int callbacks = 0;
            dropdown.onSelection = [&](int) { ++callbacks; };
            // Exercise the actual normal trigger and popup row callback.
            trigger->onClick();
            require(dropdown.isPopupOpen(), "Normal click no longer opens choice list");
            auto* viewport = dropdown.getPopupViewportForTesting();
            require(viewport && viewport->getViewedComponent(), "Normal choice list lost viewport");
            auto* content = viewport->getViewedComponent();
            const int last = parameter->choices.size() - 1;
            for (int i = 0; i <= last; ++i) {
                auto* row = dynamic_cast<juce::TextButton*>(find(*content, juce::String(id) + "Option" + juce::String(i)));
                require(row && row->getY() == i * row->getHeight() && row->getX() == 0,
                        "Choice popup lost single-column geometry");
                require(row->getToggleState() == (i == 0), "Choice popup lost selected-only highlight");
                passive(*row);
            }
            auto* row = dynamic_cast<juce::TextButton*>(find(*content, juce::String(id) + "Option" + juce::String(last)));
            row->onClick(); settle(dispatch);
            require(!dropdown.isPopupOpen() && dropdown.getSelectedIndex() == last,
                    "Normal row failed to select and close");
            require(std::abs(static_cast<juce::RangedAudioParameter*>(parameter)->getValue() - 1.f) < .00001f && callbacks == 1,
                    "Normal choice lost host mapping or selection callback");
            require(normal.values == 1 && normal.begins == 1 && normal.ends == 1,
                    "Normal choice must issue exactly one complete host gesture");
            dropdown.showPopup();
            require(dropdown.isPopupOpen(), "Stale-popup fixture failed to open");
            owner->beginModulationAssignment(1);
            { HostLog stale(*owner); dropdown.selectIndex(0); stale.unchanged(); }
            settle(dispatch);
            require(!dropdown.isPopupOpen(), "Choice popup opened before Assign did not close");
            owner->endModulationAssignment();
            dropdown.onSelection = nullptr;
        }
    }
}
inline void utilityConfigurations(const std::function<void()>& dispatch) {
    for (int theme : {0, 1, 2}) for (const auto kind : {UtilityKind::xpFilter, UtilityKind::synthFilter}) {
        auto owner = std::make_unique<Processor>();
        resetAssignments(*owner);
        const auto field = kind == UtilityKind::xpFilter ? UtilityParameter::xpMode : UtilityParameter::synthMode;
        const auto id = utilityParameterId(2, size_t(field));
        UtilityEffectPanel panel(*owner, 2, kind);
        panel.getProperties().set(vocalstackThemeProperty, theme);
        panel.setNeon(theme == 1); panel.setSize(600, 500);
        guiTestDisplay::attach(panel);
        auto* combo = dynamic_cast<modulationui::ComboBox*>(find(panel, id));
        require(combo != nullptr, "Utility configuration bypasses shared assignable combo");
        for (bool assigning : {true, false}) {
            resetAssignments(*owner);
            checkAssignment(*owner, *combo, id, assigning, dispatch, [&] {
                require(!combo->isPopupActive(), "Assignment also opened utility base-choice menu");
            });
        }
        auto* parameter = owner->parameters.getParameter(id);
        combo->setSelectedId(1, juce::sendNotificationSync);
        HostLog normal(*owner);
        combo->setSelectedId(combo->getNumItems(), juce::sendNotificationSync);
        require(parameter->convertFrom0to1(parameter->getValue()) == float(combo->getNumItems() - 1),
                "Utility configuration changed slot/choice host mapping");
        require(normal.values == 1 && normal.begins == 1 && normal.ends == 1,
                "Normal utility choice lost complete host gesture");
    }
}
inline void formants(const std::function<void()>& dispatch) {
    auto owner = std::make_unique<Processor>();
    resetAssignments(*owner);
    TuningPanel panel(*owner); panel.setSize(1000, 600);
    guiTestDisplay::attach(panel);
    auto* button = dynamic_cast<modulationui::Button*>(find(panel, "tuningFormants"));
    require(button != nullptr, "Preserve Formants must use shared assignable button");
    const bool original = button->getToggleState();
    for (bool assigning : {true, false}) {
        resetAssignments(*owner);
        checkAssignment(*owner, *button, "tuningFormants", assigning, dispatch, [&] {
            require(button->getToggleState() == original, "Assign/right-click toggled Preserve Formants");
        });
    }
    HostLog normal(*owner);
    button->setToggleState(!original, juce::sendNotificationSync);
    require(owner->parameters.getParameter("tuningFormants")->getValue() == (original ? 0.f : 1.f),
            "Normal Preserve Formants click lost host attachment mapping");
    require(normal.values == 1 && normal.begins == 1 && normal.ends == 1,
            "Normal Preserve Formants change lost complete host gesture");
}
inline void prohibitedEnable() {
    auto owner = std::make_unique<Processor>();
    resetAssignments(*owner);
    modulationui::Button button;
    button.setModulationOwner(*owner); button.setComponentID("tuningEnabled");
    owner->beginModulationAssignment(1);
    HostLog host(*owner);
    modulationui::showDiscreteAssignmentMenu(*owner, button);
    require(juce::Component::getCurrentlyModalComponent() == nullptr,
            "Latency-changing Tuning Enabled exposed a modulation menu");
    require(!owner->setModulationDepth(1, "tuningEnabled", 1.f), "Backend permits Tuning Enabled modulation");
    require(owner->getModulationState().assignments.empty(), "Prohibited enable acquired assignment");
    host.unchanged();
}
inline void scrollReleaseDeadline() {
    ChoiceDropdown::ScrollReturnState state;
    require(ChoiceDropdown::ScrollReturnState::delayMs == 2500., "Dropdown recenter delay changed");
    state.inputPressed(0, 100.); state.scrolled(200.);
    require(!state.consumeReturn(10000.), "Dropdown recentered during held scroll");
    state.inputReleased(0, 10000.);
    require(!state.consumeReturn(12499.99) && state.consumeReturn(12500.),
            "Dropdown must wait a full 2500 ms after release");
}
inline void run(const std::function<void()>& dispatch) {
    require(juce::MessageManager::getInstance()->isThisTheMessageThread(), "Run discrete UI tests on message thread");
    guiTestDisplay::enumerate();
    // Dismiss even on assertion failure so subsequent harness cases are not blocked.
    struct MenuCleanup { ~MenuCleanup() { juce::PopupMenu::dismissAllActiveMenus(); } } cleanup;
    scrollReleaseDeadline(); choices(dispatch); utilityConfigurations(dispatch); formants(dispatch); prohibitedEnable();
}
} // namespace vocalstack::discreteUiArchitectureTests
