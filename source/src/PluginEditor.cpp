#include "PluginEditor.h"

namespace vocalstack {
namespace {
using namespace juce;
Colour rgb(uint32_t hex) { return Colour(0xff000000u | hex); }
Font font(float height, bool bold = false) {
    return Font(FontOptions(height, bold ? Font::bold : Font::plain));
}

constexpr int editorWidth = 940, editorHeight = 430, drawerHeight = 442;
const std::array<Rectangle<int>, 4> controlPlates{{
    {18, 76, 104, 214}, {132, 76, 306, 214},
    {448, 76, 104, 214}, {562, 76, 104, 214}
}};
constexpr uint32_t night = 0x090d17, panel = 0x111827, outline = 0x2b3a53;
constexpr uint32_t signPink = 0xff4ca0, signCyan = 0x64f5e3, signYellow = 0xf8de6b;
constexpr uint32_t nightText = 0xf1f5ff, nightMuted = 0x9eafc8;
constexpr std::array<const char*, knobParameterIds.size()> knobNames{
    "Leveler", "Mix", "Human", "Width", "Formant", "Output"
};
constexpr std::array<const char*, knobParameterIds.size()> knobTips{
    "Even out vocal phrases while matching long-term input RMS. 0% off, 100% full riding; not a limiter or output volume",
    "True Mix: 0% processed lead only; 100% generated doubles only; blend between them",
    "How far the doubles vary in timing, pitch and tone",
    "How widely the generated takes are panned",
    "Shared formant -24 to +24 st, added to each voice formant. Double-click for 0 st",
    "Final output trim: -24 to +12 dB. Double-click for unity (0 dB). Separate from leveler correction"
};
constexpr std::array<const char*, switchParameterIds.size()> switchNames{"LEVEL", "DOUBLE", "FORMANT", "BYPASS"};

void leaveKeyboardToHost(Component& component) {
    // Numeric entry owns keys only while its transient TextEditor is open.
    if (dynamic_cast<TextEditor*>(&component) != nullptr)
        return;
    component.setWantsKeyboardFocus(false);
    component.setMouseClickGrabsKeyboardFocus(false);
    for (int i = 0; i < component.getNumChildComponents(); ++i)
        leaveKeyboardToHost(*component.getChildComponent(i));
}

void text(Graphics& g, const String& value, Rectangle<int> box, Colour color,
          float size, bool bold = false, Justification align = Justification::centredLeft) {
    g.setFont(font(size, bold));
    g.setColour(color);
    g.drawText(value, box, align, false);
}
}

Editor::Editor(Processor& processor)
    : AudioProcessorEditor(processor), owner(processor), advancedPanel(processor), choralePanel(processor) {
    setLookAndFeel(&style);
    for (size_t i = 0; i < knobs.size(); ++i) {
        auto& knob = knobs[i];
        knob.setName(knobNames[i]);
        knob.setTitle(knobNames[i]);
        knob.setTooltip(knobTips[i]);
        knob.setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
        knob.setTextBoxStyle(Slider::TextBoxBelow, false, 82, 22);
        knob.setScrollWheelEnabled(false);
        addAndMakeVisible(knob);
        knobLinks[i] = std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(
            owner.parameters, knobParameterIds[i], knob);
        if (const auto* parameter = owner.parameters.getParameter(knobParameterIds[i]))
            knob.setDoubleClickReturnValue(true, i == 4 || i == 5 ? 0.0
                : parameter->convertFrom0to1(parameter->getDefaultValue()));
        labels[i].setText(knobNames[i], dontSendNotification);
        labels[i].setJustificationType(Justification::centred);
        labels[i].setInterceptsMouseClicks(false, false);
        addAndMakeVisible(labels[i]);
    }
    for (size_t i = 0; i < switches.size(); ++i) {
        auto& button = switches[i];
        button.setButtonText(switchNames[i]);
        button.setClickingTogglesState(true);
        button.setTooltip(i == 3 ? "Bypass the whole effect with latency compensation"
                                 : "Toggle this module on or off");
        addAndMakeVisible(button);
        switchLinks[i] = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment>(
            owner.parameters, switchParameterIds[i], button);
    }

    for (auto* button : {&theme, &reset, &copy, &paste})
        addAndMakeVisible(*button);
    theme.setTooltip("Switch between the Device skin and the Neon skin");
    theme.onClick = [this] { owner.setFlash(!owner.isFlash()); applyTheme(); };
    reset.setButtonText("Reset");
    reset.setTooltip("Restore the default effect parameters");
    reset.onClick = [this] { owner.resetParameters(); };
    copy.setButtonText("Copy settings");
    copy.setTooltip("Copy this instance's settings as text");
    copy.onClick = [this] {
        SystemClipboard::copyTextToClipboard(owner.settingsText());
        copy.setButtonText("Copied");
        feedbackTicks = 48;
    };
    paste.setButtonText("Paste");
    paste.setTooltip("Load Vocal Stack settings from the clipboard");
    paste.onClick = [this] {
        paste.setButtonText(owner.pasteSettings(SystemClipboard::getTextFromClipboard()) ? "Pasted" : "Invalid");
        feedbackTicks = 48;
    };
    drawer.setName("Show Advanced");
    drawer.setTooltip("Show or hide voice controls without changing Advanced Doubler activation or sound");
    drawer.onClick = [this] { owner.setAdvancedVisible(!owner.isAdvancedVisible()); syncDrawer(); };
    addAndMakeVisible(drawer);
    routing.setButtonText("Routing...");
    routing.setTooltip("Open the native audio graph and independent FX inspector");
    routing.onClick = [this] {
        if (!routingWindow) routingWindow = std::make_unique<RoutingWindow>(owner);
        routingWindow->setNeon(style.flash);
        routingWindow->setVisible(true);
        routingWindow->toFront(false);
    };
    addAndMakeVisible(routing);
    addChildComponent(advancedPanel);
    addAndMakeVisible(choralePanel);
    addAndMakeVisible(meterPanel);
    meterPanel.onReset = [this] { owner.requestMeterReset(); };
    meterPanel.setMeters(owner.getMeters());
    syncDrawer();
    applyTheme();
    startTimerHz(30);
}

Editor::~Editor() {
    routingWindow.reset();
    setLookAndFeel(nullptr);
}

void Editor::applyTheme() {
    style.flash = owner.isFlash();
    meterPanel.setNeon(style.flash);
    advancedPanel.setNeon(style.flash);
    choralePanel.setNeon(style.flash);
        if (routingWindow) routingWindow->setNeon(style.flash);
    const auto foreground = style.flash ? rgb(nightText) : rgb(0xe4e4df);
    const auto muted = style.flash ? rgb(nightMuted) : rgb(0xaeb1a9);
    theme.setButtonText(style.flash ? "SKIN  /  NEON" : "SKIN  /  DEVICE");
    for (auto& label : labels) {
        label.setColour(Label::textColourId, foreground);
        label.setFont(font(style.flash ? 15.0f : 14.0f, true));
    }
    for (auto& knob : knobs) {
        knob.setColour(Slider::textBoxTextColourId, muted);
        knob.setColour(Slider::textBoxOutlineColourId, Colours::transparentBlack);
        knob.setColour(Slider::textBoxBackgroundColourId, Colours::transparentBlack);
        knob.setColour(Slider::textBoxHighlightColourId, rgb(style.flash ? 0x34496a : 0x4b5175));
        knob.repaint();
    }
    for (auto* button : {&theme, &reset, &copy, &paste, &drawer, &routing}) {
        button->setColour(TextButton::textColourOffId, foreground);
        button->repaint();
    }
    for (auto& button : switches) {
        button.setColour(TextButton::textColourOffId, muted);
        button.setColour(TextButton::textColourOnId, rgb(0x17171c));
        button.repaint();
    }
    resized();
    // Theme changes recreate slider text labels, so reapply their idle focus policy.
    leaveKeyboardToHost(*this);
    repaint();
}

void Editor::resized() {
    constexpr std::array<int, 6> centers{70, 183, 285, 387, 500, 614};
    for (size_t i = 0; i < knobs.size(); ++i) {
        labels[i].setBounds(centers[i] - 44, 121, 88, 20);
        knobs[i].setBounds(centers[i] - 45, 148, 90, 114);
    }
    switches[0].setBounds(32, 88, 76, 23);
    switches[1].setBounds(244, 88, 82, 23);
    switches[2].setBounds(458, 88, 84, 23);
    switches[3].setBounds(817, 26, 85, 26);
    theme.setBounds(678, 26, 126, 26);
    reset.setBounds(24, 306, 66, 23);
    copy.setBounds(100, 306, 105, 23);
    paste.setBounds(215, 306, 64, 23);
    meterPanel.setBounds(678, 76, 244, 246);
    drawer.setBounds(300, 306, 144, 23);
        routing.setBounds(450, 306, 88, 23);
    choralePanel.setBounds(18, 344, 904, 64);
    advancedPanel.setBounds(18, editorHeight, 904, drawerHeight - 12);
}

void Editor::syncDrawer() {
    const bool visible = owner.isAdvancedVisible();
    advancedPanel.setVisible(visible);
    drawer.setButtonText(visible ? "Hide Advanced" : "Show Advanced");
    setSize(editorWidth, editorHeight + (visible ? drawerHeight : 0));
    advancedPanel.sync();
    leaveKeyboardToHost(*this);
}

void Editor::timerCallback() {
    if (advancedPanel.isVisible() != owner.isAdvancedVisible())
        syncDrawer();
    advancedPanel.sync();
    if (owner.isFlash() != style.flash)
        applyTheme();
    if (feedbackTicks > 0 && --feedbackTicks == 0) {
        copy.setButtonText("Copy settings");
        paste.setButtonText("Paste");
    }
    meterPanel.setMeters(owner.getMeters());
}

void Editor::paint(Graphics& g) {
    if (style.flash)
        drawFlash(g);
    else
        drawDevice(g);
}

void Editor::drawDevice(Graphics& g) {
    g.fillAll(rgb(0x292b2a));
    g.setColour(rgb(0x373a37));
    g.fillRoundedRectangle(18.0f, 14.0f, 904.0f, 50.0f, 5.0f);
    g.setColour(rgb(0xf5a24b));
    g.fillRoundedRectangle(27.0f, 25.0f, 4.0f, 25.0f, 2.0f);
    text(g, "Vocal Stack", {41, 20, 168, 32}, rgb(0xf4f4ea), 22.0f, true);
    text(g, "VOCAL PROCESSOR", {211, 26, 160, 26}, rgb(0xa6aaa0), 11.0f, true);

    g.setColour(rgb(0x1c1f1e));
    g.fillRoundedRectangle(14.0f, 72.0f, 656.0f, 222.0f, 6.0f);
    for (const auto& plate : controlPlates) {
        const auto area = plate.toFloat();
        g.setColour(rgb(0x252826));
        g.fillRoundedRectangle(area, 4.0f);
        g.setColour(rgb(0x343a35));
        g.drawRoundedRectangle(area, 4.0f, 1.0f);
    }
    text(g, "FINAL GAIN", {570, 88, 88, 23}, rgb(0xaeb1a9), 11.0f, true, Justification::centred);
    const std::array<const char*, 4> captions{
        "PHRASE CONTROL", "GENERATED TAKES", "PITCH PRESERVED", "OUTPUT TRIM"
    };
    for (size_t i = 0; i < controlPlates.size(); ++i)
        text(g, captions[i], {controlPlates[i].getX(), 269, controlPlates[i].getWidth(), 15},
             rgb(0xaeb1a9), 9.5f, false, Justification::centred);
    text(g, "TRUE MIX", {542, 306, 124, 23},
         rgb(0x9b9f96), 10.0f, true, Justification::centredRight);
}

void Editor::drawFlash(Graphics& g) {
    g.fillAll(rgb(night));
    const auto drawPlate = [&](Rectangle<int> plate) {
        g.setColour(rgb(panel));
        g.fillRect(plate);
        g.setColour(rgb(outline));
        g.drawRect(plate);
    };
    drawPlate({18, 14, 904, 50});
    for (const auto& plate : controlPlates)
        drawPlate(plate);

    g.setColour(rgb(signPink));
    g.fillRect(18, 14, 4, 50);
    g.drawRect(35, 22, 32, 32);
    for (int x = 40; x <= 52; x += 6)
        g.drawLine(static_cast<float>(x), 46.0f, static_cast<float>(x + 8), 30.0f, 2.3f);
    text(g, "VOCAL STACK", {78, 16, 236, 30}, rgb(nightText), 27.0f, true);
    text(g, "TOKYO  /  AFTER DARK", {79, 43, 212, 14}, rgb(nightMuted), 11.0f, true);

    g.setColour(rgb(signCyan));
    g.drawRect(382, 26, 148, 26);
    g.fillRect(382, 26, 4, 26);
    text(g, "SHIBUYA", {393, 26, 101, 26}, rgb(signCyan), 14.0f, true);
    text(g, "03", {497, 26, 23, 26}, rgb(signCyan), 12.0f, true, Justification::centred);

    constexpr std::array<uint32_t, 4> accents{signYellow, signPink, signCyan, signYellow};
    const std::array<const char*, 4> captions{
        "PHRASE CONTROL", "GENERATED TAKES", "PITCH PRESERVED", "OUTPUT TRIM"
    };
    for (size_t i = 0; i < accents.size(); ++i) {
        const auto& plate = controlPlates[i];
        g.setColour(rgb(accents[i]));
        g.fillRect(plate.getX(), plate.getY(), plate.getWidth(), 2);
        text(g, captions[i], {plate.getX(), 269, plate.getWidth(), 15},
             rgb(nightMuted), 9.5f, false, Justification::centred);
    }
    text(g, "FINAL GAIN", {570, 88, 88, 23}, rgb(signYellow), 11.0f, true, Justification::centred);
    g.setColour(rgb(outline));
    for (const int x : {234, 336})
        g.drawVerticalLine(x, 121.0f, 260.0f);
    g.drawHorizontalLine(298, 18.0f, 666.0f);

    text(g, "TRUE MIX", {542, 306, 124, 23},
         rgb(nightMuted), 11.0f, true, Justification::centredRight);
}

void Editor::Style::drawRotarySlider(Graphics& g, int x, int y, int w, int h, float position,
                                    float start, float end, Slider& slider) {
    const auto area = Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
                                       static_cast<float>(w), static_cast<float>(h));
    const float radius = std::min(area.getWidth(), area.getHeight()) * (flash ? 0.41f : 0.37f);
    const auto centre = area.getCentre();
    const float angle = start + position * (end - start);
    const auto name = slider.getName();
    const auto accent = rgb(!flash ? 0xf5a24b : name == "Formant" ? signCyan
                            : name == "Leveler" || name == "Output" ? signYellow : signPink);
    Path track, value;
    track.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, start, end, true);
    g.setColour(flash ? rgb(outline) : rgb(0x50564f));
    g.strokePath(track, PathStrokeType(2.8f, PathStrokeType::curved, PathStrokeType::rounded));
    const float origin = name == "Formant" || name == "Output"
        ? start + static_cast<float>(slider.valueToProportionOfLength(0.0)) * (end - start) : start;
    if (std::abs(angle - origin) > 0.001f) {
        value.addCentredArc(centre.x, centre.y, radius, radius, 0.0f,
                            std::min(angle, origin), std::max(angle, origin), true);
        g.setColour(accent);
        g.strokePath(value, PathStrokeType(3.2f, PathStrokeType::curved, PathStrokeType::rounded));
    }
    if (name == "Formant" || name == "Output") {
        g.setColour(accent.withAlpha(0.8f));
        g.drawLine({centre.getPointOnCircumference(radius + 3.0f, origin),
                    centre.getPointOnCircumference(radius + 7.0f, origin)}, 1.2f);
    }
    const float inner = radius - 5.3f;
    const auto face = Rectangle<float>(inner * 2, inner * 2).withCentre(centre);
    if (flash)
        g.setColour(rgb(night));
    else
        g.setGradientFill(ColourGradient(rgb(0x414641), centre.x, face.getY(),
                                         rgb(0x282b28), centre.x, face.getBottom(), false));
    g.fillEllipse(face);
    g.setColour(rgb(flash ? outline : 0x555b54));
    g.drawEllipse(face, 1.0f);
    g.setColour(accent);
    const auto a = centre.getPointOnCircumference(inner - 3.6f, angle);
    const auto b = centre.getPointOnCircumference(inner - 11.0f, angle);
    g.drawLine({a, b}, 2.6f);
}

