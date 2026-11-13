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
    using juce::Viewport::Viewport;
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override {
        forwardPassiveWheel(*this, event, wheel);
    }
};

// Takes ownership of the base look-and-feel's label and returns a replacement
// owned by JUCE's Slider. Calling the base factory at the call site avoids
// virtual recursion and preserves the caller's look-and-feel defaults.
inline juce::Label* createPassiveSliderTextBox(juce::Label* originalTextBox) {
    class ValueLabel final : public juce::Label {
    public:
        void paint(juce::Graphics& g) override {
            auto* slider = dynamic_cast<juce::Slider*>(getParentComponent());
            if (!slider || !bool(slider->getProperties()["vocalstackTexturedBar"]) || isBeingEdited()) {
                juce::Label::paint(g); return;
            }
            const auto palette = themePalette(*slider);
            const auto accent = slider->findColour(juce::Slider::trackColourId);
            const auto text = "Mix " + getText().trim().replace(" %", "%");
            const auto area = getLocalBounds().reduced(2, 0);
            g.setFont(getFont()); g.setColour(palette.text);
            g.drawText(text, area, juce::Justification::centred);
            const auto track = slider->getLookAndFeel().getSliderLayout(*slider).sliderBounds;
            const float valueX = float(track.getX()) + float(track.getWidth()) * float(slider->valueToProportionOfLength(slider->getValue()));
            const juce::Graphics::ScopedSaveState save(g);
            g.reduceClipRegion(juce::Rectangle<int>(track.getX() - getX(), 0,
                juce::jmax(0, juce::roundToInt(valueX) - track.getX()), getHeight()));
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
        juce::Slider::setTextBoxStyle(position, readOnly || bool(getProperties()["vocalstackTexturedBar"]), width, height);
        restoreIdleFocusPolicy();
    }
    void setTextBoxIsEditable(bool editable) {
        juce::Slider::setTextBoxIsEditable(editable && !bool(getProperties()["vocalstackTexturedBar"]));
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
        const bool editable = !assigning && !bool(getProperties()["vocalstackTexturedBar"]);
                if (isTextBoxEditable() != editable) {
            // Only entering Assign cancels deliberate base-value entry. Selection
            // and ordinary feedback ticks must not disturb the transient editor.
            if (assigning) hideTextBox(true);
            setTextBoxIsEditable(editable);
        }
        repaint();
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
        if (hasEffective) {
            const auto point = centre.getPointOnCircumference(radius + 1, angle(effective));
            g.setColour(componentTheme(*this) == Theme::spectral ? themePalette(*this).secondary : juce::Colour(0xff64f5e3));
            g.fillEllipse(point.x - 2, point.y - 2, 4, 4);
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
    static constexpr float controlFontSize = 12.5f;
    class Style final : public juce::LookAndFeel_V4 {
    public:
        static constexpr int texturedBarHeight = 25;
        static juce::Rectangle<float> texturedBarBounds(juce::Rectangle<float> area) {
            const float height = juce::jmin(float(texturedBarHeight), juce::jmax(1.f, area.getHeight() - 8));
            return {area.getX(), area.getY() + 8 + (area.getHeight() - 8 - height) * .5f, area.getWidth(), height};
        }
        juce::Slider::SliderLayout getSliderLayout(juce::Slider& slider) override {
            if (!bool(slider.getProperties()["vocalstackTexturedBar"]))
                return juce::LookAndFeel_V4::getSliderLayout(slider);
            juce::Slider::SliderLayout layout;
            layout.sliderBounds = slider.getLocalBounds().reduced(4, 0);
            const auto bar = texturedBarBounds(layout.sliderBounds.toFloat()).toNearestInt();
            layout.textBoxBounds = bar.withSizeKeepingCentre(juce::jmin(124, bar.getWidth()), bar.getHeight());
            return layout;
        }
        juce::Label* createSliderTextBox(juce::Slider& slider) override {
            auto* label = createPassiveSliderTextBox(juce::LookAndFeel_V4::createSliderTextBox(slider));
                        label->setFont(juce::Font(juce::FontOptions(controlFontSize)));
            if (bool(slider.getProperties()["vocalstackTexturedBar"])) label->setInterceptsMouseClicks(false, false);
            return label;
        }
        void drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h,
                              float position, float, float, juce::Slider::SliderStyle,
                              juce::Slider& slider) override {
            if (bool(slider.getProperties()["vocalstackTexturedBar"])) {
                drawTexturedBar(g, {float(x), float(y), float(juce::jmax(1, w)), float(h)}, position, slider);
                return;
            }
            const auto track = juce::Rectangle<float>(float(x), float(y) + float(h) * 0.5f, float(juce::jmax(1, w)), 2.0f);
            const bool bipolar = slider.getMinimum() < 0 && slider.getMaximum() > 0;
            const float origin = bipolar ? track.getX() + float(slider.valueToProportionOfLength(0)) * track.getWidth() : track.getX();
            const float thumb = juce::jlimit(track.getX(), track.getRight(), position);
            g.setColour(componentTheme(slider) == Theme::device ? juce::Colour(0xff46515b) : themePalette(slider).border);
            g.fillRect(track);
            g.setColour(slider.findColour(juce::Slider::trackColourId));
            g.fillRect(juce::Rectangle<float>(juce::jmin(origin, thumb), track.getY(), std::abs(thumb - origin), 2));
            if (bipolar) {
                g.setColour(componentTheme(slider) == Theme::device ? juce::Colour(0xffd7dfe6) : themePalette(slider).muted);
                g.drawVerticalLine(juce::roundToInt(origin), track.getY() - 3, track.getY() + 5);
            }
            if (auto* mod = dynamic_cast<ModulationSlider*>(&slider)) mod->drawLinearModulation(g, track);
            // Draw the single base thumb last so range colour cannot split it.
            g.setColour(themePalette(slider).text);
            g.fillRoundedRectangle(thumb - 2, track.getCentreY() - 5, 4, 10, 1);
        }
        void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float position,
                              float start, float end, juce::Slider& slider) override {
            const auto centre = juce::Rectangle<float>(float(x), float(y), float(w), float(h)).getCentre();
            const float radius = float(juce::jmin(w, h)) * 0.34f;
            const float angle = start + position * (end - start);
            const bool signedRange = slider.getMinimum() < 0 && slider.getMaximum() > 0;
            const float origin = signedRange ? start + float(slider.valueToProportionOfLength(0)) * (end - start) : start;
            const auto arc = [&](float a, float b, juce::Colour colour, float width) {
                juce::Path path;
                path.addCentredArc(centre.x, centre.y, radius, radius, 0, juce::jmin(a, b), juce::jmax(a, b), true);
                g.setColour(colour); g.strokePath(path, juce::PathStrokeType(width));
            };
            arc(start, end, componentTheme(slider) == Theme::spectral ? themePalette(slider).border : juce::Colour(0xff46515b), 2.5f);
            arc(origin, angle, slider.findColour(juce::Slider::trackColourId), 3);
            if (signedRange) {
                g.setColour(componentTheme(slider) == Theme::spectral ? themePalette(slider).muted : juce::Colour(0xffd7dfe6));
                g.drawLine({centre.getPointOnCircumference(radius + 2, origin),
                            centre.getPointOnCircumference(radius + 5, origin)}, 1);
            }
            g.setColour(componentTheme(slider) == Theme::spectral ? themePalette(slider).inset : juce::Colour(0xff20262d));
            g.fillEllipse(juce::Rectangle<float>(radius * 1.55f, radius * 1.55f).withCentre(centre));
            g.setColour(slider.findColour(juce::Slider::thumbColourId));
            g.drawLine({centre.getPointOnCircumference(radius * 0.35f, angle),
                        centre.getPointOnCircumference(radius * 0.7f, angle)}, 2);
            if (auto* mod = dynamic_cast<ModulationSlider*>(&slider)) mod->drawModulation(g, centre, radius, start, end);
        }
    private:
        // Coordinate-hashed periodic noise, quantised into a small flat palette.
        // Explicit animation phase morphs the material, never the value geometry.
        static float textureHash(int x, int y) {
            uint32_t n = uint32_t(x) * 0x9e3779b9u ^ uint32_t(y) * 0x85ebca6bu ^ 0x51ed270bu;
            n ^= n >> 16; n *= 0x7feb352du; n ^= n >> 15;
            n *= 0x846ca68bu; n ^= n >> 16;
            return float(n & 65535u) / 65535.f;
        }
        static float textureNoise(float x, float y, int scale) {
            const int ix = int(std::floor(x / float(scale))), iy = int(std::floor(y / float(scale))), period = 64 / scale;
            const float fx = x / float(scale) - float(ix), fy = y / float(scale) - float(iy);
            const float u = fx * fx * (3 - 2 * fx), v = fy * fy * (3 - 2 * fy);
            const auto sample = [=](int a, int b) { return textureHash((a % period + period) % period, b); };
            const float a = sample(ix, iy) * (1 - u) + sample(ix + 1, iy) * u;
            const float b = sample(ix, iy + 1) * (1 - u) + sample(ix + 1, iy + 1) * u;
            return a * (1 - v) + b * v;
        }
        void prepareTexture(juce::Colour accent, double seconds) {
            const double frame = std::floor(seconds * 15.0);
            if (texture.isValid() && accent == textureColour && frame == textureFrame) return;
            textureColour = accent; textureFrame = frame;
            if (!texture.isValid()) texture = juce::Image(juce::Image::RGB, 128, 26, false);
            const float phase = float(std::fmod(frame / 15.0, 4096.0));
            juce::Image::BitmapData pixels(texture, juce::Image::BitmapData::writeOnly);
            for (int row = 0; row < 13; ++row) for (int col = 0; col < 64; ++col) {
                const float noise = .30f * textureNoise(float(col) + phase * 1.3f, float(row) + std::sin(phase * .18f) * 2.f, 8)
                                  + .35f * textureNoise(float(col) - phase * .45f, float(row) + phase * .3f, 2)
                                  + .35f * textureHash(col, row);
                const float shade = float(juce::jlimit(0, 5, int(noise * 6))) / 5.f;
                const auto colour = shade < .5f ? accent.interpolatedWith(juce::Colours::black, (.5f - shade) * .52f)
                                               : accent.interpolatedWith(juce::Colours::white, (shade - .5f) * .28f);
                for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx)
                    pixels.setPixelColour(col * 2 + dx, row * 2 + dy, colour);
            }
        }
        void drawTexturedBar(juce::Graphics& g, juce::Rectangle<float> area, float position, juce::Slider& slider) {
            const auto track = texturedBarBounds(area).toNearestInt();
            const auto palette = themePalette(slider);
            const auto accent = slider.findColour(juce::Slider::trackColourId);
            prepareTexture(accent, fluidAnimationSeconds(slider));
            const float thumb = juce::jlimit(float(track.getX()), float(track.getRight()), position);
            const bool bipolar = slider.getMinimum() < 0 && slider.getMaximum() > 0;
            const float origin = bipolar ? float(track.getX()) + float(slider.valueToProportionOfLength(0)) * float(track.getWidth())
                                         : float(track.getX());
            g.setColour(palette.inset); g.fillRect(track);
            {
                const juce::Graphics::ScopedSaveState save(g);
                g.reduceClipRegion(juce::Rectangle<float>(juce::jmin(origin, thumb), float(track.getY()),
                    std::abs(thumb - origin), float(track.getHeight())).getSmallestIntegerContainer());
                g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);
                for (int left = track.getX(); left < track.getRight(); left += texture.getWidth())
                    g.drawImageAt(texture, left, track.getY());
            }
            g.setColour(palette.border); g.drawRect(track, 1);
            if (bipolar) { g.setColour(palette.muted); g.drawVerticalLine(juce::roundToInt(origin), float(track.getY()), float(track.getBottom())); }
            if (auto* mod = dynamic_cast<ModulationSlider*>(&slider)) mod->drawLinearModulation(g, track.toFloat());
            // One flat base marker, drawn after the modulation range. Effective
            // feedback stays in its own lane above the entire thick bar.
            g.setColour(palette.text);
            g.fillRect(juce::Rectangle<float>(thumb - 1.5f, float(track.getY()), 3.f, float(track.getHeight())));
        }
        juce::Image texture;
        juce::Colour textureColour;
        double textureFrame = -1;
    };
