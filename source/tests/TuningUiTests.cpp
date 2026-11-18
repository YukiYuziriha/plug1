#include "PluginEditor.h"
#include "ChoiceDropdown.h"
#include "GuiTestDisplay.h"
#include <iostream>
#include <stdexcept>
#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#undef near
#undef far
#endif

namespace {
using namespace vocalstack;
void dispatch() {
#if JUCE_WINDOWS
    MSG message;
    while (PeekMessage(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message); DispatchMessage(&message);
    }
#endif
    juce::Timer::callPendingTimersSynchronously();
}
void expect(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
juce::Component* find(juce::Component& parent, const juce::String& id) {
    if (parent.getComponentID() == id) return &parent;
    for (auto* child : parent.getChildren()) if (auto* result = find(*child, id)) return result;
    return nullptr;
}
juce::Slider& slider(juce::Component& parent, const char* id) {
    auto* result = dynamic_cast<juce::Slider*>(find(parent, id));
    expect(result != nullptr, "Missing real parameter slider");
    return *result;
}
void set(Processor& p, const char* id, float value) {
    auto* parameter = p.parameters.getParameter(id);
    expect(parameter != nullptr, "Missing tuning backend parameter");
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}
float value(Processor& p, const char* id) { return p.parameters.getRawParameterValue(id)->load(); }
void click(juce::Component& parent, const char* id) {
    auto* button = dynamic_cast<juce::TextButton*>(find(parent, id));
    expect(button != nullptr, "Missing tuning button");
    button->onClick();
}
void checkPassive(juce::Component& c) {
    if (dynamic_cast<juce::TextEditor*>(&c)) return;
    expect(!c.getWantsKeyboardFocus() && !c.getMouseClickGrabsKeyboardFocus(), "Ordinary control steals host focus");
    for (auto* child : c.getChildren()) checkPassive(*child);
}
void checkBounds(juce::Component& c) {
    for (auto* child : c.getChildren()) {
        if (!child->isVisible()) continue;
        if (dynamic_cast<ParameterKnob*>(child) || dynamic_cast<ChoiceDropdown*>(child) || dynamic_cast<juce::TextButton*>(child))
            expect(c.getLocalBounds().contains(child->getBounds()), "Tuning control clipped by parent");
        checkBounds(*child);
    }
}
juce::TextButton& tab(Editor& editor, const juce::String& title) {
    const auto walk = [&](auto&& self, juce::Component& parent) -> juce::TextButton* {
        if (auto* button = dynamic_cast<juce::TextButton*>(&parent))
            if (button->getButtonText() == title) return button;
        for (auto* child : parent.getChildren()) if (auto* result = self(self, *child)) return result;
        return nullptr;
    };
    auto* result = walk(walk, editor);
    expect(result != nullptr, "Missing editor tab");
    return *result;
}
ChoiceDropdown& dropdown(juce::Component& parent, const char* id) {
    auto* result = dynamic_cast<ChoiceDropdown*>(find(parent, juce::String(id) + "Control"));
    if (result == nullptr)
        throw std::runtime_error("Missing ChoiceDropdown integration for " + std::string(id) + "Control (expected preserved parameter trigger ID)");
    expect(dynamic_cast<juce::TextButton*>(find(*result, id)) != nullptr, "Dropdown trigger lost parameter ID");
    expect(dynamic_cast<juce::Slider*>(find(parent, id)) == nullptr, "Choice parameter still has a slider");
    return *result;
}
juce::Viewport& popupViewport(ChoiceDropdown& control) {
    control.showPopup();
    expect(control.isPopupOpen(), "Showing dropdown did not open native popup");
    auto* viewport = control.getPopupViewportForTesting();
    expect(viewport != nullptr && viewport->getViewedComponent() != nullptr, "Popup has no real list viewport");
    auto* popup = viewport->getParentComponent();
    expect(popup != nullptr && popup->isOnDesktop() && popup->isShowing(), "Popup is not a showing native peer");
    expect(guiTestDisplay::selected().userArea.contains(popup->getScreenBounds()), "Popup escaped owner's selected display work area");
    checkPassive(*popup);
    expect(!popup->keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)), "Popup consumes host transport key");
    return *viewport;
}
juce::TextButton& option(juce::Viewport& viewport, const char* id, int index) {
    auto* row = dynamic_cast<juce::TextButton*>(find(*viewport.getViewedComponent(), juce::String(id) + "Option" + juce::String(index)));
    expect(row != nullptr, "Missing actual popup option button");
    return *row;
}
void checkSelection(juce::Viewport& viewport, const char* id, int count, int selected) {
    for (int i = 0; i < count; ++i) {
        auto& row = option(viewport, id, i);
        expect(row.getToggleState() == (i == selected), "Popup does not highlight exactly the selected parameter option");
        expect(row.getRadioGroupId() != 0, "Popup selection is not exposed as a radio button");
        expect(row.findColour(juce::TextButton::buttonOnColourId) != row.findColour(juce::TextButton::buttonColourId),
               "Selected option has no distinct highlight colour");
    }
}
void checkList(juce::Viewport& viewport, const char* id, int count) {
    auto* content = viewport.getViewedComponent();
    expect(content->getNumChildComponents() == count, "Popup option count changed");
    const auto first = option(viewport, id, 0).getBounds();
    expect(first.getX() == 0 && first.getY() == 0 && first.getHeight() > 0, "Popup first row is misplaced");
    for (int i = 0; i < count; ++i) {
        const auto bounds = option(viewport, id, i).getBounds();
        expect(bounds == first.translated(0, i * first.getHeight()), "Popup options are not one equal-width column");
        expect(content->getLocalBounds().contains(bounds), "Popup row clipped by content");
    }
    expect(content->getHeight() == count * first.getHeight(), "Popup content height does not match rows");
    expect(viewport.getViewHeight() == juce::jmin(count, 7) * first.getHeight(), "Popup does not cap visible list at seven rows");
    expect(viewport.getVerticalScrollBar().isVisible() == (count > 7), "Popup short/long list scrollbar mismatch");
    expect(!viewport.getHorizontalScrollBar().isVisible() && viewport.getViewPositionX() == 0, "Single column popup scrolls horizontally");
}
int selectedViewY(juce::Viewport& viewport, const char* id, int selected) {
    const auto bounds = option(viewport, id, selected).getBounds();
    return juce::jlimit(0, juce::jmax(0, viewport.getViewedComponent()->getHeight() - viewport.getViewHeight()),
                        bounds.getCentreY() - viewport.getViewHeight() / 2);
}
void checkScrollReturnState() {
    std::cout << "CHECK pure scroll-return timing (no native mouse state)\n";
    using State = ChoiceDropdown::ScrollReturnState;
    State state;
    expect(State::delayMs == 2500.0, "Scroll return delay is not 2.5 seconds");
    expect(!state.isHeld() && !state.isPending() && !state.consumeReturn(10000), "Idle scroll state returns spontaneously");
    state.scrolled(100);
    expect(!state.consumeReturn(2599.99), "Scroll returns before deadline");
    expect(state.consumeReturn(2600) && !state.isPending(), "Scroll does not return at exact deadline");
    expect(!state.consumeReturn(10000), "Scroll return fires twice");
    state.scrolled(10000); // Wheel activity.
    state.scrolled(12000); // Another wheel event restarts the deadline.
    expect(!state.consumeReturn(12500), "Wheel activity did not reset deadline");
    state.scrolled(14000); // Inertia, including at a viewport edge, is still activity.
    expect(!state.consumeReturn(16499.99) && state.consumeReturn(16500), "Inertia did not restart full 2.5-second wait");
    state.inputPressed(0, 20000);
    state.inputPressed(0, 20100); // Duplicate delivery must not require two releases.
    state.scrolled(21000);
    expect(state.isHeld() && !state.consumeReturn(30000), "Held drag returned while pointer was down");
    state.inputReleased(0, 30000);
    expect(!state.isHeld() && !state.consumeReturn(32499.99), "Drag return counted from movement instead of release");
    expect(state.consumeReturn(32500), "Drag release did not start a full 2.5-second wait");
    state.inputPressed(1, 40000); state.inputPressed(2, 40100);
    state.inputReleased(1, 41000);
    expect(state.isHeld() && !state.consumeReturn(50000), "One release cleared another pointer's hold");
    state.inputReleased(2, 51000);
    state.inputReleased(99, 53000); // Unrelated release must not postpone the deadline.
    expect(!state.consumeReturn(53499.99) && state.consumeReturn(53500), "Last held source release deadline is wrong");
    state.inputPressed(3, 60000); state.cancel();
    expect(!state.isPending() && !state.isHeld() && !state.consumeReturn(70000), "Cancel left pending return or held input");
    state.scrolled(80000);
    expect(state.consumeReturn(82500), "Cancelled state cannot be reused");
}
void checkDropdownPopup(Processor& owner, juce::Component& panel) {
    std::cout << "CHECK actual popup rows/scroll/automation\n";
    auto& range = dropdown(panel, "tuningRange");
    range.selectIndex(2);
    auto* focused = juce::Component::getCurrentlyFocusedComponent();
    auto& shortList = popupViewport(range);
    expect(juce::Component::getCurrentlyFocusedComponent() == focused, "Opening passive popup grabbed focus");
    checkList(shortList, "tuningRange", 4);
    checkSelection(shortList, "tuningRange", 4, 2);
    range.closePopup();
    expect(!range.isPopupOpen() && range.getPopupViewportForTesting() == nullptr
           && range.getPopupScrollStateForTesting() == nullptr, "Explicit close retained popup hooks");

    auto& root = dropdown(panel, "tuningRoot");
    root.selectIndex(11);
    auto& viewport = popupViewport(root);
    checkList(viewport, "tuningRoot", 12);
    checkSelection(viewport, "tuningRoot", 12, 11);
    expect(viewport.getViewPositionY() == selectedViewY(viewport, "tuningRoot", 11), "Popup did not open at selected key");
    auto* state = root.getPopupScrollStateForTesting();
    expect(state != nullptr, "Missing popup timing hook");
    state->cancel();
    // Direct wheel delivery exercises the viewport without pressing Desktop mouse sources.
    const auto now = juce::Time::getCurrentTime();
    const auto source = juce::Desktop::getInstance().getMainMouseSource();
    const juce::Point<float> point(8.f, 8.f);
    const juce::MouseEvent event(source, point, juce::ModifierKeys(), 0.f, 0.f, 0.f, 0.f, 0.f,
                                 &viewport, &viewport, now, point, now, 0, false);
    juce::MouseWheelDetails wheel;
    wheel.deltaY = 1.f;
    wheel.isSmooth = true;
    const int beforeWheel = viewport.getViewPositionY();
    viewport.mouseWheelMove(event, wheel);
    expect(viewport.getViewPositionY() < beforeWheel && state->isPending(), "Wheel did not scroll real list and schedule return");
    expect(value(owner, "tuningRoot") == 11 && root.getSelectedIndex() == 11, "Browsing wheel changed parameter");
    wheel.isInertial = true;
    viewport.mouseWheelMove(event, wheel);
    expect(value(owner, "tuningRoot") == 11, "Inertial wheel changed parameter");
    viewport.setViewPosition(0, 0);
    const double clock = juce::Time::getMillisecondCounterHiRes() + 100000;
    state->scrolled(clock);
    root.advancePopupTimeForTesting(clock + 2499);
    expect(viewport.getViewPositionY() == 0, "Popup returned before idle deadline");
    state->scrolled(clock + 2000);
    root.advancePopupTimeForTesting(clock + 2500);
    expect(viewport.getViewPositionY() == 0, "Later wheel/inertia did not postpone popup return");
    set(owner, "tuningRoot", 9);
    expect(root.getSelectedIndex() == 9 && viewport.getViewPositionY() == 0, "Automation moved actively browsed viewport or missed dropdown");
    checkSelection(viewport, "tuningRoot", 12, 9);
    root.advancePopupTimeForTesting(clock + 4499);
    expect(viewport.getViewPositionY() == 0, "Popup returned early after automation");
    root.advancePopupTimeForTesting(clock + 4500);
    expect(viewport.getViewPositionY() == selectedViewY(viewport, "tuningRoot", 9), "Popup did not return to latest automated selection");
    expect(value(owner, "tuningRoot") == 9 && !state->isPending(), "Auto return wrote parameter or scheduled another return");

    viewport.setViewPosition(0, 0);
    state->inputPressed(17, clock + 5000);
    root.advancePopupTimeForTesting(clock + 15000);
    expect(viewport.getViewPositionY() == 0, "Actual popup returned during held drag");
    state->inputReleased(17, clock + 16000);
    root.advancePopupTimeForTesting(clock + 18499);
    expect(viewport.getViewPositionY() == 0, "Actual popup returned less than 2.5 seconds after drag release");
    root.advancePopupTimeForTesting(clock + 18500);
    expect(viewport.getViewPositionY() == selectedViewY(viewport, "tuningRoot", 9), "Actual popup did not return after held drag release");
    expect(value(owner, "tuningRoot") == 9, "Held drag/return changed parameter");
    auto& row = option(viewport, "tuningRoot", 4);
    auto choose = row.onClick;
    choose();
    expect(value(owner, "tuningRoot") == 4 && root.getSelectedIndex() == 4, "Actual option click did not select parameter");
    expect(!root.isPopupOpen(), "Option click did not close popup");
    root.closePopup(); // Also safe before the queued close callback runs.
    dispatch();
    auto& scale = dropdown(panel, "tuningScale");
    auto& scaleList = popupViewport(scale);
    checkList(scaleList, "tuningScale", 7);
    expect(option(scaleList, "tuningScale", 0).getButtonText() == "Chromatic"
           && option(scaleList, "tuningScale", 1).getButtonText() == "Major"
           && option(scaleList, "tuningScale", 6).getButtonText() == "Custom", "Dropdown changed existing named scale ordering");
    scale.closePopup();
}
void capturePopup(ChoiceDropdown& key, int theme) {
    key.selectIndex(9);
    auto& list = popupViewport(key);
    auto* popup = list.getParentComponent();
    expect(componentTheme(*popup) == themeFromInt(theme), "Native popup did not inherit owner theme");
    checkSelection(list, "tuningRoot", 12, 9);
    const auto palette = themePalette(themeFromInt(theme));
    expect(option(list, "tuningRoot", 9).findColour(juce::TextButton::buttonOnColourId) == palette.accent,
           "Selected popup option does not use current theme accent");
    auto file = juce::File::getCurrentWorkingDirectory().getChildFile("tuning-popup-offscreen-" + juce::String(theme) + ".png");
    file.deleteFile();
    juce::FileOutputStream stream(file);
    expect(stream.openedOk() && juce::PNGImageFormat().writeImageToStream(popup->createComponentSnapshot(popup->getLocalBounds()), stream),
           "Offscreen popup capture failed");
    std::cout << "OFFSCREEN (no physical monitor placement): " << file.getFullPathName() << '\n';
    key.closePopup();
}
void checkStandaloneDropdowns(Processor& owner) {
    // Validate the new control even while the panel replacements are being integrated.
    guiTestDisplay::enumerate();
    juce::Component host;
    host.setWantsKeyboardFocus(false); host.setMouseClickGrabsKeyboardFocus(false);
    host.setSize(700, 150);
    ChoiceDropdown root(owner, "tuningRoot", "Key", {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"});
    ChoiceDropdown scale(owner, "tuningScale", "Scale", {"Chromatic", "Major", "Natural Minor", "Harmonic Minor", "Major Pent.", "Minor Pent.", "Custom"});
    ChoiceDropdown range(owner, "tuningRange", "Vocal Range", {"Wide", "Low", "Mid", "High"});
    host.addAndMakeVisible(root); host.addAndMakeVisible(scale); host.addAndMakeVisible(range);
    root.setBounds(10, 10, 200, 70); scale.setBounds(230, 10, 200, 70); range.setBounds(450, 10, 230, 70);
    guiTestDisplay::attach(host);
    checkDropdownPopup(owner, host);
    for (int theme = 0; theme < 3; ++theme) {
        host.getProperties().set(vocalstackThemeProperty, theme);
        root.setNeon(theme == 1);
        capturePopup(root, theme);
    }
    host.removeFromDesktop();
}
void checkDropdownLifetime(Processor& owner) {
    std::cout << "CHECK popup owner lifetime\n";
    guiTestDisplay::enumerate();
    juce::Component host;
    host.setWantsKeyboardFocus(false); host.setMouseClickGrabsKeyboardFocus(false);
    host.setSize(260, 120);
    auto control = std::make_unique<ChoiceDropdown>(owner, "tuningRange", "Vocal Range", juce::StringArray{"Wide", "Low", "Mid", "High"});
    host.addAndMakeVisible(*control); control->setBounds(10, 10, 230, 70);
    guiTestDisplay::attach(host);
    auto& viewport = popupViewport(*control);
    juce::Component::SafePointer<juce::Component> popup(viewport.getParentComponent());
    control->setEnabled(false);
    expect(!control->isPopupOpen() && popup == nullptr, "Disabling dropdown left popup alive");
    control->setEnabled(true);
    popup = popupViewport(*control).getParentComponent();
    control->setSize(220, 70);
    expect(!control->isPopupOpen() && popup == nullptr, "Resizing dropdown left stale popup anchor");
    popup = popupViewport(*control).getParentComponent();
    const int desktops = juce::Desktop::getInstance().getNumComponents();
    // Leave a deferred row-close callback queued when the owner is destroyed.
    auto choose = option(*control->getPopupViewportForTesting(), "tuningRange", 3).onClick;
    choose();
    control.reset();
    expect(popup == nullptr && juce::Desktop::getInstance().getNumComponents() == desktops - 1, "Destroying dropdown left native popup alive");
    dispatch();
    set(owner, "tuningRange", 1); // The destroyed attachment must no longer receive updates.
    host.removeFromDesktop();
}
void run() {
    checkScrollReturnState();
    // Enumeration precedes every native attach; snapshots below are explicitly offscreen.
    guiTestDisplay::enumerate();
    auto owner = std::make_unique<Processor>();
    expect(owner->getName() == "HEAVEN by lil grass block", "Processor display name was not renamed");
    checkStandaloneDropdowns(*owner);
    checkDropdownLifetime(*owner);
    guiTestDisplay::enumerate();
    Editor editor(*owner);
    const auto work = guiTestDisplay::selected().userArea;
    editor.setSize(940, juce::jmin(780, work.getHeight()));
    guiTestDisplay::attach(editor);
    dispatch();
    std::cout << "CHECK open tuning\n";
    tab(editor, "Tuning").onClick();
    auto* panel = dynamic_cast<TuningPanel*>(find(editor, "tuningPanel"));
    expect(panel != nullptr && panel->isShowing(), "Tuning tab did not open");
    expect(!tab(editor, "Stack").getToggleState() && tab(editor, "Tuning").getToggleState(), "Tab selection mismatch");
    expect(!find(editor, "leadCard")->isShowing(), "Stack leaked into tuning view");
    auto& mix = slider(*panel, "tuningMix");
    expect(mix.getSliderStyle() == juce::Slider::LinearHorizontal, "Module Mix is not a real themed linear slider");
    expect(!mix.isEnabled(), "Disabled tuner leaves module mix enabled");
    auto* status = dynamic_cast<juce::Label*>(find(*panel, "tuningPitchReadout"));
    expect(status && status->getText().startsWith("TUNING OFF"), "Disabled tuner status is not OFF");
    owner->prepareToPlay(48000, 256);
    const int offLatency = owner->getLatencySamples();
    auto* enableButton = dynamic_cast<juce::TextButton*>(find(*panel, "tuningEnabled"));
    enableButton->triggerClick();
    for (int tick = 0; tick < 6; ++tick) { juce::Thread::sleep(30); dispatch(); }
    panel->sync();
    expect(value(*owner, "tuningEnabled") == 1, "Tuning On click did not reach backend");
    expect(mix.isEnabled(), "Enabled tuner leaves module mix disabled");
    expect(owner->tuningActivationPending(), "First enable did not wait for host reactivation");
    expect(status->getText().startsWith("ACTIVATING  /  Waiting for host audio reactivation")
           && status->getText().contains("Audio reactivation requested automatically"),
           "Pending activation does not explain automatic host reactivation");
    expect(!status->getText().contains("ACTIVE  /") && !status->getText().containsIgnoreCase("restart"),
           "Pending tuner claims active processing or shows a combined restart warning");
    expect(status->getText().contains("TARGET  --") && status->getText().contains("CORRECTION  --"),
           "Pending activation invents tuning telemetry");
    expect(owner->getLatencySamples() == offLatency, "Activation intent prematurely changed host latency");
    // This fixture has no host wrapper; emulate its release/prepare acknowledgement.
    owner->releaseResources();
    owner->prepareToPlay(48000, 256);
    panel->sync();
    const int onLatency = owner->getLatencySamples();
    expect(!owner->tuningActivationPending() && onLatency == offLatency + 1344,
           "Host preparation did not commit the tuning reservation");
    expect(status->getText().startsWith("ACTIVE  /  Waiting for a voiced signal")
           && !status->getText().contains("Audio reactivation requested automatically"),
           "Prepared unvoiced tuner is not ACTIVE or retains activation instructions");
    set(*owner, "tuningEnabled", 0); panel->sync();
    expect(!owner->tuningActivationPending() && status->getText().startsWith("TUNING OFF")
           && !status->getText().contains("reactivation") && !mix.isEnabled(),
           "Turning off tuning leaves activation pending or live controls enabled");
    expect(owner->getLatencySamples() == onLatency, "Turning off prematurely reclaimed tuning latency");
    set(*owner, "tuningEnabled", 1); panel->sync();
    expect(!owner->tuningActivationPending() && owner->getLatencySamples() == onLatency
           && status->getText().startsWith("ACTIVE  /  Waiting for a voiced signal") && mix.isEnabled(),
           "Re-enabling a reserved tuner unnecessarily waits for host reactivation");
    auto* on = find(*panel, "tuningEnabled");
    auto* mixControl = find(*panel, "tuningMixControl");
    expect(on->getRight() <= mixControl->getX() && on->getY() < mixControl->getBottom(), "Module Mix is not beside Tuning On");
    std::cout << "CHECK parameter attachments\n";
    for (const auto* id : {"tuningRoot", "tuningScale", "tuningRange"}) {
            auto& control = dropdown(*panel, id);
            const int maximum = juce::String(id) == "tuningRoot" ? 11 : juce::String(id) == "tuningScale" ? 6 : 3;
            int selections = 0;
            control.onSelection = [&](int) { ++selections; };
            for (int index = 0; index <= maximum; ++index) {
                set(*owner, id, float((index + 1) % (maximum + 1)));
                const int before = selections;
                control.selectIndex(index);
                expect(value(*owner, id) == float(index) && control.getSelectedIndex() == index, "Dropdown UI to backend index mapping failed");
                expect(selections == before + 1, "Changed selection did not notify once");
                control.selectIndex(index); control.selectIndex(-1); control.selectIndex(maximum + 1);
                expect(selections == before + 1 && value(*owner, id) == float(index), "Unchanged/invalid selection wrote parameter or notified");
            }
            const int before = selections;
            for (int index = maximum; index >= 0; --index) {
                set(*owner, id, float(index));
                expect(control.getSelectedIndex() == index, "Reverse backend automation did not reach dropdown");
            }
            expect(selections == before, "Automation invoked user selection callback");
            control.onSelection = nullptr;
        }
        checkDropdownPopup(*owner, *panel);
        for (const auto* id : {"tuningReference", "tuningAmount", "tuningRetune", "tuningTransition", "tuningStability"}) {
        auto& control = slider(*panel, id);
        expect(control.getSliderStyle() == juce::Slider::RotaryHorizontalVerticalDrag, "Setting is not a real rotary control");
        const float v = float(control.getMinimum() + (control.getMaximum() - control.getMinimum()) * .6);
        control.setValue(v, juce::sendNotificationSync);
        expect(std::abs(value(*owner, id) - float(control.getValue())) < .01f, "UI to backend attachment failed");
        set(*owner, id, float(control.getMaximum()));
        expect(std::abs(control.getValue() - control.getMaximum()) < .01, "Backend automation did not reach UI");
    }
    expect(slider(*panel, "tuningAmount").getTextFromValue(1).contains("100"), "Correction displays normalized value instead of percent");
    expect(slider(*panel, "tuningReference").getTextFromValue(440).contains("440.0 Hz"), "Reference loses precision/units");
    std::cout << "CHECK notes/reset/focus\n";
    set(*owner, "tuningRoot", 0); set(*owner, "tuningScale", 1); set(*owner, "tuningNotes", 4095); panel->sync();
    expect(!dynamic_cast<juce::TextButton*>(find(*panel, "tuningNote1"))->getToggleState(), "Major scale note display incorrect");
    click(*panel, "tuningNote1");
    expect(value(*owner, "tuningScale") == 6 && int(value(*owner, "tuningNotes")) == (0xab5 | 2), "Custom note edit did not preserve effective scale");
    set(*owner, "tuningNotes", 0); panel->sync();
    expect(status && status->getText().contains("No allowed notes"), "Empty mask not reported honestly");
    expect(status->getText().contains("TARGET  --"), "Unvoiced monitor invents pitch data");
    set(*owner, "leadTuneMix", .37f); set(*owner, "voice8TuneMix", .63f);
    click(*panel, "resetTuning");
    expect(std::abs(value(*owner, "leadTuneMix") - .37f) < .001f && std::abs(value(*owner, "voice8TuneMix") - .63f) < .001f,
           "Module reset changed per-voice settings");
    panel->sync(); expect(!mix.isEnabled(), "Module reset did not restore disabled state");
    expect(!owner->tuningActivationPending() && status->getText().startsWith("TUNING OFF"),
           "Module reset left tuning activation pending");
    expect(owner->getLatencySamples() == onLatency, "Module reset prematurely reclaimed tuning reservation");
    owner->releaseResources();
    owner->prepareToPlay(48000, 256);
    panel->sync();
    expect(owner->getLatencySamples() == offLatency && !owner->tuningActivationPending(),
           "Next OFF preparation did not reclaim tuning reservation");
    checkBounds(*panel); checkPassive(*panel);

    std::cout << "CHECK voice mixes\n";
    for (int slot = -1; slot < 8; ++slot) {
        VoiceControls voice(*owner, slot, VoiceControls::ControlMode::stackSliders);
        voice.setCompact(true); voice.setSize(172, 278);
        const auto id = slot < 0 ? juce::String("leadTuneMix") : "voice" + juce::String(slot + 1) + "TuneMix";
        auto& control = slider(voice, id.toRawUTF8());
        control.setValue(.42, juce::sendNotificationSync);
        expect(std::abs(value(*owner, id.toRawUTF8()) - .42f) < .001f, "Voice mix is not attached");
        checkBounds(voice); checkPassive(voice);
        VoiceControls inspector(*owner, slot);
        inspector.setSize(440, 278); checkBounds(inspector);
    }

    std::cout << "CHECK real audio telemetry\n";
    // Feed actual audio through the processor, never fabricated UI telemetry.
    set(*owner, "tuningEnabled", 1); set(*owner, "tuningNotes", 4095);
    owner->releaseResources();
    owner->prepareToPlay(48000, 256);
    juce::AudioBuffer<float> audio(2, 256);
    juce::MidiBuffer midi;
    for (int block = 0; block < 160; ++block) {
        for (int i = 0; i < 256; ++i) {
            const float sample = .2f * std::sin(float((block * 256 + i) * juce::MathConstants<double>::twoPi * 220. / 48000.));
            for (int c = 0; c < 2; ++c) audio.setSample(c, i, sample);
        }
        owner->processBlock(audio, midi);
    }
    panel->sync();
    const auto telemetry = owner->getTuningTelemetry();
    expect(telemetry.voiced && telemetry.hasTarget, "Real telemetry test did not detect input");
    expect(status->getText().contains(juce::String(telemetry.detectedHz, 1) + " Hz"), "Monitor does not display processor telemetry");
    owner->releaseResources();

    std::cout << "CHECK three-theme offscreen snapshots\n";
    for (int theme = 0; theme < 3; ++theme) {
        owner->setSkin(theme);
        juce::Thread::sleep(40); dispatch();
        expect(componentTheme(editor) == themeFromInt(theme), "Editor did not apply theme");
        checkBounds(*panel); checkPassive(*panel);
        capturePopup(dropdown(*panel, "tuningRoot"), theme);
        const auto image = panel->createComponentSnapshot(panel->getLocalBounds(), true, 1.0f);
        auto file = juce::File::getCurrentWorkingDirectory().getChildFile("tuning-ui-offscreen-" + juce::String(theme) + ".png");
        file.deleteFile();
        juce::FileOutputStream stream(file);
        expect(stream.openedOk() && juce::PNGImageFormat().writeImageToStream(image, stream), "Offscreen capture failed");
        std::cout << "OFFSCREEN (no physical monitor placement): " << file.getFullPathName() << '\n';
        VoiceControls card(*owner, -1, VoiceControls::ControlMode::stackSliders);
        card.getProperties().set(vocalstackThemeProperty, theme);
        card.setCompact(true); card.setSize(172, 278); card.setNeon(theme == 1);
        expect(slider(card, "leadTuneMix").getHeight() >= 26 && slider(card, "leadPitch").getHeight() >= 26,
               "Compact card reclaimed slider track instead of label padding");
        const auto cardImage = card.createComponentSnapshot(card.getLocalBounds());
        auto cardFile = juce::File::getCurrentWorkingDirectory().getChildFile("tuning-voice-offscreen-" + juce::String(theme) + ".png");
        cardFile.deleteFile();
        juce::FileOutputStream cardStream(cardFile);
        expect(cardStream.openedOk() && juce::PNGImageFormat().writeImageToStream(cardImage, cardStream), "Voice snapshot failed");
        auto editorFile = juce::File::getCurrentWorkingDirectory().getChildFile("tuning-editor-offscreen-" + juce::String(theme) + ".png");
        editorFile.deleteFile();
        juce::FileOutputStream editorStream(editorFile);
        expect(editorStream.openedOk() && juce::PNGImageFormat().writeImageToStream(editor.createComponentSnapshot(editor.getLocalBounds()), editorStream), "Editor snapshot failed");
    }
    auto& key = dropdown(*panel, "tuningRoot");
    auto& openList = popupViewport(key);
    juce::Component::SafePointer<juce::Component> hiddenPopup(openList.getParentComponent());
    tab(editor, "Routing").onClick();
    dispatch();
    expect(!key.isPopupOpen() && hiddenPopup == nullptr, "Hidden tuning panel retained native popup");
    expect(!panel->isVisible() && tab(editor, "Routing").getToggleState(), "Routing behavior changed");
    tab(editor, "Stack").onClick();
    expect(!panel->isVisible() && find(editor, "leadCard")->isShowing(), "Stack did not restore");
    expect(!static_cast<juce::Component&>(editor).keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)), "Editor consumes host transport key");
    editor.removeFromDesktop();
}
}
int main() {
    std::cout << std::unitbuf;
    juce::ScopedJuceInitialiser_GUI gui;
    try { run(); std::cout << "Tuning UI checks passed\n"; return 0; }
    catch (const std::exception& e) { std::cerr << "Tuning UI failure: " << e.what() << '\n'; return 1; }
}
