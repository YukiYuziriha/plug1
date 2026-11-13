#pragma once

#include "DoublerPanel.h"
#include "CreativeEffectPanel.h"
#include "UtilityEffectPanel.h"
#include <array>
#include <memory>
#include <stdexcept>
#include <vector>

// Include at file scope and call run() on the message thread after GUI init.
namespace vocalstack::hoverWheelRegressionTests {
inline void require(bool condition, const juce::String& message) {
    if (!condition) throw std::runtime_error(message.toStdString());
}
inline void wheelOver(juce::Component& target, float deltaY, float deltaX = 0, int modifiers = 0) {
    const auto point = target.getLocalBounds().getCentre().toFloat();
    const auto now = juce::Time::getCurrentTime();
    const juce::MouseEvent event{juce::Desktop::getInstance().getMainMouseSource(), point,
        juce::ModifierKeys(modifiers), 1, 0, 0, 0, 0, &target, &target, now, point, now, 0, false};
    juce::MouseWheelDetails wheel{};
    wheel.deltaY = deltaY;
    wheel.deltaX = deltaX;
    target.mouseWheelMove(event, wheel);
}
struct ParameterChanges final : juce::AudioProcessorParameter::Listener {
    explicit ParameterChanges(Processor& p) : owner(p) {
        for (auto* parameter : owner.getParameters()) {
            values.push_back(parameter->getValue());
            parameter->addListener(this);
        }
    }
    ~ParameterChanges() override {
        for (auto* parameter : owner.getParameters()) parameter->removeListener(this);
    }
    void parameterValueChanged(int, float) override { ++changes; }
    void parameterGestureChanged(int, bool) override { ++gestures; }
    void check() {
        require(changes == 0 && gestures == 0, "Hover wheel emitted parameter automation");
        size_t index = 0;
        for (auto* parameter : owner.getParameters())
            require(parameter->getValue() == values[index++], "Hover wheel changed a parameter");
    }
    Processor& owner;
    std::vector<float> values;
    int changes = 0, gestures = 0;
};
struct Fixture {
    std::unique_ptr<Processor> owner = std::make_unique<Processor>();
    juce::Component root, outerContent, innerContent, visual;
    juce::Label header;
    PassiveViewport outer, inner;
    ParameterKnob knob{*owner, "voice1Gain", "Gain", "dB"};
    detail::ParameterSlider linear{*owner, "voice1Pan", "Pan", ""};
    VoiceControls voice{*owner, 0, VoiceControls::ControlMode::stackSliders};
    ChoralePanel chorale{*owner};
    Fixture() {
        root.setSize(600, 400);
        root.addAndMakeVisible(outer);
        outer.setBounds(root.getLocalBounds());
        outerContent.setSize(580, 1800);
        outer.setViewedComponent(&outerContent, false);
        outerContent.addAndMakeVisible(inner);
        inner.setBounds(20, 20, 500, 300);
        innerContent.setSize(900, 1200);
        inner.setViewedComponent(&innerContent, false);
        innerContent.addAndMakeVisible(visual);
        visual.setBounds(300, 80, 100, 20);
        innerContent.addAndMakeVisible(header);
        header.setText("Section header", juce::dontSendNotification);
        header.setBounds(300, 105, 100, 20);
        innerContent.addAndMakeVisible(knob);
        knob.setBounds(10, 10, 120, 100);
        innerContent.addAndMakeVisible(linear);
        linear.setBounds(140, 10, 300, 60);
        innerContent.addAndMakeVisible(voice);
        voice.setBounds(10, 120, 460, 280);
        innerContent.addAndMakeVisible(chorale);
        chorale.setBounds(10, 420, 460, 64);
    }
    ~Fixture() {
        inner.setViewedComponent(nullptr, false);
        outer.setViewedComponent(nullptr, false);
    }
    void reset() { inner.setViewPosition(0, 0); outer.setViewPosition(0, 0); }
};
inline void sliderTextBoxFactory() {
    juce::LookAndFeel_V4 base;
    juce::Slider slider;
    slider.setColour(juce::Slider::textBoxTextColourId, juce::Colours::orange);
    slider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::darkblue);
    std::unique_ptr<juce::Label> expected(base.createSliderTextBox(slider));
    std::unique_ptr<juce::Label> actual(createPassiveSliderTextBox(base.createSliderTextBox(slider)));
    for (int colour : {int(juce::Label::textColourId), int(juce::Label::backgroundColourId),
                       int(juce::Label::outlineColourId), int(juce::TextEditor::textColourId),
                       int(juce::TextEditor::backgroundColourId), int(juce::TextEditor::outlineColourId),
                       int(juce::TextEditor::highlightColourId)})
        require(actual->findColour(colour) == expected->findColour(colour), "Passive label changed JUCE colours");
    require(actual->getFont() == expected->getFont(), "Passive label changed JUCE font");

