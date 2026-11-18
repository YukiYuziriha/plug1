#include "PluginEditor.h"
#include "GuiTestDisplay.h"
#include "RoutingUiArchitectureTests.h"
#include "PassiveUiArchitectureTests.h"
#include "DiscreteUiArchitectureTests.h"
#include "EffectiveUiArchitectureTests.h"
#include <iostream>
#include <stdexcept>
#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#undef near
#undef far
#endif

namespace {
void dispatch() {
#if JUCE_WINDOWS
    MSG message;
    while (PeekMessage(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message); DispatchMessage(&message);
    }
#endif
    juce::Timer::callPendingTimersSynchronously();
}
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
template<class T> T* find(juce::Component& root) {
    if (auto* match = dynamic_cast<T*>(&root)) return match;
    for (auto* child : root.getChildren()) if (auto* match = find<T>(*child)) return match;
    return nullptr;
}
void revealNavigation() {
    using namespace vocalstack;
    guiTestDisplay::enumerate();
    auto owner = std::make_unique<Processor>();
    Editor editor(*owner);
    editor.setSize(940, juce::jmin(780, guiTestDisplay::selected().userArea.getHeight()));
    guiTestDisplay::attach(editor);
    auto* routing = find<RoutingPanel>(editor);
    auto* tuning = find<TuningPanel>(editor);
    require(routing && tuning && bool(routing->onRevealParameter), "Missing reveal coordinator");
    routing->onRevealParameter("tuningAmount");
    require(tuning->isShowing(), "Reveal navigated away from Tuning target");
    routing->onRevealParameter("leadPitch");
    require(!tuning->isVisible(), "Stack parameter reveal left Tuning selected");
    editor.removeFromDesktop();
}
void nestedReveal() {
    juce::Component root, outerContent, innerContent, target;
    juce::Viewport outer, inner;
    root.setSize(200, 120);
    root.addAndMakeVisible(outer); outer.setBounds(root.getLocalBounds());
    outerContent.setSize(600, 600); outer.setViewedComponent(&outerContent, false);
    outerContent.addAndMakeVisible(inner); inner.setBounds(300, 300, 180, 100);
    innerContent.setSize(500, 500); inner.setViewedComponent(&innerContent, false);
    innerContent.addAndMakeVisible(target); target.setBounds(350, 350, 40, 30);
    vocalstack::revealThroughViewports(target);
    require(inner.getViewArea().contains(target.getBounds()), "Inner viewport did not reveal target");
    require(outer.getViewArea().contains(outerContent.getLocalArea(&target, target.getLocalBounds())),
            "Outer viewport did not reveal target after inner scrolling");
    const auto position = outer.getViewPosition();
    vocalstack::revealThroughViewports(target);
    require(position == outer.getViewPosition(), "Already visible target moved outer viewport");
    inner.setViewedComponent(nullptr, false); outer.setViewedComponent(nullptr, false);
}
}
int main() {
    std::cout << std::unitbuf;
    juce::ScopedJuceInitialiser_GUI gui;
    int failures = 0;
    const auto run = [&](const char* name, auto test) {
        std::cout << "[RUN] " << name << '\n';
        try { test(); std::cout << "[PASS] " << name << '\n'; }
        catch (const std::exception& error) { ++failures; std::cerr << "[FAIL] " << name << ": " << error.what() << '\n'; }
        juce::PopupMenu::dismissAllActiveMenus(); dispatch();
    };
    run("Routing document/history and edit teardown", [] { vocalstack::routingUiArchitectureTests::run(); });
    run("Passive focus after Sync", [] { vocalstack::passiveUiArchitectureTests::focusAfterSync(); });
    run("Deliberate numeric entry commit/cancel", [] { vocalstack::passiveUiArchitectureTests::deliberateEditorCommitCancel(); });
    run("Modulation nested wheel policy", [] { vocalstack::passiveUiArchitectureTests::modulationPanelNestedWheel(); });
    run("Discrete control assignment contract", [] { vocalstack::discreteUiArchitectureTests::run(dispatch); });
    run("Crossover coherent effective state", [] { vocalstack::EffectiveUiArchitectureTests::crossoverEffectiveSnapshot(); });
    run("Tuning base edits and operational state", [] { vocalstack::EffectiveUiArchitectureTests::tuningBaseAndOperationalState(); });
    run("Formants assignment does not write base", [] { vocalstack::EffectiveUiArchitectureTests::preserveFormantsAssignDoesNotWriteBase(); });
    run("Page-aware reveal", revealNavigation);
    run("Nested viewport reveal", nestedReveal);
    std::cout << "UI architecture failures=" << failures << '\n';
    return failures ? 1 : 0;
}
