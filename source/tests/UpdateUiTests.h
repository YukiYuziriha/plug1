#pragma once

#include "ToneUiTests.h"
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
static_assert(creativeParameterCount == 51 && utilityParameterCount == 21);

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
    for (int skin = 0; skin < 3; ++skin) {
        auto owner = std::make_unique<Processor>(); owner->setSkin(skin);
        ModulationState state; state.modulators = {{1}, {2, ModulatorType::random}};
        juce::String error; ui::require(owner->setModulationState(state, error), error);
        RoutingPanel panel(*owner); inheritTheme(panel, skin); panel.setNeon(skin == 1);
        panel.addToDesktop(0); panel.setVisible(true);
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
                auto area = tile.getLocalBounds().toFloat().reduced(1); area.removeFromTop(15);
                const auto plot = area.reduced(3, 2);
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
        for (int factory = 0; factory < 8; ++factory) {
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
    constexpr std::array<NodeType, 5> types{NodeType::gain, NodeType::stereo, NodeType::gate, NodeType::xpFilter, NodeType::synthFilter};
    for (int theme = 0; theme < 3; ++theme) for (size_t kind = 0; kind < types.size(); ++kind) {
        auto owner = std::make_unique<Processor>(); owner->setSkin(theme); install(*owner, types[kind]);
        std::unique_ptr<juce::AudioProcessorEditor> editor(owner->createEditor());
        editor->addToDesktop(0); editor->setVisible(true); ui::action(*editor, "Routing");
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
                ui::require(slider.isDoubleClickReturnEnabled() && slider.isTextBoxEditable(), "Utility reset/numeric entry: " + id);
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
        panel->revealParameter("fx1Mix");
        auto& mix = ui::slider(*panel, "fx1Mix");
        ui::require(mix.getSliderStyle() == juce::Slider::LinearHorizontal, "Utility Mix must be linear");
        const auto mixBefore = ui::read(*owner, "fx1Mix");
        const auto mixTrack = mix.getLookAndFeel().getSliderLayout(mix).sliderBounds.toFloat();
        ui::drag(mix, mixTrack.getCentre(), mixTrack.getCentre().translated(-mixTrack.getWidth() * .3f, 0));
        ui::require(ui::read(*owner, "fx1Mix") != mixBefore, "Utility dry/wet drag did not edit its parameter");
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
    for (int theme = 0; theme < 3; ++theme) for (int width : {110, 190, 340}) for (double skew : {1., 2.}) {
        ParameterKnob::Style style;
        ModulationSlider slider;
        slider.setLookAndFeel(&style); slider.bindModulation(*owner, "fx1Mix"); inheritTheme(slider, theme);
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 58, 21);
        slider.setRange(0, 1); slider.setSkewFactor(skew); slider.setSize(width, 36);
        slider.setValue(slider.proportionOfLengthToValue(.5), juce::dontSendNotification);
        const auto track = style.getSliderLayout(slider).sliderBounds.toFloat();
        const auto start = juce::Point<float>(track.getX() + track.getWidth() * .7f, track.getCentreY());
        const auto end = start.translated(track.getWidth() * .2f, 0);
        const int sensitivity = slider.getMouseDragSensitivity();
        const bool snaps = slider.getSliderSnapsToMousePosition();
        slider.mouseDown(ui::mouse(slider, start, start));
        ui::near(slider.valueToProportionOfLength(slider.getValue()), .5, "Horizontal slider jumped on initial click");
        slider.mouseDrag(ui::mouse(slider, end, start, true));
        ui::near(slider.valueToProportionOfLength(slider.getValue()), .6, "Horizontal slider must move at half cursor speed");
        slider.mouseUp(ui::mouse(slider, end, start, true, 0));
        ui::require(slider.getMouseDragSensitivity() == sensitivity && slider.getSliderSnapsToMousePosition() == snaps,
            "Horizontal drag changed subsequent rotary configuration");
        ui::drag(slider, end, start);
        ui::near(slider.valueToProportionOfLength(slider.getValue()), .5, "Reverse relative drag changed sensitivity");
        slider.setDoubleClickReturnValue(true, .25);
        ui::doubleClick(slider, start);
        ui::near(slider.getValue(), .25, "Half-speed drag broke double-click reset");
        ui::passive(slider); slider.setLookAndFeel(nullptr);
    }
}

inline void creativeVisuals() {
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
        visual.addToDesktop(0); visual.setVisible(true); visual.setNeon(theme == 1);
        expected.addToDesktop(0); expected.setVisible(true); expected.setNeon(theme == 1);
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
        else
            ui::require(visual.getTooltip().containsIgnoreCase("not a measured impulse response"), "Model must not claim live reverb telemetry");
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

// Render the actual editor, with the selected inspector in situ, not a mock panel.
inline void screenshots() {
    rackTabs();
    constexpr std::array<NodeType, 4> types{NodeType::xpFilter, NodeType::synthFilter, NodeType::reverb, NodeType::waveshaper};
    constexpr std::array<const char*, 4> names{"xp-filter", "synth-filter", "reverb", "waveshaper"};
    for (int theme = 0; theme < 3; ++theme) {
        auto owner = std::make_unique<Processor>(); owner->setSkin(theme);
        std::unique_ptr<juce::AudioProcessorEditor> editor(owner->createEditor()); editor->setVisible(true);
        auto& add = ui::button(*editor, "+ Add voice");
        for (int i = 0; i < 6; ++i) add.onClick();
        ui::require(!add.isEnabled(), "Full Stack render did not reach eight voices");
        ui::screenshot(*editor, "update-stack-full-" + themeName(theme) + ".png");
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
            editor.reset();
        }
    }
}
} // namespace vocalstack::updateUiTests
