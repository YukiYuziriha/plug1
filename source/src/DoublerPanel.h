#pragma once

#include "PluginProcessor.h"
#include "VoiceMonitor.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace vocalstack {
namespace detail {
inline void passiveControl(juce::Component& component) {
    if (dynamic_cast<juce::TextEditor*>(&component) != nullptr) return;
    component.setWantsKeyboardFocus(false);
    component.setMouseClickGrabsKeyboardFocus(false);
    for (auto* child : component.getChildren()) passiveControl(*child);
}

// Signed tracks start at the actual zero value (gain is not symmetric).
class ParameterLookAndFeel final : public juce::LookAndFeel_V4 {
public:
    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float, float, juce::Slider::SliderStyle,
                          juce::Slider& slider) override {
        const auto cy = float(y) + float(height) * 0.5f;
        const bool signedTrack = slider.getMinimum() < 0 && slider.getMaximum() > 0;
        const float origin = float(x) + (signedTrack ? float(slider.valueToProportionOfLength(0)) * float(width) : 0.0f);
        g.setColour(juce::Colour(0xff46515b));
        g.drawLine(float(x), cy, float(x + width), cy, 3.0f);
        g.setColour(slider.findColour(juce::Slider::trackColourId));
        g.drawLine(origin, cy, sliderPos, cy, 3.0f);
        if (signedTrack) {
            g.setColour(juce::Colour(0xffd7dfe6));
            g.drawLine(origin, cy - 5, origin, cy + 5, 1.0f);
        }
        g.setColour(slider.findColour(juce::Slider::thumbColourId));
        g.fillEllipse(sliderPos - 4, cy - 4, 8, 8);
    }
};

inline void styleSlider(juce::Slider& slider, bool neon) {
    slider.setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xffe4e9ee));
    slider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff20262d));
    slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0xff526071));
    slider.setColour(juce::Slider::trackColourId, juce::Colour(neon ? 0xffff4ca0 : 0xfff5a24b));
    slider.setColour(juce::Slider::thumbColourId, juce::Colour(neon ? 0xff64f5e3 : 0xffe4e4df));
    passiveControl(slider);
}

class ParameterSlider final : public juce::Component {
public:
    ParameterSlider(Processor& owner, const juce::String& id, const juce::String& title, const char* unit) {
        label.setText(title, juce::dontSendNotification);
        label.setFont(juce::Font(juce::FontOptions(title == "PITCH" ? 13.0f : 11.5f,
                                                        title == "PITCH" ? juce::Font::bold : juce::Font::plain)));
        label.setColour(juce::Label::textColourId, juce::Colour(0xffe4e9ee));
        label.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(label);
        slider.setLookAndFeel(&style);
        slider.setComponentID(id);
        slider.setName(title);
        slider.setTitle(title);
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 76, 23);
        slider.setScrollWheelEnabled(false);
        addAndMakeVisible(slider);
        link = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(owner.parameters, id, slider);
        if (const auto* parameter = owner.parameters.getParameter(id))
            slider.setDoubleClickReturnValue(true, parameter->convertFrom0to1(parameter->getDefaultValue()));
        const auto suffix = juce::String(unit).isEmpty() ? juce::String() : juce::String(" ") + unit;
        const int decimals = juce::String(unit) == "%" ? 0 : 2;
        slider.textFromValueFunction = [suffix, decimals](double value) { return juce::String(value, decimals) + suffix; };
        slider.valueFromTextFunction = [](const juce::String& value) { return value.getDoubleValue(); };
        if (title == "Pan") {
            slider.textFromValueFunction = [](double v) {
                return std::abs(v) < 0.005 ? juce::String("C") : juce::String(std::abs(v) * 100, 0) + (v < 0 ? " L" : " R");
            };
            slider.valueFromTextFunction = [](const juce::String& value) {
                if (value.containsIgnoreCase("C")) return 0.0;
                const double v = value.getDoubleValue();
                if (value.containsIgnoreCase("L")) return -std::abs(v) / 100;
                if (value.containsIgnoreCase("R")) return std::abs(v) / 100;
                return v;
            };
        }
        slider.updateText();
        passiveControl(*this);
    }
    ~ParameterSlider() override { slider.setLookAndFeel(nullptr); }
    void resized() override {
        auto area = getLocalBounds();
        label.setBounds(area.removeFromLeft(70));
        slider.setBounds(area);
    }
    void setNeon(bool neon) { styleSlider(slider, neon); }
    juce::Slider slider;
