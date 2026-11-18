#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace vocalstack {

// Idle controls leave host shortcuts alone; deliberate text entry keeps its own
// focus policy until JUCE commits or cancels the transient editor.
inline void applyPassiveFocusPolicy(juce::Component& component) {
    if (dynamic_cast<juce::TextEditor*>(&component) != nullptr) return;
    component.setWantsKeyboardFocus(false);
    component.setMouseClickGrabsKeyboardFocus(false);
    component.setFocusContainerType(juce::Component::FocusContainerType::none);
    for (auto* child : component.getChildren()) applyPassiveFocusPolicy(*child);
}

// Reveal from the innermost viewport outwards, preserving the current position
// when the target already fits. Each outer viewport sees the updated geometry.
inline void revealThroughViewports(juce::Component& target) {
    for (auto* parent = target.getParentComponent(); parent; parent = parent->getParentComponent()) {
        auto* viewport = dynamic_cast<juce::Viewport*>(parent);
        if (!viewport || !viewport->getViewedComponent()) continue;
        const auto bounds = viewport->getViewedComponent()->getLocalArea(&target, target.getLocalBounds());
        const auto view = viewport->getViewArea();
        auto position = viewport->getViewPosition();
        if (bounds.getX() < view.getX()) position.x = bounds.getX();
        else if (bounds.getRight() > view.getRight()) position.x = bounds.getRight() - view.getWidth();
        if (bounds.getY() < view.getY()) position.y = bounds.getY();
        else if (bounds.getBottom() > view.getBottom()) position.y = bounds.getBottom() - view.getHeight();
        viewport->setViewPosition(position);
    }
}

} // namespace vocalstack
