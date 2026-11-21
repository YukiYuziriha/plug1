#pragma once

#include "PluginProcessor.h"
#include "VisualTheme.h"
#include "FluidBackground.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include <cstdint>

namespace vocalstack {

// Route directly to viewports: an intermediate control may swallow wheel events,
// and JUCE reports consumption even when a viewport is clamped at its edge.
inline void forwardPassiveWheel(juce::Component& component, const juce::MouseEvent& event,
                                const juce::MouseWheelDetails& wheel) {
    const bool vertical = wheel.deltaY != 0 && !event.mods.isShiftDown();
    bool hasVerticalRange = false;
    if (vertical) {
        auto verticalWheel = wheel;
        verticalWheel.deltaX = 0;
        for (auto* parent = &component; parent != nullptr; parent = parent->getParentComponent()) {
            auto* viewport = dynamic_cast<juce::Viewport*>(parent);
            if (viewport == nullptr || !viewport->isEnabled()) continue;
            auto* content = viewport->getViewedComponent();
            if (content == nullptr || content->getHeight() <= viewport->getViewArea().getHeight()) continue;
            hasVerticalRange = true;
            const auto before = viewport->getViewPosition();
            viewport->useMouseWheelMoveIfNeeded(event.getEventRelativeTo(viewport), verticalWheel);
            if (viewport->getViewPosition() != before) return;
        }
        // Do not turn a vertical edge gesture into horizontal scrolling.
        if (hasVerticalRange) return;
    }
    auto fallbackWheel = wheel;
    if (!vertical) {
        // JUCE otherwise turns Shift-wheel into vertical motion when an
        // ancestor has no horizontal scrollbar. Keep the requested axis at edges.
        fallbackWheel.deltaX = wheel.deltaX != 0 ? wheel.deltaX : wheel.deltaY;
        fallbackWheel.deltaY = 0;
    }
    for (auto* parent = &component; parent != nullptr; parent = parent->getParentComponent()) {
        if (auto* viewport = dynamic_cast<juce::Viewport*>(parent); viewport != nullptr && viewport->isEnabled()) {
            const auto before = viewport->getViewPosition();
            viewport->useMouseWheelMoveIfNeeded(event.getEventRelativeTo(viewport), fallbackWheel);
            if (viewport->getViewPosition() != before) return;
        }
    }
}

// Use for viewport backgrounds and ordinary child components too, not just
// parameter controls: native Viewport wheel handling consumes clamped edges.
class PassiveViewport : public juce::Viewport {
public:
    explicit PassiveViewport(const juce::String& name = {}) : juce::Viewport(name) {
        setScrollBarThickness(scrollbarMetrics::standard);
    }
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override {
        forwardPassiveWheel(*this, event, wheel);
    }
};

// Presentation only: the underlying parameter remains an unsigned 0..100 crossfade.
inline void configureCrossfadeDisplay(juce::Slider& slider, bool leadStack = false) {
    slider.getProperties().set("vocalstackMidpointCrossfade", false);
    slider.textFromValueFunction = [leadStack](double value) {
        return juce::String(juce::jlimit(0.0, 100.0, value), 0) + (leadStack ? "% Stack" : "% Wet");
    };
    slider.valueFromTextFunction = [](const juce::String& text) {
        const auto wet = text.contains("/") ? text.fromLastOccurrenceOf("/", false, false) : text;
        return juce::jlimit(0.0, 100.0, wet.retainCharacters("0123456789.+-").getDoubleValue());
    };
    slider.updateText();
}

inline bool hasMidpointCrossfade(const juce::Slider& slider) {
    return bool(slider.getProperties()["vocalstackMidpointCrossfade"]);
}

// Takes ownership of the base look-and-feel label; JUCE's Slider owns the result.
inline juce::Label* createPassiveSliderTextBox(juce::Label* originalTextBox) {
    class ValueLabel final : public juce::Label {
    public:
        void paint(juce::Graphics& g) override {
            auto* slider = dynamic_cast<juce::Slider*>(getParentComponent());
            if (!slider || !bool(slider->getProperties()["vocalstackTexturedBar"]) || bool(slider->getProperties()["vocalstackSeparateBarValue"]) || isBeingEdited()) {
                juce::Label::paint(g); return;
            }
            const auto palette = themePalette(*slider);
            const auto accent = slider->findColour(juce::Slider::trackColourId);
            const auto text = slider->getName() + " " + getText().trim().replace(" %", "%");
            const auto area = getLocalBounds().reduced(2, 0);
            g.setFont(getFont()); g.setColour(palette.text);
            g.drawText(text, area, juce::Justification::centred);
            const auto track = slider->getLookAndFeel().getSliderLayout(*slider).sliderBounds;
            const float valueX = float(track.getX()) + float(track.getWidth()) * float(slider->valueToProportionOfLength(slider->getValue()));
            const juce::Graphics::ScopedSaveState save(g);
            const bool bipolar = slider->getMinimum() < 0 && slider->getMaximum() > 0;
            const float origin = hasMidpointCrossfade(*slider) ? float(track.getCentreX()) : bipolar ? float(track.getX()) + float(track.getWidth()) * float(slider->valueToProportionOfLength(0)) : float(track.getX());
            g.reduceClipRegion(juce::Rectangle<float>(juce::jmin(origin, valueX) - float(getX()), 0.f,
                std::abs(valueX - origin), float(getHeight())).getSmallestIntegerContainer());
            g.setColour(accent.getPerceivedBrightness() >= .45f ? palette.background : palette.text);
            g.drawText(text, area, juce::Justification::centred);
        }
        void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override {
            forwardPassiveWheel(*this, event, wheel);
        }
        std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override {
            return createIgnoredAccessibilityHandler(*this);
        }
    };
    std::unique_ptr<juce::Label> original(originalTextBox);
    auto label = std::make_unique<ValueLabel>();
    original->copyAllExplicitColoursTo(*label);
    label->setJustificationType(original->getJustificationType());
    label->setFont(original->getFont());
    label->setKeyboardType(juce::TextInputTarget::decimalKeyboard);
    return label.release();
}

// Refresh from a single message-thread snapshot for the complete editor tree.
// Ordinary gestures never take keyboard focus; only explicit numeric entry does.
class ModulationSlider : public juce::Slider {
public:
    void bindModulation(Processor& processor, const juce::String& parameterId) {
        owner = &processor; id = parameterId;
        setComponentID(id);
        setWantsKeyboardFocus(false);
        setMouseClickGrabsKeyboardFocus(false);
        setScrollWheelEnabled(false);
        setVelocityModeParameters(1.0, 1, 0.0, true,
                                 static_cast<juce::ModifierKeys::Flags>(juce::ModifierKeys::shiftModifier | juce::ModifierKeys::ctrlModifier
                                 | juce::ModifierKeys::commandModifier));
    }
    void setTextBoxStyle(juce::Slider::TextEntryBoxPosition position, bool readOnly, int width, int height) {
        juce::Slider::setTextBoxStyle(position, readOnly || (bool(getProperties()["vocalstackTexturedBar"]) && !bool(getProperties()["vocalstackSeparateBarValue"])), width, height);
        restoreIdleFocusPolicy();
    }
    void setTextBoxIsEditable(bool editable) {
        juce::Slider::setTextBoxIsEditable(editable && (!bool(getProperties()["vocalstackTexturedBar"]) || bool(getProperties()["vocalstackSeparateBarValue"])));
        restoreIdleFocusPolicy();
    }
    void lookAndFeelChanged() override {
        juce::Slider::lookAndFeelChanged();
        restoreIdleFocusPolicy();
    }
    void enablementChanged() override {
        juce::Slider::enablementChanged();
        restoreIdleFocusPolicy();
    }
    // displayedSource may be selected without being in assignment mode.
    void refreshModulation(const ModulationSnapshot& snapshot, const ModulationState& state, uint32_t displayedSource) {
        const auto oldAssignment = assignment;
        const auto oldDepth = depth;
        const auto oldEffective = effective;
        const auto oldBipolar = bipolar;
        const auto oldHasEffective = hasEffective;
        assignment = displayedSource;
        if (snapshot.consistent) {
            effective = float(valueToProportionOfLength(getValue()));
            hasEffective = false;
            for (const auto& target : snapshot.targets) if (target.parameterId == id) {
                effective = target.effectiveNormalized; hasEffective = true; break;
            }
        }
        depth = 0; bipolar = true;
        float sourceAmount = 1;
        for (const auto& modulator : state.modulators) if (modulator.id == displayedSource) {
            bipolar = modulator.bipolar;
            sourceAmount = modulator.depth;
        }
        for (const auto& a : state.assignments)
            if (a.modulatorId == displayedSource && a.targetParameterId == id && a.enabled) { depth = a.depth * sourceAmount; break; }
        const bool assigning = owner && owner->modulationAssignmentId() != 0;
        const bool editable = !assigning && (!bool(getProperties()["vocalstackTexturedBar"]) || bool(getProperties()["vocalstackSeparateBarValue"]));
        if (isTextBoxEditable() != editable) {
            // Only entering Assign cancels deliberate base-value entry. Selection
            // and ordinary feedback ticks must not disturb the transient editor.
            if (assigning) hideTextBox(true);
            setTextBoxIsEditable(editable);
        }
        displayDirty = displayDirty || oldAssignment != assignment || oldDepth != depth
            || oldEffective != effective || oldBipolar != bipolar || oldHasEffective != hasEffective;
        const auto frame = std::floor(fluidAnimationSeconds(*this) * 10.0);
        if (isShowing() && (displayDirty || frame != displayedFrame)) {
            displayDirty = false;
            displayedFrame = frame;
            repaint();
        }
    }
    void mouseDown(const juce::MouseEvent& e) override {
        dragAssignment = owner ? owner->modulationAssignmentId() : 0;
        if (dragAssignment != 0) {
            dragDepth = 0;
            for (const auto& a : owner->getModulationState().assignments)
                if (a.modulatorId == dragAssignment && a.targetParameterId == id) { dragDepth = a.depth; break; }
            return;
        }
        if (getSliderStyle() == juce::Slider::LinearHorizontal && !e.mods.isPopupMenu()) {
            previousDragSensitivity = getMouseDragSensitivity();
            previousSnapToMouse = getSliderSnapsToMousePosition();
            const int trackWidth = getLookAndFeel().getSliderLayout(*this).sliderBounds.getWidth();
            // Relative travel stays half-speed even on narrow cards or resized inspectors.
            setSliderSnapsToMousePosition(false);
            setMouseDragSensitivity(juce::jmax(2, trackWidth * 2));
        }
        juce::Slider::mouseDown(e);
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (dragAssignment != 0) {
            if (owner->modulationAssignmentId() != dragAssignment) return;
            const float fine = e.mods.isShiftDown() || e.mods.isCtrlDown() || e.mods.isCommandDown() ? 0.1f : 1.0f;
            const auto amount = juce::jlimit(-1.0f, 1.0f,
                dragDepth + fine * float(e.getDistanceFromDragStartX() - e.getDistanceFromDragStartY()) / 180.0f);
            if (owner->setModulationDepth(dragAssignment, id, amount)) { depth = amount; repaint(); }
            return;
        }
        juce::Slider::mouseDrag(e);
    }
    void mouseUp(const juce::MouseEvent& e) override {
        if (dragAssignment != 0) { dragAssignment = 0; return; }
        juce::Slider::mouseUp(e);
        if (previousDragSensitivity > 0) {
            setMouseDragSensitivity(previousDragSensitivity);
            setSliderSnapsToMousePosition(previousSnapToMouse);
            previousDragSensitivity = 0;
        }
    }
    void mouseDoubleClick(const juce::MouseEvent& e) override {
        if (!owner || owner->modulationAssignmentId() == 0) juce::Slider::mouseDoubleClick(e);
    }
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override {
        forwardPassiveWheel(*this, event, wheel);
    }
    void drawLinearModulation(juce::Graphics& g, juce::Rectangle<float> track) {
        const auto x = [&](float value) { return track.getX() + juce::jlimit(0.0f, 1.0f, value) * track.getWidth(); };
        const float base = float(valueToProportionOfLength(getValue())), y = track.getCentreY();
        // Range shares the rail; effective feedback occupies a separate lane.
        // Never composite a small cyan dot over the base thumb: near-coincident
        // values otherwise look like one broken thin/fat pointer.
        if (assignment != 0 && depth != 0) {
            const float lo = bipolar ? base - std::abs(depth) : base + juce::jmin(0.0f, depth);
            const float hi = bipolar ? base + std::abs(depth) : base + juce::jmax(0.0f, depth);
            g.setColour(componentTheme(*this) == Theme::spectral
                ? (depth < 0 ? themePalette(*this).secondary : themePalette(*this).accent)
                : juce::Colour(depth < 0 ? 0xffff96c8 : 0xffb798ff));
            g.drawLine(x(lo), y, x(hi), y, 2.5f);
        }
        if (hasEffective) {
            g.setColour(componentTheme(*this) == Theme::spectral ? themePalette(*this).secondary : juce::Colour(0xff64f5e3));
            juce::Path marker;
            const float markerY = bool(getProperties()["vocalstackTexturedBar"]) ? track.getY() - 7 : y - 11;
                        marker.addTriangle(x(effective) - 3, markerY, x(effective) + 3, markerY, x(effective), markerY + 4);
            g.fillPath(marker);
        }
    }
    void drawModulation(juce::Graphics& g, juce::Point<float> centre, float radius, float start, float end) {
        const auto angle = [=](float value) { return start + juce::jlimit(0.0f, 1.0f, value) * (end - start); };
        const auto base = float(valueToProportionOfLength(getValue()));
        if (assignment != 0) {
            g.setColour((componentTheme(*this) == Theme::spectral ? themePalette(*this).accent
                            : juce::Colour(0xffb798ff)).withAlpha(0.45f));
            juce::Path eligible;
            eligible.addCentredArc(centre.x, centre.y, radius + 5, radius + 5, 0, start, end, true);
            g.strokePath(eligible, juce::PathStrokeType(1));
            const float lo = bipolar ? base - std::abs(depth) : base + juce::jmin(0.0f, depth);
            const float hi = bipolar ? base + std::abs(depth) : base + juce::jmax(0.0f, depth);
            juce::Path range;
            range.addCentredArc(centre.x, centre.y, radius + 5, radius + 5, 0, angle(lo), angle(hi), true);
            g.setColour(componentTheme(*this) == Theme::spectral
                ? (depth < 0 ? themePalette(*this).secondary : themePalette(*this).accent)
                : juce::Colour(depth < 0 ? 0xffff96c8 : 0xffb798ff));
            g.strokePath(range, juce::PathStrokeType(2.5f));
        }
        if (hasEffective && std::abs(effective - base) > .001f) {
            const auto point = centre.getPointOnCircumference(radius + 8, angle(effective));
            g.setColour(componentTheme(*this) == Theme::spectral ? themePalette(*this).secondary : juce::Colour(0xff64f5e3));
            g.fillRect(point.x - 2, point.y - 2, 4.f, 4.f);
        }
    }
private:
    void restoreIdleFocusPolicy() {
        // JUCE Label::setEditable restores keyboard focusability. Keep the idle
        // label passive without touching the deliberate transient TextEditor.
        const auto passive = [](auto&& self, juce::Component& component) -> void {
            if (dynamic_cast<juce::TextEditor*>(&component)) return;
            component.setWantsKeyboardFocus(false);
            component.setMouseClickGrabsKeyboardFocus(false);
            component.setFocusContainerType(juce::Component::FocusContainerType::none);
            for (auto* child : component.getChildren()) self(self, *child);
        };
        passive(passive, *this);
    }
    bool displayDirty = true;
    double displayedFrame = -1;
    Processor* owner = nullptr;
    juce::String id;
    uint32_t assignment = 0, dragAssignment = 0;
    float depth = 0, dragDepth = 0, effective = 0;
    bool bipolar = true, hasEffective = false;
    int previousDragSensitivity = 0;
    bool previousSnapToMouse = true;
};

class ParameterKnob final : public juce::Component {
public:
    // Baseline input to pixelFontOptions; the shared policy adds 2 exactly once.
    static constexpr float controlFontSize = 12.5f;
    class Style final : public PixelLookAndFeel {
    public:
        static constexpr float linearPointerHeight = 10.f;
        static constexpr int rotaryValueLaneHeight = 22;
        static float rotaryRingWidth(float outerRadius) { return juce::jmin(8.f, outerRadius * .38f); }
        static constexpr int texturedBarHeight = 25;
        static juce::Rectangle<float> texturedBarBounds(juce::Rectangle<float> area) {
            const float height = juce::jmin(float(texturedBarHeight), juce::jmax(1.f, area.getHeight() - 8));
            return {area.getX(), area.getY() + 8 + (area.getHeight() - 8 - height) * .5f, area.getWidth(), height};
        }
        juce::Slider::SliderLayout getSliderLayout(juce::Slider& slider) override {
            if (bool(slider.getProperties()["vocalstackInspectorRotary"]) || bool(slider.getProperties()["vocalstackCellRotary"])) {
                juce::Slider::SliderLayout layout;
                auto area = slider.getLocalBounds();
                layout.textBoxBounds = area.removeFromTop(rotaryValueLaneHeight);
                layout.sliderBounds = area;
                return layout;
            }
            if (!bool(slider.getProperties()["vocalstackTexturedBar"])) {
                auto layout = juce::LookAndFeel_V4::getSliderLayout(slider);
                if (slider.getTextBoxPosition() == juce::Slider::TextBoxAbove
                    || slider.getTextBoxPosition() == juce::Slider::TextBoxBelow)
                    layout.textBoxBounds = layout.textBoxBounds.withSizeKeepingCentre(
                        slider.getWidth(), layout.textBoxBounds.getHeight());
                return layout;
            }
            juce::Slider::SliderLayout layout;
            if (bool(slider.getProperties()["vocalstackSeparateBarValue"])) {
                auto area = slider.getLocalBounds();
                layout.textBoxBounds = area.removeFromTop(22);
                layout.sliderBounds = area.reduced(4, 0);
                return layout;
            }
            layout.sliderBounds = slider.getLocalBounds().reduced(4, 0);
            const auto bar = texturedBarBounds(layout.sliderBounds.toFloat()).toNearestInt();
            layout.textBoxBounds = bar.reduced(4, 0);
            return layout;
        }
        juce::Label* createSliderTextBox(juce::Slider& slider) override {
            auto* label = createPassiveSliderTextBox(juce::LookAndFeel_V4::createSliderTextBox(slider));
            label->setFont(juce::Font(pixelFontOptions(float(slider.getProperties().getWithDefault("vocalstackValueFontSize", controlFontSize)))));
            label->setMinimumHorizontalScale(1.f);
            if (bool(slider.getProperties()["vocalstackTexturedBar"]) && !bool(slider.getProperties()["vocalstackSeparateBarValue"]))
                label->setInterceptsMouseClicks(false, false);
            return label;
        }
        void drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h,
                              float position, float, float, juce::Slider::SliderStyle,
                              juce::Slider& slider) override {
            if (bool(slider.getProperties()["vocalstackTexturedBar"])) {
                drawTexturedBar(g, {float(x), float(y), float(juce::jmax(1, w)), float(h)}, position, slider);
                return;
            }
            const auto track = juce::Rectangle<float>(float(x), float(y) + float(h) * .5f + 1.f - linearPointerHeight * .5f,
                                                                   float(juce::jmax(1, w)), linearPointerHeight);
            const bool bipolar = slider.getMinimum() < 0 && slider.getMaximum() > 0;
            const float origin = hasMidpointCrossfade(slider) ? track.getCentreX()
                            : bipolar ? track.getX() + float(slider.valueToProportionOfLength(0)) * track.getWidth() : track.getX();
            const float thumb = juce::jlimit(track.getX(), track.getRight(), position);
            const auto palette = themePalette(slider);
            linearRail.paint(g, track.toNearestInt(), palette.inset, palette.accent, fluidAnimationSeconds(slider), .65f);
            const auto fill = juce::Rectangle<float>(juce::jmin(origin, thumb), track.getY(), std::abs(thumb - origin), track.getHeight());
            linearFill.paint(g, fill.toNearestInt(), slider.findColour(juce::Slider::trackColourId),
                             palette.accent, fluidAnimationSeconds(slider), .45f);
            if (bipolar || hasMidpointCrossfade(slider)) {
                g.setColour(componentTheme(slider) == Theme::device ? juce::Colour(0xffd7dfe6) : themePalette(slider).muted);
                g.drawVerticalLine(juce::roundToInt(origin), track.getY(), track.getBottom());
            }
            if (auto* mod = dynamic_cast<ModulationSlider*>(&slider)) mod->drawLinearModulation(g, track);
            // Draw the single base thumb last so range colour cannot split it.
            g.setColour(themePalette(slider).text);
            g.fillRect(thumb - 2, track.getY(), 4.f, linearPointerHeight);
        }
        void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float position,
                              float start, float end, juce::Slider& slider) override {
            if (bool(slider.getProperties()["vocalstackSquarePrototype"])) {
                const auto p = themePalette(slider);
                const auto box = juce::Rectangle<float>(float(x), float(y), float(w), float(h)).withSizeKeepingCentre(float(juce::jmin(w, h)) * .68f, float(juce::jmin(w, h)) * .68f);
                faceMaterials.paint(g, slider, box.toNearestInt(), p.inset, p.accent);
                g.setColour(p.border); g.drawRect(box, 4.f);
                // Open square perimeter: bottom-left -> top-left -> top-right -> bottom-right.
                const float distance = position * 3.f;
                juce::Path indicator; indicator.startNewSubPath(box.getBottomLeft());
                indicator.lineTo(box.getX(), box.getBottom() - juce::jmin(1.f, distance) * box.getHeight());
                if (distance > 1) indicator.lineTo(box.getX() + juce::jmin(1.f, distance - 1) * box.getWidth(), box.getY());
                if (distance > 2) indicator.lineTo(box.getRight(), box.getY() + (distance - 2) * box.getHeight());
                g.setColour(slider.findColour(juce::Slider::trackColourId));
                g.strokePath(indicator, juce::PathStrokeType(4.f, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
                return;
            }
            const auto centre = juce::Rectangle<float>(float(x), float(y), float(w), float(h)).getCentre();
            // The inspector supplies separate value/caption lanes. Reserve ten
            // pixels around its dial for the modulation arc and effective marker.
            const float radius = (bool(slider.getProperties()["vocalstackInspectorRotary"]) || bool(slider.getProperties()["vocalstackCellRotary"]))
                ? juce::jmax(1.f, float(juce::jmin(w, h)) * .5f - 8.f)
                : float(juce::jmin(w, h)) * 0.34f;
            const float angle = start + position * (end - start);
            const bool signedRange = slider.getMinimum() < 0 && slider.getMaximum() > 0;
            const float origin = hasMidpointCrossfade(slider) ? (start + end) * .5f
                : signedRange ? start + float(slider.valueToProportionOfLength(0)) * (end - start) : start;
            const float outer = radius + 2.4f; // Previous 4.8px progress stroke's outer edge.
            const float width = rotaryRingWidth(outer);
            const float ringRadius = outer - width * .5f;
            const auto textureBounds = juce::Rectangle<float>(outer * 2, outer * 2).withCentre(centre).getSmallestIntegerContainer();
            const auto arc = [&](ArcGeometry& cache, FluidBackground& material, float a, float b, juce::Colour colour) {
                const auto& stroke = cache.get(centre, ringRadius, a, b, width);
                const juce::Graphics::ScopedSaveState save(g);
                g.reduceClipRegion(stroke);
                // Stable full-dial bounds keep value movement from resizing/reseeding the material.
                material.paint(g, textureBounds, colour, themePalette(slider).accent, fluidAnimationSeconds(slider), .45f);
            };
            arc(trackGeometry, trackTexture, start, end, componentTheme(slider) == Theme::spectral ? themePalette(slider).border : juce::Colour(0xff46515b));
            arc(valueGeometry, valueTexture, origin, angle, slider.findColour(juce::Slider::trackColourId));
            if (signedRange || hasMidpointCrossfade(slider)) {
                g.setColour(componentTheme(slider) == Theme::spectral ? themePalette(slider).muted : juce::Colour(0xffd7dfe6));
                g.drawLine({centre.getPointOnCircumference(radius + 2, origin),
                            centre.getPointOnCircumference(radius + 5, origin)}, 1);
            }
            g.setColour(componentTheme(slider) == Theme::spectral ? themePalette(slider).inset : juce::Colour(0xff20262d));
            const float faceRadius = juce::jmax(0.f, juce::jmin(radius * .775f, outer - width - 1.f));
            const auto face = juce::Rectangle<float>(faceRadius * 2, faceRadius * 2).withCentre(centre);
                        { juce::Path clip; clip.addEllipse(face); juce::Graphics::ScopedSaveState state(g);
                          g.reduceClipRegion(clip);
                          faceMaterials.paint(g, slider, face.toNearestInt(),
                              componentTheme(slider) == Theme::spectral ? themePalette(slider).inset : juce::Colour(0xff20262d),
                              themePalette(slider).accent); }
            g.setColour(slider.findColour(juce::Slider::thumbColourId));
            g.drawLine({centre.getPointOnCircumference(faceRadius * .45f, angle),
                        centre.getPointOnCircumference(faceRadius * .9f, angle)}, 2);
            if (auto* mod = dynamic_cast<ModulationSlider*>(&slider)) mod->drawModulation(g, centre, radius, start, end);
        }
    private:
        FluidBackground linearRail{FluidBackground::Material::control}, linearFill{FluidBackground::Material::control};
        // The same 64x13 clustered field, tiled with exact three-pixel texels.
        // Explicit animation phase changes the material, never the value geometry.
        FluidBackground barMaterial{FluidBackground::Material::fatBar};
        void prepareTexture(juce::Colour accent, double seconds) {
            const double frame = std::floor(seconds * 10.0);
            if (texture.isValid() && accent == textureColour && frame == textureFrame) return;
            textureColour = accent; textureFrame = frame;
            if (!texture.isValid()) texture = juce::Image(juce::Image::RGB, 192, 39, false);
            juce::Graphics g(texture);
            barMaterial.paint(g, texture.getBounds(), accent, accent, seconds, .35f, 64);
        }
        void drawTexturedBar(juce::Graphics& g, juce::Rectangle<float> area, float position, juce::Slider& slider) {
            const auto track = texturedBarBounds(area).toNearestInt();
            const auto palette = themePalette(slider);
            const auto accent = slider.findColour(juce::Slider::trackColourId);
            prepareTexture(accent, fluidAnimationSeconds(slider));
            const float thumb = juce::jlimit(float(track.getX()), float(track.getRight()), position);
            const bool bipolar = slider.getMinimum() < 0 && slider.getMaximum() > 0;
            const float origin = hasMidpointCrossfade(slider) ? float(track.getCentreX())
                : bipolar ? float(track.getX()) + float(slider.valueToProportionOfLength(0)) * float(track.getWidth()) : float(track.getX());
            g.setColour(palette.inset); g.fillRect(track);
            {
                const juce::Graphics::ScopedSaveState save(g);
                g.reduceClipRegion(juce::Rectangle<float>(juce::jmin(origin, thumb), float(track.getY()),
                    std::abs(thumb - origin), float(track.getHeight())).getSmallestIntegerContainer());
                g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);
                for (int left = track.getX(); left < track.getRight(); left += texture.getWidth())
                    g.drawImageAt(texture, left, track.getY());
            }
            if (bool(slider.getProperties()["vocalstackSeparateBarValue"])) {
                const auto id = slider.getComponentID();
                const auto caption = id == "outputGain" || id == "globalOutput" ? "Output"
                    : id == "double" ? "Blend" : "Mix";
                g.setFont(juce::Font(pixelFontOptions(controlFontSize)));
                g.setColour(palette.text);
                g.drawText(caption, track.reduced(4, 0), juce::Justification::centred, false);
            }
            g.setColour(palette.border); g.drawRect(track, 1);
            if (bipolar || hasMidpointCrossfade(slider)) { g.setColour(palette.muted); g.drawVerticalLine(juce::roundToInt(origin), float(track.getY()), float(track.getBottom())); }
            if (auto* mod = dynamic_cast<ModulationSlider*>(&slider)) mod->drawLinearModulation(g, track.toFloat());
            // One flat base marker, drawn after the modulation range. Effective
            // feedback stays in its own lane above the entire thick bar.
            g.setColour(palette.text);
            g.fillRect(juce::Rectangle<float>(thumb - 1.5f, float(track.getY()), 3.f, float(track.getHeight())));
        }
        struct ArcGeometry {
            const juce::Path& get(juce::Point<float> c, float r, float a, float b, float w) {
                if (valid && centre == c && radius == r && start == a && end == b && width == w) return stroke;
                valid = true; centre = c; radius = r; start = a; end = b; width = w;
                juce::Path path;
                path.addCentredArc(c.x, c.y, r, r, 0, juce::jmin(a, b), juce::jmax(a, b), true);
                stroke.clear();
                juce::PathStrokeType(w, juce::PathStrokeType::curved, juce::PathStrokeType::butt).createStrokedPath(stroke, path);
                return stroke;
            }
            juce::Point<float> centre;
            float radius = 0, start = 0, end = 0, width = 0;
            bool valid = false;
            juce::Path stroke;
        };
        ButtonFluidBackground faceMaterials;
        ArcGeometry trackGeometry, valueGeometry;
        FluidBackground trackTexture{FluidBackground::Material::control}, valueTexture{FluidBackground::Material::control};
        juce::Image texture;
        juce::Colour textureColour;
        double textureFrame = -1;
    };
public:
    ParameterKnob(Processor& owner, const juce::String& id, const juce::String& title, const char* unit = "") {
        setComponentID(id + "Control");
        label.setText(title, juce::dontSendNotification);
        label.setJustificationType(juce::Justification::centred);
        label.setFont(juce::Font(pixelFontOptions(controlFontSize)));
        label.setMinimumHorizontalScale(1.f);
        label.setColour(juce::Label::textColourId, juce::Colour(0xffe4e9ee));
        label.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(label);
        slider.bindModulation(owner, id);
        slider.setName(title); slider.setTitle(title);
        slider.setTooltip(title + ": drag to change. Double-click to reset. Click the number to type a value.");
        slider.setLookAndFeel(&style);
        slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::TextBoxAbove, false, 80, 21);
        addAndMakeVisible(slider);
        link = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(owner.parameters, id, slider);
        if (auto* parameter = owner.parameters.getParameter(id))
            slider.setDoubleClickReturnValue(true, parameter->convertFrom0to1(parameter->getDefaultValue()));
        const juce::String suffix(unit);
        slider.textFromValueFunction = [suffix](double value) {
            return juce::String(value, suffix == "%" || suffix == "Hz" || suffix == "ms" ? 0 : 1)
                + (suffix.isEmpty() ? juce::String() : " " + suffix);
        };
        slider.valueFromTextFunction = [](const juce::String& value) { return value.getDoubleValue(); };
        if (id == "globalMix" || id == "choraleMix" || (id.startsWith("fx") && id.endsWith("Mix"))
                    || (id.startsWith("mix") && id.endsWith("Balance")))
            configureCrossfadeDisplay(slider);
        if (id == "double") configureCrossfadeDisplay(slider);
        if (id.endsWith("Pan")) {
            slider.textFromValueFunction = [](double value) {
                return std::abs(value) < 0.005 ? juce::String("C")
                    : juce::String(std::abs(value) * 100, 0) + (value < 0 ? " L" : " R");
            };
            slider.valueFromTextFunction = [](const juce::String& text) {
                if (text.trim().equalsIgnoreCase("C")) return 0.0;
                const double value = text.getDoubleValue();
                if (text.containsIgnoreCase("L")) return -std::abs(value) / 100;
                if (text.containsIgnoreCase("R")) return std::abs(value) / 100;
                return value;
            };
        }