private:
    ParameterLookAndFeel style;
    juce::Label label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> link;
};
} // namespace detail

class DoublerPanel final : public juce::Component {
    class VoiceCard final : public juce::Component {
    public:
        VoiceCard(Processor& owner, size_t index)
            : voiceSlot(index), pitch(owner, voicePitchParameterId(index), "PITCH", "st") {
            enabled.setComponentID(voiceParameterId(index, 0));
            enabled.setButtonText("On");
            enabled.setName("Enable voice " + juce::String(int(index + 1)));
            enabled.setClickingTogglesState(true);
            enabled.setTooltip("Mute this voice without removing its card or settings");
            addAndMakeVisible(enabled);
            link = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(owner.parameters, voiceParameterId(index, 0), enabled);
            addAndMakeVisible(pitch);
            pitch.slider.setTooltip("Musical interval: -36 to +36 semitones; independent of Fine Detune. Fractional intervals supported");
            const std::array<const char*, 7> names{"Gain", "Pan", "Delay", "Fine detune", "Formant", "Depth", "Rate"};
            const std::array<const char*, 7> units{"dB", "", "ms", "ct", "st", "x", "Hz"};
            for (size_t f = 0; f < fields.size(); ++f) {
                fields[f] = std::make_unique<detail::ParameterSlider>(owner, voiceParameterId(index, int(f + 1)), names[f], units[f]);
                addAndMakeVisible(*fields[f]);
            }
            fields[1]->slider.setTooltip("L / C / R; effective spread also includes main Width");
            fields[3]->slider.setTooltip("Fine Detune in cents, separate from musical Pitch");
            fields[4]->slider.setTooltip("Voice formant +/-24 st; adds to shared formant and remains active when shared Formant is off");
            fields[5]->slider.setTooltip("Motion depth: zero stops motion, not static offsets");
            remove.setButtonText("Remove");
            remove.setTooltip("Remove this stable voice slot; surviving voices are not renumbered");
            remove.onClick = [&owner, index] { owner.removeVoice(index); };
            if (index >= 2) addAndMakeVisible(remove);
            detail::passiveControl(*this);
        }
        void resized() override {
            auto area = getLocalBounds().reduced(8);
            auto header = area.removeFromTop(25);
            remove.setBounds(header.removeFromRight(64));
            header.removeFromLeft(112);
            enabled.setBounds(header.removeFromLeft(54));
            area.removeFromTop(5);
            pitch.setBounds(area.removeFromTop(34));
            area.removeFromTop(5);
            for (auto& field : fields) field->setBounds(area.removeFromTop(27));
        }
        void mouseDown(const juce::MouseEvent&) override { if (onSelect) onSelect(); }

