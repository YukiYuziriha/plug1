#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include "VocalEngine.h"
#include "VisualTheme.h"

namespace vocalstack {

class MeterPanel final : public juce::Component, public juce::SettableTooltipClient {
public:
    MeterPanel();
    void setMeters(const Meters&);
    void setNeon(bool);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;

    void focusGained(FocusChangeType) override { repaint(); }
    void focusLost(FocusChangeType) override { repaint(); }

    std::function<void()> onReset;

private:
    Meters meters{};
    bool neon = false;
};

} // namespace vocalstack
