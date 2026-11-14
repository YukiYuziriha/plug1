#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <iostream>
#include <stdexcept>

namespace vocalstack::guiTestDisplay {
inline const juce::Displays::Display& selected() {
    const auto& displays = juce::Desktop::getInstance().getDisplays();
    for (const auto& display : displays.displays) if (!display.isMain) return display;
    if (const auto* primary = displays.getPrimaryDisplay()) return *primary;
    throw std::runtime_error("No connected display available for native GUI fixture");
}
inline void enumerate() {
    const auto& displays = juce::Desktop::getInstance().getDisplays();
    for (const auto& display : displays.displays)
        std::cout << "DISPLAY primary=" << display.isMain << " bounds=" << display.totalArea.toString()
            << " work=" << display.userArea.toString() << " dpi=" << display.dpi
            << " scale=" << display.scale << '\n';
    const auto& display = selected();
    std::cout << "NATIVE selected " << (display.isMain ? "PRIMARY FALLBACK" : "non-primary second monitor")
              << " work=" << display.userArea.toString() << '\n';
}
inline void attach(juce::Component& component) {
    const auto work = selected().userArea;
    if (component.getWidth() > work.getWidth() || component.getHeight() > work.getHeight())
        throw std::runtime_error("Native fixture exceeds selected display work area; use a smaller native fixture or explicit offscreen render");
    component.setTopLeftPosition(work.getX() + (work.getWidth() - component.getWidth()) / 2,
                                 work.getY() + (work.getHeight() - component.getHeight()) / 2);
    component.addToDesktop(0);
    component.setVisible(true);
    if (!work.contains(component.getScreenBounds())) throw std::runtime_error("Native fixture escaped selected display");
    std::cout << "NATIVE placed bounds=" << component.getScreenBounds().toString() << '\n';
}
} // namespace vocalstack::guiTestDisplay