        void paint(juce::Graphics& g) override {
            g.setColour(juce::Colour(neon ? 0xff111827 : 0xff252826));
            g.fillRoundedRectangle(getLocalBounds().toFloat(), 4.0f);
            const auto colour = VoiceMonitor::voiceColour(voiceSlot);
            g.setColour(selected ? colour : juce::Colour(neon ? 0xff2b3a53 : 0xff515850));
            g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.7f), 4.0f, selected ? 1.6f : 1.0f);
            g.setColour(colour.withAlpha(0.12f));
            g.fillRoundedRectangle(5.0f, 5.0f, float(getWidth() - 10), 32.0f, 3.0f);
            g.setColour(colour);
            g.fillEllipse(12, 10, 22, 22);
            g.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
            g.setColour(juce::Colour(0xff151b20));
            g.drawText(juce::String(int(voiceSlot + 1)), 12, 10, 22, 22, juce::Justification::centred);
            g.setColour(juce::Colour(0xffe4e9ee));
            g.drawText("VOICE", 42, 10, 66, 22, juce::Justification::centredLeft);
        }
        void setSelected(bool value) { selected = value; repaint(); }
        void setNeon(bool value) {
            neon = value;
            for (auto* button : {&enabled, &remove}) {
                button->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffe4e9ee));
                button->setColour(juce::TextButton::textColourOnId, juce::Colour(0xff17171c));
            }
            pitch.setNeon(neon);
            for (auto& field : fields) field->setNeon(neon);
            repaint();
        }
        std::function<void()> onSelect;
    private:
        const size_t voiceSlot;
        bool neon = false, selected = false;
        juce::TextButton enabled, remove;
        detail::ParameterSlider pitch;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> link;
        std::array<std::unique_ptr<detail::ParameterSlider>, 7> fields;
    };
    // Observes clicks without taking focus or stealing the slider gesture.
    class CardClicks final : public juce::MouseListener {
    public:
        std::function<void(juce::Component*)> onClick;
        void mouseDown(const juce::MouseEvent& e) override { if (onClick) onClick(e.eventComponent); }
    } clicks;
public:
    explicit DoublerPanel(Processor& processor) : owner(processor) {
        setName("Advanced Doubler drawer");
        enabled.setComponentID("advancedOn");
        enabled.setButtonText("Voice stack");
        enabled.setName("Advanced voice stack enabled");
        enabled.setClickingTogglesState(true);
        enabled.onClick = [this] { owner.setAdvancedEnabled(enabled.getToggleState()); };
        enabled.setTooltip("Use stored voice settings instead of Basic; disabling preserves edits");
        reset.setButtonText("Reset to Basic");
        openRouting.setButtonText("Open Routing");
        openRouting.setTooltip("Open the audio graph and independent effect controls");
        openRouting.onClick = [this] { if (onOpenRouting) onOpenRouting(); };
        reset.setTooltip("Restore two Basic-derived voices; unrelated modules stay unchanged");
        reset.onClick = [this] { owner.resetVoicesToBasic(); sync(); };
        add.setButtonText("+ Add voice");
        add.onClick = [this] {
                    const auto before = owner.settings();
                    if (!owner.addVoice()) return;
                    sync();
                    for (size_t i = 0; i < cards.size(); ++i)
                        if (!before.voiceSettings[i].present) { select(i, true); break; }
                };
        for (auto* button : {&enabled, &reset, &add, &openRouting}) addAndMakeVisible(*button);
        addAndMakeVisible(monitor);
        monitor.onSelect = [this](size_t slot) { select(slot, true); };
        viewport.setViewedComponent(&cardArea, false);
        viewport.setScrollBarsShown(false, true);
        viewport.setScrollBarThickness(12);
        viewport.setScrollOnDragEnabled(false);
        addAndMakeVisible(viewport);
        clicks.onClick = [this](juce::Component* c) {
            for (size_t i = 0; i < cards.size(); ++i)
                if (c == cards[i].get() || cards[i]->isParentOf(c)) { select(i, false); break; }
        };
        for (size_t v = 0; v < cards.size(); ++v) {
            cards[v] = std::make_unique<VoiceCard>(owner, v);
            cards[v]->onSelect = [this, v] { select(v, false); };
            cards[v]->addMouseListener(&clicks, true);
            cardArea.addAndMakeVisible(*cards[v]);
        }
        sync();
        select(0, false);
        detail::passiveControl(*this);
    }
    ~DoublerPanel() override {
        for (auto& card : cards) card->removeMouseListener(&clicks);
        viewport.setViewedComponent(nullptr, false);
    }
    void sync() {
        const auto settings = owner.settings();
        enabled.setToggleState(settings.advanced, juce::dontSendNotification);
        bool changed = false;
        int count = 0;
        for (size_t i = 0; i < cards.size(); ++i) {
            const bool present = settings.voiceSettings[i].present;
            changed |= cards[i]->isVisible() != present;
            cards[i]->setVisible(present);
            if (present) ++count;
        }
        add.setEnabled(count < 8);
        if (changed) resized();
        if (!settings.voiceSettings[selectedSlot].present)
            for (size_t i = 0; i < cards.size(); ++i)
                if (settings.voiceSettings[i].present) { select(i, false); break; }
        monitor.update(owner.getVoiceSnapshots());
    }
    void setNeon(bool neon) {
        for (auto* button : {&enabled, &reset, &add, &openRouting}) {
            button->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffe4e9ee));
            button->setColour(juce::TextButton::textColourOnId, juce::Colour(0xff17171c));
        }
        openRouting.setColour(juce::TextButton::textColourOffId, juce::Colour(neon ? 0xff64f5e3 : 0xfff5a24b));
        monitor.setNeon(neon);
        for (auto& card : cards) card->setNeon(neon);
    }
    std::function<void()> onOpenRouting;
    void resized() override {
        enabled.setBounds(0, 0, 164, 26);
        auto actions = getLocalBounds().removeFromTop(26);
        reset.setBounds(actions.removeFromRight(116));
        actions.removeFromRight(12);
        openRouting.setBounds(actions.removeFromRight(128));
        actions.removeFromRight(8);
        add.setBounds(actions.removeFromRight(116));
        monitor.setBounds(0, 32, getWidth(), 100);
        viewport.setBounds(0, 140, getWidth(), juce::jmax(0, getHeight() - 140));
        int x = 0;
        for (auto& card : cards) if (card->isVisible()) {
            card->setBounds(x, 0, 290, 278);
            x += 298;
        }
        cardArea.setSize(juce::jmax(getWidth(), x - 8), 278);
    }
