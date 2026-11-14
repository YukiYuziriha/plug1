#include "VisualTheme.h"
#pragma once

#include "UtilityFilters.h"
#include "FxVisuals.h"
#include "ParameterKnob.h"
#include "ModulationUI.h"
#include "DoublerPanel.h" // detail::passiveControl
#include <array>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace vocalstack {

struct UtilityPanelTests;

// Presentation only: models use effective targets, edits always address host bases.
class UtilityEffectPanel final : public juce::Component, private juce::Timer {
    friend struct UtilityPanelTests;
    class EditSlider final : public ModulationSlider {
    public:
        std::function<void()> beginEdit, endEdit;
        Processor* owner = nullptr;
        void mouseDown(const juce::MouseEvent& e) override {
            editing = owner && owner->modulationAssignmentId() == 0 && !e.mods.isPopupMenu();
            if (editing && beginEdit) beginEdit();
            ModulationSlider::mouseDown(e);
        }
        void mouseUp(const juce::MouseEvent& e) override {
            ModulationSlider::mouseUp(e);
            if (editing && endEdit) endEdit();
            editing = false;
        }

        void mouseDoubleClick(const juce::MouseEvent& e) override {
            const bool edit = owner && owner->modulationAssignmentId() == 0 && !e.mods.isPopupMenu();
            if (edit && beginEdit) beginEdit();
            ModulationSlider::mouseDoubleClick(e);
            if (edit && endEdit) endEdit();
        }
    private:
        bool editing = false;
    };
    struct Row final : juce::Component {
        Row(UtilityEffectPanel& p, const juce::String& parameterId, const juce::String& name,
            const char* unit, bool discrete, bool mode, size_t field)
            : panel(p), id(parameterId) {
            setComponentID(id + "Control");
            label.setText(name, juce::dontSendNotification);
            label.setFont(juce::Font(pixelFontOptions(ParameterKnob::controlFontSize)));
                        label.setMinimumHorizontalScale(1.f);
            label.setInterceptsMouseClicks(false, false); addAndMakeVisible(label);
            if (mode) {
                combo.setComponentID(id); combo.setName(name);
                if (panel.kind == UtilityKind::xpFilter)
                    for (size_t i = 0; i < xpFilterConfigurations.size(); ++i) combo.addItem(xpFilterConfigurations[i], int(i + 1));
                else {
                    static_assert(synthFilterConfigurations.size() == 13);
                    for (size_t i = 0; i < synthFilterConfigurations.size(); ++i)
                        combo.addItem(synthFilterConfigurations[i], int(i + 1));
                }
                combo.setScrollWheelEnabled(false);
                addAndMakeVisible(combo);
                combo.onChange = [this] { panel.editValue(id, float(combo.getSelectedId() - 1)); };
            } else if (discrete) {
                toggle.setModulationOwner(panel.owner); toggle.setComponentID(id);
                toggle.setName(name); toggle.setTooltip(name);
                toggle.setButtonText(field == size_t(UtilityParameter::stereoSwap) ? "Swap"
                    : field == size_t(UtilityParameter::stereoInvertLeft) ? "Inv L"
                    : field == size_t(UtilityParameter::stereoInvertRight) ? "Inv R" : name);
                toggle.setClickingTogglesState(true);
                toggle.onClick = [this] { panel.editValue(id, toggle.getToggleState() ? 1.f : 0.f); };
                addAndMakeVisible(toggle); label.setVisible(false);
            } else {
                slider.owner = &panel.owner; slider.bindModulation(panel.owner, id);
                slider.setName(name); slider.setLookAndFeel(&style);
                if (field == utilityParameterCount)
                    slider.getProperties().set("vocalstackTexturedBar", true);
                slider.setSliderStyle(field == utilityParameterCount ? juce::Slider::LinearHorizontal
                                                                   : juce::Slider::RotaryHorizontalVerticalDrag);
                slider.setTextBoxStyle(juce::Slider::TextBoxAbove, false, 76, 21);
                addAndMakeVisible(slider);
                link = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(panel.owner.parameters, id, slider);
                if (auto* parameter = panel.owner.parameters.getParameter(id))
                    slider.setDoubleClickReturnValue(true, parameter->convertFrom0to1(parameter->getDefaultValue()));
                const juce::String suffix(unit);
                const bool gain = field == size_t(UtilityParameter::gainDb) && panel.kind == UtilityKind::gain;
                if (gain) slider.setSkewFactorFromMidPoint(-12.0);
                slider.textFromValueFunction = [suffix, gain](double v) {
                    if (gain && v <= -120) return juce::String::fromUTF8("-\xe2\x88\x9e dB");
                    if (suffix == "Hz" && v >= 1000) return juce::String(v / 1000, 2) + " kHz";
                    return juce::String(v, suffix == "%" || suffix == "Hz" ? 0 : 2) + (suffix == ":1" ? suffix : " " + suffix);
                };
                slider.valueFromTextFunction = [suffix, gain](const juce::String& text) {
                    if (gain && (text.containsIgnoreCase("inf") || text.contains(juce::String::fromUTF8("\xe2\x88\x9e")))) return -120.0;
                    return text.getDoubleValue() * (suffix == "Hz" && text.containsIgnoreCase("k") ? 1000.0 : 1.0);
                };
                slider.beginEdit = [this] { panel.begin(id); };
                slider.endEdit = [this] { panel.end(); };
                slider.onValueChange = [this] { panel.changed(); };
                // Numeric entry is a child Label, not a Slider drag.
                slider.addMouseListener(this, true); slider.updateText();
            }
            detail::passiveControl(*this);
        }
        ~Row() override { slider.removeMouseListener(this); slider.setLookAndFeel(nullptr); }
        void mouseDown(const juce::MouseEvent& e) override {
            if (e.eventComponent != &slider && slider.isParentOf(e.eventComponent)
                && panel.owner.modulationAssignmentId() == 0 && !e.mods.isPopupMenu()) panel.begin(id);
        }
        void mouseUp(const juce::MouseEvent& e) override {
            if (e.eventComponent != &slider && slider.isParentOf(e.eventComponent)) panel.end();
        }
        void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override {
            forwardPassiveWheel(*this, e, wheel);
        }
        void refresh() {
            if (combo.isVisible()) combo.setSelectedId(int(toneui::visualParameter(panel.owner, id)) + 1, juce::dontSendNotification);
            if (toggle.isVisible()) toggle.setToggleState(toneui::visualParameter(panel.owner, id) >= .5f, juce::dontSendNotification);
        }
        void resized() override {
            auto b = getLocalBounds().reduced(2, 1);
            label.setJustificationType(juce::Justification::centred);
            const bool textured = slider.getSliderStyle() == juce::Slider::LinearHorizontal
                && bool(slider.getProperties()["vocalstackTexturedBar"]);
            label.setVisible(!toggle.isVisible() && !textured);
            if (combo.isVisible()) {
                label.setBounds(b.removeFromTop(17));
                combo.setBounds(b.withHeight(24));
            } else {
                if (!textured) label.setBounds(b.removeFromBottom(28));
                slider.setTextBoxStyle(juce::Slider::TextBoxAbove, false, juce::jmin(76, b.getWidth()), 21);
                slider.setBounds(b);
            }
            toggle.setBounds(getLocalBounds().reduced(0, 2));
        }
        void setNeon(bool neon) {
            const auto c = themePalette(*this, neon);
            label.setColour(juce::Label::textColourId, c.text);
            slider.setColour(juce::Slider::trackColourId, c.accent);
            slider.setColour(juce::Slider::thumbColourId, c.text);
            slider.setColour(juce::Slider::textBoxTextColourId, c.text);
            slider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
            slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            combo.setColour(juce::ComboBox::backgroundColourId, c.inset);
            combo.setColour(juce::ComboBox::textColourId, c.text);
            combo.setColour(juce::ComboBox::outlineColourId, c.border);
            combo.setColour(juce::ComboBox::arrowColourId, c.accent);
            toggle.setColour(juce::TextButton::buttonColourId, c.inset);
            toggle.setColour(juce::TextButton::buttonOnColourId, c.accent);
            toggle.setColour(juce::TextButton::textColourOffId, c.text);
            toggle.setColour(juce::TextButton::textColourOnId, c.background);
        }
        UtilityEffectPanel& panel;
        juce::String id;
        ParameterKnob::Style style;
        juce::Label label;
        EditSlider slider;
        modulationui::Button toggle;
        juce::ComboBox combo;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> link;
    };
    class Visual final : public toneui::FxMonitorView, public juce::SettableTooltipClient {
        friend struct UtilityPanelTests;
        struct Target {
            juce::RangedAudioParameter* parameter = nullptr;
            size_t index = 0;
            float read(const ModulationSnapshot& snapshot, float fallback) {
                if (!parameter) return fallback;
                if (index >= snapshot.targets.size() || snapshot.targets[index].parameterId != parameter->paramID) {
                    index = 0;
                    while (index < snapshot.targets.size() && snapshot.targets[index].parameterId != parameter->paramID) ++index;
                }
                if (index < snapshot.targets.size() && std::isfinite(snapshot.targets[index].effectiveNormalized))
                    return parameter->convertFrom0to1(std::clamp(snapshot.targets[index].effectiveNormalized, 0.f, 1.f));
                return parameter->convertFrom0to1(parameter->getValue());
            }
        };
    public:
        explicit Visual(UtilityEffectPanel& p) : FxMonitorView(p.owner, p.slot, p.isFilter()), panel(p) {
            setComponentID("utilityVisual"); setName("Utility response and live pre/post monitor");
            for (size_t i = 0; i < utilityParameterCount; ++i)
                if (utilityParameters[i].kind == panel.kind)
                    targets[i].parameter = monitorOwner.parameters.getParameter(utilityParameterId(monitorSlot, i));
            onTarget.parameter = monitorOwner.parameters.getParameter(fxParameterId(monitorSlot, "On"));
            mixTarget.parameter = monitorOwner.parameters.getParameter(fxParameterId(monitorSlot, "Mix"));
            setTooltip("Filter: drag horizontally for cutoff, vertically for resonance. Curve uses effective modulation targets, not live smoothed DSP state. PRE/POST FFT is stereo power, Hann-normalized peak dBFS; it is not the response curve. Synth curves are cached, finite-impulse small-signal estimates, not exact nonlinear transfer functions. Comb delay is frozen.");
        }
        ~Visual() override {
            if (curveScheduler) curveScheduler->cancel(curveTicket);
            finishDrag();
        }
        bool responseReady() const {
            return panel.kind != UtilityKind::synthFilter
                || (!synthCurve.bins.empty() && synthCurve.request == curveRequest());
        }
        void refresh() {
            const auto next = monitorOwner.getModulationSnapshot();
            if (next.consistent) {
                for (size_t i = 0; i < utilityParameterCount; ++i) {
                    const auto& d = utilityParameters[i];
                    if (d.kind == panel.kind)
                        settings.values[i] = std::clamp(targets[i].read(next, d.initial), d.min, d.max);
                }
                enabled = onTarget.read(next, 1) >= .5f;
                mix = mixTarget.read(next, 100);
                ready = true;
            }
            if (panel.kind == UtilityKind::synthFilter && ready) updateSynthCurve();
            repaint();
        }
        void paint(juce::Graphics& g) override {
            const auto c = themePalette(*this, neon);
            g.fillAll(c.inset); g.setFont(juce::Font(pixelFontOptions(10.f)));
            if (!panel.isFilter()) { paintMeters(g, c); return; }
            auto b = getLocalBounds().reduced(7);
            const double rate = responseSampleRate();
            const bool synth = panel.kind == UtilityKind::synthFilter;
            const bool cached = !synth || (!synthCurve.bins.empty() && synthCurve.request.rate == rate);
            const bool updating = synth && (!cached || !(synthCurve.request == curveRequest()));
            const bool approximate = synth || UtilityEffect::response(rate, UtilityKind::xpFilter, 1000, settings).approximate;
            g.setColour(c.text);
            g.drawText(!ready ? "Waiting for coherent targets" : synth && !cached
                ? (curveFailed ? "Curve unavailable / retrying" : "Calculating small-signal curve...")
                : updating ? "Approximate cached curve / updating..." : approximate
                ? "Approximate / frozen small-signal model" : "Exact frozen, settled linear response", b.removeFromTop(15), juce::Justification::centredLeft);
            g.setColour(c.muted);
            g.drawText(enabled ? "Effective targets | wet response (before Mix)" : "BYPASS | stored wet response (not audible)", b.removeFromTop(15), juce::Justification::centredLeft);
            const auto plot = plotBounds();
            for (float hz : {20.f, 100.f, 1000.f, 10000.f, 20000.f}) {
                const float x = plot.getX() + plot.getWidth() * float(std::log(hz / 20) / std::log(1000.0));
                g.setColour(c.border); g.drawVerticalLine(int(x), plot.getY(), plot.getBottom());
                g.setColour(c.muted); g.drawText(hz >= 1000 ? juce::String(int(hz / 1000)) + "k" : juce::String(int(hz)), int(x) - 14, int(plot.getBottom()) + 2, 28, 12, juce::Justification::centred);
            }
            for (int db : {-48, -24, 0, 24}) {
                const float y = responseY(plot, double(db));
                g.setColour(c.border); g.drawHorizontalLine(int(y), plot.getX(), plot.getRight());
                g.setColour(c.muted); g.drawText(juce::String(db), 0, int(y) - 6, 26, 12, juce::Justification::centredRight);
            }
            // Independent axes: wet transfer dB on the left; FFT dBFS on the right.
            if (hasLiveFrame()) {
                juce::Graphics::ScopedSaveState clip(g); g.reduceClipRegion(plot.toNearestInt());
                g.setColour(c.muted.withAlpha(.4f * opacity)); g.strokePath(inputSpectrum().path(plot, rate), juce::PathStrokeType(1));
                g.setColour(c.secondary.withAlpha(.6f * opacity)); g.strokePath(outputSpectrum().path(plot, rate), juce::PathStrokeType(1));
            }
            g.setColour(c.muted);
            for (int db : {0, -48, -96}) {
                const float y = plot.getBottom() - plot.getHeight() * float(db + 96) / 96;
                g.drawText(juce::String(db), int(plot.getRight()) + 2, int(y) - 6, 26, 12, juce::Justification::centredLeft);
            }
            if (ready) {
                // Live FFT repaints must not rebuild an unchanged response path.
                if (pathSettings.values != settings.values || pathRate != rate || pathBounds != plot
                    || pathRevision != curveRevision) {
                    responsePath.clear();
                    const int steps = juce::jlimit(64, 1600, int(plot.getWidth()) * 3);
                    for (int i = 0; i <= steps; ++i) {
                        const double hz = 20 * std::pow(1000., double(i) / steps);
                        if (hz >= rate * .5 || !cached) break;
                        const double magnitude = synth ? synthMagnitude(hz)
                            : UtilityEffect::response(rate, UtilityKind::xpFilter, hz, settings).magnitude;
                        const double db = 20 * std::log10(std::max(1.e-12, magnitude));
                        const float x = plot.getX() + plot.getWidth() * float(i) / float(steps), y = responseY(plot, db);
                        if (i == 0) responsePath.startNewSubPath(x, y); else responsePath.lineTo(x, y);
                    }
                    pathSettings = settings; pathRate = rate; pathBounds = plot; pathRevision = curveRevision;
                }
                g.setColour(c.accent.withAlpha(enabled ? 1.f : .45f)); g.strokePath(responsePath, juce::PathStrokeType(1.8f));
                const float cutoff = settings.values[panel.cutoffField()];
                const float x = plot.getX() + plot.getWidth() * float(std::log(cutoff / 20) / std::log(1000.0));
                const float y = plot.getBottom() - settings.values[panel.resonanceField()] * .01f * plot.getHeight();
                g.setColour(c.accent); g.drawRect(x - 4, y - 4, 8.f, 8.f, 1.5f);
            }
            g.setColour(c.muted);
            g.drawText("L: response dB | R: FFT dBFS | Mix " + juce::String(mix, 0) + "%", 7, getHeight() - 30, getWidth() - 14, 13, juce::Justification::centredLeft);
            g.drawText(hasLiveFrame() ? (isStreamStalled() ? "PRE / POST FFT (stalled / fading)" : "PRE dim / POST teal FFT | drag cutoff / resonance") : "No live FFT | drag cutoff / resonance", 7, getHeight() - 16, getWidth() - 14, 13, juce::Justification::centredLeft);
        }
        void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override {
            forwardPassiveWheel(*this, e, wheel);
        }
        void mouseDown(const juce::MouseEvent& e) override {
            if (!panel.isFilter() || !plotBounds().contains(e.position) || !e.mods.isLeftButtonDown()
                || e.mods.isPopupMenu() || monitorOwner.modulationAssignmentId() != 0) return;
            cutoff = monitorOwner.parameters.getParameter(utilityParameterId(monitorSlot, panel.cutoffField()));
            resonance = monitorOwner.parameters.getParameter(utilityParameterId(monitorSlot, panel.resonanceField()));
            if (!cutoff || !resonance) { cutoff = resonance = nullptr; return; }
            panel.begin(utilityParameterId(monitorSlot, panel.cutoffField()));
            if (panel.onBeginEdit) panel.onBeginEdit(utilityParameterId(monitorSlot, panel.resonanceField()));
            cutoff->beginChangeGesture(); resonance->beginChangeGesture(); dragging = true;
            mouseDrag(e);
        }
        void mouseDrag(const juce::MouseEvent& e) override {
            if (!dragging) return;
            if (monitorOwner.modulationAssignmentId() != 0) { finishDrag(); return; }
            const auto b = plotBounds();
            const double x = juce::jlimit(0., 1., double((e.position.x - b.getX()) / b.getWidth()));
            const float r = 100 * juce::jlimit(0.f, 1.f, (b.getBottom() - e.position.y) / b.getHeight());
            cutoff->setValueNotifyingHost(cutoff->convertTo0to1(float(20 * std::pow(1000., x))));
            resonance->setValueNotifyingHost(resonance->convertTo0to1(r));
            panel.changed();
        }
        void mouseUp(const juce::MouseEvent&) override { finishDrag(); }
    private:
        struct CurveRequest {
            UtilitySettings targets;
            double rate = 48000;
            bool operator==(const CurveRequest& other) const {
                return rate == other.rate && targets.values == other.targets.values;
            }
        };
        struct SynthCurve {
            CurveRequest request;
            std::vector<UtilityResponse> bins;
        };
        // Shared by all Synth visuals (including different editors). Tickets contain only
        // snapshots/results; neither the worker nor its queue can access UI or processors.
        class CurveScheduler final {
            friend struct UtilityPanelTests;
        public:
            struct Ticket {
                CurveRequest pending, active;
                SynthCurve result;
                bool queued = false, running = false, completed = false;
                bool failed = false, cancelled = false;
            };
            static std::shared_ptr<CurveScheduler> shared() {
                // Keep the worker alive across virtualization churn. Visuals also retain
                // ownership so static teardown cannot invalidate a surviving client.
                static auto scheduler = std::make_shared<CurveScheduler>();
                return scheduler;
            }
            using Render = std::function<std::vector<UtilityResponse>(double, const UtilitySettings&)>;
            explicit CurveScheduler(Render renderCurve = [](double rate, const UtilitySettings& targets) {
                return UtilityEffect::synthResponseCurve(rate, targets, 32768);
            }) : render(std::move(renderCurve)), worker([this] { run(); }) {}
            ~CurveScheduler() {
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    stopping = true;
                }
                wake.notify_one();
                // Only scheduler shutdown joins, never ordinary component destruction.
                // At most the current fixed-size render must finish; queued work is dropped.
                worker.join();
            }
            bool poll(const std::shared_ptr<Ticket>& ticket, SynthCurve& curve, bool& failed) {
                std::lock_guard<std::mutex> lock(mutex);
                if (!ticket->completed) return false;
                failed = ticket->failed;
                if (!failed) curve = std::move(ticket->result);
                ticket->completed = false;
                return true;
            }
            bool submit(const std::shared_ptr<Ticket>& ticket, const CurveRequest& request) {
                std::lock_guard<std::mutex> lock(mutex);
                if (stopping || ticket->cancelled) return false;
                if ((ticket->running && ticket->active == request)
                    || (ticket->completed && !ticket->failed && ticket->result.request == request)) {
                    // Targets may have returned to the in-flight snapshot.
                    removePending(ticket);
                    return true;
                }
                if (ticket->queued) {
                    // Replace without moving its FIFO position: busy clients cannot jump
                    // the queue or starve other visible nodes/the inspector.
                    ticket->pending = request;
                    return true;
                }
                if (pendingCount == pending.size()) return false; // Polling clients retry.
                ticket->pending = request;
                ticket->queued = true;
                pending[pendingCount++] = ticket;
                wake.notify_one();
                return true;
            }
            void discardPending(const std::shared_ptr<Ticket>& ticket) {
                std::lock_guard<std::mutex> lock(mutex);
                removePending(ticket);
            }
            void cancel(const std::shared_ptr<Ticket>& ticket) {
                if (!ticket) return;
                std::lock_guard<std::mutex> lock(mutex);
                ticket->cancelled = true;
                removePending(ticket);
            }
        private:
            // All ticket/queue state is guarded by mutex, held only for bookkeeping,
            // never during rendering or waiting for a render to complete.
            void removePending(const std::shared_ptr<Ticket>& ticket) {
                for (size_t i = 0; i < pendingCount; ++i) {
                    if (pending[i] != ticket) continue;
                    for (size_t j = i + 1; j < pendingCount; ++j)
                        pending[j - 1] = std::move(pending[j]);
                    pending[--pendingCount].reset();
                    ticket->queued = false;
                    break;
                }
            }
            void run() {
                for (;;) {
                    std::shared_ptr<Ticket> ticket;
                    CurveRequest request;
                    {
                        std::unique_lock<std::mutex> lock(mutex);
                        wake.wait(lock, [this] { return stopping || pendingCount != 0; });
                        if (stopping) return;
                        ticket = pending[0];
                        request = ticket->pending;
                        removePending(ticket);
                        ticket->active = request;
                        ticket->running = true;
                    }
                    SynthCurve result;
                    bool failed = false;
                    try {
                        result = {request, render(request.rate, request.targets)};
                    } catch (...) { failed = true; }
                    {
                        std::lock_guard<std::mutex> lock(mutex);
                        ticket->running = false;
                        if (!stopping && !ticket->cancelled) {
                            ticket->result = std::move(result);
                            ticket->failed = failed;
                            ticket->completed = true;
                        }
                    }
                }
            }
            std::mutex mutex;
            std::condition_variable wake;
            // One active render plus at most one pending snapshot per client, capped
            // globally at the 16 visible routing nodes + inspector. No overflow queue.
            std::array<std::shared_ptr<Ticket>, 17> pending{};
            size_t pendingCount = 0;
            bool stopping = false;
            const Render render;
            std::thread worker;
        };
        double responseSampleRate() const {
            const double rate = hasLiveFrame() ? frame.sampleRate : monitorOwner.getSampleRate();
            // Match UtilityEffect's supported-rate fallback so cached bin spacing is correct.
            return std::isfinite(rate) && rate >= 8000 && rate <= 384000 ? rate : 48000;
        }
        CurveRequest curveRequest() const {
            CurveRequest request;
            request.rate = responseSampleRate();
            // Only curve inputs invalidate the cache: Mix, bypass and other utilities do not.
            for (auto id : {UtilityParameter::synthMode, UtilityParameter::synthCutoff,
                            UtilityParameter::synthResonance, UtilityParameter::synthDrive,
                            UtilityParameter::synthFeedback})
                request.targets[id] = settings[id];
            request.targets[UtilityParameter::synthMode] = std::floor(request.targets[UtilityParameter::synthMode]);
            return request;
        }
        void updateSynthCurve() {
            // Never replace a current cache with an older in-flight completion (A/B/A).
            const auto request = curveRequest();
            if (curveTicket) {
                SynthCurve completed;
                if (curveScheduler->poll(curveTicket, completed, curveFailed) && !curveFailed
                    && (completed.request == request || synthCurve.bins.empty() || !(synthCurve.request == request))) {
                    synthCurve = std::move(completed);
                    ++curveRevision;
                }
            }
            if (!synthCurve.bins.empty() && synthCurve.request == request) {
                if (curveTicket) curveScheduler->discardPending(curveTicket);
                return;
            }
            const auto now = std::chrono::steady_clock::now();
            // Coalesce every UI tick, not every 100 ms. A queued ticket keeps its
            // FIFO position and an active render is never cancelled by new targets.
            // Only failures back off; ordinary edits must not reset a debounce clock.
            if (curveFailed && now < nextCurveUpdate) return;
            if (curveFailed) nextCurveUpdate = now + std::chrono::milliseconds(250);
            try {
                if (!curveScheduler) curveScheduler = CurveScheduler::shared();
                if (!curveTicket) curveTicket = std::make_shared<CurveScheduler::Ticket>();
                curveScheduler->submit(curveTicket, request);
            } catch (...) { curveFailed = true; }
        }
        double synthMagnitude(double hz) const {
            const auto& bins = synthCurve.bins;
            const double bin = std::clamp(hz / synthCurve.request.rate * double((bins.size() - 1) * 2),
                                          0., double(bins.size() - 1));
            const auto lo = static_cast<size_t>(bin), hi = std::min(lo + 1, bins.size() - 1);
            return bins[lo].magnitude + (bin - double(lo)) * (bins[hi].magnitude - bins[lo].magnitude);
        }
        juce::Rectangle<float> plotBounds() const {
            return {29.f, 43.f, float(juce::jmax(1, getWidth() - 61)), float(juce::jmax(1, getHeight() - 95))};
        }
        static float responseY(juce::Rectangle<float> b, double db) {
            return b.getBottom() - b.getHeight() * float((std::clamp(db, -48., 24.) + 48) / 72);
        }
        void finishDrag() {
            if (!dragging) return;
            cutoff->endChangeGesture(); resonance->endChangeGesture(); dragging = false;
            cutoff = resonance = nullptr; panel.end();
        }
        void paintMeters(juce::Graphics& g, const ThemePalette& c) {
            auto b = getLocalBounds().reduced(8);
            g.setColour(c.text);
            g.drawText(hasLiveFrame() ? "LIVE STEREO RMS | pre / post" : "No live pre / post monitor", b.removeFromTop(18), juce::Justification::centredLeft);
            for (int post = 0; post < 2; ++post) {
                double power = 0;
                if (hasLiveFrame()) for (const auto& channel : post ? frame.output : frame.input)
                    for (float v : channel) power += double(v) * v;
                const double rms = std::sqrt(power / double(2 * FxMonitorFrame::sampleCount));
                const float db = float(20 * std::log10(std::max(1.e-12, rms)));
                auto row = b.removeFromTop(22);
                g.setColour(c.muted); g.drawText(post ? "POST" : "PRE", row.removeFromLeft(34), juce::Justification::centredLeft);
                g.drawText(rms > 0 ? juce::String(db, 1) + " dBFS" : "-inf dBFS", row.removeFromRight(78), juce::Justification::centredRight);
                auto bar = row.toFloat().reduced(3, 7);
                g.setColour(c.border); g.fillRect(bar);
                g.setColour((post ? c.accent : c.secondary).withAlpha(opacity));
                g.fillRect(bar.withWidth(bar.getWidth() * juce::jlimit(0.f, 1.f, (db + 72) / 72)));
            }
            if (panel.kind == UtilityKind::gate) {
                const float reduction = std::max(0.f, monitorOwner.getCreativeGainReduction(monitorSlot));
                g.setColour(c.accent); g.drawText("Actual gate GR: " + juce::String(reduction, 1) + " dB", b.removeFromTop(20), juce::Justification::centredLeft);
                auto bar = b.removeFromTop(5).toFloat(); g.setColour(c.border); g.fillRect(bar);
                g.setColour(c.accent); g.fillRect(bar.withWidth(bar.getWidth() * juce::jlimit(0.f, 1.f, reduction / 96)));
            }
        }
        UtilityEffectPanel& panel;
        UtilitySettings settings;
        std::array<Target, utilityParameterCount> targets;
        Target onTarget, mixTarget;
        SynthCurve synthCurve;
        juce::Path responsePath;
        UtilitySettings pathSettings;
        juce::Rectangle<float> pathBounds;
        double pathRate = 0;
        size_t curveRevision = 0, pathRevision = 0;
        bool curveFailed = false;
        std::chrono::steady_clock::time_point nextCurveUpdate{};
        std::shared_ptr<CurveScheduler> curveScheduler;
        std::shared_ptr<CurveScheduler::Ticket> curveTicket;
        bool enabled = true, ready = false, dragging = false;
        float mix = 100;
        juce::RangedAudioParameter* cutoff = nullptr;
        juce::RangedAudioParameter* resonance = nullptr;
    };
