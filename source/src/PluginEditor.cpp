#include "PluginEditor.h"

namespace vocalstack {
namespace {
using namespace juce;
Colour rgb(uint32_t hex) { return Colour(0xff000000u | hex); }
Font font(float height, bool bold = false) {
    return Font(FontOptions(height, bold ? Font::bold : Font::plain));
}

constexpr int editorWidth = 748, editorHeight = 294;
constexpr uint32_t night = 0x090d17, panel = 0x111827, outline = 0x2b3a53;
constexpr uint32_t signPink = 0xff4ca0, signCyan = 0x64f5e3, signYellow = 0xf8de6b;
constexpr uint32_t nightText = 0xf1f5ff, nightMuted = 0x9eafc8;
constexpr std::array<const char*, 5> knobNames{"Level", "Double", "Human", "Width", "Formant"};
constexpr std::array<const char*, 5> knobTips{
    "How strongly active phrases are leveled",
    "How much of the two generated takes is mixed in",
    "How far the doubles vary in timing, pitch and tone",
    "How widely the generated takes are panned",
    "Move the vocal resonances while keeping the lead pitch"
};
constexpr std::array<const char*, 4> switchNames{"LEVEL", "DOUBLE", "FORMANT", "BYPASS"};

void text(Graphics& g, const String& value, Rectangle<int> box, Colour color,
          float size, bool bold = false, Justification align = Justification::centredLeft) {
    g.setFont(font(size, bold));
    g.setColour(color);
    g.drawText(value, box, align, false);
}
}

Editor::Editor(Processor& processor) : AudioProcessorEditor(processor), owner(processor) {
    setLookAndFeel(&style);
    for (size_t i = 0; i < knobs.size(); ++i) {
        auto& knob = knobs[i];
        knob.setName(knobNames[i]);
        knob.setTitle(knobNames[i]);
        knob.setTooltip(knobTips[i]);
        knob.setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
        knob.setTextBoxStyle(Slider::TextBoxBelow, false, 67, 20);
        knob.setScrollWheelEnabled(false);
        addAndMakeVisible(knob);
        knobLinks[i] = std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(
            owner.parameters, parameterIds[i], knob);
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
            owner.parameters, parameterIds[i + 5], button);
    }

    for (auto* button : {&theme, &reset, &copy, &paste})
        addAndMakeVisible(*button);
    theme.setTooltip("Switch between the compact Device skin and the Neon skin");
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
    setSize(editorWidth, editorHeight);
    applyTheme();
    startTimerHz(30);
}

Editor::~Editor() { setLookAndFeel(nullptr); }

void Editor::applyTheme() {
    style.flash = owner.isFlash();
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
    for (auto* button : {&theme, &reset, &copy, &paste}) {
        button->setColour(TextButton::textColourOffId, foreground);
        button->repaint();
    }
    for (auto& button : switches) {
        button.setColour(TextButton::textColourOffId, muted);
        button.setColour(TextButton::textColourOnId, rgb(0x17171c));
        button.repaint();
    }
    resized();
    repaint();
}

void Editor::resized() {
    const std::array<int, 5> deviceCenters{91, 222, 337, 452, 583};
    const std::array<int, 5> neonCenters{82, 203, 324, 445, 566};
    const auto& centers = style.flash ? neonCenters : deviceCenters;
    for (size_t i = 0; i < knobs.size(); ++i) {
        labels[i].setBounds(centers[i] - 44, style.flash ? 113 : 129, 88, 20);
        knobs[i].setBounds(centers[i] - 43, style.flash ? 139 : 149, 86, 96);
    }
    if (style.flash) {
        switches[0].setBounds(42, 79, 78, 23);
        switches[1].setBounds(284, 79, 80, 23);
        switches[2].setBounds(525, 79, 82, 23);
    } else {
        switches[0].setBounds(47, 88, 88, 23);
        switches[1].setBounds(293, 88, 88, 23);
        switches[2].setBounds(539, 88, 88, 23);
    }
    switches[3].setBounds(642, 25, 85, 26);
    theme.setBounds(513, 25, 118, 26);
    reset.setBounds(24, 259, 66, 23);
    copy.setBounds(99, 259, 105, 23);
    paste.setBounds(212, 259, 64, 23);
}

void Editor::timerCallback() {
    if (owner.isFlash() != style.flash)
        applyTheme();
    if (feedbackTicks > 0 && --feedbackTicks == 0) {
        copy.setButtonText("Copy settings");
        paste.setButtonText("Paste");
    }
    meters = owner.getMeters();
    repaint(636, 76, 105, 175);
}

void Editor::paint(Graphics& g) {
    if (style.flash)
        drawFlash(g);
    else
        drawDevice(g);
    drawMeters(g);
}

void Editor::drawDevice(Graphics& g) {
    g.fillAll(rgb(0x292b2a));
    g.setColour(rgb(0x373a37));
    g.fillRoundedRectangle(17.0f, 13.0f, 714.0f, 50.0f, 5.0f);
    g.setColour(rgb(0xf5a24b));
    g.fillRoundedRectangle(27.0f, 25.0f, 4.0f, 25.0f, 2.0f);
    text(g, "Vocal Stack", {41, 20, 168, 32}, rgb(0xf4f4ea), 22.0f, true);
    text(g, "VOCAL PROCESSOR", {172, 25, 160, 26}, rgb(0xa6aaa0), 11.0f, true);

    g.setColour(rgb(0x1c1f1e));
    g.fillRoundedRectangle(17.0f, 74.0f, 714.0f, 173.0f, 6.0f);
    for (const auto area : {Rectangle<float>(25, 80, 132, 160),
                           Rectangle<float>(166, 80, 342, 160),
                           Rectangle<float>(517, 80, 132, 160)}) {
        g.setColour(rgb(0x252826));
        g.fillRoundedRectangle(area, 4.0f);
        g.setColour(rgb(0x343a35));
        g.drawRoundedRectangle(area, 4.0f, 1.0f);
    }
    text(g, "LEVELER  /  DOUBLER  /  FORMANT", {417, 258, 309, 24},
         rgb(0x9b9f96), 11.0f, true, Justification::centredRight);
}