private:
    void select(size_t slot, bool reveal) {
        selectedSlot = slot;
        monitor.select(slot);
        for (size_t i = 0; i < cards.size(); ++i) cards[i]->setSelected(i == slot);
        if (reveal && cards[slot]->isVisible())
            viewport.setViewPosition(juce::jlimit(0, juce::jmax(0, cardArea.getWidth() - viewport.getWidth()), cards[slot]->getX()), 0);
    }
    Processor& owner;
    size_t selectedSlot = 0;
    juce::TextButton enabled, reset, add, openRouting;
    VoiceMonitor monitor;
    juce::Component cardArea;
    juce::Viewport viewport;
    std::array<std::unique_ptr<VoiceCard>, 8> cards;
};

class ChoralePanel final : public juce::Component {
public:
    explicit ChoralePanel(Processor& owner)
        : mix(owner, "choraleMix", "Mix", "%"), motion(owner, "choraleMotion", "Motion", "%") {
        setName("Chorale section");
        enabled.setComponentID("choraleOn");
        enabled.setButtonText("CHORALE");
        enabled.setClickingTogglesState(true);
        enabled.setTooltip("Enable the separate Chorale effect (off by default)");
        addAndMakeVisible(enabled);
        addAndMakeVisible(mix);
        addAndMakeVisible(motion);
        link = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(owner.parameters, "choraleOn", enabled);
        detail::passiveControl(*this);
    }
    void resized() override {
        enabled.setBounds(12, 16, 112, 26);
        mix.setBounds(148, 12, 328, 34);
        motion.setBounds(500, 12, getWidth() - 512, 34);
    }
    void paint(juce::Graphics& g) override {
        g.setColour(juce::Colour(neon ? 0xff111827 : 0xff252826));
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 4.0f);
        g.setColour(juce::Colour(neon ? 0xff2b3a53 : 0xff515850));
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 4.0f, 1.0f);
    }
    void setNeon(bool value) {
        neon = value;
        enabled.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffe4e9ee));
        enabled.setColour(juce::TextButton::textColourOnId, juce::Colour(0xff17171c));
        mix.setNeon(neon);
        motion.setNeon(neon);
        repaint();
    }
private:
    bool neon = false;
    juce::TextButton enabled;
    detail::ParameterSlider mix, motion;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> link;
};
} // namespace vocalstack
