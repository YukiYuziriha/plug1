#pragma once

#include "FxVisuals.h"
#include "TuningPanel.h"
#include "GuiTestDisplay.h"
#include <memory>
#include <stdexcept>

namespace vocalstack {

// Coordinator hooks; no static registration. Run on the JUCE message thread.
// The first two hooks are non-native; the Assign hook places its native owner
// on the selected display before opening the real shared assignment menu.
struct EffectiveUiArchitectureTests {
    static void require(bool ok, const char* message) {
        if (!ok) throw std::runtime_error(message);
    }
    static void set(Processor& owner, const char* id, float value) {
        auto* parameter = owner.parameters.getParameter(id);
        require(parameter != nullptr, "Missing effective UI fixture parameter");
        parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
    }
    static float base(Processor& owner, const char* id) {
        return owner.parameters.getRawParameterValue(id)->load();
    }
    static void target(Processor& owner, ModulationSnapshot& snapshot, const char* id, float value) {
        auto* parameter = owner.parameters.getParameter(id);
        require(parameter != nullptr, "Missing effective UI snapshot target");
        snapshot.targets.push_back({id, parameter->getValue(), parameter->convertTo0to1(value), 0});
    }

    static void crossoverEffectiveSnapshot() {
        auto storage = std::make_unique<Processor>();
        auto& owner = *storage;
        set(owner, "fx1On", 1);
        set(owner, "fx1BandCount", 3);
        set(owner, "fx1CrossoverLow", 250);
        set(owner, "fx1CrossoverHigh", 4000);
        // APVTS float normalization can round-trip slightly away from the
        // requested Hz. Detect writes against the actual stored bases exactly.
        const auto bases = [&] {
            return std::array<float, 4>{base(owner, "fx1On"), base(owner, "fx1BandCount"),
                base(owner, "fx1CrossoverLow"), base(owner, "fx1CrossoverHigh")};
        };
        const auto before = bases();
        require(before[0] == 1 && before[1] == 3
                && std::abs(before[2] - 250) < .01f && std::abs(before[3] - 4000) < .01f,
                "Crossover fixture did not set the intended base values");
        toneui::CrossoverVisual visual(owner, 1);
        ModulationSnapshot snapshot;
        snapshot.consistent = true;
        target(owner, snapshot, "fx1On", 0);
        target(owner, snapshot, "fx1BandCount", 2);
        target(owner, snapshot, "fx1CrossoverLow", 800);
        target(owner, snapshot, "fx1CrossoverHigh", 6000);
        const auto check = [&](const auto& model) {
            require(!model.enabled && model.count == 2, "Crossover bypass/count read host bases instead of effective snapshot");
            require(std::abs(model.low - 800) < 1 && std::abs(model.high - 6000) < 1,
                    "Crossover cutoff model ignored effective values");
        };
        check(visual.renderParameters(snapshot));
        auto overlapping = snapshot;
        overlapping.consistent = false;
        for (auto& value : overlapping.targets) value.effectiveNormalized = 1;
        check(visual.renderParameters(overlapping));
        require(bases() == before, "Crossover model wrote host bases");
        ModulationSnapshot unassigned;
        unassigned.consistent = true;
        const auto model = visual.renderParameters(unassigned);
        require(model.enabled && model.count == 3 && model.low == before[2] && model.high == before[3],
                "Removed crossover assignments did not return to base");
        require(bases() == before, "Removing crossover display assignments wrote host bases");
    }

