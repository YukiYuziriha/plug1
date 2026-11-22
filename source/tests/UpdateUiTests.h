#pragma once

#include "ToneUiTests.h"
#include "GuiTestDisplay.h"
#include "PresetTests.h"
#include "CreativeEffectPanel.h"
#include "CreativeEffectVisual.h"
#include "UtilityEffectPanel.h"
#include "VisualTheme.h"
#include "RoutingPanel.h"

namespace vocalstack::updateUiTests {
namespace ui = toneUiTests;
static_assert(int(NodeType::gain) == 14 && int(NodeType::stereo) == 15 && int(NodeType::gate) == 16
              && int(NodeType::xpFilter) == 17 && int(NodeType::synthFilter) == 18);
static_assert(creativeParameterCount == 68 && utilityParameterCount == 21);

template <typename Predicate>
inline void waitFor(Predicate predicate, const juce::String& reason) {
    for (int attempt = 0; attempt < 100; ++attempt) {
        juce::Timer::callPendingTimersSynchronously();
        if (predicate()) return;
        juce::Thread::sleep(5);
    }
    ui::require(predicate(), reason);
}

inline juce::String themeName(int theme) {
    return theme == 2 ? "spectral" : theme == 1 ? "neon" : "device";
}
inline Graph effectGraph(NodeType type) {
    return {{{1, NodeType::lead}, {100, type}, {13, NodeType::output}}, {{1, 100, 0, 0}, {100, 13, 0, 0}}};
}
inline void install(Processor& owner, NodeType type) {
    juce::String error;
    ui::require(owner.setRoutingGraph(effectGraph(type), error), error);
    auto layout = owner.getRoutingLayout();
    layout.setProperty("selectedNode", 100, nullptr);
    layout.removeProperty("selectedVoiceSlot", nullptr);
    owner.setRoutingLayout(layout);
}
inline void write(Processor& owner, const juce::String& id, float value) {
    ui::write(owner, id, value);
}
inline bool samePixels(const juce::Image& a, const juce::Image& b) {
    if (a.getBounds() != b.getBounds()) return false;
    for (int y = 0; y < a.getHeight(); ++y)
        for (int x = 0; x < a.getWidth(); ++x)
            if (a.getPixelAt(x, y) != b.getPixelAt(x, y)) return false;
    return true;
}
inline void inheritTheme(juce::Component& root, int theme) {
    root.getProperties().set(vocalstackThemeProperty, theme);
}

inline juce::TextButton& routingButton(juce::Component& root, const juce::String& text) {
    auto* button = ui::find<juce::TextButton>(root, [&](auto& b) { return b.getButtonText() == text; });
    ui::require(button && button->isShowing() && button->isEnabled() && bool(button->onClick), "Missing routing action: " + text);
    return *button;
}

inline void routingModulatorRail() {
    for (int skin = 0; skin < 3; ++skin) {
        auto owner = std::make_unique<Processor>(); owner->setSkin(skin);
        ModulationState state; state.modulators = {{1}, {2, ModulatorType::random}};
        juce::String error;
        ui::require(owner->setModulationState(state, error), error);
        auto panel = std::make_unique<RoutingPanel>(*owner);
        inheritTheme(*panel, skin); panel->setNeon(skin == 1);
        panel->addToDesktop(0); panel->setVisible(true);
        auto& lfo = routingButton(*panel, "LFO 1");
        auto& random = routingButton(*panel, "Random 2");
        auto* rail = lfo.findParentComponentOfClass<juce::Viewport>();
        ui::require(rail && rail->getWidth() == 66 && lfo.getHeight() == 41 && random.getY() == 44,
                    "Routing rail/tile stride was not halved to 66/44 pixels");
        ui::require(lfo.getTooltip().contains("LFO 1") && random.getTooltip().contains("Random 2"),
                    "Compact rail lost full modulator names/tooltips");
        auto* toggle = ui::find<juce::TextButton>(*panel, [](auto& b) { return b.getName() == "Toggle modulation editor"; });
        auto* editor = ui::find<ModulationPanel>(*panel, [](auto&) { return true; });
        auto* canvas = ui::find<juce::Component>(*panel, [](auto& c) { return c.getName() == "Routing node canvas"; });
        ui::require(toggle && editor && canvas && toggle->isShowing() && !editor->isVisible(),
                    "Bottom show/hide must be reachable even before selecting a modulator");
        lfo.onClick();
        ui::require(editor->isShowing() && editor->selectedId() == 1, "LFO selection did not show the default editor");
        const int withEditor = canvas->getHeight();
        const bool inspector = bool(owner->getRoutingLayout().getProperty("inspectorVisible", true));
        owner->beginModulationAssignment(1);
        toggle->onClick();
        ui::require(!editor->isVisible() && canvas->getHeight() > withEditor, "Hidden modulation editor did not release canvas height");
        ui::require(!bool(owner->getRoutingLayout().getProperty("modulationVisible", true)), "Bottom hidden preference was not saved");
        routingButton(*panel, "Undo").onClick();
        ui::require(editor->isShowing(), "Bottom hide Undo did not restore the editor");
        routingButton(*panel, "Redo").onClick();
        random.onClick();
        for (auto size : {juce::Point<int>(904, 442), juce::Point<int>(1800, 1000)}) {
            panel->setSize(size.x, size.y);
            juce::Thread::sleep(80); juce::Timer::callPendingTimersSynchronously();
            ui::require(!editor->isVisible() && editor->selectedId() == 2 && toggle->isShowing()
                            && panel->getLocalBounds().contains(toggle->getBounds()) && toggle->getY() >= canvas->getBottom(),
                        "Selection/resize/timer reopened or stranded the sticky-hidden bottom editor");
            ui::require(owner->modulationAssignmentId() == 1, "Bottom hide or modulator selection cancelled Assign");
            ui::require(bool(owner->getRoutingLayout().getProperty("inspectorVisible", true)) == inspector,
                        "Bottom visibility changed the independent right inspector preference");
        }
        ui::require(!toggle->getWantsKeyboardFocus() && !toggle->getMouseClickGrabsKeyboardFocus(),
                    "Bottom toggle would steal DAW keyboard shortcuts");
        juce::MemoryBlock binary; owner->getStateInformation(binary);
        panel.reset();
        auto restored = std::make_unique<Processor>(); restored->setStateInformation(binary.getData(), int(binary.getSize()));
        panel = std::make_unique<RoutingPanel>(*restored); panel->addToDesktop(0); panel->setVisible(true);
        editor = ui::find<ModulationPanel>(*panel, [](auto&) { return true; });
        toggle = ui::find<juce::TextButton>(*panel, [](auto& b) { return b.getName() == "Toggle modulation editor"; });
        ui::require(editor && toggle && !editor->isVisible() && editor->selectedId() == 2 && toggle->isShowing(),
                    "Binary recall lost hidden bottom preference or modulator selection");
        toggle->onClick();
        ui::require(editor->isShowing() && editor->selectedId() == 2, "Always-reachable Show lost the retained selection");
    }
}

inline void routingWaveformPreviews() {
    guiTestDisplay::enumerate();
    for (int skin = 0; skin < 3; ++skin) {
        auto owner = std::make_unique<Processor>(); owner->setSkin(skin);
        ModulationState state; state.modulators = {{1}, {2, ModulatorType::random}};
        juce::String error; ui::require(owner->setModulationState(state, error), error);
        RoutingPanel panel(*owner); inheritTheme(panel, skin); panel.setNeon(skin == 1);
        guiTestDisplay::attach(panel);
        auto& tile = routingButton(panel, "LFO 1");
        for (auto waveform : {ModulationWaveform::sine, ModulationWaveform::triangle, ModulationWaveform::saw, ModulationWaveform::square})
            for (bool bipolar : {false, true}) for (float amount : {0.0f, .65f}) for (bool shifted : {false, true}) {
                auto& model = state.modulators.front();
                model.waveform = waveform; model.bipolar = bipolar; model.depth = amount;
                model.phase = shifted ? .625f : .1875f; model.shape = shifted ? .77f : .23f;
                ui::require(owner->setModulationState(state, error), error);
                juce::Thread::sleep(80); juce::Timer::callPendingTimersSynchronously();
                // Use one rasterizer for the component and oracle: Direct2D and software
                // strokes have different pixel coverage even for the same DSP path.
                auto actual = tile.createComponentSnapshot(tile.getLocalBounds(), true, 1.0f, juce::SoftwareImageType());
                auto evaluator = std::make_unique<ModulationBackend>();
                ModulationBackend::Plan plan; plan.modulatorCount = 1; plan.modulators[0] = model;
                plan.modulators[0].rateHz = 1; plan.modulators[0].sync = false;
                ui::require(evaluator->publish(plan), "Preview reference plan rejected"); evaluator->prepare(48000);
                std::array<float, ModulationBackend::maxTargets> contributions{};
                auto area = tile.getLocalBounds().toFloat().reduced(1);
                const auto caption = area.removeFromTop(17);
                const auto plot = area.reduced(3, 2);
                ui::require(plot.getHeight() >= 18 && !plot.intersects(caption)
                        && tile.getLocalBounds().toFloat().contains(plot),
                            "Rail must reserve separate enlarged caption and useful waveform lanes");
                juce::Path curve;
                for (int i = 0; i <= 64; ++i) {
                    evaluator->tick(contributions);
                    const auto sample = evaluator->snapshots().front().output;
                    const float x = plot.getX() + float(i) / 64 * plot.getWidth();
                    const float y = plot.getCentreY() - sample * plot.getHeight() * .45f;
                    if (i == 0) curve.startNewSubPath(x, y); else curve.lineTo(x, y);
                    evaluator->advance(750);
                }
                juce::Image reference(juce::Image::ARGB, actual.getWidth(), actual.getHeight(), true, juce::SoftwareImageType());
                {
                    // Direct2D commits drawing when the Graphics context is released.
                    juce::Graphics g(reference); g.setColour(juce::Colours::white); g.strokePath(curve, juce::PathStrokeType(1.5f));
                }
                const auto accent = tile.findColour(juce::TextButton::buttonOnColourId);
                const auto background = actual.getPixelAt(actual.getWidth() - 3, actual.getHeight() - 3);
                juce::Image expectedImage(juce::Image::ARGB, actual.getWidth(), actual.getHeight(), true, juce::SoftwareImageType());
                {
                    juce::Graphics g(expectedImage); g.fillAll(background);
                    g.setColour(accent.withAlpha(.2f));
                    g.drawHorizontalLine(int(plot.getCentreY()), plot.getX(), plot.getRight());
                    g.setColour(accent); g.strokePath(curve, juce::PathStrokeType(1.5f));
                }
                int checked = 0, matched = 0;
                // The rail updates on a timer; elapsed wall time alone does not
                // guarantee that its pending callback has observed this edit.
                for (int attempt = 0; attempt < 100; ++attempt) {
                    juce::Timer::callPendingTimersSynchronously();
                    actual = tile.createComponentSnapshot(tile.getLocalBounds(), true, 1.0f, juce::SoftwareImageType());
                    checked = matched = 0;
                    for (int y = 0; y < reference.getHeight(); ++y) for (int x = 0; x < reference.getWidth(); ++x) {
                        const auto coverage = reference.getPixelAt(x, y).getAlpha();
                        if (coverage < 100) continue;
                        const auto expected = expectedImage.getPixelAt(x, y);
                        ++checked; const auto pixel = actual.getPixelAt(x, y);
                        const int distance = std::abs(int(pixel.getRed()) - int(expected.getRed()))
                                           + std::abs(int(pixel.getGreen()) - int(expected.getGreen()))
                                           + std::abs(int(pixel.getBlue()) - int(expected.getBlue()));
                        if (distance < 60) ++matched;
                    }
                    if (checked > 10 && matched >= checked * .9) break;
                    juce::Thread::sleep(5);
                }
                ui::require(checked > 10 && matched >= checked * .9,
                            "Rail trace differs from DSP waveform/phase/skew/polarity/amount in " + themeName(skin)
                                + " waveform=" + juce::String(int(waveform)) + " bipolar=" + juce::String(int(bipolar))
                                + " amount=" + juce::String(amount) + " phase=" + juce::String(model.phase)
                                + " matched=" + juce::String(matched) + "/" + juce::String(checked));
            }
        auto& random = routingButton(panel, "Random 2");
        const auto before = random.createComponentSnapshot(random.getLocalBounds());
        owner->prepareToPlay(48000, 64);
        juce::AudioBuffer<float> audio(2, 64); audio.clear(); juce::MidiBuffer midi;
        owner->processBlock(audio, midi);
        for (int i = 0; i < 3; ++i) { juce::Thread::sleep(80); juce::Timer::callPendingTimersSynchronously(); }
        ui::require(!samePixels(before, random.createComponentSnapshot(random.getLocalBounds())),
                    "Random rail did not draw published output history");
        juce::Component::SafePointer<juce::TextButton> retained(&random);
        state.modulators.push_back({3}); ui::require(owner->setModulationState(state, error), error);
        juce::Thread::sleep(80); juce::Timer::callPendingTimersSynchronously();
        ui::require(retained && &routingButton(panel, "Random 2") == retained.getComponent(),
                    "Adding another modulator destroyed Random's actual history tile");
        owner->releaseResources();
    }
}

inline void themeState() {
    for (int theme = 0; theme < 3; ++theme) {
        auto owner = std::make_unique<Processor>();
        owner->setSkin(theme);
        write(*owner, utilityParameterId(15, size_t(UtilityParameter::synthFeedback)), -37);
        write(*owner, creativeParameterId(15, size_t(CreativeParameter::reverbSwell)), 730);
        const auto text = owner->settingsText();
        juce::MemoryBlock binary; owner->getStateInformation(binary);
        auto restored = std::make_unique<Processor>();
        restored->setStateInformation(binary.getData(), int(binary.getSize()));
        ui::require(restored->getSkin() == theme, "Binary state lost theme " + themeName(theme));
        ui::require(restored->pasteSettings(text) && restored->getSkin() == theme, "Text state lost theme");
        ui::near(ui::read(*restored, utilityParameterId(15, size_t(UtilityParameter::synthFeedback))), -37, "Last utility slot recall");
        ui::near(ui::read(*restored, creativeParameterId(15, size_t(CreativeParameter::reverbSwell))), 730, "Expanded creative slot recall");
        {
            std::unique_ptr<juce::AudioProcessorEditor> editor(restored->createEditor());
            ui::require(editor->getProperties().contains(vocalstackThemeProperty), "Editor must publish theme on root");
            ui::require(int(editor->getProperties()[vocalstackThemeProperty]) == theme, "Reopened editor theme differs from state");
            auto& output = ui::slider(*editor, "outputGain");
            ui::require(componentTheme(output) == themeFromInt(theme), "Private LookAndFeel control did not inherit theme");
            ui::passive(*editor);
        }
        presetTests::TemporaryFolder files;
        PresetManager manager(*owner, files.preferences());
        const auto patch = presetTests::document(manager.captureText("Theme isolation")).getChild(0);
        ui::require(!patch.hasProperty("skin") && !patch.hasProperty("flash"), "Preset leaked theme preference");
        for (int factory = 0; factory < PresetManager::factoryNames().size(); ++factory) {
            presetTests::success(manager.load({PresetManager::factoryNames()[factory], {}, factory}));
            ui::require(owner->getSkin() == theme, "Factory changed current theme");
            presetTests::success(manager.undoLoad());
            ui::require(owner->getSkin() == theme, "Undo factory changed current theme");
        }
    }
    auto owner = std::make_unique<Processor>();
    owner->setSkin(-1); ui::require(owner->getSkin() == 0, "Skin lower clamp");
    owner->setSkin(3); ui::require(owner->getSkin() == 2, "Skin upper clamp");
    for (const bool flash : {false, true}) {
        auto legacy = presetTests::document(owner->settingsText());
        legacy.removeProperty("skin", nullptr); legacy.setProperty("flash", flash, nullptr);
        ui::require(owner->pasteSettings(legacy.toXmlString()) && owner->getSkin() == int(flash), "Legacy flash migration");
        owner->setFlash(flash);
        ui::require(owner->getSkin() == int(flash) && owner->isFlash() == flash, "Legacy flash API compatibility");
    }
}

inline void utilityInspectors() {
    guiTestDisplay::enumerate();
    constexpr std::array<NodeType, 5> types{NodeType::gain, NodeType::stereo, NodeType::gate, NodeType::xpFilter, NodeType::synthFilter};
    for (int theme = 0; theme < 3; ++theme) for (size_t kind = 0; kind < types.size(); ++kind) {
        auto owner = std::make_unique<Processor>(); owner->setSkin(theme); install(*owner, types[kind]);
        std::unique_ptr<juce::AudioProcessorEditor> editor(owner->createEditor());
        const auto work = guiTestDisplay::selected().userArea;
        editor->setSize(std::min(editor->getWidth(), work.getWidth()), std::min(editor->getHeight(), work.getHeight()));
        guiTestDisplay::attach(*editor); ui::action(*editor, "Routing");
        auto* routing = ui::find<RoutingPanel>(*editor, [](auto&) { return true; });
        ui::require(routing != nullptr, "Missing embedded routing");
        auto* panel = ui::find<UtilityEffectPanel>(*routing, [](auto&) { return true; });
        ui::require(panel && panel->isShowing(), "Routing did not create visible utility inspector");
        ui::require(componentTheme(*panel) == themeFromInt(theme), "Utility inspector did not inherit root theme");
        const auto wiring = owner->getRoutingGraph().nodes.size();
        ui::GestureLog gestures(*owner);
        int controls = 0;
        for (size_t field = 0; field < utilityParameterCount; ++field) {
            const auto& d = utilityParameters[field]; if (size_t(d.kind) != kind) continue;
            ++controls; const auto id = utilityParameterId(1, field);
            panel->revealParameter(id);
            if (field == size_t(UtilityParameter::xpMode) || field == size_t(UtilityParameter::synthMode)) {
                auto& combo = ui::combo(*panel, id);
                ui::require(combo.getNumItems() == int(d.max + 1), "Incomplete utility mode list: " + id);
                for (int mode = 0; mode <= int(d.max); ++mode) {
                    combo.setSelectedId(mode + 1, juce::sendNotificationSync);
                    ui::near(ui::read(*owner, id), mode, "Mode UI binding: " + id);
                    write(*owner, id, d.max - float(mode));
                    waitFor([&] { return combo.getSelectedId() == int(d.max) - mode + 1; }, "Mode host binding: " + id);
                }
            } else if (d.discrete) {
                auto* toggle = ui::find<juce::Button>(*panel, [&](auto& c) { return c.getComponentID() == id; });
                ui::require(toggle && toggle->isShowing() && toggle->getWidth() >= 24 && toggle->getWidth() <= 64
                                && toggle->getHeight() >= 24 && toggle->getHeight() <= 28,
                            "Missing or unusable compact utility toggle: " + id);
                for (const bool on : {true, false}) {
                    toggle->setToggleState(on, juce::dontSendNotification); toggle->onClick();
                    ui::near(ui::read(*owner, id), float(on), "Utility toggle write: " + id);
                    write(*owner, id, float(!on));
                    waitFor([&] { return toggle->getToggleState() == !on; }, "Utility toggle host binding: " + id);
                }
            } else {
                auto& slider = ui::slider(*panel, id); auto& parameter = ui::parameter(*owner, id);
                const bool sparse = d.kind == UtilityKind::gain;
                ui::require(slider.getWidth() >= 64 && (sparse ? slider.getParentComponent()->getHeight() == 44
                    : slider.getParentComponent()->getWidth() == 82 && slider.getParentComponent()->getHeight() == 102),
                            "Utility rotary must retain its enlarged compact 82x102 cell: " + id);
                for (auto* child : slider.getChildren()) if (auto* value = dynamic_cast<juce::Label*>(child)) {
                    const auto font = value->getFont();
                    ui::require(std::abs(font.getHeight() - 14.5f) < .001f && value->getMinimumHorizontalScale() == 1.f,
                                "Utility numeric font lost exact +2 height or is squeezed: " + id);
                    const auto textArea = slider.getLookAndFeel().getLabelBorderSize(*value).subtractedFrom(value->getLocalBounds());
                    ui::require(font.getAscent() + font.getDescent() <= float(textArea.getHeight()), "Utility numeric font clips vertically: " + id);
                    for (double number : {slider.getMinimum(), slider.getMaximum(), slider.getValue()})
                        ui::require(juce::GlyphArrangement::getStringWidth(font, slider.getTextFromValue(number)) <= float(textArea.getWidth()),
                                    "Utility enlarged value does not fit: " + id + " " + slider.getTextFromValue(number));
                }
                ui::require(bool(slider.getProperties()["vocalstackTexturedBar"]) == sparse, "Utility sparse-group styling: " + id);
                ui::require(slider.isDoubleClickReturnEnabled() && slider.isTextBoxEditable() == !sparse, "Utility reset/passive fat caption: " + id);
                ui::near(slider.getMinimum(), d.min, "Utility UI minimum"); ui::near(slider.getMaximum(), d.max, "Utility UI maximum");
                const auto before = ui::read(*owner, id);
                const auto track = slider.getLookAndFeel().getSliderLayout(slider).sliderBounds.toFloat();
                const auto start = track.getCentre();
                const float direction = slider.valueToProportionOfLength(before) > .5 ? -1.f : 1.f;
                ui::drag(slider, start, start.translated(direction * track.getWidth() * .3f, 0));
                ui::require(ui::read(*owner, id) != before, "Utility slider drag did not edit parameter: " + id);
                ui::near(ui::read(*owner, id), slider.getValue(), "Utility slider write: " + id);
                write(*owner, id, parameter.convertFrom0to1(.63f));
                ui::near(slider.getValue(), ui::read(*owner, id), "Utility slider host binding: " + id);
                ui::near(slider.getDoubleClickReturnValue(), d.initial, "Utility double-click default");
                ui::require(!slider.getBounds().isEmpty() && slider.getParentComponent()->getLocalBounds().contains(slider.getBounds()), "Utility row clipping");
            }
        }
        constexpr std::array<int, 5> counts{1, 5, 6, 4, 5};
        ui::require(controls == counts[kind], "Utility inspector coverage count changed");
        write(*owner, "fx1Mix", 75);
        waitFor([&] { juce::Timer::callPendingTimersSynchronously(); return ui::slider(*panel, "fx1Mix").getParentComponent()->isVisible(); }, "Legacy Gain Wet did not reveal on host automation");
        panel->revealParameter("fx1Mix");
        auto& mix = ui::slider(*panel, "fx1Mix");
        ui::require(mix.getSliderStyle() == juce::Slider::LinearHorizontal, "Utility Mix must be linear");
        auto* mixRow = mix.getParentComponent();
        ui::require(bool(mix.getProperties()["vocalstackTexturedBar"]) && mixRow
                        && mixRow->getHeight() == 44,
                    "Utility Mix must use a 44px textured row");
        for (auto* child : mixRow->getChildren())
            if (auto* title = dynamic_cast<juce::Label*>(child))
                ui::require(!title->isVisible(), "Utility Mix must hide its external title");
        const auto mixLayout = mix.getLookAndFeel().getSliderLayout(mix);
        const auto mixBar = ParameterKnob::Style::texturedBarBounds(mixLayout.sliderBounds.toFloat()).toNearestInt();
        ui::require(mixLayout.sliderBounds.getHeight() == mix.getHeight() && mixBar.getHeight() == 25
                        && mixBar.getY() >= 8 && mixBar.contains(mixLayout.textBoxBounds),
                    "Utility Mix must retain its modulation lane and caption inside the 25px bar");
        auto* value = ui::find<juce::Label>(mix, [](auto&) { return true; });
        ui::require(value && value->getBounds() == mixLayout.textBoxBounds && !mix.isTextBoxEditable()
                        && !value->isEditable(),
                    "Utility Mix caption must be display-only");
        const auto captionPoint = editor->getLocalPoint(value, value->getLocalBounds().getCentre());
        auto* hit = editor->getComponentAt(captionPoint);
        ui::require(hit == &mix, "Utility Mix caption must hit the slider, not its label");
        const auto start = hit->getLocalPoint(editor.get(), captionPoint).toFloat();
        const auto end = start.translated(mixLayout.sliderBounds.getWidth() * .2f, 0);
        write(*owner, "fx1Mix", 30);
        waitFor([&] { return std::abs(mix.getValue() - 30) < .01; }, "Utility Mix drag baseline host binding");
        const auto focus = juce::Component::getCurrentlyFocusedComponent();
        ui::click(*hit, start);
        ui::near(ui::read(*owner, "fx1Mix"), 30, "Utility Mix caption click must not jump");
        hit->mouseDown(ui::mouse(*hit, start, start));
        ui::near(mix.valueToProportionOfLength(mix.getValue()), .3, "Utility Mix caption drag must not jump on mouseDown");
        hit->mouseDrag(ui::mouse(*hit, end, start, true));
        ui::near(mix.valueToProportionOfLength(mix.getValue()), .4, "Utility Mix caption drag must move at half cursor speed");
        hit->mouseUp(ui::mouse(*hit, end, start, true, 0));
        ui::require(value->getCurrentTextEditor() == nullptr
                        && juce::Component::getCurrentlyFocusedComponent() == focus,
                    "Utility Mix caption click/drag must not open entry or capture focus");
        ui::near(ui::read(*owner, "fx1Mix"), mix.getValue(), "Utility dry/wet binding");
        ui::require(owner->getRoutingGraph().nodes.size() == wiring, "Utility edit changed routing");
        gestures.balanced("Utility inspector"); ui::passive(*editor);
        const auto state = owner->settingsText();
        auto restored = std::make_unique<Processor>(); ui::require(restored->pasteSettings(state), "Utility inspector state recall");
        for (size_t field = 0; field < utilityParameterCount; ++field)
            ui::near(ui::read(*restored, utilityParameterId(1, field)), ui::read(*owner, utilityParameterId(1, field)), "Utility inspector edits lost on recall");
    }
}

inline void halfSpeedSliders() {
    auto owner = std::make_unique<Processor>();
    for (int theme = 0; theme < 3; ++theme) for (int width : {110, 190, 340}) for (double skew : {1., 2.})
        for (bool textured : {false, true}) {
        juce::Component root;
        ParameterKnob::Style style;
        ModulationSlider slider;
        slider.setLookAndFeel(&style); slider.bindModulation(*owner, "fx1Mix"); inheritTheme(slider, theme);
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        ui::require(!bool(slider.getProperties()["vocalstackTexturedBar"]), "Shared slider style must not opt into textured bars by default");
        if (textured) slider.getProperties().set("vocalstackTexturedBar", true);
        slider.setTextBoxStyle(textured ? juce::Slider::TextBoxAbove : juce::Slider::TextBoxRight,
                               false, textured ? 68 : 58, textured ? 18 : 21);
        slider.setRange(0, 1); slider.setSkewFactor(skew); slider.setSize(width, textured ? 44 : 36);
        root.setSize(slider.getWidth(), slider.getHeight()); root.setVisible(true); root.addAndMakeVisible(slider);
        slider.setTextBoxIsEditable(true);
        ui::require(slider.isTextBoxEditable() == !textured, "Only textured Mix must reject numeric entry");
        const double initial = textured ? .3 : .5;
                slider.setValue(slider.proportionOfLengthToValue(initial), juce::dontSendNotification);
        const auto layout = style.getSliderLayout(slider);
        const auto track = layout.sliderBounds.toFloat();
        if (textured) {
            const auto bar = ParameterKnob::Style::texturedBarBounds(track).toNearestInt();
            ui::require(layout.sliderBounds.getHeight() == 44 && bar.getHeight() == 25 && bar.getY() >= 8
                            && bar.contains(layout.textBoxBounds),
                        "Textured half-speed fixture must retain its lane and caption inside the 25px bar");
        }
        const auto start = textured ? layout.textBoxBounds.getCentre().toFloat()
                                            : juce::Point<float>(track.getX() + track.getWidth() * .7f, track.getCentreY());
                if (textured) ui::require(root.getComponentAt(start.toInt()) == &slider,
                                          "Textured caption must pass the root hit test through to the slider");
                const auto focus = juce::Component::getCurrentlyFocusedComponent();
        const auto end = start.translated(track.getWidth() * .2f, 0);
        const int sensitivity = slider.getMouseDragSensitivity();
        const bool snaps = slider.getSliderSnapsToMousePosition();
        slider.mouseDown(ui::mouse(slider, start, start));
        ui::near(slider.valueToProportionOfLength(slider.getValue()), initial, "Horizontal slider jumped on initial click");
        slider.mouseDrag(ui::mouse(slider, end, start, true));
        ui::near(slider.valueToProportionOfLength(slider.getValue()), initial + .1, "Horizontal slider must move at half cursor speed");
        slider.mouseUp(ui::mouse(slider, end, start, true, 0));
        ui::require(slider.getMouseDragSensitivity() == sensitivity && slider.getSliderSnapsToMousePosition() == snaps,
            "Horizontal drag changed subsequent rotary configuration");
        ui::drag(slider, end, start);
        ui::near(slider.valueToProportionOfLength(slider.getValue()), initial, "Reverse relative drag changed sensitivity");
        slider.setDoubleClickReturnValue(true, .25);
        ui::doubleClick(slider, start);
        ui::near(slider.getValue(), .25, "Half-speed drag broke double-click reset");
        if (textured) {
            auto* caption = ui::find<juce::Label>(slider, [](auto&) { return true; });
            ui::require(caption && caption->getCurrentTextEditor() == nullptr
                            && juce::Component::getCurrentlyFocusedComponent() == focus,
                        "Textured caption gestures must not open numeric entry or capture focus");
        }
        ui::passive(slider); slider.setLookAndFeel(nullptr);
    }
}

inline void creativeVisuals() {
    guiTestDisplay::enumerate();
    constexpr std::array<CreativeKind, 5> kinds{CreativeKind::delay, CreativeKind::reverb, CreativeKind::compressor, CreativeKind::bitcrusher, CreativeKind::granular};
    constexpr std::array<CreativeParameter, 5> fields{CreativeParameter::delayTime, CreativeParameter::reverbDecay, CreativeParameter::compressorThreshold, CreativeParameter::crusherBits, CreativeParameter::granularPitch};
    for (int theme = 0; theme < 3; ++theme) for (size_t k = 0; k < kinds.size(); ++k) {
        auto owner = std::make_unique<Processor>(); auto reference = std::make_unique<Processor>();
        if (kinds[k] == CreativeKind::granular) install(*owner, NodeType::granular);
        const auto id = creativeParameterId(1, size_t(fields[k]));
        auto& parameter = ui::parameter(*owner, id);
        for (size_t field = 0; field < creativeParameterCount; ++field) {
            if (creativeParameters[field].kind != kinds[k]) continue;
            const auto fieldId = creativeParameterId(1, field);
            const auto baseValue = ui::parameter(*owner, fieldId).convertFrom0to1(.3f);
            write(*owner, fieldId, baseValue); write(*reference, fieldId, baseValue);
        }
        CreativeEffectPanel panel(*owner, 1, kinds[k]); inheritTheme(panel, theme);
        panel.setNeon(theme == 1); panel.setSize(400, 760);
        for (size_t field = 0; field < creativeParameterCount; ++field) {
            const auto& d = creativeParameters[field]; if (d.kind != kinds[k]) continue;
            const auto fieldId = creativeParameterId(1, field);
            auto* control = ui::find<juce::Component>(panel, [&](auto& c) { return c.getComponentID() == fieldId; });
            ui::require(control != nullptr && componentTheme(*control) == themeFromInt(theme), "Missing/themeless creative inspector field: " + fieldId);
            if (!d.discrete) {
                auto& slider = ui::slider(panel, fieldId);
                ui::near(slider.getMinimum(), d.min, "Creative inspector minimum");
                ui::near(slider.getMaximum(), d.max, "Creative inspector maximum");
                ui::near(slider.getValue(), ui::read(*owner, fieldId), "Creative inspector host binding");
            }
        }
        CreativeEffectVisual visual(*owner, 1, kinds[k]); inheritTheme(visual, theme); visual.setSize(400, 238);
        CreativeEffectVisual expected(*reference, 1, kinds[k]); inheritTheme(expected, theme); expected.setSize(400, 238);
        guiTestDisplay::attach(visual); visual.setNeon(theme == 1);
        guiTestDisplay::attach(expected); expected.setNeon(theme == 1);
        ui::require(visual.visibleInHierarchy() && expected.visibleInHierarchy(), "Creative model fixture must be showing");
        // Compare parameter models at one explicit clock instant: independent
        // wall-clock refreshes advance Delay by different amounts.
        constexpr double modelTimeMs = 1000;
        visual.refreshAt(modelTimeMs); expected.refreshAt(modelTimeMs);
        const auto base = ui::render(visual);
        ui::require(samePixels(base, ui::render(expected)), "Creative base model fixtures differ");
        ModulationState state; state.modulators = {{1}};
        state.modulators[0].waveform = ModulationWaveform::square; state.modulators[0].rateHz = .1f;
        state.assignments = {{1, "fx1Mix", -.2f, true}};
        for (size_t field = 0; field < creativeParameterCount; ++field)
            if (creativeParameters[field].kind == kinds[k]) state.assignments.push_back({1, creativeParameterId(1, field), .2f, true});
        juce::String error; ui::require(owner->setModulationState(state, error), error);
        owner->prepareToPlay(48000, 64);
        juce::AudioBuffer<float> audio(2, 64); audio.clear(); juce::MidiBuffer midi; owner->processBlock(audio, midi);
        const auto snapshot = owner->getModulationSnapshot();
        ui::require(snapshot.consistent, "Creative model needs a coherent snapshot");
        for (const auto& assignment : state.assignments)
            ui::require(std::any_of(snapshot.targets.begin(), snapshot.targets.end(), [&](const auto& target) {
                return target.parameterId == assignment.targetParameterId;
            }), "Creative model snapshot missing assigned field: " + assignment.targetParameterId);
        for (const auto& target : snapshot.targets) {
            auto& p = ui::parameter(*owner, target.parameterId);
            write(*reference, target.parameterId, p.convertFrom0to1(target.effectiveNormalized));
        }

        visual.refreshAt(modelTimeMs); expected.refreshAt(modelTimeMs);
        const auto modulated = ui::render(visual);
        const auto context = " (" + CreativeEffectPanel::effectName(kinds[k]) + ", " + themeName(theme) + ")";
        ui::require(!samePixels(base, modulated), "Creative visual ignored effective modulation" + context);
        ui::require(samePixels(modulated, ui::render(expected)), "Creative model mixed host bases and effective snapshot values" + context);
        if (kinds[k] == CreativeKind::granular) {
            const auto actual = visual.granularModel(), effective = expected.granularModel();
            ui::require(visual.hasParameterSnapshot(), "Granular parameter model has no coherent snapshot");
            ui::near(actual.lengthMs, effective.lengthMs, "Effective granular length");
            ui::near(actual.densityHz, effective.densityHz, "Effective granular density");
            ui::near(actual.spacingMs, effective.spacingMs, "Effective granular window spacing");
            ui::near(actual.spanMs, effective.spanMs, "Effective granular axis span");
            ui::near(actual.overlap, effective.overlap, "Effective granular overlap");
            ui::near(actual.pitchSemitones, effective.pitchSemitones, "Effective granular pitch");
            ui::near(actual.lookbackMs, effective.lookbackMs, "Effective granular lookback");
            ui::near(actual.scatterMs, effective.scatterMs, "Effective granular scatter");
            ui::near(actual.guardMs, effective.guardMs, "Effective granular pitch/freeze guard");
            ui::near(actual.historyNearMs, effective.historyNearMs, "Effective granular history near");
            ui::near(actual.historyFarMs, effective.historyFarMs, "Effective granular history far");
            ui::require(actual.reverse == effective.reverse && actual.frozen == effective.frozen,
                        "Granular model mixed effective discrete flags with host bases");
        }
        double comparisonTimeMs = modelTimeMs + 125;
        if (kinds[k] == CreativeKind::delay) {
            ui::near(float(visual.timingPhase()), 0, "Fixed-time Delay comparison advanced phase");
            ui::near(float(expected.timingPhase()), 0, "Fixed-time reference advanced phase");
            ui::near(float(visual.delayIntervalSeconds()), float(expected.delayIntervalSeconds()),
                     "Delay interval differs from effective-parameter reference");
            comparisonTimeMs = modelTimeMs + visual.delayIntervalSeconds() * 250;
        }
        visual.refreshAt(comparisonTimeMs); expected.refreshAt(comparisonTimeMs);
        const auto later = ui::render(visual);
        ui::require(samePixels(later, ui::render(expected)), "Creative models differ at the same later timestamp" + context);
        if (kinds[k] == CreativeKind::delay) {
            ui::near(float(visual.timingPhase()), .25f, "Delay must advance exactly one quarter interval");
            ui::near(float(expected.timingPhase()), .25f, "Reference must advance exactly one quarter interval");
            ui::require(!samePixels(modulated, later), "Deterministic Delay animation did not move" + context);
        } else {
            ui::require(samePixels(modulated, later), "Nonanimated creative model changed with time" + context);
        }
        visual.refreshAt(comparisonTimeMs); expected.refreshAt(comparisonTimeMs);
        ui::require(samePixels(later, ui::render(visual)) && samePixels(later, ui::render(expected)),
                    "Repeated timestamp must preserve exact creative model pixels" + context);
        ui::near(ui::read(*owner, id), parameter.convertFrom0to1(.3f), "Visual changed host base");
        write(*owner, "fx1On", 0); visual.refreshAt(comparisonTimeMs);
        ui::require(!samePixels(later, ui::render(visual)), "Creative visual did not display bypass" + context);
        if (kinds[k] == CreativeKind::granular)
            ui::require(visual.getTooltip().containsIgnoreCase("parameter")
                            && visual.getTooltip().containsIgnoreCase("not live"), "Granular tooltip must distinguish parameter envelopes from measured grains");
        else if (kinds[k] == CreativeKind::reverb)
            ui::require(visual.getTooltip().containsIgnoreCase("actual stereo min/max")
                            && visual.getTooltip().containsIgnoreCase("foreground model axes")
                            && visual.getTooltip().containsIgnoreCase("not measured IR"), "Reverb must distinguish measured background from model guide");
        else if (kinds[k] == CreativeKind::compressor)
            ui::require(visual.getTooltip().containsIgnoreCase("actual DSP gain reduction") && visual.getTooltip().containsIgnoreCase("parameter-derived static transfer"), "Compressor must distinguish measured GR from transfer model");
        else
            ui::require(visual.getTooltip().containsIgnoreCase("not a measured impulse response"), "Model must not claim a measured impulse response");
        ui::passive(visual); owner->releaseResources();
    }
}

inline void rackTabs() {
    for (int theme = 0; theme < 3; ++theme) {
        auto owner = std::make_unique<Processor>(); owner->setSkin(theme);
        Graph graph{{{1, NodeType::lead}, {100, NodeType::reverb}, {101, NodeType::gain}, {13, NodeType::output}},
                    {{1, 100, 0, 0}, {100, 101, 0, 0}, {101, 13, 0, 0}}};
        juce::String error; ui::require(owner->setRoutingGraph(graph, error), error);
        std::unique_ptr<juce::AudioProcessorEditor> editor(owner->createEditor());
        editor->addToDesktop(0); editor->setVisible(true);
        auto* rack = ui::find<juce::Component>(*editor, [](auto& c) { return c.getComponentID() == "mainBusFxRack"; });
        ui::require(rack != nullptr, "Missing Stack FX rack");
        const auto select = [&](int id) -> juce::Component& {
            auto* tab = ui::find<juce::Component>(*rack, [&](auto& c) {
                return c.getComponentID() == "mainBusFxTab" + juce::String(id);
            });
            ui::require(tab != nullptr, "Missing FX tab");
            auto* handle = ui::find<juce::TextButton>(*tab, [](auto& b) { return b.getComponentID() == "mainBusFxDrag"; });
            ui::require(handle && bool(handle->onClick), "Missing FX tab selection action");
            const auto focus = juce::Component::getCurrentlyFocusedComponent();
            handle->onClick();
            auto* body = ui::find<juce::Component>(*rack, [&](auto& c) {
                return c.getComponentID() == "mainBusFx" + juce::String(id);
            });
            auto* other = ui::find<juce::Component>(*rack, [&](auto& c) {
                return c.getComponentID() == "mainBusFx" + juce::String(id == 100 ? 101 : 100);
            });
            ui::require(body && body->isShowing() && other && !other->isVisible() && handle->getToggleState(),
                        "FX tabs must show exactly the selected body");
            ui::require(body->getWidth() >= rack->getWidth() - 12 && rack->getLocalBounds().contains(body->getBounds()),
                        "Selected FX body must use the full rack width without clipping");
            ui::require(juce::Component::getCurrentlyFocusedComponent() == focus, "FX selection captured host keys");
            return *body;
        };
        auto& reverb = select(100);
        juce::Component::SafePointer<juce::Slider> retained(&ui::slider(reverb, "fx1Mix"));
        select(101);
        write(*owner, "fx1Mix", 37);
        auto& reopened = select(100);
        ui::require(retained && &ui::slider(reopened, "fx1Mix") == retained.getComponent(),
                    "Tab switching replaced the retained Mix slider");
        int mixes = 0;
        const auto count = [&](auto&& self, juce::Component& component) -> void {
            if (dynamic_cast<juce::Slider*>(&component) && component.getComponentID() == "fx1Mix") ++mixes;
            for (auto* child : component.getChildren()) self(self, *child);
        };
        count(count, reopened);
        ui::require(mixes == 1 && retained->getSliderStyle() == juce::Slider::LinearHorizontal,
                    "Selected FX must retain one linear Mix slider, not a duplicate rotary control");
        waitFor([&] { return std::abs(retained->getValue() - 37) < .01; }, "Hidden FX lost its host binding");
    }
}

inline void materialChromeRegression() {
    auto owner = std::make_unique<Processor>();
    for (int theme = 0; theme < 3; ++theme) {
        owner->setSkin(theme);
        std::unique_ptr<juce::AudioProcessorEditor> editor(owner->createEditor());
        auto* bar = ui::find<PresetBar>(*editor, [](auto&) { return true; });
        auto* box = ui::find<juce::ComboBox>(*bar, [](auto&) { return true; });
        ui::require(box && bool(box->getProperties()["vocalstackPresetSelector"]), "Actual preset selector lacks themed material");
        const auto p = themePalette(*box);
        ui::require(box->findColour(juce::ComboBox::textColourId) == p.text
                    && box->findColour(juce::ComboBox::outlineColourId) == p.border, "Preset selector ignored theme");
        ui::passive(*bar);
        auto* style = dynamic_cast<PixelLookAndFeel*>(&box->getLookAndFeel());
        ui::require(style != nullptr, "Preset selector lost pixel look-and-feel");
        juce::Image popup(juce::Image::RGB, 240, 100, false);
        { juce::Graphics g(popup); style->drawPopupMenuBackgroundWithOptions(g, 240, 100,
            juce::PopupMenu::Options().withTargetComponent(box)); }
        ui::require(popup.getPixelAt(0, 0) == p.border, "Popup theme/square border mismatch");
        std::set<juce::uint32> shades;
        for (int y = 2; y < 98; ++y) for (int x = 2; x < 238; ++x) shades.insert(popup.getPixelAt(x, y).getARGB());
        ui::require(shades.size() > 4, "Popup has no procedural texture");
        auto& slider = ui::slider(*editor, voiceParameterId(0, "Gain"));
        ui::require(slider.isTextBoxEditable() && !bool(slider.getProperties()["vocalstackTexturedBar"]),
                    "Voice numeric entry replaced by passive FX Mix caption");
        slider.showTextBox();
        auto* label = ui::find<juce::Label>(slider, [](auto& l) { return l.getCurrentTextEditor() != nullptr; });
        ui::require(label && label->getCurrentTextEditor()->getWantsKeyboardFocus(), "Voice numeric entry cannot own keys");
        slider.hideTextBox(true);
        ui::require(!slider.getWantsKeyboardFocus(), "Voice edit retained host focus policy");
        ParameterKnob::Style railStyle;
        juce::Slider rail; inheritTheme(rail, theme); rail.setRange(0, 1);
        rail.setColour(juce::Slider::trackColourId, p.accent);
        juce::Image image(juce::Image::ARGB, 160, 40, true);
        { juce::Graphics g(image); railStyle.drawLinearSlider(g, 10, 0, 140, 40, 80, 0, 140, juce::Slider::LinearHorizontal, rail); }
        for (int y = 0; y < 40; ++y) {
            const bool onRail = y >= 16 && y < 26;
            ui::require((image.getPixelAt(30, y).getAlpha() != 0) == onRail
                        && (image.getPixelAt(79, y).getAlpha() != 0) == onRail,
                        "Linear track must match the 10-pixel white pointer height");
        }
        ui::require(image.getPixelAt(79, 20) == p.text, "Linear pointer lost readable theme foreground");
    }
}
inline void squareChromeAndBadges() {
    materialChromeRegression();
    auto owner = std::make_unique<Processor>();
    for (int theme = 0; theme < 3; ++theme) {
        for (int slot : {-1, 0, 7}) {
            VoiceControls card(*owner, slot);
            inheritTheme(card, theme); card.setSize(220, 480);
            juce::Image actual(juce::Image::ARGB, 220, 480, true);
            { juce::Graphics g(actual); card.paint(g); }
            const auto colour = theme == 0
                ? (slot < 0 ? juce::Colour(0xfff8de6b) : VoiceMonitor::voiceColour(size_t(slot)))
                : (slot < 0 ? themePalette(card).secondary : themePalette(card).accent);
            juce::Image expected(juce::Image::ARGB, 22, 22, true);
            { juce::Graphics g(expected);
                g.fillAll(colour); g.setColour(juce::Colour(0xff151b20));
                g.setFont(juce::Font(pixelFontOptions(12.f, juce::Font::bold)));
                g.drawText(juce::String(slot + 1), 0, 0, 22, 22, juce::Justification::centred);
            }
            for (int y = 0; y < 22; ++y) for (int x = 0; x < 22; ++x)
                ui::require(actual.getPixelAt(x + 12, y + 10) == expected.getPixelAt(x, y),
                            "Card badge must be square and display Lead 0 / stable voice number");
        }
        PixelLookAndFeel style;
        juce::ComboBox box; box.setSize(100, 30);
        box.setColour(juce::ComboBox::backgroundColourId, juce::Colours::red);
        box.setColour(juce::ComboBox::outlineColourId, juce::Colours::red);
        juce::Image image(juce::Image::ARGB, 100, 30, true);
        { juce::Graphics g(image); style.drawComboBox(g, 100, 30, false, 0, 0, 0, 0, box); }
        for (auto point : {juce::Point<int>(0, 0), {99, 0}, {0, 29}, {99, 29}})
            ui::require(image.getPixelAt(point.x, point.y).getAlpha() == 255,
                        "Shared dropdown has a rounded corner");
    }
}

inline void saveImage(const juce::Image& image, const juce::String& name) {
    juce::FileOutputStream stream(juce::File::getCurrentWorkingDirectory().getChildFile(name));
    ui::require(stream.openedOk() && stream.setPosition(0) && stream.truncate().wasOk(), "Cannot open " + name);
    ui::require(juce::PNGImageFormat().writeImageToStream(image, stream), "Cannot encode " + name);
}
// Component snapshots of the actual separate popup peers, placed at their real
// screen coordinates over the editor snapshot; no painted/mock menu substitute.
inline void captureOpenPopup(juce::Component& editor, const juce::String& name) {
    auto& desktop = juce::Desktop::getInstance();
    std::vector<juce::Component*> popups;
    auto bounds = editor.getScreenBounds();
    for (int i = 0; i < desktop.getNumComponents(); ++i) {
        auto* c = desktop.getComponent(i);
        if (c != &editor && c->isVisible() && c->isCurrentlyModal()) {
            popups.push_back(c); bounds = bounds.getUnion(c->getScreenBounds());
        }
    }
    ui::require(!popups.empty(), "No actual expanded popup peer for " + name);
    juce::Image image(juce::Image::ARGB, bounds.getWidth(), bounds.getHeight(), true);
    {
    juce::Graphics g(image);
    g.drawImageAt(editor.createComponentSnapshot(editor.getLocalBounds()), editor.getScreenX() - bounds.getX(), editor.getScreenY() - bounds.getY());
    for (auto* c : popups) {
        const auto popup = c->createComponentSnapshot(c->getLocalBounds());
        ui::require(popup.getPixelAt(0, 0) == themePalette(editor).border, "Live popup lost originating theme/square border");
        if (name.startsWith("routing-context")) ui::require(popup.getHeight() < 200, "Captured Add menu instead of node context menu");
        g.drawImageAt(popup, c->getScreenX() - bounds.getX(), c->getScreenY() - bounds.getY());
        saveImage(popup, name + "-popup-crop.png");
    }
    }
    saveImage(image, name + ".png");
    juce::PopupMenu::dismissAllActiveMenus();
}

// Render the actual editor, with the selected inspector in situ, not a mock panel.
inline void screenshots() {
    constexpr std::array<NodeType, 4> types{NodeType::xpFilter, NodeType::synthFilter, NodeType::reverb, NodeType::waveshaper};
    constexpr std::array<const char*, 4> names{"xp-filter", "synth-filter", "reverb", "waveshaper"};
    for (int theme = 0; theme < 3; ++theme) {
        auto owner = std::make_unique<Processor>(); owner->setSkin(theme);
        std::unique_ptr<juce::AudioProcessorEditor> editor(owner->createEditor()); editor->addToDesktop(0); editor->setVisible(true);
        auto& add = ui::button(*editor, "+ Add voice");
        for (int i = 0; i < 6; ++i) add.onClick();
        ui::require(!add.isEnabled(), "Full Stack render did not reach eight voices");
        auto* lead = ui::find<VoiceControls>(*editor, [](auto& card) { return card.getComponentID() == "leadCard"; });
        ui::require(lead != nullptr, "Stack preview is missing the Lead card");
        auto* cards = lead->findParentComponentOfClass<juce::Viewport>();
        ui::require(cards != nullptr, "Stack preview is missing the card viewport");
        cards->setViewPosition(0, 0);
        ui::screenshot(*editor, "update-stack-full-" + themeName(theme) + ".png");
        auto* voice = ui::find<VoiceControls>(*editor, [](auto& card) { return card.getComponentID() != "leadCard"; });
        ui::require(voice != nullptr, "Voice card crop missing");
        saveImage(voice->createComponentSnapshot(voice->getLocalBounds()), "voice-sliders-" + themeName(theme) + "-crop.png");
        auto* preset = ui::find<juce::ComboBox>(*editor, [](auto& c) { return bool(c.getProperties()["vocalstackPresetSelector"]); });
        ui::require(preset != nullptr, "Actual preset selector missing");
        const auto* focus = juce::Component::getCurrentlyFocusedComponent();
        preset->showPopup();
        captureOpenPopup(*editor, "stack-presets-expanded-" + themeName(theme));
        ui::require(juce::Component::getCurrentlyFocusedComponent() == focus, "Preset menu stole host focus");
        editor.reset();
        for (size_t i = 0; i < types.size(); ++i) {
            install(*owner, types[i]);
            editor.reset(owner->createEditor()); editor->addToDesktop(0); editor->setVisible(true); ui::action(*editor, "Routing");
            auto* routing = ui::find<RoutingPanel>(*editor, [](auto&) { return true; });
            ui::require(routing != nullptr, "Screenshot routing missing");
            if (i < 2) {
                auto* filter = ui::find<UtilityEffectPanel>(*routing, [](auto&) { return true; });
                ui::require(filter != nullptr, "Screenshot filter inspector missing");
                waitFor([&] { return filter->responseReady(); }, "Screenshot filter curve did not complete");
            }
            else if (i == 2) ui::require(ui::find<CreativeEffectPanel>(*routing, [](auto&) { return true; }) != nullptr, "Screenshot reverb inspector missing");
            else ui::require(ui::find<ToneEffectPanel>(*routing, [](auto&) { return true; }) != nullptr, "Screenshot waveshaper inspector missing");
            ui::screenshot(*editor, "update-" + juce::String(names[i]) + "-" + themeName(theme) + ".png");
            if (i == 2) {
                auto* canvas = ui::find<juce::Component>(*routing, [](auto& c) { return c.getName() == "Routing node canvas"; });
                ui::require(canvas != nullptr, "Routing canvas missing");
                ui::action(*routing, "Fit");
                const auto layout = owner->getRoutingLayout();
                juce::Point<float> position;
                bool foundNode = false;
                for (auto n : layout) if (int(n.getProperty("id")) == 100) {
                    position = {float(n.getProperty("x")), float(n.getProperty("y"))}; foundNode = true;
                }
                ui::require(foundNode, "Context-menu capture lacks persisted node geometry");
                const auto point = (position + juce::Point<float>(24, 14)) * float(layout.getProperty("zoom", 1.f))
                    + juce::Point<float>(float(layout.getProperty("panX")), float(layout.getProperty("panY")));
                auto mouseSource = juce::Desktop::getInstance().getMainMouseSource();
                const auto previousPointer = mouseSource.getScreenPosition();
                mouseSource.setScreenPosition(canvas->localPointToGlobal(point));
                const auto* contextFocus = juce::Component::getCurrentlyFocusedComponent();
                canvas->mouseDown(ui::mouse(*canvas, point, point, false, juce::ModifierKeys::rightButtonModifier));
                captureOpenPopup(*editor, "routing-context-" + themeName(theme));
                mouseSource.setScreenPosition(previousPointer);
                ui::require(juce::Component::getCurrentlyFocusedComponent() == contextFocus, "Node context menu stole host focus");
                saveImage(canvas->createComponentSnapshot(canvas->getLocalBounds()), "routing-nodes-" + themeName(theme) + "-crop.png");
            }
            editor.reset();
        }
    }
}
} // namespace vocalstack::updateUiTests