    require(actual->getJustificationType() == expected->getJustificationType(),
            "Passive label changed JUCE justification");
    // These standalone labels have no native peer, so getAccessibilityHandler()
    // would return null without exercising either label's accessibility factory.
    auto expectedAccessibility = expected->createAccessibilityHandler();
    auto actualAccessibility = actual->createAccessibilityHandler();
    require(expectedAccessibility != nullptr && actualAccessibility != nullptr
                && actualAccessibility->getRole() == expectedAccessibility->getRole(),
            "Passive label changed JUCE accessibility role");

    // Model Editor::Style using the same shared factory, independently of
    // ParameterKnob::Style, so the coordinator's call pattern stays covered.
    struct EditorStyle final : juce::LookAndFeel_V4 {
        juce::Label* createSliderTextBox(juce::Slider& target) override {
            return createPassiveSliderTextBox(juce::LookAndFeel_V4::createSliderTextBox(target));
        }
    } style;
    ModulationSlider control;
    control.setLookAndFeel(&style);
    control.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 68, 19);
    control.setSize(120, 100);
    control.setTextBoxIsEditable(true);
    require(!control.getWantsKeyboardFocus(), "Shared factory made idle slider focusable");
    control.showTextBox();
    juce::Label* valueLabel = nullptr;
    for (auto* child : control.getChildren())
        if (auto* label = dynamic_cast<juce::Label*>(child)) valueLabel = label;
    require(valueLabel != nullptr && valueLabel->getCurrentTextEditor() != nullptr,
            "Shared factory disabled deliberate numeric entry");
    require(valueLabel->getCurrentTextEditor()->getWantsKeyboardFocus(),
            "Shared factory disabled deliberate text focus");
    control.hideTextBox(true);
    require(valueLabel->getCurrentTextEditor() == nullptr, "Cancelled entry left its editor open");
    control.setLookAndFeel(nullptr);
}
inline void fxHoveredSurfaces() {
    auto owner = std::make_unique<Processor>();
    for (int theme : {0, 1, 2}) for (int width : {280, 940}) {
        juce::Component root, content;
        PassiveViewport outer;
        root.setSize(width + 20, 220); root.setVisible(true);
        root.addAndMakeVisible(outer); outer.setBounds(root.getLocalBounds());
        content.setSize(width, 1800); outer.setViewedComponent(&content, false);
        CreativeEffectPanel creative(*owner, 1, CreativeKind::reverb);
        UtilityEffectPanel utility(*owner, 2, UtilityKind::stereo);
        ParameterChanges parameters(*owner);
        const auto exercise = [&](auto& panel, const juce::String& knobId, const juce::String& toggleId,
                                  const juce::String& mixId) {
            panel.getProperties().set(vocalstackThemeProperty, theme);
            panel.setNeon(theme == 1);
            content.addAndMakeVisible(panel); panel.setBounds(0, 0, width, 180);
            const auto find = [](auto&& self, juce::Component& component, const juce::String& id) -> juce::Component* {
                if (component.getComponentID() == id) return &component;
                for (auto* child : component.getChildren()) if (auto* found = self(self, *child, id)) return found;
                return nullptr;
            };
            const auto dispatch = [&](juce::Component& surface, juce::Component& expected) {
                outer.setViewPosition(0, 0);
                const auto point = root.getLocalPoint(&surface, surface.getLocalBounds().getCentre());
                auto* hit = root.getComponentAt(point);
                require(hit == &expected, "FX wheel point missed its actual rendered target: " + expected.getComponentID());
                auto* inner = hit->findParentComponentOfClass<juce::Viewport>();
                require(inner && inner != &outer, "FX surface lost its nested viewport");
                const auto before = inner->getViewPosition();
                const auto focus = juce::Component::getCurrentlyFocusedComponent();
                const auto local = hit->getLocalPoint(&root, point).toFloat();
                const auto now = juce::Time::getCurrentTime();
                const juce::MouseEvent event{juce::Desktop::getInstance().getMainMouseSource(), local,
                    {}, 1, 0, 0, 0, 0, hit, hit, now, local, now, 0, false};
                juce::MouseWheelDetails wheel{}; wheel.deltaY = before.y > 0 ? .3f : -.3f;
                hit->mouseWheelMove(event, wheel);
                require(inner->getViewPositionX() == before.x
                            && (before.y > 0 ? inner->getViewPositionY() < before.y : inner->getViewPositionY() > before.y),
                        "Hovered FX surface did not scroll its nearest viewport");
                require(outer.getViewPosition() == juce::Point<int>(), "FX wheel also scrolled the Stack ancestor");
                require(juce::Component::getCurrentlyFocusedComponent() == focus, "FX hover wheel captured host keys");
                parameters.check();
            };
            for (const auto& id : {knobId, mixId}) {
                auto* slider = dynamic_cast<juce::Slider*>(find(find, panel, id));
                require(slider != nullptr, "Missing FX wheel slider: " + id);
                require(slider->getSliderStyle() == (id == mixId ? juce::Slider::LinearHorizontal
                                                               : juce::Slider::RotaryHorizontalVerticalDrag),
                        "FX wheel fixture has the wrong control mode");
                auto* controlRow = slider->getParentComponent();
                require(slider->getWidth() >= 64 && controlRow->getHeight() == (id == mixId ? 44 : 100),
                        "FX wheel target lost its readable compact control geometry: " + id);
                require((id == mixId || controlRow->getWidth() == 68)
                            && bool(slider->getProperties()["vocalstackTexturedBar"]) == (id == mixId),
                        "Only the long FX Mix wheel target should use the textured bar: " + id);
                juce::Label* value = nullptr;
                for (auto* child : slider->getChildren()) if (auto* label = dynamic_cast<juce::Label*>(child)) value = label;
                require(value != nullptr, "Missing native numeric wheel target");
                if (id == mixId) {
                    const auto layout = slider->getLookAndFeel().getSliderLayout(*slider);
                    const auto bar = ParameterKnob::Style::texturedBarBounds(layout.sliderBounds.toFloat()).toNearestInt();
                    require(layout.sliderBounds.getHeight() == slider->getHeight() && bar.getHeight() == 25
                                && bar.getY() >= 8 && bar.contains(layout.textBoxBounds)
                                && value->getBounds() == layout.textBoxBounds,
                            "FX Mix display-only caption must sit inside its 25px bar with a modulation lane");
                    require(!slider->isTextBoxEditable() && !value->isEditable(),
                            "FX Mix caption must not allow numeric entry");
                } else require(slider->getTextBoxPosition() == juce::Slider::TextBoxAbove,
                               "Rotary numeric wheel target must remain above the knob");
                panel.revealParameter(id); dispatch(*value, id == mixId ? static_cast<juce::Component&>(*slider) : *value);
                require(value->getCurrentTextEditor() == nullptr, "Hover wheel opened numeric entry");
                auto* row = slider->getParentComponent();
                juce::Label* title = nullptr;
                for (auto* child : row->getChildren())
                    if (auto* label = dynamic_cast<juce::Label*>(child); label && label->isVisible()) title = label;
                if (id == mixId) {
                    require(title == nullptr, "Compact FX Mix must hide its external title");

                } else {
                    require(title && title->getY() >= slider->getBottom(), "Missing label-below rotary wheel surface");
                    panel.revealParameter(id); dispatch(*title, *row);
                }
            }
            auto* toggle = dynamic_cast<juce::Button*>(find(find, panel, toggleId));
            require(toggle && toggle->getWidth() >= 24 && toggle->getWidth() <= 64
                        && toggle->getHeight() >= 24 && toggle->getHeight() <= 28,
                    "Missing usable compact FX toggle wheel target");
            panel.revealParameter(toggleId); dispatch(*toggle, *toggle);
            panel.setVisible(false);
        };
        exercise(creative, creativeParameterId(1, size_t(CreativeParameter::reverbDecay)),
                 creativeParameterId(1, size_t(CreativeParameter::reverbFreeze)), "fx1Mix");
        exercise(utility, utilityParameterId(2, size_t(UtilityParameter::stereoWidth)),
                 utilityParameterId(2, size_t(UtilityParameter::stereoSwap)), "fx2Mix");
        outer.setViewedComponent(nullptr, false);
    }
}
inline void run() {
    sliderTextBoxFactory();
    fxHoveredSurfaces();
    Fixture f;
    ParameterChanges parameters(*f.owner);
    const auto focus = juce::Component::getCurrentlyFocusedComponent();
    int selections = 0;
    f.voice.onSelect = [&] { ++selections; };
    auto checkPassive = [&] {
        parameters.check();
        require(selections == 0, "Hover wheel selected a voice as if clicked");
        require(juce::Component::getCurrentlyFocusedComponent() == focus, "Hover wheel stole keyboard focus");
    };
    auto innerOnly = [&](juce::Component& target) {
        f.reset();
        wheelOver(target, -.3f);
        require(f.inner.getViewPositionX() == 0, "Vertical wheel moved the horizontal axis");
        require(f.inner.getViewPositionY() > 0, "Hovered passive control did not scroll its nearest viewport: "
                        + target.getName() + " / " + target.getComponentID() + " / " + typeid(target).name());
        require(f.outer.getViewPositionY() == 0, "One wheel event scrolled both viewports");
        checkPassive();
    };
    innerOnly(f.knob.slider);
    innerOnly(f.visual);
    innerOnly(f.header);
    innerOnly(f.inner);
    // A non-intercepting title hit-tests to its wrapper; the editable value
    // label forwards without opening its transient text editor.
    auto* titleTarget = f.knob.getComponentAt(40, 8);
    require(titleTarget != nullptr, "Missing knob title hit target");
    innerOnly(*titleTarget);
    bool foundValueLabel = false;
    for (auto* child : f.knob.slider.getChildren()) if (dynamic_cast<juce::Label*>(child)) {
        foundValueLabel = true;
        require(!child->getWantsKeyboardFocus(), "Idle value label is focusable");
        innerOnly(*child);
    }
    require(foundValueLabel, "Missing slider value label");
    innerOnly(f.linear.slider);
    innerOnly(f.linear);
    for (auto* child : f.linear.slider.getChildren())
        if (dynamic_cast<juce::Label*>(child)) innerOnly(*child);
    for (auto* child : f.voice.getChildren()) if (auto* button = dynamic_cast<juce::Button*>(child))
        innerOnly(*button);
    for (auto* child : f.chorale.getChildren()) if (auto* button = dynamic_cast<juce::Button*>(child))
        innerOnly(*button);
    f.knob.setEnabled(false);
    innerOnly(f.knob);
    f.knob.setEnabled(true);

    // Plain visuals and headers must reach PassiveViewport through JUCE's
    // normal Component bubbling, rather than relying on a control override.
    const std::array<juce::Component*, 4> targets{&f.knob.slider, &f.visual, &f.header, &f.inner};
    for (auto* target : targets) {
        f.reset();
        f.inner.setViewPosition(0, 100000);
        const auto bottom = f.inner.getViewPosition();
        f.inner.setViewPosition(0, bottom.y - 1);
        wheelOver(*target, -.3f);
        require(f.inner.getViewPosition() == bottom && f.outer.getViewPosition() == juce::Point<int>(),
                "Wheel reaching the inner edge also scrolled its ancestor");
        checkPassive();
        wheelOver(*target, -.3f);
        require(f.inner.getViewPosition() == bottom && f.outer.getViewPositionY() > 0,
                "Nested bottom edge failed to hand wheel to ancestor");
        f.outer.setViewPosition(0, 100000);
        const auto outerBottom = f.outer.getViewPosition();
        wheelOver(*target, -.3f);
        require(f.inner.getViewPosition() == bottom && f.outer.getViewPosition() == outerBottom,
                "Wheel moved a clamped viewport or converted vertical motion to horizontal");
        f.inner.setViewPosition(0, 0);
        f.outer.setViewPosition(0, 200);
        wheelOver(*target, .3f);
        require(f.inner.getViewPositionY() == 0 && f.outer.getViewPositionY() < 200,
                "Nested top edge failed to hand wheel to ancestor");
        checkPassive();
    }

    // A horizontal inner viewport must not divert a vertical ancestor gesture.
    f.innerContent.setSize(900, 100);
    for (auto* target : targets) {
        f.reset();
        wheelOver(*target, -.3f);
        require(f.inner.getViewPosition() == juce::Point<int>() && f.outer.getViewPositionY() > 0,
                "Horizontal inner row diverted vertical Stack scrolling");
        f.outer.setViewPosition(0, 100000);
        const auto bottom = f.outer.getViewPosition();
        wheelOver(*target, -.3f);
        require(f.inner.getViewPosition() == juce::Point<int>() && f.outer.getViewPosition() == bottom,
                "Vertical Stack edge unexpectedly scrolled a horizontal row");
        checkPassive();
    }
    for (auto* target : targets) {
        f.reset();
        wheelOver(*target, -.3f, -.2f);
        require(f.inner.getViewPosition() == juce::Point<int>() && f.outer.getViewPositionY() > 0,
                "Diagonal wheel bypassed vertical ancestor priority");
        checkPassive();
        for (int modifiers : {0, int(juce::ModifierKeys::shiftModifier)}) {
            const auto deltaY = modifiers == 0 ? 0.0f : -.3f;
            const auto deltaX = modifiers == 0 ? -.3f : 0.0f;
            f.reset();
            wheelOver(*target, deltaY, deltaX, modifiers);
            require(f.inner.getViewPositionX() > 0 && f.inner.getViewPositionY() == 0
                        && f.outer.getViewPosition() == juce::Point<int>(),
                    "Horizontal or Shift-wheel failed to scroll only the nearest horizontal viewport");
            f.inner.setViewPosition(100000, 0);
            const auto rightEdge = f.inner.getViewPosition();
            f.outer.setViewPosition(0, 200);
            wheelOver(*target, deltaY, deltaX, modifiers);
            require(f.inner.getViewPosition() == rightEdge && f.outer.getViewPosition() == juce::Point<int>(0, 200),
                    "Horizontal edge gesture unexpectedly moved a vertical ancestor");
            checkPassive();
        }
    }

    f.innerContent.setSize(900, 1200);
    f.reset();
    f.knob.slider.showTextBox();
    juce::Label* valueLabel = nullptr;
    for (auto* child : f.knob.slider.getChildren())
        if (auto* label = dynamic_cast<juce::Label*>(child)) valueLabel = label;
    require(valueLabel != nullptr && valueLabel->getCurrentTextEditor() != nullptr,
            "Parameter value label cannot open deliberate entry");
    auto* entry = valueLabel->getCurrentTextEditor();
    entry->setText("-3.25", false);
    const auto entryFocus = juce::Component::getCurrentlyFocusedComponent();
    wheelOver(f.visual, -.3f);
    require(valueLabel->getCurrentTextEditor() == entry && entry->getText() == "-3.25"
                && juce::Component::getCurrentlyFocusedComponent() == entryFocus,
            "Background wheel disturbed deliberate text entry or focus");
    parameters.check();
    f.knob.slider.hideTextBox(true);
    checkPassive();

    f.innerContent.setSize(100, 100);
    f.reset();
    wheelOver(f.knob.slider, -.3f);
    require(f.outer.getViewPositionY() > 0, "Non-scrollable inner viewport swallowed wheel");
    f.outerContent.setSize(100, 100);
    f.reset();
    wheelOver(f.knob.slider, -.3f);
    require(f.outer.getViewPosition() == juce::Point<int>() && f.inner.getViewPosition() == juce::Point<int>(),
            "Wheel moved content without overflow");
    checkPassive();
}
} // namespace vocalstack::hoverWheelRegressionTests
