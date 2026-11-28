#pragma once
#include "UpdateUiTests.h"
#include "UtilityIntegrationTests.h"
#include "MainBusFxRack.h"

namespace vocalstack::factoryFxUiTests {
namespace ui = toneUiTests;
using P = CreativeParameter;
inline void contractAndState() {
    static_assert(int(NodeType::splitter) == 19 && int(NodeType::convolution) == 20 && int(NodeType::saturator) == 21);
    static_assert(newCreativeParameterStart == 2087 && parameterIds.size() == 3897);
    auto owner = std::make_unique<Processor>();
    for (size_t slot = 1; slot < 16; ++slot) for (size_t p = 51; p < 62; ++p) {
        const auto id = creativeParameterId(slot, p);
        ui::require(ui::parameter(*owner, id).getParameterIndex() == int(2087 + (slot - 1) * 11 + p - 51), "Appended factory FX host index");
        ui::write(*owner, id, creativeParameters[p].max);
        ui::near(owner->settings().fxSettings[slot].creative.values[p], creativeParameters[p].max, "Appended factory FX settings bridge");
    }
    auto g = defaultGraph();
    for (auto type : {NodeType::convolution, NodeType::saturator}) {
        auto edit = mainBusFx::add(g, type); ui::require(edit.changed, edit.error); g = std::move(edit.graph);
    }
    juce::String error; ui::require(owner->setRoutingGraph(g, error), error);
    ui::write(*owner, "fx1convolution_space", 3); ui::write(*owner, "fx1convolution_brightness", 7);
    ui::write(*owner, "fx2saturator_character", 82); ui::write(*owner, "fx2saturator_level_match", 1);
    juce::MemoryBlock state; owner->getStateInformation(state);
    auto copy = std::make_unique<Processor>(); copy->setStateInformation(state.getData(), int(state.getSize()));
    ui::near(ui::read(*copy, "fx1convolution_space"), 3, "Factory IR binary recall");
    ui::near(ui::read(*copy, "fx1convolution_brightness"), 7, "Brightness binary recall");
    ui::near(ui::read(*copy, "fx2saturator_character"), 82, "Saturation binary recall");
    ui::near(ui::read(*copy, "fx2saturator_level_match"), 1, "Level match binary recall");
    const auto segment = mainBusFx::discover(copy->getRoutingGraph());
    ui::require(segment.editable() && segment.effects.size() == 3, "Main-bus factory FX graph recall");
    ui::require(copy->pasteSettings(owner->settingsText()), "Factory FX portable text state");
    auto legacy = juce::ValueTree::fromXml(*juce::parseXML(owner->settingsText()));
    for (int i = legacy.getNumChildren(); --i >= 0;) {
        const auto child = legacy.getChild(i);
        if (child.hasType("PARAM") && (child["id"].toString().contains("convolution_") || child["id"].toString().contains("saturator_")))
            legacy.removeChild(i, nullptr);
    }
    // Graph-only restoration seeds appended controls from the nested graph;
    // absent new controls on genuinely old nodes use descriptor defaults.
    ui::require(copy->pasteSettings(legacy.toXmlString()), "Graph-only factory FX migration");
    ModulationState modulation;
    Modulator source; source.id = 71; source.phase = .25f;
    modulation.modulators.push_back(source);
    const auto lastContinuous = creativeParameterId(15, size_t(P::saturatorOutput));
    ui::write(*copy, lastContinuous, -12);
    modulation.assignments.push_back({71, lastContinuous, .25f, true});
    ui::require(copy->setModulationState(modulation, error), "Appended last-slot modulation assignment rejected");
    copy->prepareToPlay(48000, 64);
    juce::AudioBuffer<float> silence(2, 64); silence.clear(); juce::MidiBuffer midi;
    copy->processBlock(silence, midi);
    const auto snapshot = copy->getModulationSnapshot();
    bool moved = false;
    for (const auto& target : snapshot.targets) if (target.parameterId == lastContinuous)
        moved = target.effectiveNormalized > target.baseNormalized + .1f;
    ui::require(snapshot.consistent && moved, "Appended final-slot parameter did not receive modulation");
    ui::near(ui::read(*copy, lastContinuous), -12, "Appended modulation changed host base");
    copy->resetParameters();
    for (size_t slot = 1; slot < 16; ++slot) for (size_t p = 51; p < creativeParameterCount; ++p)
        ui::near(ui::read(*copy, creativeParameterId(slot, p)), creativeParameters[p].initial, "Factory FX reset default");
    for (auto type : {NodeType::convolution, NodeType::saturator})
        for (double rate : {44100., 48000., 96000.}) for (int channels : {1, 2}) {
            auto audioOwner = std::make_unique<Processor>();
            updateUiTests::install(*audioOwner, type);
            utilityIntegrationTests::prepare(*audioOwner, rate, channels);
            const double e = utilityIntegrationTests::render(*audioOwner, 12000, rate, channels);
            ui::require(e > .00001, "Factory FX processor audio is silent");
            ui::write(*audioOwner, "fx1Mix", 0);
            utilityIntegrationTests::render(*audioOwner, 12000, rate, channels);
            audioOwner->reset();
            ui::require(utilityIntegrationTests::render(*audioOwner, 6000, rate, channels, true) == 0, "Factory FX processor reset silence");
            ui::write(*audioOwner, "fx1Mix", 100); ui::write(*audioOwner, "outputGain", 12);
            ui::write(*audioOwner, "bypass", 1); audioOwner->reset();
            const int latency = audioOwner->getLatencySamples();
            constexpr int blocks[]{1, 7, 511, 3, 64, 127};
            for (int position = 0, block = 0; position < latency + 1500; ++block) {
                const int count = std::min(blocks[block % 6], latency + 1500 - position);
                juce::AudioBuffer<float> input(channels, count); input.clear(); juce::MidiBuffer events;
                if (position == 0) for (int c = 0; c < channels; ++c) input.setSample(c, 0, c ? -.125f : .25f);
                audioOwner->processBlockBypassed(input, events);
                for (int c = 0; c < channels; ++c) for (int i = 0; i < count; ++i)
                    ui::near(input.getSample(c, i), position + i == latency ? (c ? -.125f : .25f) : 0.f,
                             "Factory FX whole-plugin bypass is not latency-aligned untouched dry", 1.e-6f);
                position += count;
            }
        }
}
inline void controls() {
    for (int theme = 0; theme < 3; ++theme) for (auto kind : {CreativeKind::convolution, CreativeKind::saturator}) {
        auto owner = std::make_unique<Processor>(); owner->setSkin(theme);
        const auto rackGraph = mainBusFx::add(defaultGraph(), kind == CreativeKind::convolution ? NodeType::convolution : NodeType::saturator);
        juce::String error; ui::require(rackGraph.changed && owner->setRoutingGraph(rackGraph.graph, error), "New FX rack insertion: " + error);
        {
            std::unique_ptr<juce::AudioProcessorEditor> editor(owner->createEditor());
            editor->addToDesktop(0); editor->setVisible(true); ui::action(*editor, "Stack");
            const auto* focus = juce::Component::getCurrentlyFocusedComponent();
            ui::action(*editor, CreativeEffectPanel::effectName(kind) + " #100");
            auto* body = ui::find<CreativeEffectPanel>(*editor, [](auto& c) { return c.isShowing(); });
            ui::require(body != nullptr, "New FX main-bus tab did not select its actual panel");
            ui::require(juce::Component::getCurrentlyFocusedComponent() == focus, "New FX main-bus tab captured host focus");
            ui::passive(*editor);
        }
        CreativeEffectPanel panel(*owner, 1, kind);
        panel.getProperties().set(vocalstackThemeProperty, theme); panel.setNeon(theme == 1);
        panel.addToDesktop(0); panel.setVisible(true);
        for (int width : {220, 400, 760}) {
            panel.setSize(width, 620); juce::Timer::callPendingTimersSynchronously();
            ui::passive(panel); ui::visibleLayout(panel);
            auto* visual = ui::find<CreativeEffectVisual>(panel, [](auto&) { return true; });
            ui::require(visual != nullptr, "Focused factory FX visual missing");
            visual->refreshAt(0);
            ui::require(panel.createComponentSnapshot(panel.getLocalBounds()).isValid(), "Factory FX layout snapshot");
        }
        if (kind == CreativeKind::convolution) {
            auto& combo = ui::combo(panel, "fx1convolution_space");
            ui::require(combo.getNumItems() == 4 && combo.getItemText(0).contains("Church") && combo.getItemText(3).contains("Plate"), "Synthetic IR choices missing");
            const auto* focus = juce::Component::getCurrentlyFocusedComponent();
            combo.setSelectedId(4, juce::sendNotificationSync);
            ui::near(ui::read(*owner, "fx1convolution_space"), 3, "IR choice host attachment");
            ui::require(juce::Component::getCurrentlyFocusedComponent() == focus, "IR choice captured host focus");
        } else {
            for (const auto* suffix : {"saturator_drive", "saturator_character", "saturator_tone", "saturator_output"})
                ui::require(ui::slider(panel, "fx1" + juce::String(suffix)).getWidth() > 0, "Focused saturator control missing");
            ui::require(ui::find<juce::Button>(panel, [](auto& b) { return b.getComponentID() == "fx1saturator_level_match"; }) != nullptr, "Level match control missing");
        }
    }
}
inline void screenshots() {
    for (int theme = 0; theme < 3; ++theme) for (int view = 0; view < 3; ++view) {
        auto owner = std::make_unique<Processor>(); owner->setSkin(theme);
        updateUiTests::install(*owner, view == 2 ? NodeType::saturator : NodeType::convolution);
        if (view == 1) ui::write(*owner, "fx1convolution_space", 3);
        if (view == 2) ui::write(*owner, "fx1Mix", 100);
        std::unique_ptr<juce::AudioProcessorEditor> editor(owner->createEditor());
        editor->addToDesktop(0); editor->setVisible(true); ui::action(*editor, "Routing");
        const juce::String stem = juce::String(view == 0 ? "convolution-church" : view == 1 ? "convolution-plate" : "saturator") + "-" + updateUiTests::themeName(theme);
        editor->getProperties().set(vocalstackAnimationTimeOverrideProperty, 33.3);
        juce::Timer::callPendingTimersSynchronously();
        if (auto* model = ui::find<CreativeEffectVisual>(*editor, [](auto& c) { return c.isShowing(); })) model->refreshAt(0);
        ui::screenshot(*editor, stem + "-routing.png");
        CreativeEffectPanel panel(*owner, 1, view == 2 ? CreativeKind::saturator : CreativeKind::convolution);
        panel.setLookAndFeel(&editor->getLookAndFeel()); panel.getProperties().set(vocalstackThemeProperty, theme);
        panel.getProperties().set(vocalstackAnimationTimeOverrideProperty, 33.3);
                panel.setNeon(theme == 1); panel.setSize(840, 460); panel.addToDesktop(0); panel.setVisible(true);
        auto* visual = ui::find<CreativeEffectVisual>(panel, [](auto&) { return true; });
        visual->refreshAt(0); ui::passive(panel); ui::visibleLayout(panel);
        ui::screenshot(panel, stem + "-controls.png");
        if (view == 0) {
            auto& combo = ui::combo(panel, "fx1convolution_space"); combo.showPopup();
            updateUiTests::captureOpenPopup(panel, stem + "-chooser");
        }
        panel.setLookAndFeel(nullptr);
    }
}
inline void run() { contractAndState(); controls(); }
} // namespace vocalstack::factoryFxUiTests
