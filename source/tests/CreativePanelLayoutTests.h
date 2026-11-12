#pragma once

#include "CreativeEffectPanel.h"
#include <array>
#include <memory>
#include <stdexcept>
#include <vector>

// Message-thread suite. The parent runner can include this header and call run()
// after ScopedJuceInitialiser_GUI; no plugin build or audio processing is needed.
namespace vocalstack::creativePanelLayoutTests {

inline void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

template <typename T>
inline T* find(juce::Component& root) {
    if (auto* result = dynamic_cast<T*>(&root)) return result;
    for (auto* child : root.getChildren())
        if (auto* result = find<T>(*child)) return result;
    return nullptr;
}

inline juce::Component* parameterControl(juce::Component& root, const juce::String& id) {
    if (root.getComponentID() == id) return &root;
    for (auto* child : root.getChildren())
        if (auto* result = parameterControl(*child, id)) return result;
    return nullptr;
}

inline void passive(juce::Component& root) {
    require(!root.getWantsKeyboardFocus(), "Creative control takes idle keyboard focus");
    require(!root.getMouseClickGrabsKeyboardFocus(), "Creative click grabs keyboard focus");
    for (auto* child : root.getChildren()) passive(*child);
}

inline void run() {
    auto owner = std::make_unique<Processor>();
    constexpr std::array kinds{CreativeKind::delay, CreativeKind::reverb,
        CreativeKind::compressor, CreativeKind::bitcrusher, CreativeKind::granular};
    juce::Component root;
    for (auto kind : kinds) {
        CreativeEffectPanel panel(*owner, 1, kind);
        root.addAndMakeVisible(panel);
        auto* viewport = find<juce::Viewport>(panel);
        auto* visual = find<CreativeEffectVisual>(panel);
        require(viewport && visual, "Creative viewport or visual missing");
        auto* content = viewport->getViewedComponent();
        require(content != nullptr, "Creative content missing");
        std::vector<juce::String> ids{fxParameterId(1, "Mix")};
        for (size_t i = 0; i < creativeParameterCount; ++i)
            if (creativeParameters[i].kind == kind) ids.push_back(creativeParameterId(1, i));

        for (int theme = 0; theme < 3; ++theme) {
            root.getProperties().set(vocalstackThemeProperty, theme);
            panel.setNeon(theme == 1);
            for (int width : {190, 220, 320, 480, 700, 1000, 1400}) {
                panel.setBounds(0, 0, width, 360);
                std::vector<juce::Rectangle<int>> cells;
                auto* mix = dynamic_cast<juce::Slider*>(parameterControl(panel, ids.front()));
                require(mix && mix->getSliderStyle() == juce::Slider::LinearHorizontal,
                        "Creative Mix must be a standalone horizontal slider");
                auto* mixRow = mix->getParentComponent();
                const bool split = content->getWidth() - 16 >= 680;
                require(!split || mixRow->getX() > visual->getRight(),
                        "Wide creative inspector must put controls beside the graph");
                require(split || mixRow->getY() > visual->getBottom(),
                        "Sidebar creative controls must follow the graph");
                for (const auto& id : ids) {
                    auto* control = parameterControl(panel, id);
                    require(control != nullptr, "Creative parameter disappeared during reflow");
                    auto* row = control->getParentComponent();
                    require(content->getLocalBounds().contains(row->getBounds()),
                            "Creative row outside scrollable content");
                    for (const auto& previous : cells)
                        require(!previous.intersects(row->getBounds()), "Creative rows overlap");
                    cells.push_back(row->getBounds());
                    passive(*control);
                    if (auto* slider = dynamic_cast<juce::Slider*>(control)) {
                        require(slider->getTextBoxPosition() == juce::Slider::TextBoxAbove,
                                "Creative values must be above controls");
                        if (slider != mix) require(row->getWidth() <= 84, "Creative knob gaps grow with width");
                        for (auto* sibling : row->getChildren())
                            if (auto* label = dynamic_cast<juce::Label*>(sibling))
                                require(label->getY() >= slider->getBottom(), "Creative caption must be below control");
                    } else {
                        require(control->getWidth() <= 58 && control->getHeight() == 28,
                                "Creative two-state button must remain compact");
                    }
                    panel.revealParameter(id);
                    const auto visible = viewport->getLocalArea(row, row->getLocalBounds());
                    require(viewport->getLocalBounds().contains(visible), "Creative row not fully scroll reachable");
                }
                require(visual->getHeight() >= 216, "Creative graph lost its readable height");
            }
        }

        panel.setSize(320, 280);
        viewport->setViewPosition(0, 420);
        const auto savedY = viewport->getViewPositionY();
        require(savedY > 0, "Creative scroll fixture has no range");
        panel.setSize(1400, 2000);
        require(viewport->getViewPositionY() == 0, "Tall inspector should not retain a physical scroll offset");
        panel.setSize(320, 280);
        require(viewport->getViewPositionY() == savedY, "Creative resize lost preferred scroll anchor");
        CreativeEffectPanel second(*owner, 2, kind);
        second.setSize(320, 280);
        require(find<juce::Viewport>(second)->getViewPositionY() == 0,
                "Creative instances must not share scroll state");
        require(viewport->getViewPositionY() == savedY, "Second creative instance changed first scroll state");
    }
}

} // namespace vocalstack::creativePanelLayoutTests
