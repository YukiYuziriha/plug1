#pragma once
#include "FactoryFxUiTests.h"
#include "GuiTestDisplay.h"
#include "MainBusFxRackPanel.h"

namespace vocalstack::measuredFieldUiTests {
namespace ui = toneUiTests;
inline void save(juce::Component& component, const juce::String& name) {
    const auto image = component.createComponentSnapshot(component.getLocalBounds());
    const auto file = juce::File::getCurrentWorkingDirectory().getChildFile(name);
    ui::require(!file.existsAsFile() || file.deleteFile(), "Cannot replace measured field PNG");
    juce::FileOutputStream stream(file);
    ui::require(stream.openedOk() && juce::PNGImageFormat().writeImageToStream(image, stream), "Measured field PNG write");
}
inline void run(bool render = false) {
    guiTestDisplay::enumerate();
    for (int theme = 0; theme < (render ? 3 : 1); ++theme)
    for (auto kind : {CreativeKind::reverb, CreativeKind::convolution, CreativeKind::saturator}) {
        const auto type = kind == CreativeKind::reverb ? NodeType::reverb : kind == CreativeKind::convolution ? NodeType::convolution : NodeType::saturator;
        const juce::String stem = CreativeEffectPanel::effectName(kind).toLowerCase() + "-" + updateUiTests::themeName(theme);
        auto owner = std::make_unique<Processor>(); owner->setSkin(theme); owner->setMotionPercent(0);
        updateUiTests::install(*owner, type);
                if (render) {
                    auto graph = owner->getRoutingGraph();
                    RoutingNode mix; mix.id=12; mix.type=NodeType::mix; mix.mainControls=true;
                    graph.nodes.push_back(mix); graph.links.front().source=12; graph.links.push_back({1,12,0});
                    juce::String error; ui::require(owner->setRoutingGraph(graph,error), "Measured rack scene: " + error);
                }
                utilityIntegrationTests::prepare(*owner);
        ui::write(*owner, "fx1Mix", 100);
        if (kind == CreativeKind::reverb) { ui::write(*owner, "fx1reverb_decay", 4); ui::write(*owner, "fx1reverb_duck", 0); }
        if (kind == CreativeKind::saturator) ui::write(*owner, "fx1saturator_drive", 0);
        std::unique_ptr<juce::AudioProcessorEditor> editor(owner->createEditor());
        editor->setSize(1280, 800);
        guiTestDisplay::attach(*editor); ui::action(*editor, render ? "Stack" : "Routing");
                if (render) ui::action(*editor, CreativeEffectPanel::effectName(kind) + " #100");
        CreativeEffectPanel panel(*owner, 1, kind);
        panel.setLookAndFeel(&editor->getLookAndFeel());
        panel.getProperties().set(vocalstackThemeProperty, theme);
        panel.getProperties().set(vocalstackAnimationTimeOverrideProperty, 0.0);
        panel.setNeon(theme == 1); panel.setSize(840, 700);
        guiTestDisplay::attach(panel);
        auto* visual = ui::find<CreativeEffectVisual>(panel, [](auto&) { return true; });
        ui::require(visual != nullptr, "Measured field visual missing");
        visual->refreshAt(0);
        const auto idle = visual->createComponentSnapshot(visual->getLocalBounds());
        ui::require(!visual->hasMeasuredActivity(), "Idle invented measured data");
        juce::MidiBuffer midi; juce::AudioBuffer<float> audio(2, 256);
        int position = 0;
        uint64_t previous = 0; bool sawTail = false, changed = false;
        juce::Image active;
        const int frames = render ? 150 : 85;
        juce::String csv = "frame,seconds,drive_db,input_rms,wet_rms,history_samples\n";
        for (int frame = 0; frame < frames; ++frame) {
            const float drive = frame < 30 ? 0.f : frame < 65 ? 12.f : frame < 105 ? 30.f : 3.f;
            if (kind == CreativeKind::saturator) ui::write(*owner, "fx1saturator_drive", drive);
            // Exactly 1920 real samples per image: 25 fps at 48 kHz. Irregular
            // callback sizes deliberately cross monitor-bin and host boundaries.
            constexpr int blocks[]{1, 7, 251, 3, 127, 64};
            for (int remaining = 1920, block = 0; remaining > 0; ++block) {
                const int count = std::min(remaining, blocks[block % 6]);
                audio.setSize(2, count, false, false, true);
                for (int i = 0; i < count; ++i, ++position) {
                    const double t = position / 48000.;
                    const double envelope = kind == CreativeKind::saturator
                        ? .12 + .08 * std::sin(t * 3.1) : t < .55 ? .4 * std::sin(3.141592653589793 * t / .55) : 0.;
                    audio.setSample(0, i, float(envelope * (std::sin(t * 2 * 3.141592653589793 * 173) + .28 * std::sin(t * 2 * 3.141592653589793 * 631))));
                    audio.setSample(1, i, float(envelope * (.7 * std::sin(t * 2 * 3.141592653589793 * 229))));
                }
                owner->processBlock(audio, midi); remaining -= count;
            }
            visual->refreshAt(position / 48.);
            if (frame < 2) continue; // Latency/warmup is deliberately not faked.
            ui::require(visual->hasMeasuredActivity() && visual->measuredSequence() > previous, "Measured field did not advance with Motion 0");
            previous = visual->measuredSequence();
            FxMonitorFrame measured;
            ui::require(owner->readFxMonitor(1, measured) && measured.hasWet, "Processor history missing");
            const auto& latest = measured.history.back();
            if (frame > 25 && latest.inputRms == 0 && latest.wetRms > .00001f) sawTail = true;
            csv += juce::String(frame) + "," + juce::String(position / 48000., 3) + "," + juce::String(drive) + "," + juce::String(latest.inputRms, 8) + "," + juce::String(latest.wetRms, 8) + "," + juce::String(measured.historySamples) + "\n";
            const auto image = visual->createComponentSnapshot(visual->getLocalBounds());
            if (frame == 20) active = image;
            if (frame == 50) changed = !updateUiTests::samePixels(active, image);
            if (render) {
                const auto suffix = "-" + juce::String(frame - 2).paddedLeft('0', 3) + ".png";
                save(panel, stem + "-panel" + suffix);
                save(*visual, stem + "-graph" + suffix);
                if (auto* actual = ui::find<CreativeEffectVisual>(*editor, [](auto& c) { return c.visibleInHierarchy(); })) actual->refreshAt(position / 48.);
                if (kind == CreativeKind::reverb)
                    if (auto* rack = ui::find<MainBusFxRackPanel>(*editor, [](auto& c) { return c.isShowing(); }))
                        save(*rack, stem + "-stack-row" + suffix);
                if (frame == 20 || frame == 75) {
                    if (auto* actual = ui::find<CreativeEffectVisual>(*editor, [](auto& c) { return c.visibleInHierarchy(); })) actual->refreshAt(position / 48.);
                    // Component snapshot of a verified native fixture, not a
                    // screen grab or a claim about host presentation/listening.
                    save(*editor, stem + "-editor-native-" + juce::String(frame) + ".png");
                }
            }
        }
        ui::require(changed && !updateUiTests::samePixels(idle, active), "Audio data did not change actual graph pixels");
        if (kind != CreativeKind::saturator) ui::require(sawTail, "Measured graph lost the wet tail after dry input stopped");
        if (kind == CreativeKind::reverb) {
            ui::write(*owner, "fx1reverb_freeze", 1);
            audio.setSize(2, 256); audio.clear();
            for (int i = 0; i < 100; ++i) { audio.clear(); owner->processBlock(audio, midi); }
            FxMonitorFrame frozen;
            ui::require(owner->readFxMonitor(1, frozen) && frozen.history.back().inputRms == 0 && frozen.wetRms > 0,
                        "Freeze must display actual held wet tail, not freeze capture");
            visual->refreshAt(position / 48.);
        }
        visual->refreshAt(position / 48. + 500);
        ui::require(!visual->hasMeasuredActivity(), "Stopped processing held stale field");
        ui::write(*owner, "fx1On", 0);
        audio.setSize(2, 256); audio.clear();
        for (int i = 0; i < 24; ++i) owner->processBlock(audio, midi);
        visual->refreshAt(position / 48. + 600);
        ui::require(!visual->hasMeasuredActivity(), "Bypass held measured field");
        ui::write(*owner, "fx1On", 1); ui::write(*owner, "bypass", 1);
        for (int i = 0; i < 24; ++i) { audio.clear(); owner->processBlock(audio, midi); }
        FxMonitorFrame bypassed;
        ui::require(owner->readFxMonitor(1, bypassed) && !bypassed.hasWet && bypassed.historyUsed == 0, "Whole-plugin bypass retained field");
        ui::write(*owner, "bypass", 0);
        for (int i = 0; i < 24; ++i) { audio.clear(); owner->processBlockBypassed(audio, midi); }
        ui::require(owner->readFxMonitor(1, bypassed) && !bypassed.hasWet && bypassed.historyUsed == 0, "Host bypass retained field");
        owner->reset(); visual->refreshAt(position / 48. + 700);
        ui::require(!visual->hasMeasuredActivity(), "Reset retained field");
        panel.setVisible(false); visual->refreshAt(position / 48. + 800);

        // The offscreen editor may own an additional subscription; hidden panels
        // must still release theirs without reading live DSP or affecting sound.
        if (render) {
            juce::File::getCurrentWorkingDirectory().getChildFile(stem + "-measurements.csv").replaceWithText(csv);
            std::cout << "RENDER " << stem << ": native panel and editor on selected display; component snapshots; 25 fps measured PCM, no listening claim\n";
        }
        panel.setLookAndFeel(nullptr);
    }
}
}