public:
    ParameterKnob(Processor& owner, const juce::String& id, const juce::String& title, const char* unit = "") {
        setComponentID(id + "Control");
        label.setText(title, juce::dontSendNotification);
        label.setJustificationType(juce::Justification::centred);
        label.setFont(juce::Font(juce::FontOptions(controlFontSize)));
                label.setMinimumHorizontalScale(1.f);
        label.setColour(juce::Label::textColourId, juce::Colour(0xffe4e9ee));
        label.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(label);
        slider.bindModulation(owner, id);
        slider.setName(title); slider.setTitle(title);
        slider.setLookAndFeel(&style);
        slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::TextBoxAbove, false, 68, 19);
        addAndMakeVisible(slider);
        link = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(owner.parameters, id, slider);
        if (auto* parameter = owner.parameters.getParameter(id))
            slider.setDoubleClickReturnValue(true, parameter->convertFrom0to1(parameter->getDefaultValue()));
        const juce::String suffix(unit);
        slider.textFromValueFunction = [suffix](double value) {
            return juce::String(value, suffix == "%" ? 0 : 2)
                + (suffix.isEmpty() ? juce::String() : " " + suffix);
        };
        slider.valueFromTextFunction = [](const juce::String& value) { return value.getDoubleValue(); };
        if (id == "double") {
            slider.textFromValueFunction = [](double value) {
                return juce::String(100 - value, 0) + " / " + juce::String(value, 0);
            };
            slider.valueFromTextFunction = [](const juce::String& text) {
                return text.contains("/") ? text.fromFirstOccurrenceOf("/", false, false).getDoubleValue() : text.getDoubleValue();
            };
        }
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
            (slider.getComponentID().startsWith("fx") || slider.getComponentID() == "choraleMix"));
        slider.setTextBoxStyle(juce::Slider::TextBoxAbove, false, 68, value ? 18 : 19);
        resized();
    }
    ~ParameterKnob() override { slider.setLookAndFeel(nullptr); }
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override {
        forwardPassiveWheel(*this, event, wheel);
    }
    void resized() override {
        auto area = getLocalBounds();
                const bool merged = bool(slider.getProperties()["vocalstackTexturedBar"]);
                label.setVisible(!merged);
                if (!merged) label.setBounds(area.removeFromBottom(28));
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
