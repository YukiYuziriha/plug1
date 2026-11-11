#pragma once

#include "ToneUiTests.h"
#include "PresetTests.h"
#include "CreativeEffectPanel.h"
#include "CreativeEffectVisual.h"
#include "UtilityEffectPanel.h"
#include "VisualTheme.h"

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
                ui::require(toggle != nullptr, "Missing utility toggle: " + id);
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
                const float y = float(slider.getHeight()) * .5f;
                ui::drag(slider, {12.f, y}, {float(slider.getWidth()) * .55f, y});
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
        auto& mix = ui::slider(*panel, "fx1Mix");
        ui::drag(mix, {12.f, float(mix.getHeight()) * .5f}, {float(mix.getWidth()) * .55f, float(mix.getHeight()) * .5f});
        ui::near(ui::read(*owner, "fx1Mix"), mix.getValue(), "Utility dry/wet binding");
        ui::require(owner->getRoutingGraph().nodes.size() == wiring, "Utility edit changed routing");
        gestures.balanced("Utility inspector"); ui::passive(*editor);
        const auto state = owner->settingsText();
        auto restored = std::make_unique<Processor>(); ui::require(restored->pasteSettings(state), "Utility inspector state recall");
        for (size_t field = 0; field < utilityParameterCount; ++field)
            ui::near(ui::read(*restored, utilityParameterId(1, field)), ui::read(*owner, utilityParameterId(1, field)), "Utility inspector edits lost on recall");
    }
}

inline void creativeVisuals() {
    constexpr std::array<CreativeKind, 5> kinds{CreativeKind::delay, CreativeKind::reverb, CreativeKind::compressor, CreativeKind::bitcrusher, CreativeKind::granular};
    constexpr std::array<CreativeParameter, 5> fields{CreativeParameter::delayTime, CreativeParameter::reverbDecay, CreativeParameter::compressorThreshold, CreativeParameter::crusherBits, CreativeParameter::granularPitch};
    for (int theme = 0; theme < 3; ++theme) for (size_t k = 0; k < kinds.size(); ++k) {
        auto owner = std::make_unique<Processor>(); auto reference = std::make_unique<Processor>();
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
        const auto base = ui::render(visual);
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
        visual.refresh(); expected.refresh();
        const auto modulated = ui::render(visual);
        ui::require(!samePixels(base, modulated), "Creative visual ignored effective modulation");
        ui::require(samePixels(modulated, ui::render(expected)), "Creative model mixed host bases and effective snapshot values");
        ui::near(ui::read(*owner, id), parameter.convertFrom0to1(.3f), "Visual changed host base");
        write(*owner, "fx1On", 0); visual.refresh();
        ui::require(!samePixels(modulated, ui::render(visual)), "Creative visual did not display bypass");
        ui::require(visual.getTooltip().containsIgnoreCase("not a measured impulse response"), "Model must not claim live reverb telemetry");
        ui::passive(visual); owner->releaseResources();
    }
}

// Render the actual editor, with the selected inspector in situ, not a mock panel.
inline void screenshots() {
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
