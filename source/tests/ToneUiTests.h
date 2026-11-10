#pragma once

#include "ToneEffectPanel.h"
#include "RoutingPanel.h"
#include <array>
#include <cmath>
#include <complex>
#include <limits>
#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>
#include <vector>

// Include at file scope. MAIN calls run() on the GUI/message thread after its
// ScopedJuceInitialiser_GUI has been constructed. Checks use public APIs only;
// the live-view check prepares DSP and polls pending timers with a bounded wait.
namespace vocalstack::toneUiTests {

inline void require(bool condition, const juce::String& message) {
    if (!condition) throw std::runtime_error(("Tone UI: " + message).toStdString());
}
inline void near(float actual, float expected, const juce::String& message, float tolerance = 0.001f) {
    require(std::isfinite(actual) && std::abs(actual - expected) <= tolerance,
            message + " (expected " + juce::String(expected, 5) + ", got " + juce::String(actual, 5) + ")");
}
inline juce::RangedAudioParameter& parameter(Processor& owner, const juce::String& id) {
    auto* p = owner.parameters.getParameter(id);
    require(p != nullptr, "missing parameter " + id);
    return *p;
}
inline float read(Processor& owner, const juce::String& id) {
    const auto* p = owner.parameters.getRawParameterValue(id);
    require(p != nullptr, "missing raw parameter " + id);
    return p->load(std::memory_order_relaxed);
}
inline void write(Processor& owner, const juce::String& id, float value) {
    auto& p = parameter(owner, id);
    p.beginChangeGesture();
    p.setValueNotifyingHost(p.convertTo0to1(value));
    p.endChangeGesture();
}

template <class T, class Predicate>
inline T* find(juce::Component& root, Predicate matches) {
    if (auto* result = dynamic_cast<T*>(&root); result && matches(*result)) return result;
    for (auto* child : root.getChildren())
        if (auto* result = find<T>(*child, matches)) return result;
    return nullptr;
}
inline juce::Button& button(juce::Component& root, const juce::String& text) {
    auto* result = find<juce::Button>(root, [&text](auto& b) { return b.getButtonText() == text; });
    require(result != nullptr, "missing button " + text);
    return *result;
}
inline juce::Slider& slider(juce::Component& root, const juce::String& id) {
    auto* result = find<juce::Slider>(root, [&id](auto& s) { return s.getComponentID() == id; });
    require(result != nullptr, "missing slider " + id);
    return *result;
}
inline juce::ComboBox& combo(juce::Component& root, const juce::String& id) {
    auto* result = find<juce::ComboBox>(root, [&id](auto& c) { return c.getComponentID() == id; });
    require(result != nullptr, "missing choice " + id);
    return *result;
}
inline void action(juce::Component& root, const juce::String& text) {
    auto& b = button(root, text);
    require(b.isEnabled() && bool(b.onClick), "unavailable action " + text);
    // Match the existing test runner: invoke synchronously, not triggerClick's timer.
    b.onClick();
}
inline juce::String componentPath(const juce::Component& component) {
    juce::String kind = "Component";
    if (dynamic_cast<const juce::Slider*>(&component)) kind = "Slider";
    else if (dynamic_cast<const juce::ComboBox*>(&component)) kind = "ComboBox";
    else if (dynamic_cast<const juce::Label*>(&component)) kind = "Label";
    else if (dynamic_cast<const juce::Button*>(&component)) kind = "Button";
    else if (dynamic_cast<const toneui::Control*>(&component)) kind = "ToneControl";
    else if (dynamic_cast<const ToneEffectPanel*>(&component)) kind = "ToneEffectPanel";
    const auto name = component.getName();
    const auto id = component.getComponentID();
    auto part = kind + "[name=" + name + ", id=" + id + "]";
    if (const auto* parent = component.getParentComponent()) {
        int index = 0;
        for (; index < parent->getNumChildComponents(); ++index)
            if (parent->getChildComponent(index) == &component) break;
        part = componentPath(*parent) + "/child" + juce::String(index) + ":" + part;
    }
    return part;
}
inline void passive(juce::Component& root) {
    if (dynamic_cast<juce::TextEditor*>(&root)) return; // Deliberate numeric entry owns keys.
    require(!root.getWantsKeyboardFocus() && !root.getMouseClickGrabsKeyboardFocus(),
            "idle control requests host keyboard focus: " + componentPath(root)
                + " wants=" + juce::String(int(root.getWantsKeyboardFocus()))
                + " clickGrabs=" + juce::String(int(root.getMouseClickGrabsKeyboardFocus())));
    for (auto* child : root.getChildren()) passive(*child);
}
inline void visibleLayout(juce::Component& root) {
    for (auto* child : root.getChildren()) {
        if (!child->isVisible()) continue;
        require(!child->getBounds().isEmpty(), "visible child has empty bounds: " + child->getName());
        require(root.getLocalBounds().intersects(child->getBounds()),
                "visible child is wholly clipped: " + child->getName());
        visibleLayout(*child);
    }
}
inline void transport(juce::Component& root) {
    passive(root);
    auto* peer = root.getPeer();
    require(peer != nullptr, "missing GUI peer for transport-key check");
    require(!peer->handleKeyPress(juce::KeyPress(juce::KeyPress::spaceKey)), "consumed host Space");
    require(!peer->handleKeyPress(juce::KeyPress(juce::KeyPress::returnKey)), "consumed host Return");
}
inline juce::MouseEvent mouse(juce::Component& target, juce::Point<float> point,
                              juce::Point<float> down, bool dragged = false,
                              int modifiers = juce::ModifierKeys::leftButtonModifier, int clicks = 1) {
    const auto now = juce::Time::getCurrentTime();
    return {juce::Desktop::getInstance().getMainMouseSource(), point, juce::ModifierKeys(modifiers),
            1, 0, 0, 0, 0, &target, &target, now, down, now, clicks, dragged};
}
inline void enterValue(juce::Slider& target, double value) {
    require(target.isTextBoxEditable(), "numeric entry disabled: " + componentPath(target));
    target.showTextBox();
    auto* entry = find<juce::TextEditor>(target, [](auto&) { return true; });
    require(entry != nullptr && entry->getWantsKeyboardFocus(), "deliberate numeric entry cannot own keys");
    entry->setText(juce::String(value, 6), false);
    target.hideTextBox(false);
    require(find<juce::TextEditor>(target, [](auto&) { return true; }) == nullptr,
            "numeric commit left its transient editor open");
    passive(target);
}
inline void click(juce::Component& target, juce::Point<float> point) {
    target.mouseDown(mouse(target, point, point));
    target.mouseUp(mouse(target, point, point, false, 0));
}
inline void drag(juce::Component& target, juce::Point<float> from, juce::Point<float> to,
                 int modifiers = juce::ModifierKeys::leftButtonModifier) {
    target.mouseDown(mouse(target, from, from, false, modifiers));
    target.mouseDrag(mouse(target, to, from, true, modifiers));
    target.mouseUp(mouse(target, to, from, true, 0));
}
inline void doubleClick(juce::Component& target, juce::Point<float> point) {
    // Include the preceding down/up sequence; the second down carries clickCount=2.
    click(target, point);
    target.mouseDown(mouse(target, point, point, false, juce::ModifierKeys::leftButtonModifier, 2));
    target.mouseDoubleClick(mouse(target, point, point, false, juce::ModifierKeys::leftButtonModifier, 2));
    target.mouseUp(mouse(target, point, point, false, 0, 2));
}
inline juce::Image render(juce::Component& root) {
    auto image = root.createComponentSnapshot(root.getLocalBounds(), true, 1.0f);
    require(image.isValid() && image.getWidth() == root.getWidth() && image.getHeight() == root.getHeight(),
            "invalid component snapshot");
    return image;
}
inline void screenshot(juce::Component& root, const juce::String& name) {
    const auto image = render(root);
    const auto file = juce::File::getCurrentWorkingDirectory().getChildFile(name);
    juce::FileOutputStream stream(file);
    require(stream.openedOk() && stream.setPosition(0), "cannot open screenshot " + file.getFullPathName());
    require(stream.truncate().wasOk(), "cannot truncate screenshot " + name);
    juce::PNGImageFormat png;
    require(png.writeImageToStream(image, stream), "PNG encoding failed " + name);
    stream.flush();
    require(stream.getStatus().wasOk() && file.getSize() > 0, "PNG write failed " + name);
    std::cout << "Snapshot: " << file.getFullPathName().toStdString() << '\n';
}

class GestureLog final : private juce::AudioProcessorParameter::Listener {
public:
    struct Count { int begins = 0, ends = 0, depth = 0, values = 0, ungestured = 0; };
    explicit GestureLog(Processor& owner) : counts(size_t(owner.getParameters().size())) {
        for (auto* p : owner.getParameters()) { parameters.push_back(p); p->addListener(this); }
    }
    ~GestureLog() override { for (auto* p : parameters) p->removeListener(this); }
    Count count(const juce::RangedAudioParameter& p) const { return counts.at(size_t(p.getParameterIndex())); }
    int begins() const { int total = 0; for (const auto& c : counts) total += c.begins; return total; }
    void balanced(const juce::String& context) const {
        require(!invalid, context + ": nested/unmatched host gesture");
        for (size_t i = 0; i < counts.size(); ++i) {
            const auto& c = counts[i];
            const auto* identified = dynamic_cast<juce::RangedAudioParameter*>(parameters[i]);
            require(c.depth == 0 && c.begins == c.ends && c.ungestured == 0,
                    context + ": unbalanced gesture or ungestured parameter write: "
                        + (identified ? identified->paramID : parameters[i]->getName(96))
                        + " begins=" + juce::String(c.begins) + " ends=" + juce::String(c.ends)
                        + " depth=" + juce::String(c.depth) + " values=" + juce::String(c.values)
                        + " ungestured=" + juce::String(c.ungestured));
        }
    }
private:
    void parameterValueChanged(int index, float) override {
        auto& c = counts.at(size_t(index));
        ++c.values;
        if (c.depth == 0) ++c.ungestured;
    }
    void parameterGestureChanged(int index, bool starting) override {
        auto& c = counts.at(size_t(index));
        if (starting) { ++c.begins; if (++c.depth != 1) invalid = true; }
        else { ++c.ends; if (--c.depth != 0) invalid = true; }
    }
    bool invalid = false;
    std::vector<Count> counts;
    std::vector<juce::AudioProcessorParameter*> parameters;
};

// The public graph's documented axes: log 20..20k, +/-24 dB, with a
// 34-pixel right gutter for the independent spectrum dBFS scale.
inline juce::Rectangle<float> plot(const toneui::EqGraph& graph) {
    return graph.getLocalBounds().toFloat().withTrimmedLeft(32).withTrimmedRight(34)
        .withTrimmedTop(24).withTrimmedBottom(23);
}
inline juce::Point<float> eqPoint(const toneui::EqGraph& graph, float frequency, float db) {
    const auto a = plot(graph);
    return {a.getX() + a.getWidth() * std::log(frequency / 20.0f) / std::log(1000.0f),
            a.getCentreY() - db * a.getHeight() / 48.0f};
}
inline int enabledBands(Processor& owner) {
    int count = 0;
    for (size_t b = 0; b < 8; ++b) count += read(owner, eqBandParameterId(1, b, "On")) >= 0.5f ? 1 : 0;
    return count;
}
inline std::array<float, toneParametersPerSlot> slotValues(Processor& owner) {
    std::array<float, toneParametersPerSlot> result{};
    for (size_t f = 0; f < result.size(); ++f) result[f] = read(owner, parameterIds[toneParameterIndex(1, f)]);
    return result;
}
inline void verifyCurve(Processor& owner, toneui::EqGraph& graph, bool neon) {
    EqualizerSettings settings;
    settings.gainDb = read(owner, toneParameterId(1, "EqGain"));
    settings.shiftSemitones = read(owner, toneParameterId(1, "EqShift"));
    settings.adaptiveQ = read(owner, toneParameterId(1, "EqAdaptiveQ")) >= 0.5f;
    for (size_t b = 0; b < 8; ++b) {
        auto& band = settings.bands[b];
        band.enabled = read(owner, eqBandParameterId(1, b, "On")) >= 0.5f;
        band.frequency = read(owner, eqBandParameterId(1, b, "Frequency"));
        band.gainDb = read(owner, eqBandParameterId(1, b, "Gain"));
        band.q = read(owner, eqBandParameterId(1, b, "Q"));
        band.mode = int(std::lround(read(owner, eqBandParameterId(1, b, "Mode"))));
    }
    graph.refresh(true);
    const auto image = render(graph);
    const juce::Colour accent(neon ? 0xff64f5e3 : 0xfff5a24b);
    for (float hz : {350.0f, 1400.0f, 6500.0f}) {
        const auto db = float(20 * std::log10(Equalizer::frequencyResponse(hz, 48000, settings)));
        const auto p = eqPoint(graph, hz, juce::jlimit(-24.0f, 24.0f, db)).roundToInt();
        bool found = false;
        for (int y = p.y - 3; y <= p.y + 3; ++y) for (int x = p.x - 3; x <= p.x + 3; ++x) {
            if (!image.getBounds().contains(x, y)) continue;
            const auto pixel = image.getPixelAt(x, y);
            found |= std::abs(int(pixel.getRed()) - int(accent.getRed())) < 45
                && std::abs(int(pixel.getGreen()) - int(accent.getGreen())) < 45
                && std::abs(int(pixel.getBlue()) - int(accent.getBlue())) < 45;
        }
        require(found, "requested coefficient response not drawn near " + juce::String(hz) + " Hz");
    }
}

inline void eqPanel(bool neon) {
    Processor owner;
    write(owner, fxParameterId(1, "On"), 1);
    ToneEffectPanel panel(owner, 1, NodeType::equalizer);
    panel.setSize(400, 550);
    panel.setNeon(neon);
    panel.setVisible(true);
    panel.addToDesktop(0);
    auto* graph = find<toneui::EqGraph>(panel, [](auto&) { return true; });
    require(graph != nullptr && graph->getWidth() >= 350 && plot(*graph).getHeight() >= 210,
            "EQ plot too small at 400 x 550");
    visibleLayout(panel);
    transport(panel);
    auto& mode = combo(panel, eqBandParameterId(1, 0, "Mode"));
    require(mode.getNumItems() == 14, "EQ must expose all 14 supported modes");
    mode.sendLookAndFeelChange();
    transport(panel);
    require(enabledBands(owner) == 0, "fresh EQ should have all bands off");
    std::set<juce::uint32> colours;
    for (size_t b = 0; b < 8; ++b) colours.insert(toneui::bandColour(b).getARGB());
    require(colours.size() == 8, "band colours are not stable/distinct");
    GestureLog gestures(owner);
    const auto fId = eqBandParameterId(1, 0, "Frequency");
    const auto gId = eqBandParameterId(1, 0, "Gain");
    const auto qId = eqBandParameterId(1, 0, "Q");
    const auto created = eqPoint(*graph, 1000, 6);
    graph->mouseDown(mouse(*graph, created, created));
    require(enabledBands(owner) == 0, "single click must not create before mouseUp");
    graph->mouseUp(mouse(*graph, created, created, false, 0));
    require(enabledBands(owner) == 1, "single down/up did not create exactly one band");
    const auto beforeDouble = slotValues(owner);
    doubleClick(*graph, created);
    require(enabledBands(owner) == 1 && slotValues(owner) == beforeDouble,
            "double-click on existing band duplicated or edited it");
    near(read(owner, eqBandParameterId(1, 0, "Mode")), 0, "new band is not a bell");
    near(read(owner, fId), 1000, "bell creation frequency", 0.1f);
    near(read(owner, gId), 6, "bell creation gain", 0.02f);
    const auto frequencyBefore = gestures.count(parameter(owner, fId));
    const auto gainBefore = gestures.count(parameter(owner, gId));
    const auto moved = eqPoint(*graph, 2400, 9);
    graph->mouseDown(mouse(*graph, created, created));
    graph->mouseDrag(mouse(*graph, moved, created, true));
    require(gestures.count(parameter(owner, fId)).depth == 1 && gestures.count(parameter(owner, gId)).depth == 1,
            "point drag must keep frequency/gain host gestures open");
    graph->mouseDrag(mouse(*graph, moved.translated(2, 0), created, true));
    graph->mouseUp(mouse(*graph, moved, created, true, 0));
    // Last drag position, not mouseUp position, supplies the requested values.
    const auto expectedFrequency = 20.0f * std::pow(1000.0f,
        (moved.x + 2 - plot(*graph).getX()) / plot(*graph).getWidth());
    near(read(owner, fId), expectedFrequency, "drag frequency", 0.2f);
    near(read(owner, gId), 9, "drag gain", 0.02f);
    require(enabledBands(owner) == 1, "dragging an existing band created an extra band");
    const auto frequencyAfter = gestures.count(parameter(owner, fId));
    const auto gainAfter = gestures.count(parameter(owner, gId));
    require(frequencyAfter.begins == frequencyBefore.begins + 1 && frequencyAfter.ends == frequencyBefore.ends + 1
        && gainAfter.begins == gainBefore.begins + 1 && gainAfter.ends == gainBefore.ends + 1,
        "drag must emit exactly one balanced gesture per affected parameter");
    const auto qBefore = read(owner, qId);
    const auto qGestures = gestures.count(parameter(owner, qId));
    juce::MouseWheelDetails wheel{};
    wheel.deltaY = 0.2f;
    graph->mouseWheelMove(mouse(*graph, moved.translated(2, 0), moved, false, 0), wheel);
    near(read(owner, qId), qBefore * std::exp(0.4f), "wheel Q", 0.002f);
    require(gestures.count(parameter(owner, qId)).begins == qGestures.begins + 1,
            "Q wheel missing complete host gesture");
    gestures.balanced("bell and wheel");

    // Shift moves handles, but dragging still writes the unshifted band frequency.
    write(owner, toneParameterId(1, "EqShift"), 12);
    graph->refresh();
    drag(*graph, eqPoint(*graph, read(owner, fId) * 2, 9), eqPoint(*graph, 4000, 7));
    near(read(owner, fId), 2000, "drag must compensate global semitone shift", 0.2f);
    near(read(owner, gId), 7, "shifted drag gain", 0.02f);
    write(owner, toneParameterId(1, "EqShift"), 0);
    write(owner, toneParameterId(1, "EqGain"), 3);
    verifyCurve(owner, *graph, neon);
    screenshot(panel, neon ? "tone-eq-neon.png" : "tone-eq-device.png");

    const auto editsBeforeSelection = gestures.begins();
    const auto valuesBeforeSelection = slotValues(owner);
    action(panel, "8");
    require(gestures.begins() == editsBeforeSelection && slotValues(owner) == valuesBeforeSelection,
            "selection must not write host parameters");
    require(combo(panel, eqBandParameterId(1, 7, "Mode")).getNumItems() == 14,
            "mode attachment did not retarget stable band 8");
    auto& lastFrequency = slider(panel, eqBandParameterId(1, 7, "Frequency"));
    write(owner, eqBandParameterId(1, 7, "Frequency"), 7654);
    near(float(lastFrequency.getValue()), 7654, "frequency attachment ignored host automation", 0.1f);
    enterValue(lastFrequency, 4321);
    near(read(owner, eqBandParameterId(1, 7, "Frequency")), 4321, "frequency slider failed to write band 8", 0.1f);
    near(read(owner, fId), 2000, "band 8 edit changed band 1", 0.2f);
    combo(panel, eqBandParameterId(1, 7, "Mode")).setSelectedId(14, juce::sendNotificationSync);
    near(read(owner, eqBandParameterId(1, 7, "Mode")), 13, "14th mode mapping");
    action(panel, "1");
    const auto retainedFrequency = read(owner, fId);
    action(panel, "Remove");
    near(read(owner, eqBandParameterId(1, 0, "On")), 0, "remove should disable band 1");
    near(read(owner, fId), retainedFrequency, "remove erased stable band settings");
    gestures.balanced("selection, attachments and remove");
    visibleLayout(panel);
    transport(panel);
}

inline void edgesAndCapacity(bool neon) {
    // Fresh graphs isolate each edge from earlier shelves, cuts and handle hits.
    for (const int mode : {1, 2, 6, 10, 13}) {
        Processor owner;
        write(owner, fxParameterId(1, "On"), 1);
        toneui::EqGraph graph(owner, 1);
        graph.setSize(384, 265);
        graph.setNeon(neon);
        GestureLog gestures(owner);
        const auto a = plot(graph);
        const auto from = mode == 13 ? juce::Point<float>(a.getCentreX(), a.getBottom() - 1)
            : juce::Point<float>(mode == 1 || mode == 6 ? a.getX() + 1 : a.getRight() - 1,
                                a.getCentreY() - (mode == 6 || mode == 10 ? 55.0f : 0.0f));
        const auto to = eqPoint(graph, mode == 1 || mode == 6 ? 180.0f : 4500.0f, 5);
        graph.mouseMove(mouse(graph, from, from, false, 0));
        require(graph.getTooltip().containsIgnoreCase(mode == 13 ? "notch" : mode <= 2 ? "shelf" : "cut"),
                "edge gesture lacks operation feedback");
        drag(graph, from, to);
        require(enabledBands(owner) == 1, "edge drag did not create exactly one band");
        near(read(owner, eqBandParameterId(1, 0, "Mode")), float(mode), "edge gesture selected wrong mode");
        gestures.balanced("edge creation");
    }
    Processor owner;
    toneui::EqGraph graph(owner, 1);
    graph.setSize(384, 265);
    graph.setNeon(neon);
    GestureLog gestures(owner);
    const std::array<float, 8> frequencies{45, 100, 230, 500, 1100, 2500, 5500, 12500};
    std::array<juce::String, 8> identities;
    for (size_t b = 0; b < 8; ++b) {
        identities[b] = eqBandParameterId(1, b, "Frequency");
        doubleClick(graph, eqPoint(graph, frequencies[b], float(3 + b)));
        require(enabledBands(owner) == int(b + 1), "band creation skipped/reused an occupied slot");
        near(read(owner, identities[b]), frequencies[b], "stable creation ID", 0.5f);
    }
    const auto full = slotValues(owner);
    const int beforeNinth = gestures.begins();
    doubleClick(graph, eqPoint(graph, 1700, -14));
    require(enabledBands(owner) == 8 && slotValues(owner) == full && gestures.begins() == beforeNinth,
            "ninth band must be a no-op, not overwrite an existing band");
    write(owner, eqBandParameterId(1, 3, "On"), 0);
    graph.refresh();
    doubleClick(graph, eqPoint(graph, 1700, -14));
    require(enabledBands(owner) == 8, "disabled stable slot could not be reused");
    near(read(owner, identities[3]), 1700, "creation did not reuse first disabled band", 0.2f);
    for (size_t b = 0; b < 8; ++b) {
        require(identities[b] == eqBandParameterId(1, b, "Frequency"), "band IDs were renumbered");
        if (b != 3) near(read(owner, identities[b]), frequencies[b], "reuse changed another band", 0.5f);
    }
    gestures.balanced("capacity and stable IDs");
}

inline void waveAndCrossover(bool neon) {
    for (const auto type : {NodeType::waveshaper, NodeType::crossover}) {
        Processor owner;
        if (type == NodeType::crossover) write(owner, toneParameterId(1, "BandCount"), 3);
        ToneEffectPanel panel(owner, 1, type);
        panel.setSize(400, 550);
        panel.setNeon(neon);
        panel.setVisible(true);
        panel.addToDesktop(0);
        visibleLayout(panel);
        transport(panel);
        GestureLog gestures(owner);
        const auto id = toneParameterId(1, type == NodeType::waveshaper ? "WaveDrive" : "CrossoverLow");
        const float requested = type == NodeType::waveshaper ? 17.0f : 420.0f;
        write(owner, id, requested);
        auto& bound = slider(panel, id);
        near(float(bound.getValue()), requested, "host-to-slider attachment", 0.1f);
        enterValue(bound, requested + 2);
        near(read(owner, id), requested + 2, "slider-to-host attachment", 0.1f);
        const auto choice = toneParameterId(1, type == NodeType::waveshaper ? "WaveShape" : "BandCount");
        auto& menu = combo(panel, choice);
        require(menu.getNumItems() == (type == NodeType::waveshaper ? 3 : 2), "wrong shape/band choice count");
        menu.sendLookAndFeelChange();
        transport(panel);
        menu.setSelectedId(type == NodeType::waveshaper ? 3 : 2, juce::sendNotificationSync);
        near(read(owner, choice), type == NodeType::waveshaper ? 2.0f : 3.0f, "choice raw parameter mapping");
        if (type == NodeType::waveshaper) {
            enterValue(slider(panel, fxParameterId(1, "Mix")), 63);
            near(read(owner, fxParameterId(1, "Mix")), 63, "waveshaper dry/wet attachment");
            screenshot(panel, neon ? "tone-wave-neon.png" : "tone-wave-device.png");
        } else {
            require(slider(panel, toneParameterId(1, "CrossoverHigh")).isEnabled(), "3-band high crossover disabled");
            screenshot(panel, neon ? "tone-crossover-neon.png" : "tone-crossover-device.png");
        }
        gestures.balanced("wave/crossover controls");
        transport(panel);
    }
    // Construct in two-band mode so assertions do not depend on a 30 Hz timer.
    Processor two;
    write(two, toneParameterId(1, "BandCount"), 2);
    ToneEffectPanel panel(two, 1, NodeType::crossover);
    panel.setSize(400, 550);
    panel.setNeon(neon);
    require(!slider(panel, toneParameterId(1, "CrossoverHigh")).isEnabled(), "2-band high crossover must be inactive");
    visibleLayout(panel);
    passive(panel);
}

inline void fixture(Processor& owner, int bands = 3) {
    write(owner, fxParameterId(1, "On"), 1);
    write(owner, toneParameterId(1, "BandCount"), float(bands));
    Graph graph{{{1, NodeType::lead}, {100, NodeType::crossover}, {13, NodeType::output}}, {{1, 100, 0, 0}}};
    graph.nodes[1].bandCount = bands; // New-node registration initializes its host parameters.
    juce::String error;
    const bool accepted = owner.setRoutingGraph(graph, error);
    require(accepted, "routing fixture rejected: " + error);
    near(read(owner, toneParameterId(1, "BandCount")), float(bands), "routing fixture band count");
    juce::ValueTree layout("RoutingLayout");
    layout.setProperty("zoom", 1, nullptr);
    layout.setProperty("panX", 0, nullptr);
    layout.setProperty("panY", 0, nullptr);
    const std::array<int, 3> ids{1, 100, 13}, xs{20, 260, 540};
    for (size_t i = 0; i < ids.size(); ++i) {
        juce::ValueTree node("NODE");
        node.setProperty("id", ids[i], nullptr);
        node.setProperty("x", xs[i], nullptr);
        node.setProperty("y", 20, nullptr);
        layout.addChild(node, -1, nullptr);
    }
    owner.setRoutingLayout(layout);
}
inline int links(Processor& owner, std::uint32_t source, std::uint32_t destination, int sourcePin, int inputPin = 0) {
    const auto graph = owner.getRoutingGraph();
    int count = 0;
    for (const auto& l : graph.links)
        count += l.source == source && l.destination == destination && l.sourcePin == sourcePin && l.inputPin == inputPin ? 1 : 0;
    return count;
}
inline juce::Component& canvas(RoutingPanel& panel) {
    auto* result = find<juce::Component>(panel, [](auto& c) { return c.getName() == "Routing node canvas"; });
    require(result != nullptr && result->getBounds() == juce::Rectangle<int>(12, 70, 840, 688), "routing canvas bounds changed");
    return *result;
}
inline void routingPorts(bool neon) {
    Processor owner;
    fixture(owner);
    RoutingPanel panel(owner);
    panel.setSize(1280, 800);
    panel.setNeon(neon);
    panel.setVisible(true);
    panel.addToDesktop(0);
    auto& surface = canvas(panel);
    const juce::Point<float> outputInput{540, 62}, empty{680, 260}, leadOutput{210, 62};
    for (int port = 0; port < 3; ++port) {
        drag(surface, {450, float(62 + 22 * port)}, outputInput);
        require(owner.getRoutingError().isEmpty(), "source port drag rejected: " + owner.getRoutingError());
        require(links(owner, 100, 13, port) == 1, "output cable lost sourcePin " + juce::String(port));
    }
    require(owner.getRoutingGraph().links.size() == 4, "fan-in replaced another crossover cable");
    transport(panel);
    auto* inspector = find<ToneEffectPanel>(panel, [](auto&) { return true; });
    require(inspector != nullptr && inspector->getWidth() >= 320 && inspector->getWidth() <= 400,
                "multiband inspector was not integrated in its adaptive viewport");
    visibleLayout(*inspector);
    require(find<juce::Button>(panel, [](auto& b) { return b.getButtonText() == "Disconnect"; }) == nullptr,
            "obsolete Disconnect toolbar still present");
    screenshot(panel, neon ? "tone-routing-neon.png" : "tone-routing-device.png");

    // The last occupied-input link is High; moving its destination must retain #2.
    drag(surface, outputInput, empty);
    require(links(owner, 100, 13, 2) == 0 && links(owner, 100, 13, 0) == 1 && links(owner, 100, 13, 1) == 1,
            "input-end disconnect removed the wrong band");
    write(owner, toneParameterId(1, "CrossoverLow"), 420);
    action(panel, "Undo");
    require(links(owner, 100, 13, 2) == 1, "Undo did not restore High sourcePin 2");
    near(read(owner, toneParameterId(1, "CrossoverLow")), 420, "cable undo rewound tone automation");
    action(panel, "Redo");
    require(links(owner, 100, 13, 2) == 0, "Redo did not disconnect High");
    action(panel, "Undo");

    // Selection does not change the ordinary output gesture: move its destination,
    // or disconnect on empty space, never move its source without Ctrl/Command.
    click(surface, {495, 84});
    drag(surface, {450, 106}, empty);
    require(links(owner, 100, 13, 2) == 0 && links(owner, 1, 100, 0) == 1
            && links(owner, 100, 13, 0) == 1 && links(owner, 100, 13, 1) == 1,
            "selected occupied output drag did not disconnect only High's destination");
    action(panel, "Undo");
    require(links(owner, 100, 13, 2) == 1, "output-destination Undo lost High pin");
    for (const int modifier : {juce::ModifierKeys::ctrlModifier, juce::ModifierKeys::commandModifier}) {
        click(surface, {495, 84});
        drag(surface, {450, 106}, leadOutput, juce::ModifierKeys::leftButtonModifier | modifier);
        require(links(owner, 1, 13, 0) == 1 && links(owner, 100, 13, 2) == 0,
                "Ctrl/Command source-end reconnection failed/default sourcePin not reset");
        require(links(owner, 1, 100, 0) == 0 && owner.getRoutingGraph().links.size() == 3
                && links(owner, 100, 13, 0) == 1 && links(owner, 100, 13, 1) == 1,
                "source replacement did not clear the new output's previous links or changed other bands");
        action(panel, "Undo");
        require(links(owner, 100, 13, 2) == 1 && links(owner, 1, 100, 0) == 1
                && links(owner, 1, 13, 0) == 0 && owner.getRoutingGraph().links.size() == 4,
                "source-end Undo did not restore both occupied outputs");
    }

    // A self-cycle rejection must not leave a draft or remove the original High.
    drag(surface, outputInput, {260, 62});
    require(owner.getRoutingError().isNotEmpty() && owner.getRoutingGraph().links.size() == 4
            && links(owner, 100, 13, 2) == 1, "rejected input reconnect changed accepted High cable");
    transport(panel);

    Processor two;
    fixture(two, 2);
    RoutingPanel twoPanel(two);
    twoPanel.setSize(1280, 800);
    twoPanel.setNeon(neon);
    auto& twoCanvas = canvas(twoPanel);
    drag(twoCanvas, {450, 84}, outputInput);
    require(links(two, 100, 13, 1) == 1, "2-band High must use sourcePin 1");
    const auto count = two.getRoutingGraph().links.size();
    drag(twoCanvas, {450, 106}, outputInput);
    require(two.getRoutingGraph().links.size() == count && links(two, 100, 13, 2) == 0,
            "inactive third output allowed new cable creation");

    // Switching count does not delete the stored third-port cable; instantiate a
    // fresh view to inspect the new mode without sleeping for its UI timer.
    write(owner, toneParameterId(1, "BandCount"), 2);
    RoutingPanel stored(owner);
    stored.setSize(1280, 800);
    stored.setNeon(neon);
    require(links(owner, 100, 13, 2) == 1, "2-band mode erased stored High wiring");
    (void) render(stored); // Covers inactive/dashed cable paint with no live DSP.
    passive(stored);
}

inline void spectrumCalibration() {
    constexpr size_t bin = 64;
    std::array<std::array<float, toneui::FxSpectrum::size>, 2> samples{};
    const auto fill = [&](float left, float right) {
        for (size_t i = 0; i < toneui::FxSpectrum::size; ++i) {
            const auto x = float(std::sin(6.283185307179586 * double(bin * i) / toneui::FxSpectrum::size));
            samples[0][i] = left * x; samples[1][i] = right * x;
        }
    };
    toneui::FxSpectrum spectrum;
    for (const auto amplitudes : {std::array<float, 2>{1, 1}, {1, -1}, {.25f, -.25f}, {1, 0}}) {
        fill(amplitudes[0], amplitudes[1]); spectrum.clear(); spectrum.update(samples);
        const float expected = float(10 * std::log10((amplitudes[0] * amplitudes[0]
            + amplitudes[1] * amplitudes[1]) * .5));
        near(spectrum.levelsDbFS()[bin], expected, "Hann/stereo-power FFT calibration", .002f);
        for (float db : spectrum.levelsDbFS()) require(std::isfinite(db) && db >= -96, "invalid FFT level");
    }
    // DC/Nyquist must not receive the interior bins' factor of two.
    for (bool nyquist : {false, true}) {
        for (size_t i = 0; i < toneui::FxSpectrum::size; ++i)
            samples[0][i] = samples[1][i] = nyquist && i % 2 ? -1.0f : 1.0f;
        spectrum.clear(); spectrum.update(samples);
        near(spectrum.levelsDbFS()[nyquist ? toneui::FxSpectrum::size / 2 : 0], 0, "FFT endpoint normalization", .002f);
    }
    samples = {}; spectrum.clear(); spectrum.update(samples);
    for (float db : spectrum.levelsDbFS()) near(db, -96, "silence FFT floor");
    samples[0][0] = std::numeric_limits<float>::quiet_NaN();
    samples[1][1] = std::numeric_limits<float>::infinity();
    spectrum.update(samples);
    for (float db : spectrum.levelsDbFS()) near(db, -96, "nonfinite FFT sanitization");
}

inline void staticTransfer() {
    for (int shape : {0, 1, 2}) for (float drive : {-24.0f, 0.0f, 48.0f})
        for (float output : {-60.0f, 0.0f, 24.0f}) for (float mix : {0.0f, .4f, 1.0f})
            for (float x : {-2.0f, -.25f, 0.0f, .25f, 2.0f}) {
                const double curve = toneui::WaveshaperVisual::settledTransfer(x, drive, output, mix, shape, true);
                Waveshaper dsp; dsp.prepare(48000);
                WaveshaperSettings settings; settings.driveDb = drive; settings.outputDb = output;
                settings.mix = mix; settings.shape = shape;
                float left = x, right = -x;
                dsp.process(left, right, settings);
                // Second identical sample exercises settled ADAA's transfer limit.
                left = x; right = -x; dsp.process(left, right, settings);
                near(float(curve), left, "static curve differs from settled DSP", .00002f);
                near(float(toneui::WaveshaperVisual::settledTransfer(-x, drive, output, mix, shape, true)),
                     right, "static curve right/polarity", .00002f);
                near(float(toneui::WaveshaperVisual::settledTransfer(x, drive, output, mix, shape, false)),
                     x, "static bypass must be exact", 0);
            }
    near(float(toneui::WaveshaperVisual::settledTransfer(2, 48, 0, 1, 1, true)), 1, "hard upper limit");
    near(float(toneui::WaveshaperVisual::settledTransfer(-2, 48, 0, 1, 1, true)), -1, "hard lower limit");
}

inline void crossoverResponse() {
    constexpr double tau = 6.283185307179586;
    for (double rate : {44100.0, 48000.0, 96000.0}) for (int count : {2, 3}) {
        constexpr std::array<double, 6> frequencies{40, 250, 900, 4000, 10000, 18000};
        std::array<std::array<std::complex<double>, frequencies.size()>, 3> measured{};
        Multiband dsp; dsp.prepare(rate);
        // Direct DFT of a long impulse includes the actual all-pass alignment,
        // without FFT-bin rounding or a steady-state/smoothing timing guess.
        for (size_t i = 0; i < 32768; ++i) {
            const auto bands = dsp.process(i == 0 ? 1.0f : 0.0f, 0, count, 250, 4000);
            for (size_t f = 0; f < frequencies.size(); ++f) {
                const auto phase = std::polar(1.0, -tau * frequencies[f] * double(i) / rate);
                for (size_t b = 0; b < 3; ++b) measured[b][f] += double(bands[b][0]) * phase;
            }
        }
        for (size_t f = 0; f < frequencies.size(); ++f) {
            const auto response = toneui::CrossoverVisual::responseMagnitudes(frequencies[f], rate, 250, 4000, count);
            for (size_t b = 0; b < 3; ++b)
                near(float(response[b]), float(std::abs(measured[b][f])), "crossover analytic vs DSP impulse", .00002f);
            near(float(std::abs(measured[0][f] + measured[1][f] + measured[2][f])), 1,
                 "crossover recombination magnitude", .00002f);
        }
    }
}

inline void liveMonitorViews(bool neon) {
    Processor owner;
    fixture(owner);
    owner.prepareToPlay(48000, 128);
    juce::Component root;
    root.setSize(800, 300);
    root.setVisible(true); root.addToDesktop(0);
    toneui::CrossoverVisual first(owner, 1), second(owner, 1);
    first.setNeon(neon); second.setNeon(neon);
    root.addAndMakeVisible(first); root.addAndMakeVisible(second);
    first.setBounds(0, 0, 390, 290); second.setBounds(400, 0, 390, 290);
    const auto feed = [&](float amplitude = .2f) {
        juce::AudioBuffer<float> audio(2, 128); juce::MidiBuffer midi;
        for (int block = 0; block < 128; ++block) {
            for (int i = 0; i < 128; ++i) {
                const float x = amplitude * std::sin(float(6.283185307179586 * 1500 * (block * 128 + i) / 48000));
                audio.setSample(0, i, x); audio.setSample(1, i, -x);
            }
            owner.processBlock(audio, midi);
        }
    };
    const auto poll = [&](const auto& predicate) {
        const double deadline = juce::Time::getMillisecondCounterHiRes() + 2000;
        do {
            juce::Timer::callPendingTimersSynchronously();
            if (predicate()) return true;
            juce::Thread::sleep(1);
        } while (juce::Time::getMillisecondCounterHiRes() < deadline);
        return false;
    };
    feed();
    FxMonitorFrame captured;
    require(owner.readFxMonitor(1, captured), "visible views did not retain actual capture");
    require(poll([&] { return first.hasLiveFrame() && second.hasLiveFrame(); }), "visible views never consumed frame");
    require(first.monitorFrame().sequence == captured.sequence && second.monitorFrame().sequence == captured.sequence,
            "views did not display same actual published frame");
    toneui::FxSpectrum expected, expectedOutput;
    expected.update(captured.input); expectedOutput.update(captured.output);
    require(first.monitorFrame().bandCount == 3, "live crossover port count missing");
    for (size_t band = 0; band < 3; ++band)
        near(first.monitorFrame().bandRms[band], captured.bandRms[band], "displayed band RMS not from capture", 0);
    float peak = -96;
    for (size_t b = 0; b < toneui::FxSpectrum::bins; ++b) {
        near(first.inputSpectrum().levelsDbFS()[b], expected.levelsDbFS()[b], "live spectrum not derived from actual input", .002f);
        near(first.outputSpectrum().levelsDbFS()[b], expectedOutput.levelsDbFS()[b], "live spectrum not derived from actual output", .002f);
        peak = std::max(peak, expected.levelsDbFS()[b]);
    }
    require(peak > -30, "actual antiphase signal missing from display");
    render(first);

    // Complete a routing reset and fresh silent publication between GUI polls.
    // Identity/rate stay unchanged, so only epoch can clear the FFT peak history.
    const auto beforeReset = captured;
    auto graph = owner.getRoutingGraph();
    graph.links.push_back({100, 13, 0, 0});
    juce::String error;
    require(owner.setRoutingGraph(graph, error), "live-view routing reset rejected: " + error);
    feed(0);
    require(owner.readFxMonitor(1, captured) && captured.epoch != beforeReset.epoch
            && captured.sequence > beforeReset.sequence && captured.nodeId == beforeReset.nodeId
            && captured.sampleRate == beforeReset.sampleRate, "routing reset did not publish a fresh epoch");
    require(first.monitorFrame().epoch == beforeReset.epoch && second.monitorFrame().epoch == beforeReset.epoch,
            "reset regression must leave old frames displayed until the next GUI poll");
    require(poll([&] { return first.hasLiveFrame() && second.hasLiveFrame()
            && first.monitorFrame().epoch == captured.epoch && second.monitorFrame().epoch == captured.epoch; }),
            "views did not consume reset epoch");
    expected.clear(); expectedOutput.clear();
    expected.update(captured.input); expectedOutput.update(captured.output);
    near(expected.levelsDbFS()[64], -96, "post-reset input was not silent", .002f);
    for (const auto* view : {&first, &second}) for (size_t b = 0; b < toneui::FxSpectrum::bins; ++b) {
        near(view->inputSpectrum().levelsDbFS()[b], expected.levelsDbFS()[b], "old input FFT peak survived routing epoch", .002f);
        near(view->outputSpectrum().levelsDbFS()[b], expectedOutput.levelsDbFS()[b], "old output FFT peak survived routing epoch", .002f);
    }

    first.setVisible(false);
    require(!first.hasLiveFrame() && owner.readFxMonitor(1, captured), "hiding one view released the other view");
    second.setVisible(false);
    require(!second.hasLiveFrame() && !owner.readFxMonitor(1, captured), "last hidden view retained stale data");
    first.setVisible(true);
    require(!owner.readFxMonitor(1, captured), "reshown view reused old subscription frame");
    feed();
    require(poll([&] { return first.hasLiveFrame(); }), "reshown view failed to receive fresh data");
    root.setVisible(false);
    require(!first.hasLiveFrame() && !owner.readFxMonitor(1, captured), "hidden parent retained monitor");
    owner.releaseResources();
}

inline void run() {
    require(juce::MessageManager::getInstance()->isThisTheMessageThread(), "run() requires initialized GUI message thread");
    spectrumCalibration();
    staticTransfer();
    crossoverResponse();
    for (const bool neon : {false, true}) {
        eqPanel(neon);
        edgesAndCapacity(neon);
        waveAndCrossover(neon);
        routingPorts(neon);
        liveMonitorViews(neon);
    }
    std::cout << "Tone UI tests passed (both skins; graph gestures, calibrated FFT, static curves, live monitors, source pins and history).\n";
}
} // namespace vocalstack::toneUiTests