        slider.updateText(); setNeon(false);
        setWantsKeyboardFocus(false); setMouseClickGrabsKeyboardFocus(false);
    }
    void setLinear(bool value) {
        slider.setSliderStyle(value ? juce::Slider::LinearHorizontal : juce::Slider::RotaryHorizontalVerticalDrag);
        slider.getProperties().set("vocalstackTexturedBar", value &&
            (slider.getComponentID() == "globalOutput" || slider.getComponentID() == "outputGain" || slider.getComponentID() == "globalMix" || slider.getComponentID() == "double"
                         || slider.getComponentID().startsWith("fx") || slider.getComponentID() == "choraleMix" || slider.getComponentID() == "choraleMotion"
                         || (slider.getComponentID().startsWith("mix") && slider.getComponentID().endsWith("Balance"))));
        slider.setTextBoxStyle(juce::Slider::TextBoxAbove, false, 80, value ? 20 : 21);
        resized();
    }
    ~ParameterKnob() override { slider.setLookAndFeel(nullptr); }
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override {
        forwardPassiveWheel(*this, event, wheel);
    }
    int captionHeightForWidth(int width) const {
        juce::AttributedString caption;
        caption.append(label.getText(), label.getFont());
        juce::TextLayout layout;
        layout.createLayout(caption, float(juce::jmax(1, width - 10)));
        return juce::jmax(30, int(std::ceil(layout.getHeight())) + 2);
    }
    void resized() override {
        auto area = getLocalBounds();
                const bool merged = bool(slider.getProperties()["vocalstackTexturedBar"]);
                label.setVisible(!merged);
                if (!merged) label.setBounds(area.removeFromBottom(captionHeightForWidth(area.getWidth())));
                slider.setBounds(area);
    }
    void setNeon(bool neon) {
        const auto p = themePalette(*this, neon);
        const auto selectedTheme = componentTheme(*this, neon);
        const auto text = selectedTheme == Theme::device ? juce::Colour(0xffe4e9ee) : p.text;
        label.setColour(juce::Label::textColourId, text);
        slider.setColour(juce::Slider::trackColourId, p.accent);
        slider.setColour(juce::Slider::thumbColourId, selectedTheme == Theme::device ? p.text : p.secondary);
        slider.setColour(juce::Slider::textBoxTextColourId, text);
        slider.setColour(juce::Slider::textBoxHighlightColourId, p.accent.withAlpha(0.3f));
        slider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        for (auto* child : slider.getChildren()) {
            child->setWantsKeyboardFocus(false); child->setMouseClickGrabsKeyboardFocus(false);
        }
    }
    ModulationSlider slider;
private:
    Style style;
    juce::Label label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> link;
};

inline void refreshParameterModulation(juce::Component& root, const ModulationSnapshot& snapshot,
                                       const ModulationState& state, uint32_t assigning) {
    if (auto* slider = dynamic_cast<ModulationSlider*>(&root)) slider->refreshModulation(snapshot, state, assigning);
    // Keep dormant duplicate controls in sync too: switching inspectors/views
    // must never briefly expose editable base text while Assign is active.
    for (auto* child : root.getChildren())
        refreshParameterModulation(*child, snapshot, state, assigning);
}

} // namespace vocalstack
