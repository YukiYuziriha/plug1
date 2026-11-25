#include "VisualTheme.h"
#include "PluginEditor.h"

namespace vocalstack {
namespace {
using namespace juce;
constexpr auto sidechainParameterId = "sidechainBlend";
Colour rgb(uint32_t hex) { return Colour(0xff000000u | hex); }

Colour sidechainAccent(Theme theme) {
    const auto p = themePalette(theme);
    if (theme == Theme::neon) return rgb(0x63dfff);
    return theme == Theme::device ? p.secondary : p.accent;
}

bool hasUsableSidechainBlend(const Graph& graph) {
    const auto blend = std::find_if(graph.nodes.begin(), graph.nodes.end(), [](const auto& node) {
        return node.id == sidechainBlendNodeId && node.type == NodeType::sidechainBlend && node.enabled;
    });
    if (blend == graph.nodes.end()) return false;
    const auto source = std::find_if(graph.nodes.begin(), graph.nodes.end(), [](const auto& node) {
        return node.type == NodeType::sidechain;
    });
    const auto output = std::find_if(graph.nodes.begin(), graph.nodes.end(), [](const auto& node) {
        return node.type == NodeType::output;
    });
    if (source == graph.nodes.end() || output == graph.nodes.end()) return false;

    // Accept edited routes, but require an external donor and an Output path.
    const auto reaches = [&](uint32_t from, uint32_t to, int destinationPin) {
        std::vector<uint32_t> visited{from};
        for (size_t i = 0; i < visited.size(); ++i) {
            const auto next = visited[i];
            for (const auto& wire : graph.links) {
                if (wire.source != next) continue;
                if (wire.destination == to) {
                    if (destinationPin < 0 || wire.inputPin == destinationPin) return true;
                    continue;
                }
                if (std::find(visited.begin(), visited.end(), wire.destination) == visited.end())
                    visited.push_back(wire.destination);
            }
        }
        return false;
    };
    return reaches(source->id, blend->id, 1) && reaches(blend->id, output->id, -1);
}

using namespace editorLayout;
const std::array<Rectangle<int>, 4> controlPlates{{
    {18, 76, 104, 214}, {132, 76, 306, 214},
    {448, 76, 104, 214}, {562, 76, 104, 214}
}};
constexpr std::array<const char*, 4> controlCaptions{"", "GENERATED TAKES", "PITCH PRESERVED", "OUTPUT TRIM"};
constexpr int knobLabelWidth = 100, leadStackLabelPadding = 4;
constexpr uint32_t night = 0x090d17, outline = 0x2b3a53;
constexpr uint32_t signPink = 0xff4ca0, signCyan = 0x64f5e3, signYellow = 0xf8de6b;

constexpr std::array<const char*, knobParameterIds.size()> knobNames{
    "Gain Rider", "Lead / Stack", "Human", "Width", "Formant", "Output"
};
constexpr std::array<const char*, knobParameterIds.size()> knobTips{
    "Set how strongly HEAVEN evens out phrases. The live dB readout shows the gain added or cut before the rack. 0% is off; this is not a limiter.",
    "Blend the processed lead with generated voices. Use Mix for the untouched input.",
    "How far the doubles vary in timing, pitch and tone",
    "How widely the generated takes are panned",
    "Make the voice tone deeper or brighter without changing the note. Double-click to reset.",
    "Turn the final output up or down. Double-click for 0 dB (no change)."
};
constexpr std::array<const char*, switchParameterIds.size()> switchNames{"RIDE", "DOUBLE", "FORMANT", "BYPASS"};

// The Routing owner supplies this small public bridge; never fall back to a
// processor-only write that would silently bypass the graph's Undo history.
template <typename Routing>
void connectMainBusUndo(MainBusFxRackPanel& rack, Routing& routing) {
    if constexpr (requires(const Graph& graph, String& error) { routing.commitExternalGraphEdit(graph, error); })
        rack.commitGraph = [&routing](const Graph& graph, String& error) {
            return routing.commitExternalGraphEdit(graph, error);
        };
}

void leaveKeyboardToHost(Component& component) {
    // Numeric entry owns keys only while its transient TextEditor is open.
    if (dynamic_cast<TextEditor*>(&component) != nullptr)
        return;
    component.setWantsKeyboardFocus(false);
    component.setMouseClickGrabsKeyboardFocus(false);
    for (int i = 0; i < component.getNumChildComponents(); ++i)
        leaveKeyboardToHost(*component.getChildComponent(i));
}

void text(const Component& source, Graphics& g, const String& value, Rectangle<int> box, Colour color,
          float size, bool bold = false, Justification align = Justification::centredLeft) {
    g.setFont(themeFont(source, size, bold ? Font::bold : Font::plain));
    g.setColour(color);
    g.drawText(value, box, align, false);
}

Font fitTextWidth(Font font, const String& value, int width) {
    const int textWidth = GlyphArrangement::getStringWidthInt(font, value);
    return textWidth > width ? font.withHeight(font.getHeight() * float(width) / float(textWidth)) : font;
}

void drawControlCaption(const Component& source, Graphics& g, size_t index, Colour colour) {
    const auto& plate = controlPlates[index];
    const Rectangle<int> bounds{plate.getX() + 3, 267, plate.getWidth() - 6, 18};
    const String caption(controlCaptions[index]);
    g.setFont(fitTextWidth(themeFont(source, 11.5f), caption, bounds.getWidth() - 2));
    g.setColour(colour);
    g.drawText(caption, bounds, Justification::centred, false);
}
}

Editor::Editor(Processor& processor)
    : AudioProcessorEditor(processor), owner(processor), presets(processor), globalMix(processor, "globalMix", "Mix", "%"), routingPanel(processor),
      advancedPanel(processor), mainBusFxRack(processor) {
    style.setTooltipThemeSource(*this);
    setLookAndFeel(&style);
    tips.setLookAndFeel(&style);
    getProperties().set(vocalstackAnimationTimeProperty, 0.0);
    addAndMakeVisible(stackViewport);
    stackViewport.setComponentID("stackViewport");
    layoutCanvas.setOpaque(true); // Every skin paints its full canvas background.
    stackViewport.setViewedComponent(&layoutCanvas, false);
    stackViewport.setScrollBarsShown(true, false);
    layoutCanvas.draw = [this](Graphics& g) {
        const auto skin = componentTheme(*this);
        if (skin == Theme::spectral) drawSpectral(g);
        else if (skin == Theme::neon || skin == Theme::grass) drawGrid(g);
        else drawDevice(g);
    };
    addAndMakeVisible(presets);
    presets.onPatchLoaded = [this] {

        applyTheme();
        syncDrawer();
    };
    setResizable(true, true);
    setResizeLimits(940, 560, 2400, 2200);
    const auto savedLayout = owner.getRoutingLayout();
    selectedVoiceSlot = juce::jlimit(-1, 7, int(savedLayout.getProperty("selectedVoiceSlot", -1)));
    layoutCanvas.addAndMakeVisible(motion);
    motion.setComponentID("decorativeMotion");
    motion.setName("Motion"); motion.setTitle("Motion");
    motion.setLookAndFeel(&motionStyle);
    motion.getProperties().set("vocalstackTexturedBar", true);
    motion.setSliderStyle(Slider::LinearHorizontal);
    motion.setRange(0, 100, .01);
    motion.setValue(owner.getMotionPercent(), dontSendNotification);
    motion.setTextBoxStyle(Slider::TextBoxAbove, false, 100, 25);
    motion.setDoubleClickReturnValue(true, 3);
    motion.setScrollWheelEnabled(false);
    motion.textFromValueFunction = [](double v) { return String(v, 2).trimCharactersAtEnd("0").trimCharactersAtEnd(".") + "%"; };
    motion.valueFromTextFunction = [](const String& s) { return s.getDoubleValue(); };
    motion.updateText();
    motion.setTooltip("Decorative Motion only: 0% freeze, 3% original speed, 100% = 33.333x. Shift-drag for fine control. Does not slow audio telemetry.");
    motion.onValueChange = [this] {
        decorativeMotion.advance(Time::getMillisecondCounterHiRes() * .001, isShowing(), owner.getMotionPercent());
        owner.setMotionPercent(motion.getValue());
    };
    layoutCanvas.addAndMakeVisible(globalMix);
    globalMix.slider.getProperties().set("vocalstackSeparateBarValue", true);
    globalMix.setLinear(true);
    globalMix.slider.setTooltip("Untouched input / complete processed output. Bypass preserves this setting");
    assignmentBanner.setComponentID("stackAssignmentBanner");
    assignmentBanner.setInterceptsMouseClicks(false, false);

    layoutCanvas.addChildComponent(assignmentBanner);
    endAssignment.setComponentID("stackAssignmentDone");
    endAssignment.setButtonText("Done");
    endAssignment.onClick = [this] { owner.endModulationAssignment(); };
    layoutCanvas.addChildComponent(endAssignment);
    // Host automation only; keep the passive slider behavior without binding modulation.
    sidechainBlend.setComponentID(sidechainParameterId);
    sidechainBlend.setScrollWheelEnabled(false);
    sidechainBlend.setName("Sidechain");
    sidechainBlend.setTitle("Sidechain");
    sidechainBlend.setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
    sidechainBlend.setTextBoxStyle(Slider::TextBoxBelow, false, 96, 26);
    sidechainBlend.setDoubleClickReturnValue(true, 0.0);
    sidechainBlend.setTooltip("Blend the Lead branch with the host sidechain. 0% leaves the Lead unchanged.");
    layoutCanvas.addAndMakeVisible(sidechainBlend);
    sidechainLink = std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(
        owner.parameters, sidechainParameterId, sidechainBlend);
    sidechainLabel.setComponentID("sidechainBlendLabel");
    sidechainLabel.setJustificationType(Justification::centred);
    sidechainLabel.setInterceptsMouseClicks(false, false);
    layoutCanvas.addAndMakeVisible(sidechainLabel);
    updateSidechainAppearance(false);

    for (size_t i = 0; i < knobs.size(); ++i) {
        auto& knob = knobs[i];
        auto& parent = i == 0 ? gainRiderControls : static_cast<Component&>(layoutCanvas);
        knob.bindModulation(owner, knobParameterIds[i]);
        knob.setName(knobNames[i]);
        knob.setTitle(knobNames[i]);
        knob.setTooltip(knobTips[i]);
        knob.setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
        knob.setTextBoxStyle(Slider::TextBoxBelow, false, 96, 26);
        knob.setScrollWheelEnabled(false);
        parent.addAndMakeVisible(knob);
        knobLinks[i] = std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(
            owner.parameters, knobParameterIds[i], knob);
        if (i == 1) configureCrossfadeDisplay(knob, true);
        if (const auto* parameter = owner.parameters.getParameter(knobParameterIds[i]))
            knob.setDoubleClickReturnValue(true, i == 4 || i == 5 ? 0.0
                : parameter->convertFrom0to1(parameter->getDefaultValue()));

        labels[i].setText(knobNames[i], dontSendNotification);
        labels[i].setJustificationType(Justification::centred);
        labels[i].setInterceptsMouseClicks(false, false);
        parent.addAndMakeVisible(labels[i]);
    }
    gainRiderReadout.setComponentID("gainRiderReadout");
    gainRiderReadout.setName("Gain Rider correction");
    gainRiderReadout.setJustificationType(Justification::centred);

    gainRiderReadout.setInterceptsMouseClicks(false, false);
    gainRiderControls.addAndMakeVisible(gainRiderReadout);
    for (size_t i = 0; i < switches.size(); ++i) {
        auto& button = switches[i];
        button.setModulationOwner(owner);
        button.setComponentID(switchParameterIds[i]);
        button.setName(switchNames[i]);
        button.setButtonText(switchNames[i]);
        button.setClickingTogglesState(true);
        button.setTooltip(i == 3 ? "Bypass the whole effect with latency compensation"
                                 : "Toggle this module on or off");
        auto& parent = i == 0 ? gainRiderControls : static_cast<Component&>(layoutCanvas);
        parent.addAndMakeVisible(button);
        switchLinks[i] = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment>(
            owner.parameters, switchParameterIds[i], button);
    }

    for (auto* button : {&theme, &reset, &copy, &paste})
        layoutCanvas.addAndMakeVisible(*button);
    theme.setTooltip("Cycle skins: Device / Neon / Spectral / Grass");
    theme.onClick = [this] { owner.setSkin((owner.getSkin() + 1) % (int(Theme::grass) + 1)); applyTheme(); };
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
    paste.setTooltip("Load HEAVEN settings from the clipboard");
    paste.onClick = [this] {
        paste.setButtonText(owner.pasteSettings(SystemClipboard::getTextFromClipboard()) ? "Pasted" : "Invalid");
        feedbackTicks = 48;
    };
    // Stack controls are always visible; legacy activation/state remains host-compatible.
    stackTab.setButtonText("Stack");
    stackTab.setTooltip("Show the vocal stack controls");
    stackTab.onClick = [this] { routingVisible = false; syncDrawer(); };
    routingTab.setButtonText("Routing");
    routingTab.setTooltip("Show the audio graph and independent FX inspector");
    routingTab.onClick = [this] { openRouting(); };
    layoutCanvas.addAndMakeVisible(stackTab);
    layoutCanvas.addAndMakeVisible(routingTab);
    addChildComponent(routingPanel);

    layoutCanvas.addChildComponent(advancedPanel);
    advancedPanel.setLeadExtras(gainRiderControls);
    advancedPanel.onVoiceSelected = [this](int slot) {
        selectedVoiceSlot = slot;
        auto tree = owner.getRoutingLayout();
        tree.setProperty("selectedVoiceSlot", slot, nullptr);
        owner.setRoutingLayout(tree);
        if (!restoringWindow) routingPanel.selectVoice(slot);
    };
    routingPanel.onSelectVoice = [this](int slot) {
        selectedVoiceSlot = slot;
        advancedPanel.selectVoice(slot, false, false);
    };
    routingPanel.onRevealParameter = [this](const String& id) { revealParameter(id); };
    addChildComponent(revealBubble);
    advancedPanel.selectVoice(selectedVoiceSlot, false, false);
    connectMainBusUndo(mainBusFxRack, routingPanel);
    mainBusFxRack.onOpenRouting = [this] { openRouting(); };
    layoutCanvas.addAndMakeVisible(mainBusFxRack);
    mainBusFxRack.onPreferredHeightChanged = [this] { if (!routingVisible) layoutStackRack(); };
    mainBusFxRack.sync();
    layoutCanvas.addAndMakeVisible(meterPanel);
    meterPanel.onReset = [this] { owner.requestMeterReset(); };
    meterPanel.setMeters(owner.getMeters());
    syncDrawer();
    applyTheme();
    setSize(juce::jlimit(940, 2400, int(savedLayout.getProperty("editorWidth", editorWidth))),
            juce::jlimit(560, 2200, int(savedLayout.getProperty("editorHeight", defaultWindowHeight))));
    restoringWindow = false;
    bypassUiLink = std::make_unique<ParameterAttachment>(*owner.parameters.getParameter("bypass"),
        [this](float value) { updateBypassControls(value); }, nullptr);
    bypassUiLink->sendInitialUpdate();
    timerCallback();
    startTimerHz(30);
}

Editor::~Editor() {
    stopTimer();
    motion.setLookAndFeel(nullptr);
    bypassUiLink.reset();
    rememberWindow();
    tips.setLookAndFeel(nullptr);
    setLookAndFeel(nullptr);
}

void Editor::openRouting() {
    routingVisible = true;
    syncDrawer();
    if (!routingOpened) {
        routingOpened = true;
        routingPanel.fitIfUnpositioned();
    }
}

void Editor::applyTheme() {
    const auto selectedTheme = themeFromInt(owner.getSkin());
    getProperties().set(vocalstackThemeProperty, int(selectedTheme));
    style.flash = selectedTheme == Theme::neon;
    const auto palette = themePalette(*this);
    auto& stackScrollBar = stackViewport.getVerticalScrollBar();
    const auto scrollAccent = selectedTheme == Theme::neon ? palette.secondary : palette.accent;
    stackScrollBar.setColour(ScrollBar::thumbColourId, palette.border.interpolatedWith(scrollAccent, .55f));
    stackScrollBar.setColour(ScrollBar::backgroundColourId, palette.inset);
    style.setColour(TooltipWindow::backgroundColourId, palette.inset);
    style.setColour(TooltipWindow::textColourId, palette.text);
    style.setColour(TooltipWindow::outlineColourId, palette.border);
    style.setColour(TextEditor::backgroundColourId, palette.inset);
    style.setColour(TextEditor::textColourId, palette.text);
    style.setColour(TextEditor::outlineColourId, palette.border);
    style.setColour(TextEditor::focusedOutlineColourId, palette.accent);
    presets.applyTheme();
    globalMix.setNeon(style.flash);
    motion.setColour(Slider::trackColourId, palette.secondary);
    motion.setColour(Slider::textBoxTextColourId, palette.text);
    motion.setColour(Slider::textBoxBackgroundColourId, Colours::transparentBlack);
    motion.setColour(Slider::textBoxOutlineColourId, Colours::transparentBlack);
    const bool usePalette = selectedTheme == Theme::spectral || selectedTheme == Theme::grass;
    assignmentBanner.setFont(themeFont(*this, 12.f, Font::bold));
    assignmentBanner.setColour(Label::textColourId, usePalette ? palette.accent : rgb(0xb798ff));
    meterPanel.setNeon(style.flash);
    advancedPanel.setNeon(style.flash);
    mainBusFxRack.setNeon(style.flash);
    routingPanel.setNeon(style.flash);
    const auto foreground = palette.text;
    const auto muted = palette.muted;
    theme.setButtonText(selectedTheme == Theme::grass ? "SKIN / GRASS"
                     : selectedTheme == Theme::spectral ? "SKIN / SPECTRAL"
                     : style.flash ? "SKIN  /  NEON" : "SKIN  /  DEVICE");
    for (auto& label : labels) {
        label.setColour(Label::textColourId, foreground);
        label.setFont(themeFont(*this, 14.f));
    }
    auto& leadStackLabel = labels[1];
    leadStackLabel.setBorderSize({0, leadStackLabelPadding, 0, leadStackLabelPadding});
    leadStackLabel.setFont(fitTextWidth(leadStackLabel.getFont(), leadStackLabel.getText(),
                                      knobLabelWidth - 2 * leadStackLabelPadding - 2));
    const auto themeKnob = [&](Slider& knob) {
        knob.setColour(Slider::trackColourId, palette.accent);
        knob.setColour(Slider::thumbColourId, palette.text);
        knob.setColour(Slider::textBoxTextColourId, muted);
        knob.setColour(Slider::textBoxOutlineColourId, Colours::transparentBlack);
        knob.setColour(Slider::textBoxBackgroundColourId, Colours::transparentBlack);
        knob.setColour(Slider::textBoxHighlightColourId, usePalette
                    ? palette.accent.withAlpha(0.3f) : rgb(style.flash ? 0x34496a : 0x4b5175));
        knob.repaint();
    };
    for (auto& knob : knobs) themeKnob(knob);
    themeKnob(sidechainBlend);
    sidechainLabel.setFont(themeFont(*this, 14.f));
    updateSidechainAppearance(sidechainBlend.isEnabled());
    gainRiderReadout.setFont(themeFont(*this, 11.5f, Font::bold));
    updateGainRiderReadout(owner.getMeters().correctionDb);
    for (auto* button : {&theme, &reset, &copy, &paste, &stackTab, &routingTab}) {
        button->setColour(TextButton::textColourOffId, foreground);
        button->setColour(TextButton::textColourOnId, rgb(0x17171c));
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
    if (getWidth() <= 0 || getHeight() <= 0) return;
    const int contentHeight = jmax(1, getHeight() - presetFooterHeight);
    presets.setBounds(18, contentHeight + 4, jmax(0, getWidth() - 36), presetFooterHeight - 8);
    const int viewportWidth = routingVisible ? getWidth() : jmin(getWidth(), editorWidth + stackViewport.getScrollBarThickness());
    stackViewport.setBounds((getWidth() - viewportWidth) / 2, 0, viewportWidth, contentHeight);
    stackViewport.setScrollBarsShown(!routingVisible, false);
    layoutCanvas.setTransform(AffineTransform());
    if (routingVisible) {
        layoutCanvas.setSize(getWidth(), contentHeight);
        stackViewport.setViewPosition(0, 0);
    } else {
        layoutStackRack();
    }
    constexpr std::array<int, 6> centers{70, 183, 285, 387, 500, 614};
    for (size_t i = 1; i < knobs.size(); ++i) {
        labels[i].setBounds(centers[i] - knobLabelWidth / 2, 120, knobLabelWidth, 24);
        knobs[i].setBounds(centers[i] - 50, 148, 100, 116);
    }
    sidechainLabel.setBounds(centers[0] - 50, 120, 100, 24);
    sidechainBlend.setBounds(centers[0] - 50, 148, 100, 116);
    switches[0].setBounds(10, 4, 80, 28);
    labels[0].setBounds(0, 37, 100, 24);
    knobs[0].setBounds(0, 65, 100, 116);
    gainRiderReadout.setBounds(0, 184, 100, 18);
    switches[1].setBounds(242, 86, 86, 28);
    switches[2].setBounds(456, 86, 88, 28);
    const int right = routingVisible ? getWidth() - 18 : 922;
    switches[3].setBounds(right - 85, 25, 85, 28);
    globalMix.setBounds(right - 185, 10, 90, 54);
    theme.setBounds(right - 332, 25, 138, 28);
    reset.setBounds(24, 304, 72, 28);
    copy.setBounds(106, 304, 116, 28);
    paste.setBounds(232, 304, 72, 28);
    meterPanel.setBounds(678, 76, 244, 246);

    motion.setBounds(228, 14, 140, 46);
    stackTab.setBounds(376, 25, 76, 28);
    routingTab.setBounds(458, 25, 86, 28);
    routingPanel.setBounds(18, 100, jmax(0, getWidth() - 36), jmax(0, contentHeight - 118));
    const int assignmentY = routingVisible ? 70 : stackAssignmentY;
    const int assignmentDoneX = routingVisible ? getWidth() - 98 : 790;
    const int assignmentGap = routingVisible ? 6 : 8;
    assignmentBanner.setBounds(24, assignmentY, jmax(0, assignmentDoneX - 24 - assignmentGap), assignmentRowHeight);
    endAssignment.setBounds(assignmentDoneX, assignmentY, 74, assignmentRowHeight);
    advancedPanel.setBounds(18, stackPanelY, 904, stackPanelHeight);
    if (!restoringWindow) rememberWindow();
}

void Editor::layoutStackRack() {
    constexpr int inset = 18, width = editorWidth - 2 * inset;
    const int rackHeight = mainBusFxRack.preferredHeightForWidth(width);
    mainBusFxRack.setBounds(inset, stackRackY, width, rackHeight);
    layoutCanvas.setSize(editorWidth, mainBusFxRack.getBottom() + stackBottomPadding);
}

void Editor::syncDrawer() {

    for (size_t i = 1; i < knobs.size(); ++i) {
        knobs[i].setVisible(!routingVisible);
        labels[i].setVisible(!routingVisible);
    }
    sidechainBlend.setVisible(!routingVisible);
    sidechainLabel.setVisible(!routingVisible);
    for (size_t i = 1; i < switches.size() - 1; ++i)
        switches[i].setVisible(!routingVisible);
    for (auto* button : {&theme, &reset, &copy, &paste})
        button->setVisible(!routingVisible);
    meterPanel.setVisible(!routingVisible);
    mainBusFxRack.setVisible(!routingVisible);
    advancedPanel.setVisible(!routingVisible);
    routingPanel.setVisible(routingVisible);
    stackTab.setToggleState(!routingVisible, dontSendNotification);
    routingTab.setToggleState(routingVisible, dontSendNotification);

    if (!routingVisible) {
        const auto tree = owner.getRoutingLayout();
        selectedVoiceSlot = jlimit(-1, 7, int(tree.getProperty("selectedVoiceSlot", selectedVoiceSlot)));
        advancedPanel.selectVoice(selectedVoiceSlot, true, false);
        syncSidechain(true);
        mainBusFxRack.sync();
    }
    resized();
    advancedPanel.sync();
    leaveKeyboardToHost(*this);
    repaint();
}

void Editor::updateBypassControls(float bypassValue) {
    const bool bypassed = bypassValue >= 0.5f;
    globalMix.setEnabled(!bypassed);
    globalMix.setAlpha(bypassed ? 0.35f : 1.0f);
}

void Editor::updateGainRiderReadout(float correctionDb) {
    const auto p = themePalette(*this);
    const bool unity = !std::isfinite(correctionDb) || std::abs(correctionDb) < 0.05f;
    const auto signedGain = unity ? String("0.0")
        : (correctionDb > 0.f ? "+" : "") + String(correctionDb, 1);
    gainRiderReadout.setText("Ride " + signedGain + " dB", dontSendNotification);
    gainRiderReadout.setColour(Label::textColourId, unity ? p.muted : p.text);
}

void Editor::syncSidechain(bool force) {
    if (owner.externalSidechainChannels() <= 0) {
        sidechainInsertionChecked = false;
        nextSidechainPollMs = 0.0;
        if (sidechainBlend.isEnabled()) updateSidechainAppearance(false);
        return;
    }

    // Keep bus changes responsive without copying the routing snapshot at meter rate.
    const double now = Time::getMillisecondCounterHiRes();
    if (!force && now < nextSidechainPollMs) return;
    nextSidechainPollMs = now + 100.0;
    bool available = false;
    if (!owner.routingRestartPending()) {
        const bool newlyEnabled = !sidechainInsertionChecked;
        sidechainInsertionChecked = true;
        auto graph = owner.getRoutingGraph();
        // Only offer automatic insertion on bus activation. Do not fight subsequent Routing edits.
        if (newlyEnabled && std::none_of(graph.nodes.begin(), graph.nodes.end(), [](const auto& node) {
                return node.id == sidechainBlendNodeId && node.type == NodeType::sidechainBlend;
            })) {
            String ignored;
            if (owner.ensureSidechainBlend(ignored)) graph = owner.getRoutingGraph();
        }
        available = hasUsableSidechainBlend(graph) && !owner.routingRestartPending();
    }
    if (available != sidechainBlend.isEnabled()) updateSidechainAppearance(available);
}

void Editor::updateSidechainAppearance(bool available) {
    const auto accent = sidechainAccent(componentTheme(*this));
    sidechainBlend.setEnabled(available);
    sidechainBlend.setColour(Slider::trackColourId, available ? accent : rgb(0x717171));
    sidechainBlend.setColour(Slider::textBoxTextColourId, available ? accent : rgb(0xa5a5a5));
    sidechainLabel.setEnabled(available);
    sidechainLabel.setText(available ? "Sidechain" : "Sidechain?", dontSendNotification);
    sidechainLabel.setColour(Label::textColourId, available ? accent : rgb(0xa5a5a5));
    layoutCanvas.repaint(controlPlates[0]);
}

void Editor::visibilityChanged() {
    decorativeMotion.pause();
}

void Editor::timerCallback() {
    if (isShowing()) {
        const double now = Time::getMillisecondCounterHiRes() * 0.001;
        decorativeMotion.advance(now, true, owner.getMotionPercent());
        motion.setValue(owner.getMotionPercent(), dontSendNotification);
        getProperties().set(vocalstackAnimationTimeProperty, decorativeMotion.seconds());
        // Sliders consume the fresh phase through their existing modulation
        // refresh. Repaint whole surfaces only for a new background frame.
        const double frame = std::floor(jlimit(0.0, 1.0e9, fluidAnimationSeconds(*this)) * 10.0);
        if (frame != lastBackgroundFrame) {
            lastBackgroundFrame = frame;
            style.setDecorativePhase(fluidAnimationSeconds(*this));
            // Popup and tooltip peers are outside this component subtree. Repaint
            // only peers sharing this editor's look-and-feel, on the same clock.
            auto& desktop = Desktop::getInstance();
            for (int i = 0; i < desktop.getNumComponents(); ++i) {
                auto* peer = desktop.getComponent(i);
                if (peer != this && &peer->getLookAndFeel() == &style) peer->repaint();
            }
            // The canvas paints its whole rectangle. Skip covered root material,
            // but retain root damage below short content in a tall viewport.
            auto exposed = RectangleList<int>(getLocalBounds());
            exposed.subtract(getLocalArea(&layoutCanvas, layoutCanvas.getLocalBounds()).getIntersection(stackViewport.getBounds()));
            for (const auto& rect : exposed) repaint(rect);
            // Voice/rack surrounds still rely on this decorative clock; excluding
            // their whole rectangles would freeze materials between value edits.
            if (layoutCanvas.isShowing()) layoutCanvas.repaint();
        }
    } else {
        decorativeMotion.pause();
    }
    if (advancedPanel.isVisible() != !routingVisible)
        syncDrawer();
    advancedPanel.sync();
    if (themeFromInt(owner.getSkin()) != componentTheme(*this))
        applyTheme();
    if (feedbackTicks > 0 && --feedbackTicks == 0) {
        copy.setButtonText("Copy settings");
        paste.setButtonText("Paste");
    }
    const auto meters = owner.getMeters();
    meterPanel.setMeters(meters);
    if (!routingVisible) {
        syncSidechain();
        updateGainRiderReadout(meters.correctionDb);
    }
    updateBypassControls(owner.parameters.getRawParameterValue("bypass")->load());
    const auto assigning = owner.modulationAssignmentId();
    const auto modulationState = owner.getModulationState();
    modulationui::refreshWithSelection(*this, owner, owner.getModulationSnapshot(), modulationState, assigning);
    const auto repaintSwitches = [](auto&& self, Component& component) -> void {
        if (dynamic_cast<modulationui::Button*>(&component)) component.repaint();
        for (auto* child : component.getChildren()) if (child->isVisible()) self(self, *child);
    };
    repaintSwitches(repaintSwitches, *this);
    assignmentBanner.setVisible(assigning != 0);
    endAssignment.setVisible(assigning != 0);
    if (assigning != 0) {
        String name = "Modulator " + String(assigning);
        for (const auto& mod : modulationState.modulators) if (mod.id == assigning)
            name = String(mod.type == ModulatorType::lfo ? "LFO " : "Random ") + String(assigning);
        assignmentBanner.setText("Assigning: " + name + "  /  drag a control to set depth", dontSendNotification);
    }
}

void Editor::revealParameter(const String& id) {
    const auto find = [&](auto&& self, Component& parent) -> Component* {
        if (parent.getComponentID() == id) return &parent;
        for (auto* child : parent.getChildren())
            if (auto* found = self(self, *child)) return found;
        return nullptr;
    };
    auto* target = find(find, layoutCanvas);
    if (target == nullptr) return;
    routingVisible = false;
    syncDrawer();
    if (gainRiderControls.isParentOf(target)) {
        advancedPanel.selectVoice(-1);
        const auto bounds = layoutCanvas.getLocalArea(target, target->getLocalBounds());
        stackViewport.setViewPosition(0, jmax(0, bounds.getBottom() - stackViewport.getHeight() + 12));
    }
    revealBubble.toFront(false);
    AttributedString text;
    text.append("Target: " + (target->getName().isNotEmpty() ? target->getName() : id),
                themeFont(*this, 12.f), Colours::white);
    revealBubble.showAt(target, text, 1600, true, false);
    leaveKeyboardToHost(revealBubble);
}

void Editor::rememberWindow() {
    if (restoringWindow || getWidth() <= 0 || getHeight() <= 0) return;
    auto tree = owner.getRoutingLayout();
    if (int(tree.getProperty("editorWidth")) == getWidth() && int(tree.getProperty("editorHeight")) == getHeight()) return;
    tree.setProperty("editorWidth", getWidth(), nullptr);
    tree.setProperty("editorHeight", getHeight(), nullptr);
    owner.setRoutingLayout(tree);
}

bool Editor::keyPressed(const KeyPress& key) {
    if (key == KeyPress::escapeKey && owner.modulationAssignmentId() != 0) {
        owner.endModulationAssignment(); return true;
    }
    return false;
}

void Editor::paint(Graphics& g) {
    const auto palette = themePalette(*this);
    const auto tint = componentTheme(*this) == Theme::grass ? palette.secondary : palette.accent;
    rootBackground.paint(g, getLocalBounds(), palette.background, tint, fluidAnimationSeconds(*this));
}

void Editor::drawDevice(Graphics& g) {
    canvasBackground.paint(g, layoutCanvas.getLocalBounds(), rgb(0x292b2a),
                           themePalette(*this).accent, fluidAnimationSeconds(*this));
    headerBackground.paint(g, {18, 14, routingVisible ? getWidth() - 36 : 904, 50},
        rgb(0x373a37), themePalette(*this).accent, fluidAnimationSeconds(*this), .45f);
    g.setColour(rgb(0xf5a24b));
    g.fillRect(27.0f, 25.0f, 4.0f, 25.0f);
    text(*this, g, "HEAVEN", {41, 16, 168, 27}, rgb(0xf4f4ea), 23.0f, true);
    text(*this, g, "by lil grass block", {41, 42, 168, 18}, rgb(0xf4f4ea), 12.0f);


    if (routingVisible)
        return;

    plateSurround.paint(g, {14, 72, 656, 222}, rgb(0x1c1f1e),
        themePalette(*this).accent, fluidAnimationSeconds(*this), .45f);
    for (size_t i = 0; i < controlPlates.size(); ++i) {
        const auto& plate = controlPlates[i];
        const auto area = plate.toFloat();
        {
            Graphics::ScopedSaveState state(g);
            Path clip; clip.addRectangle(area);
            g.reduceClipRegion(clip);
            controlBackgrounds[i].paint(g, plate, rgb(0x252826), themePalette(*this).accent, fluidAnimationSeconds(*this), 1.f, 24);
        }
        g.setColour(rgb(0x343a35));
        g.drawRect(area, 1.0f);
    }
    text(*this, g, "FINAL GAIN", {570, 88, 88, 23}, rgb(0xaeb1a9), 12.0f, true, Justification::centred);
    for (size_t i = 1; i < controlPlates.size(); ++i)
        drawControlCaption(*this, g, i, rgb(0xaeb1a9));
    text(*this, g, "TRUE MIX", {542, 306, 124, 23},
         rgb(0x9b9f96), 12.0f, true, Justification::centredRight);
}

void Editor::drawGrid(Graphics& g) {
    const auto skin = componentTheme(*this);
    const auto p = themePalette(skin);
    const bool grass = skin == Theme::grass;
    const double phase = fluidAnimationSeconds(*this);
    canvasBackground.paint(g, layoutCanvas.getLocalBounds(), p.background, grass ? p.secondary : p.accent, phase);
    const Rectangle<int> header{18, 14, routingVisible ? getWidth() - 36 : 904, 50};
    headerBackground.paint(g, header, p.panel, p.accent, phase, .45f);
    g.setColour(p.border);
    g.drawRect(header);

    g.setColour(p.accent);
    g.fillRect(18, 14, 4, 50);
    g.drawRect(35, 22, 32, 32);
    for (int x = 40; x <= 52; x += 6)
        g.drawLine(static_cast<float>(x), 46.0f, static_cast<float>(x + 8), 30.0f, 2.3f);
    text(*this, g, "HEAVEN", {78, 16, 142, 27}, p.text, 23.0f, !grass);
    text(*this, g, "by lil grass block", {78, 42, 142, 18}, p.text, 12.0f);

    if (routingVisible) return;

    for (size_t i = 0; i < controlPlates.size(); ++i) {
        const auto& plate = controlPlates[i];
        controlBackgrounds[i].paint(g, plate, p.panel, p.accent, phase, 1.f, 24);
        g.setColour(p.border);
        g.drawRect(plate);
        g.setColour(i == 0 ? (sidechainBlend.isEnabled() ? sidechainAccent(skin) : p.border)
                           : i == 2 && !grass ? p.secondary : p.accent);
        g.fillRect(plate.getX(), plate.getY(), plate.getWidth(), 2);
        if (i != 0) drawControlCaption(*this, g, i, p.muted);
    }
    text(*this, g, "FINAL GAIN", {570, 88, 88, 23}, p.accent, 12.0f, !grass, Justification::centred);
    g.setColour(p.border);
    for (const int x : {234, 336})
        g.drawVerticalLine(x, 121.0f, 260.0f);
    g.drawHorizontalLine(298, 18.0f, 666.0f);

    text(*this, g, "TRUE MIX", {542, 306, 124, 23}, p.muted, 12.0f, !grass, Justification::centredRight);
}

void Editor::drawSpectral(Graphics& g) {
    const auto p = themePalette(*this);
    canvasBackground.paint(g, layoutCanvas.getLocalBounds(), p.background, p.accent, fluidAnimationSeconds(*this));
    headerBackground.paint(g, {18, 14, routingVisible ? getWidth() - 36 : 904, 50},
        p.panel, p.accent, fluidAnimationSeconds(*this), .45f);
    g.setColour(p.accent);
    g.fillRect(18, 14, 3, 50);
    text(*this, g, "HEAVEN", {36, 16, 168, 27}, p.text, 23.0f);
    text(*this, g, "by lil grass block", {36, 42, 168, 18}, p.text, 12.0f);

    if (routingVisible) return;
    for (size_t i = 0; i < controlPlates.size(); ++i) {
        const auto& plate = controlPlates[i];
        controlBackgrounds[i].paint(g, plate, p.panel, p.accent, fluidAnimationSeconds(*this), 1.f, 24);
        g.setColour(p.border);
        g.drawRect(plate);
        if (i != 0) drawControlCaption(*this, g, i, p.muted);
    }
    text(*this, g, "FINAL GAIN", {570, 88, 88, 23}, p.muted, 12.0f, false, Justification::centred);
    text(*this, g, "TRUE MIX", {542, 306, 124, 23}, p.muted, 12.0f, false, Justification::centredRight);
}

void Editor::Style::drawRotarySlider(Graphics& g, int x, int y, int w, int h, float position,
                                    float start, float end, Slider& slider) {
    const auto area = Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
                                       static_cast<float>(w), static_cast<float>(h));
    const float radius = std::min(area.getWidth(), area.getHeight()) * (flash ? 0.41f : 0.37f);
    const float ringWidth = ParameterKnob::Style::rotaryRingWidth(radius + 2.56f);
    const float ringRadius = radius + 2.56f - ringWidth * .5f;
    const auto centre = area.getCentre();
    const float angle = start + position * (end - start);
    const auto name = slider.getName();
    const auto skin = componentTheme(slider);
    const auto p = themePalette(skin);
    const bool usePalette = skin == Theme::spectral || skin == Theme::grass;
    const bool sidechain = slider.getComponentID() == sidechainParameterId;
    const auto accent = sidechain ? slider.findColour(Slider::trackColourId)
        : usePalette ? (name == "Formant" ? p.secondary : p.accent)
        : rgb(!flash ? 0xf5a24b : name == "Formant" ? signCyan
             : slider.getComponentID() == "level" || name == "Output" ? signYellow : signPink);
    Path track, value;
    track.addCentredArc(centre.x, centre.y, ringRadius, ringRadius, 0.0f, start, end, true);
    g.setColour(p.border);
    g.strokePath(track, PathStrokeType(ringWidth, PathStrokeType::curved, PathStrokeType::butt));
    const float origin = name == "Formant" || name == "Output"
        ? start + static_cast<float>(slider.valueToProportionOfLength(0.0)) * (end - start) : start;
    if (std::abs(angle - origin) > 0.001f) {
        value.addCentredArc(centre.x, centre.y, ringRadius, ringRadius, 0.0f,
                            std::min(angle, origin), std::max(angle, origin), true);
        Path stroke;
        PathStrokeType(ringWidth, PathStrokeType::curved, PathStrokeType::butt).createStrokedPath(stroke, value);
        const Graphics::ScopedSaveState save(g);
        g.reduceClipRegion(stroke);
        arcTexture.paint(g, stroke.getBounds().getSmallestIntegerContainer(), accent, sidechain ? accent : p.accent,
                         fluidAnimationSeconds(slider), .45f);
    }
    if (name == "Formant" || name == "Output") {
        g.setColour(accent.withAlpha(0.8f));
        g.drawLine({centre.getPointOnCircumference(radius + 3.0f, origin),
                    centre.getPointOnCircumference(radius + 7.0f, origin)}, 1.2f);
    }
    const float inner = jmax(1.f, radius + 2.56f - ringWidth - 1.f);
    const auto face = Rectangle<float>(inner * 2, inner * 2).withCentre(centre);
    if (usePalette || flash)
        g.setColour(usePalette ? p.inset : rgb(night));
    else
        g.setGradientFill(ColourGradient(rgb(0x414641), centre.x, face.getY(),
                                         rgb(0x282b28), centre.x, face.getBottom(), false));
    g.fillEllipse(face);
    { Path clip; clip.addEllipse(face); Graphics::ScopedSaveState state(g);
      g.reduceClipRegion(clip); g.setOpacity(.18f);
      buttonBackground.paint(g, slider, face.toNearestInt(), usePalette || flash ? p.inset : rgb(0x343a34),
                             sidechain && !slider.isEnabled() ? accent : p.accent); }
    g.setColour(usePalette ? p.border : rgb(flash ? outline : 0x555b54));
    g.drawEllipse(face, 1.0f);
    g.setColour(accent);
    const auto a = centre.getPointOnCircumference(inner - 3.6f, angle);
    const auto b = centre.getPointOnCircumference(inner - 11.0f, angle);
    g.drawLine({a, b}, 2.6f);
    if (auto* modulation = dynamic_cast<ModulationSlider*>(&slider); modulation && !sidechain)
        modulation->drawModulation(g, centre, radius, start, end);
}

void Editor::Style::drawButtonBackground(Graphics& g, Button& button, const Colour&,
                                         bool hover, bool down) {
    const auto area = button.getLocalBounds().toFloat().reduced(0.7f);
    const bool lit = button.getToggleState();
    const auto name = button.getButtonText();
    const bool viewTab = name == "Routing" || name == "Stack";
    const bool routingAction = name == "Open Routing" || viewTab;
    const auto selectedTheme = componentTheme(button);
    const bool grassTab = selectedTheme == Theme::grass && viewTab;
    const auto p = themePalette(button);
    const auto accent = name == "BYPASS" ? rgb(0xf58680)
        : grassTab ? rgb(0xa4bd7a)
        : selectedTheme == Theme::device ? rgb(0xe3a15e)
        : routingAction || name == "FORMANT" ? p.secondary : p.accent;
    auto fill = lit ? accent : grassTab ? rgb(0x5b5037)
        : routingAction ? accent.withAlpha(selectedTheme == Theme::device ? 0.18f : 0.12f)
        : selectedTheme == Theme::device ? rgb(0x333733) : p.panel;
    if (hover) fill = fill.brighter(0.12f);
    if (down) fill = fill.darker(0.12f);
    // Keep lit/pressed/hover colours as the material base, with the existing
    // border and text painted afterwards, clipped to the square silhouette.
    {
        Graphics::ScopedSaveState state(g);
        Path clip;
        clip.addRectangle(area);
        g.reduceClipRegion(clip);
        buttonBackground.paint(g, button, button.getLocalBounds(), fill, p.accent);
    }
    const bool focused = button.hasKeyboardFocus(false);
    if (selectedTheme != Theme::device) {
        g.setColour(grassTab && !lit && !focused ? rgb(0x877c54)
            : lit || focused || routingAction || name.startsWith("SKIN") ? accent : p.border);
        g.drawRect(area, focused ? 2.0f : 1.0f);
    } else {
        g.setColour(lit || routingAction ? accent.brighter(0.13f) : rgb(0x515850));
        g.drawRect(area, focused ? 1.6f : 0.7f);
    }
}

juce::Font Editor::Style::getTextButtonFont(juce::TextButton& button, int) {
    const auto skin = componentTheme(button);
    return themeFont(skin, 12.f, skin == Theme::device || skin == Theme::neon ? Font::bold : Font::plain);
}

} // namespace vocalstack
