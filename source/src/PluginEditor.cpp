#include "VisualTheme.h"
#include "PluginEditor.h"

namespace vocalstack {
namespace {
using namespace juce;
Colour rgb(uint32_t hex) { return Colour(0xff000000u | hex); }
Font font(float height, bool bold = false) {
    return Font(pixelFontOptions(height, bold ? Font::bold : Font::plain));
}

using namespace editorLayout;
const std::array<Rectangle<int>, 4> controlPlates{{
    {18, 76, 104, 214}, {132, 76, 306, 214},
    {448, 76, 104, 214}, {562, 76, 104, 214}
}};
constexpr uint32_t night = 0x090d17, panel = 0x111827, outline = 0x2b3a53;
constexpr uint32_t signPink = 0xff4ca0, signCyan = 0x64f5e3, signYellow = 0xf8de6b;
constexpr uint32_t nightText = 0xf1f5ff, nightMuted = 0x9eafc8;
constexpr std::array<const char*, knobParameterIds.size()> knobNames{
    "Leveler", "Lead / Stack", "Human", "Width", "Formant", "Output"
};
constexpr std::array<const char*, knobParameterIds.size()> knobTips{
    "Make quiet and loud phrases more even. 0% is off. This does not prevent clipping.",
    "Blend the processed lead with generated voices. Use Mix for the untouched input.",
    "How far the doubles vary in timing, pitch and tone",
    "How widely the generated takes are panned",
    "Make the voice tone deeper or brighter without changing the note. Double-click to reset.",
    "Turn the final output up or down. Double-click for 0 dB (no change)."
};
constexpr std::array<const char*, switchParameterIds.size()> switchNames{"LEVEL", "DOUBLE", "FORMANT", "BYPASS"};

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

void text(Graphics& g, const String& value, Rectangle<int> box, Colour color,
          float size, bool bold = false, Justification align = Justification::centredLeft) {
    g.setFont(font(size, bold));
    g.setColour(color);
    g.drawText(value, box, align, false);
}
}

Editor::Editor(Processor& processor)
    : AudioProcessorEditor(processor), owner(processor), presets(processor), globalMix(processor, "globalMix", "Mix", "%"), routingPanel(processor),
      advancedPanel(processor), mainBusFxRack(processor) {
    setLookAndFeel(&style);
    getProperties().set(vocalstackAnimationTimeProperty, 0.0);
    addAndMakeVisible(stackViewport);
    stackViewport.setComponentID("stackViewport");
    layoutCanvas.setOpaque(true); // Every skin paints its full canvas background.
    stackViewport.setViewedComponent(&layoutCanvas, false);
    stackViewport.setScrollBarsShown(true, false);
    layoutCanvas.draw = [this](Graphics& g) {
        if (componentTheme(*this) == Theme::spectral) drawSpectral(g);
        else if (style.flash) drawFlash(g);
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
    assignmentBanner.setInterceptsMouseClicks(false, false);
    assignmentBanner.setFont(font(12, true));
    layoutCanvas.addChildComponent(assignmentBanner);
    endAssignment.setButtonText("Done");
    endAssignment.onClick = [this] { owner.endModulationAssignment(); };
    layoutCanvas.addChildComponent(endAssignment);
    for (size_t i = 0; i < knobs.size(); ++i) {
        auto& knob = knobs[i];
        knob.bindModulation(owner, knobParameterIds[i]);
        knob.setName(knobNames[i]);
        knob.setTitle(knobNames[i]);
        knob.setTooltip(knobTips[i]);
        knob.setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
        knob.setTextBoxStyle(Slider::TextBoxBelow, false, 82, 22);
        knob.setScrollWheelEnabled(false);
        layoutCanvas.addAndMakeVisible(knob);
        knobLinks[i] = std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(
            owner.parameters, knobParameterIds[i], knob);
        if (const auto* parameter = owner.parameters.getParameter(knobParameterIds[i]))
            knob.setDoubleClickReturnValue(true, i == 4 || i == 5 ? 0.0
                : parameter->convertFrom0to1(parameter->getDefaultValue()));
        if (i == 1 || i == 5) {
            knob.setLookAndFeel(i == 1 ? &leadMixStyle : &outputStyle);
            knob.getProperties().set("vocalstackTexturedBar", true);
            knob.getProperties().set("vocalstackSeparateBarValue", true);
            knob.setSliderStyle(Slider::LinearHorizontal);
            knob.setTextBoxStyle(Slider::TextBoxAbove, false, 90, 20);
            if (i == 1) configureCrossfadeDisplay(knob, true);
        }
        labels[i].setText(knobNames[i], dontSendNotification);
        labels[i].setJustificationType(Justification::centred);
        labels[i].setInterceptsMouseClicks(false, false);
        layoutCanvas.addAndMakeVisible(labels[i]);
    }
    for (size_t i = 0; i < switches.size(); ++i) {
        auto& button = switches[i];
        button.setModulationOwner(owner);
        button.setComponentID(switchParameterIds[i]);
        button.setName(switchNames[i]);
        button.setButtonText(switchNames[i]);
        button.setClickingTogglesState(true);
        button.setTooltip(i == 3 ? "Bypass the whole effect with latency compensation"
                                 : "Toggle this module on or off");
        layoutCanvas.addAndMakeVisible(button);
        switchLinks[i] = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment>(
            owner.parameters, switchParameterIds[i], button);
    }

    for (auto* button : {&theme, &reset, &copy, &paste})
        layoutCanvas.addAndMakeVisible(*button);
    theme.setTooltip("Cycle skins: Device / Neon / Spectral");
    theme.onClick = [this] { owner.setSkin((owner.getSkin() + 1) % 3); applyTheme(); };
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
    assignmentBanner.setColour(Label::textColourId, selectedTheme == Theme::spectral ? palette.accent : rgb(0xb798ff));
    meterPanel.setNeon(style.flash);
    advancedPanel.setNeon(style.flash);
    mainBusFxRack.setNeon(style.flash);
    routingPanel.setNeon(style.flash);
    const auto foreground = palette.text;
    const auto muted = palette.muted;
    theme.setButtonText(selectedTheme == Theme::spectral ? "SKIN / SPECTRAL"
                       : style.flash ? "SKIN  /  NEON" : "SKIN  /  DEVICE");
    for (auto& label : labels) {
        label.setColour(Label::textColourId, foreground);
        label.setFont(font(14.0f, false));
    }
    for (auto& knob : knobs) {
        knob.setColour(Slider::trackColourId, palette.accent);
        knob.setColour(Slider::thumbColourId, palette.text);
        knob.setColour(Slider::textBoxTextColourId, muted);
        knob.setColour(Slider::textBoxOutlineColourId, Colours::transparentBlack);
        knob.setColour(Slider::textBoxBackgroundColourId, Colours::transparentBlack);
        knob.setColour(Slider::textBoxHighlightColourId, selectedTheme == Theme::spectral
                    ? palette.accent.withAlpha(0.3f) : rgb(style.flash ? 0x34496a : 0x4b5175));
        knob.repaint();
    }
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
    presets.setBounds(18, getHeight() - 48, jmax(0, getWidth() - 36), 44);
    const int viewportWidth = routingVisible ? getWidth() : jmin(getWidth(), editorWidth + stackViewport.getScrollBarThickness());
    stackViewport.setBounds((getWidth() - viewportWidth) / 2, 0, viewportWidth, contentHeight);
    stackViewport.setScrollBarsShown(!routingVisible, false);
    layoutCanvas.setTransform(AffineTransform());
    layoutCanvas.setSize(routingVisible ? getWidth() : editorWidth, routingVisible ? contentHeight : stackCanvasHeight);
    if (routingVisible) stackViewport.setViewPosition(0, 0);
    constexpr std::array<int, 6> centers{70, 183, 285, 387, 500, 614};
    for (size_t i = 0; i < knobs.size(); ++i) {
        labels[i].setBounds(centers[i] - 44, 121, 88, 20);
        knobs[i].setBounds(centers[i] - 45, i == 1 || i == 5 ? 170 : 148, 90, i == 1 || i == 5 ? 72 : 114);
    }
    switches[0].setBounds(32, 88, 76, 23);
    switches[1].setBounds(244, 88, 82, 23);
    switches[2].setBounds(458, 88, 84, 23);
    const int right = routingVisible ? getWidth() - 18 : 922;
    switches[3].setBounds(right - 85, 26, 85, 26);
    globalMix.setBounds(right - 185, 10, 90, 54);
    theme.setBounds(right - 320, 26, 126, 26);
    reset.setBounds(24, 306, 66, 23);
    copy.setBounds(100, 306, 105, 23);
    paste.setBounds(215, 306, 64, 23);
    meterPanel.setBounds(678, 76, 244, 246);

    motion.setBounds(228, 14, 146, 40);
    stackTab.setBounds(382, 26, 70, 26);
    routingTab.setBounds(458, 26, 78, 26);
    routingPanel.setBounds(18, 100, jmax(0, getWidth() - 36), jmax(0, contentHeight - 118));
    assignmentBanner.setBounds(24, routingVisible ? 70 : 588 + rackExtraHeight, routingVisible ? jmax(0, getWidth() - 118) : 760, 22);
    endAssignment.setBounds(routingVisible ? getWidth() - 88 : 790, routingVisible ? 70 : 588 + rackExtraHeight, 64, 22);
    mainBusFxRack.setBounds(18, 344, 904, MainBusFxRackPanel::preferredHeight);
    advancedPanel.setBounds(18, stackPanelY, 904, stackPanelHeight);
    if (!restoringWindow) rememberWindow();
}

void Editor::syncDrawer() {

    for (auto& knob : knobs) knob.setVisible(!routingVisible);
    for (auto& label : labels) label.setVisible(!routingVisible);
    for (size_t i = 0; i < switches.size() - 1; ++i)
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
    meterPanel.setMeters(owner.getMeters());
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
    revealBubble.toFront(false);
    AttributedString text;
    text.append("Target: " + (target->getName().isNotEmpty() ? target->getName() : id),
                font(12.0f), Colours::white);
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
    rootBackground.paint(g, getLocalBounds(), palette.background, palette.accent, fluidAnimationSeconds(*this));
}

void Editor::drawDevice(Graphics& g) {
    canvasBackground.paint(g, layoutCanvas.getLocalBounds(), rgb(0x292b2a),
                           themePalette(*this).accent, fluidAnimationSeconds(*this));
    headerBackground.paint(g, {18, 14, routingVisible ? getWidth() - 36 : 904, 50},
        rgb(0x373a37), themePalette(*this).accent, fluidAnimationSeconds(*this), .45f);
    g.setColour(rgb(0xf5a24b));
    g.fillRect(27.0f, 25.0f, 4.0f, 25.0f);
    text(g, "Vocal Stack", {41, 20, 168, 32}, rgb(0xf4f4ea), 22.0f, true);


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
    canvasBackground.paint(g, layoutCanvas.getLocalBounds(), rgb(night),
                           themePalette(*this).accent, fluidAnimationSeconds(*this));
    const auto drawPlate = [&](Rectangle<int> plate) {
        headerBackground.paint(g, plate, rgb(panel), themePalette(*this).accent,
            fluidAnimationSeconds(*this), .45f);
        g.setColour(rgb(outline));
        g.drawRect(plate);
    };
    drawPlate({18, 14, routingVisible ? getWidth() - 36 : 904, 50});

    g.setColour(rgb(signPink));
    g.fillRect(18, 14, 4, 50);
    g.drawRect(35, 22, 32, 32);
    for (int x = 40; x <= 52; x += 6)
        g.drawLine(static_cast<float>(x), 46.0f, static_cast<float>(x + 8), 30.0f, 2.3f);
    text(g, "VOCAL STACK", {78, 20, 142, 30}, rgb(nightText), 22.0f, true);


    if (routingVisible)
        return;

    for (size_t i = 0; i < controlPlates.size(); ++i) {
        const auto& plate = controlPlates[i];
        controlBackgrounds[i].paint(g, plate, rgb(panel), themePalette(*this).accent, fluidAnimationSeconds(*this), 1.f, 24);
        g.setColour(rgb(outline));
        g.drawRect(plate);
    }

    constexpr std::array<uint32_t, 4> accents{signPink, signPink, signCyan, signPink};
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
    text(g, "FINAL GAIN", {570, 88, 88, 23}, rgb(signPink), 11.0f, true, Justification::centred);
    g.setColour(rgb(outline));
    for (const int x : {234, 336})
        g.drawVerticalLine(x, 121.0f, 260.0f);
    g.drawHorizontalLine(298, 18.0f, 666.0f);

    text(g, "TRUE MIX", {542, 306, 124, 23},
         rgb(nightMuted), 11.0f, true, Justification::centredRight);
}

void Editor::drawSpectral(Graphics& g) {
    const auto p = themePalette(*this);
    canvasBackground.paint(g, layoutCanvas.getLocalBounds(), p.background, p.accent, fluidAnimationSeconds(*this));
    headerBackground.paint(g, {18, 14, routingVisible ? getWidth() - 36 : 904, 50},
        p.panel, p.accent, fluidAnimationSeconds(*this), .45f);
    g.setColour(p.accent);
    g.fillRect(18, 14, 3, 50);
    text(g, "Vocal Stack", {36, 21, 168, 30}, p.text, 23.0f);

    if (routingVisible) return;
    const std::array<const char*, 4> captions{
        "PHRASE CONTROL", "GENERATED TAKES", "PITCH PRESERVED", "OUTPUT TRIM"
    };
    for (size_t i = 0; i < controlPlates.size(); ++i) {
        const auto& plate = controlPlates[i];
        controlBackgrounds[i].paint(g, plate, p.panel, p.accent, fluidAnimationSeconds(*this), 1.f, 24);
        g.setColour(p.border);
        g.drawRect(plate);
        text(g, captions[i], {plate.getX(), 269, plate.getWidth(), 15},
             p.muted, 9.5f, false, Justification::centred);
    }
    text(g, "FINAL GAIN", {570, 88, 88, 23}, p.muted, 10.5f, false, Justification::centred);
    text(g, "TRUE MIX", {542, 306, 124, 23}, p.muted, 10.0f, false, Justification::centredRight);
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
    const auto p = themePalette(slider);
    const bool spectral = componentTheme(slider) == Theme::spectral;
    const auto accent = spectral ? (name == "Formant" ? p.secondary : p.accent) : rgb(!flash ? 0xf5a24b : name == "Formant" ? signCyan
                            : name == "Leveler" || name == "Output" ? signYellow : signPink);
    Path track, value;
    track.addCentredArc(centre.x, centre.y, ringRadius, ringRadius, 0.0f, start, end, true);
    g.setColour(spectral ? p.border : flash ? rgb(outline) : rgb(0x50564f));
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
        arcTexture.paint(g, stroke.getBounds().getSmallestIntegerContainer(), accent, p.accent,
                         fluidAnimationSeconds(slider), .45f);
    }
    if (name == "Formant" || name == "Output") {
        g.setColour(accent.withAlpha(0.8f));
        g.drawLine({centre.getPointOnCircumference(radius + 3.0f, origin),
                    centre.getPointOnCircumference(radius + 7.0f, origin)}, 1.2f);
    }
    const float inner = jmax(1.f, radius + 2.56f - ringWidth - 1.f);
    const auto face = Rectangle<float>(inner * 2, inner * 2).withCentre(centre);
    if (spectral || flash)
        g.setColour(spectral ? p.inset : rgb(night));
    else
        g.setGradientFill(ColourGradient(rgb(0x414641), centre.x, face.getY(),
                                         rgb(0x282b28), centre.x, face.getBottom(), false));
    g.fillEllipse(face);
    { Path clip; clip.addEllipse(face); Graphics::ScopedSaveState state(g);
      g.reduceClipRegion(clip); g.setOpacity(.18f);
      buttonBackground.paint(g, slider, face.toNearestInt(), spectral || flash ? p.inset : rgb(0x343a34), p.accent); }
    g.setColour(spectral ? p.border : rgb(flash ? outline : 0x555b54));
    g.drawEllipse(face, 1.0f);
    g.setColour(accent);
    const auto a = centre.getPointOnCircumference(inner - 3.6f, angle);
    const auto b = centre.getPointOnCircumference(inner - 11.0f, angle);
    g.drawLine({a, b}, 2.6f);
    if (auto* modulation = dynamic_cast<ModulationSlider*>(&slider))
        modulation->drawModulation(g, centre, radius, start, end);
}

void Editor::Style::drawButtonBackground(Graphics& g, Button& button, const Colour&,
                                         bool hover, bool down) {
    const auto area = button.getLocalBounds().toFloat().reduced(0.7f);
    const bool lit = button.getToggleState();
    const auto name = button.getButtonText();
    const bool routingAction = name == "Open Routing" || name == "Routing" || name == "Stack";
    const auto selectedTheme = componentTheme(button);
    const auto p = themePalette(button);
    const auto accent = name == "BYPASS" ? rgb(0xf58680)
        : selectedTheme == Theme::device ? rgb(0xe3a15e)
        : routingAction || name == "FORMANT" ? p.secondary : p.accent;
    auto fill = lit ? accent : routingAction ? accent.withAlpha(selectedTheme == Theme::device ? 0.18f : 0.12f)
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
        g.setColour(lit || focused || routingAction || name.startsWith("SKIN") ? accent : p.border);
        g.drawRect(area, focused ? 2.0f : 1.0f);
    } else {
        g.setColour(lit || routingAction ? accent.brighter(0.13f) : rgb(0x515850));
        g.drawRect(area, focused ? 1.6f : 0.7f);
    }
}

juce::Font Editor::Style::getTextButtonFont(juce::TextButton& button, int) {
    return font(12.0f, componentTheme(button) != Theme::spectral);
}

} // namespace vocalstack