void Editor::drawFlash(Graphics& g) {
    g.fillAll(rgb(night));
    const std::array<Rectangle<int>, 5> plates{{
        {22, 13, 705, 51},
        {22, 75, 120, 170},
        {151, 75, 350, 170},
        {510, 75, 113, 170},
        {632, 75, 95, 170}
    }};
    for (const auto& plate : plates) {
        g.setColour(rgb(panel));
        g.fillRect(plate);
        g.setColour(rgb(outline));
        g.drawRect(plate);
    }

    g.setColour(rgb(signPink));
    g.fillRect(22, 13, 4, 51);
    g.drawRect(35, 22, 32, 32);
    for (int x = 40; x <= 52; x += 6)
        g.drawLine(static_cast<float>(x), 46.0f, static_cast<float>(x + 8), 30.0f, 2.3f);
    text(g, "VOCAL STACK", {78, 16, 236, 30}, rgb(nightText), 27.0f, true);
    text(g, "TOKYO  /  AFTER DARK", {79, 43, 212, 14}, rgb(nightMuted), 11.0f, true);

    g.setColour(rgb(signCyan));
    g.drawRect(326, 25, 148, 26);
    g.fillRect(326, 25, 4, 26);
    text(g, "SHIBUYA", {337, 25, 101, 26}, rgb(signCyan), 14.0f, true);
    text(g, "03", {441, 25, 23, 26}, rgb(signCyan), 12.0f, true, Justification::centred);

    constexpr std::array<uint32_t, 4> accents{signYellow, signPink, signCyan, signCyan};
    for (size_t i = 0; i < accents.size(); ++i) {
        const auto& plate = plates[i + 1];
        g.setColour(rgb(accents[i]));
        g.fillRect(plate.getX(), plate.getY(), plate.getWidth(), 2);
    }
    g.setColour(rgb(outline));
    for (const int x : {264, 385})
        g.drawVerticalLine(x, 110.0f, 234.0f);
    g.drawHorizontalLine(252, 22.0f, 727.0f);

    g.setColour(rgb(signPink));
    for (int x = 490; x <= 514; x += 6)
        g.drawLine(static_cast<float>(x), 276.0f, static_cast<float>(x + 7), 265.0f, 1.7f);
    text(g, "NIGHT SIGNAL  /  THREE VOICES", {528, 259, 199, 23},
         rgb(nightMuted), 11.0f, true, Justification::centredRight);
}

void Editor::drawMeters(Graphics& g) {
    const auto muted = style.flash ? rgb(nightMuted) : rgb(0xbabdb3);
    const int x = style.flash ? 650 : 661;
    const int spacing = style.flash ? 36 : 32;
    const std::array<float, 2> levels{meters.input, meters.output};
    const std::array<const char*, 2> names{"IN", "OUT"};
    for (size_t i = 0; i < levels.size(); ++i) {
        const int column = x + spacing * static_cast<int>(i);
        text(g, names[i], {column - 6, 91, 30, 19}, muted, 11.0f, true, Justification::centred);
        const auto db = Decibels::gainToDecibels(levels[i], -54.0f);
        for (int tick = 0; tick < 22; ++tick) {
            const bool lit = db > -54.0f + static_cast<float>(tick) * 2.45f;
            const auto good = style.flash ? rgb(signCyan) : rgb(0x9ac37d);
            g.setColour(!lit ? rgb(style.flash ? 0x293b51 : 0x343b34) :
                        tick > 19 ? rgb(0xf78677) : tick > 16 ? rgb(0xf0b66d) : good);
            const Rectangle<float> segment{static_cast<float>(column), 207.0f - tick * 4.35f, 18.0f, 3.1f};
            if (style.flash)
                g.fillRect(segment);
            else
                g.fillRoundedRectangle(segment, 0.7f);
        }
    }
    const auto sign = meters.correctionDb > 0.05f ? "+" : "";
    text(g, sign + String(meters.correctionDb, 1) + " dB", {x - 8, 218, 73, 19}, muted, 11.0f,
         false, Justification::centred);
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
                            : name == "Level" ? signYellow : signPink);
    Path track, value;
    track.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, start, end, true);
    g.setColour(flash ? rgb(outline) : rgb(0x50564f));
    g.strokePath(track, PathStrokeType(2.8f, PathStrokeType::curved, PathStrokeType::rounded));
    const float origin = name == "Formant" ? (start + end) * 0.5f : start;
    if (std::abs(angle - origin) > 0.001f) {
        value.addCentredArc(centre.x, centre.y, radius, radius, 0.0f,
                            std::min(angle, origin), std::max(angle, origin), true);
        g.setColour(accent);
        g.strokePath(value, PathStrokeType(3.2f, PathStrokeType::curved, PathStrokeType::rounded));
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