void Editor::Style::drawButtonBackground(Graphics& g, Button& button, const Colour&,
                                         bool hover, bool down) {
    const auto area = button.getLocalBounds().toFloat().reduced(0.7f);
    const bool lit = button.getToggleState();
    const auto name = button.getButtonText();
    const auto accent = rgb(name == "BYPASS" ? 0xf58680 : !flash ? 0xe3a15e
                            : name == "LEVEL" ? signYellow : name == "FORMANT" ? signCyan : signPink);
    auto fill = lit ? accent : rgb(flash ? panel : 0x333733);
    if (hover) fill = fill.brighter(0.12f);
    if (down) fill = fill.darker(0.12f);
    g.setColour(fill);
    const bool focused = button.hasKeyboardFocus(false);
    if (flash) {
        g.fillRect(area);
        g.setColour(lit || focused || name == "SKIN  /  NEON" ? accent : rgb(outline));
        g.drawRect(area, focused ? 2.0f : 1.0f);
    } else {
        g.fillRoundedRectangle(area, 3.0f);
        g.setColour(lit ? accent.brighter(0.13f) : rgb(0x515850));
        g.drawRoundedRectangle(area, 3.0f, focused ? 1.6f : 0.7f);
    }
}

juce::Font Editor::Style::getTextButtonFont(juce::TextButton&, int) { return font(12.0f, true); }

} // namespace vocalstack