    static void tuningBaseAndOperationalState() {
        auto storage = std::make_unique<Processor>();
        auto& owner = *storage;
        set(owner, "tuningEnabled", 1);
        set(owner, "tuningRoot", 0);
        set(owner, "tuningScale", float(int(TuningScale::Custom)));
        set(owner, "tuningNotes", 1);
        TuningPanel panel(owner);
        require(panel.historyPosition == 0, "Construction advanced pitch history");
        ModulationSnapshot emptyLive;
        emptyLive.consistent = true;
        target(owner, emptyLive, "tuningNotes", 0);
        panel.syncFromSnapshot(emptyLive, false);
        require(panel.notes[0].getToggleState() && panel.baseMask() == 1,
                "Effective note mask replaced editable base toggles");
        require(panel.status.getText().contains("No allowed notes")
                && panel.status.getText().contains("LIVE ALLOWED  0 / 12"),
                "NoAllowedNotes warning/live count read base mask");
        require(panel.historyPosition == 0, "Explicit control sync advanced pitch history");
        auto overlapping = emptyLive;
        overlapping.consistent = false;
        overlapping.targets.front().effectiveNormalized = 1;
        panel.syncFromSnapshot(overlapping, true);
        require(panel.operationalMask() == 0 && panel.historyPosition == 1,
                "Overlapping publication lost retained operational snapshot or periodic history");

        set(owner, "tuningNotes", 0);
        auto live = ModulationSnapshot{};
        live.consistent = true;
        target(owner, live, "tuningNotes", 1u << 7);
        panel.syncFromSnapshot(live, false);
        require(!panel.notes[7].getToggleState() && panel.baseMask() == 0,
                "Live notes leaked into base note editing");
        require(!panel.status.getText().contains("No allowed notes")
                && panel.status.getText().contains("LIVE ALLOWED  1 / 12"),
                "Base empty mask incorrectly suppressed effective allowed note");
        // A note click still edits the base mask, never the live G-only mask.
        panel.notes[0].onClick();
        require(base(owner, "tuningNotes") == 1 && panel.baseMask() == 1,
                "Note edit was derived from effective rather than base notes");
        require(panel.historyPosition == 1, "Note edit advanced pitch history");

        set(owner, "tuningEnabled", 0);
        live.targets.clear();
        target(owner, live, "tuningEnabled", 1);
        target(owner, live, "tuningRoot", 2);
        target(owner, live, "tuningScale", float(int(TuningScale::Major)));
        target(owner, live, "tuningNotes", 0x0fff);
        panel.syncFromSnapshot(live, false);
        require(panel.operationalValue("tuningRoot") == 2
                && panel.operationalValue("tuningScale") == float(int(TuningScale::Major)),
                "Live root/scale ignored effective snapshot");
        TuningSettings settings;
        settings.root = 2; settings.scale = TuningScale::Major;
        require(panel.operationalMask() == tuningNoteMask(settings), "Live note mask did not intersect effective root/scale");
        require(!panel.status.getText().contains("TUNING OFF") && !panel.mix.isEnabled(),
                "Operational enable and base control enablement were conflated");
        require(base(owner, "tuningEnabled") == 0 && base(owner, "tuningRoot") == 0
                && base(owner, "tuningScale") == float(int(TuningScale::Custom)),
                "Operational refresh wrote tuning bases");
        // Removing only the live note-mask assignment must restore the C-only
        // base intersection, not silently enable every note of D major.
        live.targets.pop_back();
        panel.syncFromSnapshot(live, false);
        settings.allowedNotesMask = 1;
        require(panel.operationalMask() == tuningNoteMask(settings) && panel.operationalMask() == 0
                && panel.status.getText().contains("No allowed notes"),
                "Named scale lost its unassigned base-mask intersection");
        live.targets.clear();
        target(owner, live, "tuningEnabled", 0);
        set(owner, "tuningEnabled", 1);
        panel.syncFromSnapshot(live, false);
        require(panel.status.getText().contains("TUNING OFF") && panel.mix.isEnabled(),
                "Effective off did not override base on in telemetry");
    }

    static void preserveFormantsAssignDoesNotWriteBase() {
        guiTestDisplay::enumerate();
        auto storage = std::make_unique<Processor>();
        auto& owner = *storage;
        set(owner, "tuningFormants", 0);
        ModulationState state;
        state.modulators = {{1}};
        juce::String error;
        require(owner.setModulationState(state, error), "Assign button fixture rejected");
        TuningPanel panel(owner);
        panel.setSize(904, 730);
        guiTestDisplay::attach(panel);
        require(panel.formants.getComponentID() == "tuningFormants", "Formants lost parameter ID");
        owner.beginModulationAssignment(1);
        const auto now = juce::Time::getCurrentTime();
        const juce::Point<float> point(10, 10);
        const auto event = [&](juce::ModifierKeys modifiers) {
            return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), point, modifiers,
                0.f, 0.f, 0.f, 0.f, 0.f, &panel.formants, &panel.formants, now, point, now, 1, false);
        };
        panel.formants.mouseDown(event(juce::ModifierKeys::leftButtonModifier));
        panel.formants.mouseDrag(event(juce::ModifierKeys::leftButtonModifier));
        panel.formants.mouseUp(event(juce::ModifierKeys()));
        panel.formants.mouseDoubleClick(event(juce::ModifierKeys::leftButtonModifier));
        juce::PopupMenu::dismissAllActiveMenus();
        require(base(owner, "tuningFormants") == 0 && !panel.formants.getToggleState(),
                "Assign click/drag/double-click wrote Preserve Formants base");
        owner.endModulationAssignment();
    }
};

} // namespace vocalstack