public:
    UtilityEffectPanel(Processor& processor, size_t fxSlot, UtilityKind effectKind)
        : owner(processor), slot(fxSlot), kind(effectKind), visual(*this) {
        setName(effectName(kind) + " inspector");
        content.addAndMakeVisible(visual);
        overview.setText(description(kind), juce::dontSendNotification);
        overview.setFont(juce::Font(pixelFontOptions(11.5f))); overview.setJustificationType(juce::Justification::topLeft);
        overview.setInterceptsMouseClicks(false, false); content.addAndMakeVisible(overview);
        addRow(fxParameterId(slot, "Mix"), "Dry / Wet", "%", false, false, utilityParameterCount);
        for (size_t i = 0; i < utilityParameterCount; ++i) {
            const auto& d = utilityParameters[i];
            if (d.kind == kind) addRow(utilityParameterId(slot, i), d.label, d.unit, d.discrete,
                i == size_t(UtilityParameter::xpMode) || i == size_t(UtilityParameter::synthMode), i);
        }
        // Count actual displayed continuous controls, including the slot dry/wet.
        const auto count = std::count_if(rows.begin(), rows.end(), [](const auto& row) { return row->slider.isVisible(); });
        if (count <= 2) for (auto& row : rows) if (row->slider.isVisible()) {
            row->slider.getProperties().set("vocalstackTexturedBar", true);
            row->slider.setSliderStyle(juce::Slider::LinearHorizontal);
        }
        viewport.setViewedComponent(&content, false); viewport.setScrollBarsShown(true, false); addAndMakeVisible(viewport);
        detail::passiveControl(*this); setNeon(false); timerCallback(); startTimerHz(60);
    }
    ~UtilityEffectPanel() override { stopTimer(); viewport.setViewedComponent(nullptr, false); }
    bool responseReady() const { return visual.responseReady(); }
    std::function<void(const juce::String&)> onBeginEdit;
    std::function<void()> onEndEdit, onValueChanged;
    static juce::String effectName(UtilityKind kind) {
        switch (kind) {
            case UtilityKind::gain: return "Gain";
            case UtilityKind::stereo: return "Stereo";
            case UtilityKind::gate: return "Gate";
            case UtilityKind::xpFilter: return "XP Filter";
            case UtilityKind::synthFilter: return "Synth Filter";
        }
        return {};
    }
    static juce::String description(UtilityKind kind) {
        switch (kind) {
            case UtilityKind::gain: return "Level trim. The minimum is exact mute (-infinity), not -120 dB. Dry / Wet blends with the original signal.";
            case UtilityKind::stereo: return "Mid/side width: 0% mono, 100% unchanged, 200% double side. Balance attenuates the opposite side; swap and polarity follow.";
            case UtilityKind::gate: return "Stereo-linked downward expansion with opening attack, hold and closing release. Meter shows actual DSP gain reduction.";
            case UtilityKind::xpFilter: return "Cascaded state-variable filter. Response is the settled wet model; drive is a local small-signal approximation.";
            case UtilityKind::synthFilter: return "13 filter modes: digital, ladder, dirty, diode, analog, comb and four-formant morph. Cached finite-impulse estimate; nonlinear and moving-delay responses are approximate.";
        }
        return {};
    }
    void setNeon(bool value) {
        neon = value; visual.setNeon(value);
        overview.setColour(juce::Label::textColourId, themePalette(*this, neon).muted);
        for (auto& row : rows) row->setNeon(value);
        repaint();
    }
    void revealParameter(const juce::String& id) {
        for (const auto& row : rows) if (row->id == id) viewport.setViewPosition(0, juce::jmax(0, row->getY() - 26));
    }
    void paint(juce::Graphics& g) override {
            const auto palette = themePalette(*this, neon);
            background.paint(g, {0, -viewport.getViewPositionY(), content.getWidth(), content.getHeight()}, palette.panel, palette.accent, fluidAnimationSeconds(*this));
        }
    void resized() override {
        const auto position = viewport.getViewPosition();
        viewport.setBounds(getLocalBounds());
        const int width = juce::jmax(1, getWidth() - viewport.getScrollBarThickness());
        const int inner = juce::jmax(1, width - 16);
        const bool wide = getWidth() >= 620;
        const int visualWidth = wide ? inner * 44 / 100 : inner;
        const int controlX = wide ? 8 + visualWidth + 6 : 8;
        const int controlWidth = wide ? inner - visualWidth - 6 : inner;
        int y = 8;
        const int visualHeight = isFilter() ? 238 : kind == UtilityKind::gate ? 110 : 72;
        visual.setBounds(8, y, visualWidth, visualHeight); y += visualHeight + 6;
        const int overviewHeight = juce::jmax(54, int(std::ceil(
            juce::GlyphArrangement::getStringWidth(overview.getFont(), overview.getText())
                / double(juce::jmax(1, visualWidth - 8)))) * 16 + 8);
        overview.setBounds(8, y, visualWidth, overviewHeight); y += overviewHeight + 6;
        const int visualBottom = y;
        if (wide) y = 8;
        constexpr int knobWidth = 68, knobHeight = 100, knobGap = 2;
        int x = controlX, rowHeight = 0;
        const auto flush = [&] {
            if (rowHeight != 0) { y += rowHeight + knobGap; x = controlX; rowHeight = 0; }
        };
        for (const auto& row : rows) {
            if (row->combo.isVisible() || (row->slider.isVisible()
                            && row->slider.getSliderStyle() == juce::Slider::LinearHorizontal)) {
                flush();
                // Mix's editable value and name are overlaid inside the track.
                const int height = row->combo.isVisible() ? 46 : 44;
                row->setBounds(controlX, y, controlWidth, height); y += height + knobGap;
            } else {
                const bool toggle = row->toggle.isVisible();
                const int cellWidth = toggle ? 64 : knobWidth;
                const int cellHeight = toggle ? 28 : knobHeight;
                // Keep switches out of tall knob rows, but let adjacent switches share a row.
                if (rowHeight != 0 && (rowHeight != cellHeight || x + cellWidth > controlX + controlWidth)) flush();
                row->setBounds(x, y, juce::jmin(controlWidth, cellWidth), cellHeight);
                x += cellWidth + knobGap;
                rowHeight = cellHeight;
            }
        }
        flush();
        content.setSize(width, juce::jmax(getHeight(), juce::jmax(y, visualBottom) + 8));
        viewport.setViewPosition(position);
    }
private:
    bool isFilter() const { return kind == UtilityKind::xpFilter || kind == UtilityKind::synthFilter; }
    size_t cutoffField() const { return size_t(kind == UtilityKind::xpFilter ? UtilityParameter::xpCutoff : UtilityParameter::synthCutoff); }
    size_t resonanceField() const { return size_t(kind == UtilityKind::xpFilter ? UtilityParameter::xpResonance : UtilityParameter::synthResonance); }
    void addRow(const juce::String& id, const juce::String& name, const char* unit, bool discrete, bool mode, size_t field) {
        auto row = std::make_unique<Row>(*this, id, name, unit, discrete, mode, field);
        content.addAndMakeVisible(*row); rows.push_back(std::move(row));
    }
    void begin(const juce::String& id) {
        if (!editing) { editing = true; if (onBeginEdit) onBeginEdit(id); }
    }
    void end() {
        if (!editing) return;
        editing = false; if (onEndEdit) onEndEdit(); visual.refresh();
    }
    void changed() {
        // SliderAttachment has delivered the host value before this callback.
        // Use one coherent target snapshot, just as graph drags do, without waiting
        // for the automation timer or replacing modulation with an unmodulated preview.
        visual.refresh();
        if (onValueChanged) onValueChanged();
    }
    void editValue(const juce::String& id, float value) {
        if (owner.modulationAssignmentId() != 0) return;
        if (auto* p = owner.parameters.getParameter(id)) {
            begin(id); p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(value)); p->endChangeGesture(); changed(); end();
        }
    }
    void timerCallback() override { for (auto& row : rows) row->refresh(); visual.refresh(); }
    Processor& owner;
    size_t slot;
    UtilityKind kind;
    bool neon = false, editing = false;
    FluidBackground background;
    Visual visual;
    juce::Component content;
    PassiveViewport viewport;
    juce::Label overview;
    std::vector<std::unique_ptr<Row>> rows;
};

} // namespace vocalstack
