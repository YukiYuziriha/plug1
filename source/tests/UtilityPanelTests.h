#pragma once

#include "UtilityEffectPanel.h"
#include <atomic>
#include <iostream>
#include <stdexcept>

namespace vocalstack {
// Narrow friend seam: deterministic worker tests never touch audio or start an editor.
struct UtilityPanelTests {
    using Visual = UtilityEffectPanel::Visual;
    using Scheduler = Visual::CurveScheduler;
    using Request = Visual::CurveRequest;
    using Curve = Visual::SynthCurve;
    static void require(bool condition, const char* reason) {
        if (!condition) throw std::runtime_error(reason);
    }
    template <class Predicate> static void wait(Predicate predicate) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!predicate()) {
            require(std::chrono::steady_clock::now() < deadline, "Worker did not settle latest targets");
            juce::Thread::sleep(1);
        }
    }
    static Request request(float cutoff) {
        Request r; r.targets[UtilityParameter::synthCutoff] = cutoff; return r;
    }
    static void scheduling() {
        std::atomic<int> calls{0};
        std::atomic<bool> release{false};
        Scheduler scheduler([&](double, const UtilitySettings& s) {
            if (++calls == 1) {
                // Self-releasing watchdog also makes failed assertions safe to unwind.
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
                while (!release && std::chrono::steady_clock::now() < deadline) juce::Thread::sleep(1);
            }
            return std::vector<UtilityResponse>{{s[UtilityParameter::synthCutoff], true}};
        });
        auto busy = std::make_shared<Scheduler::Ticket>();
        require(scheduler.submit(busy, request(100)), "Initial submit rejected");
        wait([&] { return calls.load() == 1; });
        std::array<std::shared_ptr<Scheduler::Ticket>, 18> clients;
        for (auto& client : clients) client = std::make_shared<Scheduler::Ticket>();
        require(scheduler.submit(busy, request(200)), "Busy follow-up rejected");
        require(scheduler.submit(busy, request(100)), "Return to active target rejected");
        { std::lock_guard<std::mutex> lock(scheduler.mutex);
          require(scheduler.pendingCount == 0 && !busy->queued, "Return to active target retained obsolete pending work"); }
        scheduler.submit(busy, request(200));
        for (int i = 0; i < 16; ++i) require(scheduler.submit(clients[size_t(i)], request(float(300 + i))), "Bounded queue rejected valid client");
        require(!scheduler.submit(clients[16], request(900)), "Queue overflow was not bounded");
        for (int i = 0; i < 100; ++i) scheduler.submit(busy, request(float(1000 + i)));
        {
            std::lock_guard<std::mutex> lock(scheduler.mutex);
            require(scheduler.pendingCount == 17 && scheduler.pending[0] == busy,
                    "Coalescing grew queue or changed FIFO position");
            require(busy->pending == request(1099), "Coalescing retained stale targets");
        }
        scheduler.cancel(clients[3]);
        require(scheduler.submit(clients[16], request(900)), "Cancellation did not release bounded slot");
        release = true;
        Curve curve; bool failed = false;
        wait([&] { return scheduler.poll(clients[16], curve, failed); });
        require(!failed && curve.request == request(900), "Neighbour starved behind busy client");
        wait([&] { return scheduler.poll(busy, curve, failed) && curve.request == request(1099); });
        require(calls == 18, "Duplicate/intermediate requests were rendered");

        // A completed, unpolled request must not be rendered again by fast ticks.
        auto ticket = clients[17];
        scheduler.submit(ticket, request(777));
        wait([&] { std::lock_guard<std::mutex> lock(scheduler.mutex); return ticket->completed; });
        const int completedCalls = calls;
        for (int i = 0; i < 100; ++i) scheduler.submit(ticket, request(777));
        require(calls == completedCalls, "Unpolled completion was rescheduled");
        require(scheduler.poll(ticket, curve, failed) && curve.request == request(777), "Latest completion lost");

        std::atomic<bool> entered{false}, unblock{false};
        Scheduler cancelling([&](double, const UtilitySettings&) {
            entered = true;
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (!unblock && std::chrono::steady_clock::now() < deadline) juce::Thread::sleep(1);
            return std::vector<UtilityResponse>{{1, true}};
        });
        auto destroyed = std::make_shared<Scheduler::Ticket>();
        auto survivor = std::make_shared<Scheduler::Ticket>();
        cancelling.submit(destroyed, request(10)); wait([&] { return entered.load(); });
        const auto start = std::chrono::steady_clock::now();
        cancelling.cancel(destroyed);
        require(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(100), "Client destruction joined in-flight DSP work");
        cancelling.submit(survivor, request(20)); unblock = true;
        wait([&] { return cancelling.poll(survivor, curve, failed); });
        require(!cancelling.poll(destroyed, curve, failed), "Cancelled active client published a result");
    }
    static juce::Image render(juce::Component& component) {
        juce::Image image(juce::Image::ARGB, component.getWidth(), component.getHeight(), true, juce::SoftwareImageType());
        juce::Graphics graphics(image); component.paintEntireComponent(graphics, true); return image;
    }
    static bool different(const juce::Image& a, const juce::Image& b) {
        for (int y = 0; y < a.getHeight(); ++y) for (int x = 0; x < a.getWidth(); ++x)
            if (a.getPixelAt(x, y) != b.getPixelAt(x, y)) return true;
        return false;
    }
    struct Gestures final : juce::AudioProcessorParameter::Listener {
        int starts = 0, ends = 0;
        void parameterValueChanged(int, float) override {}
        void parameterGestureChanged(int, bool starting) override { starting ? ++starts : ++ends; }
    };
    static void layouts() {
        auto owner = std::make_unique<Processor>();
        for (auto kind : {UtilityKind::gain, UtilityKind::stereo, UtilityKind::gate,
                          UtilityKind::xpFilter, UtilityKind::synthFilter}) {
            UtilityEffectPanel panel(*owner, 1, kind);
            for (int width : {190, 204, 220, 280, 320, 400, 560, 619, 620, 640, 800, 860}) {
                panel.setSize(width, 280);
                const bool wide = width >= 620;
                const int inner = panel.content.getWidth() - 16;
                const int controlWidth = wide ? inner - inner * 44 / 100 - 6 : inner;
                const int controlX = wide ? panel.visual.getRight() + 6 : 8;
                require(!wide || (panel.visual.getWidth() < inner / 2
                                 && ([&] { for (const auto& row : panel.rows) if (row->isVisible()) return row->getY() == panel.visual.getY() + (kind == UtilityKind::stereo ? 34 : 0); return false; })()),
                        "Wide Utility inspector still uses a full-width vertical column");
                require(panel.content.getWidth() <= panel.viewport.getViewArea().getWidth(),
                        "Utility inspector requires horizontal scrolling");
                for (size_t i = 0; i < panel.rows.size(); ++i) {
                    const auto& row = panel.rows[i];
                    if (!row->isVisible()) continue; // Default Gain has no legacy Wet row.
                    require(panel.content.getLocalBounds().contains(row->getBounds()), "Utility parameter clipped");
                    require(row->getX() >= controlX, "Wide Utility control escaped its right-hand group");
                    panel.revealParameter(row->id);
                    require(panel.viewport.getViewArea().contains(row->getBounds()),
                            "Utility parameter is not fully scroll reachable");
                    if (i != 0 && panel.rows[i - 1]->isVisible() && row->getY() > panel.rows[i - 1]->getY())
                        require(row->getY() == panel.rows[i - 1]->getBottom() + 2,
                                "Utility control rows must retain a 2px vertical gap");
                    require(!row->getBounds().intersects(panel.visual.getBounds())
                            && !row->getBounds().intersects(panel.overview.getBounds()), "Utility control overlaps its visual");
                    for (size_t j = 0; j < i; ++j)
                        if (panel.rows[j]->isVisible()) require(!row->getBounds().intersects(panel.rows[j]->getBounds()), "Utility parameters overlap");
                    if (row->toggle.isVisible()) {
                        require(row->getWidth() == 64 && row->getHeight() == 28, "Utility switch is not compact");
                    } else if (row->combo.isVisible()) {
                        require(row->getWidth() == controlWidth, "Filter mode lost its control-group row");
                    } else {
                        const bool mix = row->id == fxParameterId(1, "Mix") || kind == UtilityKind::gain;
                        require(row->slider.getSliderStyle() == (mix ? juce::Slider::LinearHorizontal
                                                                 : juce::Slider::RotaryHorizontalVerticalDrag),
                                "Utility parameter changed control style");
                        require(row->getWidth() == (mix ? controlWidth : 68)
                                && row->getHeight() == (mix ? 44 : 100), "Utility control stretched with inspector");
                        require(bool(row->slider.getProperties()["vocalstackTexturedBar"]) == mix,
                                "Only Mix or a sparse gain group should use the textured FX bar");
                        if (mix) {
                            const auto layout = row->style.getSliderLayout(row->slider);
                            const auto track = ParameterKnob::Style::texturedBarBounds(layout.sliderBounds.toFloat());
                            require(!row->label.isVisible()
                                        && row->slider.getBounds() == row->getLocalBounds().reduced(2, 1),
                                    "Utility Mix must fill its compact row without an external caption");
                            require(layout.sliderBounds.getY() == 0 && track.getY() >= 8 && track.getHeight() == 25,
                                    "Utility Mix clips its modulation lane or 25px track");
                            require(layout.textBoxBounds.intersects(track.toNearestInt()),
                                    "Utility Mix numeric entry must overlay its track");
                            bool valueFound = false;
                            for (auto* child : row->slider.getChildren())
                                if (auto* label = dynamic_cast<juce::Label*>(child); label && label->isVisible())
                                    valueFound = true;
                            require(valueFound, "Utility Mix lost its native numeric textbox");
                        }
                        require(row->slider.getTextBoxPosition() == juce::Slider::TextBoxAbove,
                                "Utility value must retain the native textbox configuration");
                        if (!mix) {
                            require(row->label.isVisible() && row->label.getY() >= row->slider.getBottom(),
                                    "Utility name is not below its control");
                            require((row->getX() - controlX) % 70 == 0,
                                    "Utility rotary columns must have a 2px gap");
                            require(row->slider.getTextBoxWidth() == 64 && row->slider.getTextBoxHeight() == 21,
                                    "Utility rotary value must fit the narrower cell");
                        }
                    }
                }
                panel.viewport.setViewPosition(0, 100);
                const int oldY = panel.viewport.getViewPositionY();
                panel.setSize(width + 1, 280);
                require(panel.viewport.getViewPositionY() == juce::jmin(oldY,
                            panel.content.getHeight() - panel.viewport.getViewArea().getHeight()),
                        "Utility resize reset scroll position");
            }
        }
    }
    static void controls() {
        auto owner = std::make_unique<Processor>();
        const auto focus = juce::Component::getCurrentlyFocusedComponent();
        for (auto kind : {UtilityKind::xpFilter, UtilityKind::synthFilter}) {
            UtilityEffectPanel panel(*owner, 1, kind);
            require(panel.getTimerInterval() <= 17, "Panel cadence regressed below 60 Hz");
            UtilityEffectPanel::Row* cutoff = nullptr;
            for (auto& row : panel.rows) if (row->id == utilityParameterId(1, panel.cutoffField())) cutoff = row.get();
            require(cutoff != nullptr, "Cutoff control missing");
            for (bool neon : {false, true}) for (int width : {190, 200, 220, 500}) {
                panel.setNeon(neon); panel.setSize(width, 380);
                for (const auto& row : panel.rows) {
                    require(panel.content.getLocalBounds().contains(row->getBounds()), "Narrow card clipped a parameter");
                    require(row->getWidth() >= (row->toggle.isVisible() ? 48 : 68), "Narrow control is unusably small");
                    if (row->combo.isVisible()) require(row->getWidth() == panel.visual.getWidth(), "Mode selector is truncated to a knob cell");
                }
                if (width == 190 || width == 220) {
                    panel.viewport.setViewPosition(0, 0);
                    const auto name = "utility-" + juce::String(int(kind)) + "-" + juce::String(width)
                        + (neon ? "-neon.png" : "-device.png");
                    juce::FileOutputStream output(juce::File::getCurrentWorkingDirectory().getChildFile(name));
                    require(output.openedOk() && juce::PNGImageFormat().writeImageToStream(render(panel), output), "Could not save narrow layout evidence");
                    panel.viewport.setViewPosition(0, 100000);
                    juce::FileOutputStream controlsOutput(juce::File::getCurrentWorkingDirectory().getChildFile("controls-" + name));
                    require(controlsOutput.openedOk() && juce::PNGImageFormat().writeImageToStream(render(panel), controlsOutput), "Could not save scrolled controls evidence");
                }
                panel.revealParameter(cutoff->id);
                const auto oldValue = cutoff->slider.getValue();
                const auto beforeKnob = render(cutoff->slider);
                const auto beforeGraph = render(panel.visual);
                const auto editStart = std::chrono::steady_clock::now();
                cutoff->slider.setValue(oldValue < 2000 ? 4200 : 350, juce::sendNotificationSync);
                require(std::chrono::steady_clock::now() - editStart < std::chrono::milliseconds(100), "Knob callback blocked on response DSP");
                bool valueLabel = false;
                for (auto* child : cutoff->slider.getChildren()) if (auto* label = dynamic_cast<juce::Label*>(child)) {
                    valueLabel = true;
                    require(label->getText() == cutoff->slider.getTextFromValue(cutoff->slider.getValue()), "Numeric knob feedback is stale");
                }
                require(valueLabel, "Knob value label missing");
                const auto* parameter = owner->parameters.getParameter(cutoff->id);
                require(std::abs(panel.visual.settings.values[panel.cutoffField()] - parameter->convertFrom0to1(parameter->getValue())) < 1,
                        "Knob edit waits for timer or displays a stale target");
                require(different(beforeKnob, render(cutoff->slider)), "Knob/value feedback did not redraw synchronously");
                require(different(beforeGraph, render(panel.visual)), "Graph handle did not follow knob synchronously");
                panel.viewport.setViewPosition(0, 0);
                const auto point = cutoff->slider.getLocalBounds().getCentre().toFloat();
                const auto now = juce::Time::getCurrentTime();
                juce::MouseEvent event{juce::Desktop::getInstance().getMainMouseSource(), point, {}, 1, 0, 0, 0, 0,
                    &cutoff->slider, &cutoff->slider, now, point, now, 0, false};
                juce::MouseWheelDetails wheel{}; wheel.deltaY = -.3f;
                const auto value = parameter->getValue();
                cutoff->slider.mouseWheelMove(event, wheel);
                require(panel.viewport.getViewPositionY() > 0, "Hovered utility knob swallowed internal scroll");
                require(parameter->getValue() == value, "Wheel changed a host parameter");
                for (auto* child : cutoff->slider.getChildren()) if (dynamic_cast<juce::Label*>(child)) {
                    panel.viewport.setViewPosition(0, 0);
                    child->mouseWheelMove(event.getEventRelativeTo(child), wheel);
                    require(panel.viewport.getViewPositionY() > 0, "Hovered value label swallowed internal scroll");
                }
                require(juce::Component::getCurrentlyFocusedComponent() == focus, "Passive control stole DAW keyboard focus");
            }
            {
                juce::Component outerContent;
                juce::Viewport outer;
                outer.setSize(500, 380); outerContent.setSize(500, 2000);
                outer.setViewedComponent(&outerContent, false);
                outerContent.addAndMakeVisible(panel);
                panel.viewport.setViewPosition(0, 100000);
                const auto point = cutoff->slider.getLocalBounds().getCentre().toFloat();
                const auto now = juce::Time::getCurrentTime();
                juce::MouseEvent event{juce::Desktop::getInstance().getMainMouseSource(), point, {}, 1, 0, 0, 0, 0,
                    &cutoff->slider, &cutoff->slider, now, point, now, 0, false};
                juce::MouseWheelDetails wheel{}; wheel.deltaY = -.3f;
                cutoff->slider.mouseWheelMove(event, wheel);
                require(outer.getViewPositionY() > 0, "Utility card edge swallowed wheel instead of forwarding to rack");
                outerContent.removeChildComponent(&panel); outer.setViewedComponent(nullptr, false);
            }
            {
                int begins = 0, ends = 0;
                panel.onBeginEdit = [&](const juce::String&) { ++begins; };
                panel.onEndEdit = [&] { ++ends; };
                auto* cutoffParameter = owner->parameters.getParameter(cutoff->id);
                auto* resonanceParameter = owner->parameters.getParameter(utilityParameterId(1, panel.resonanceField()));
                Gestures gestures;
                cutoffParameter->addListener(&gestures); resonanceParameter->addListener(&gestures);
                const auto point = panel.visual.plotBounds().getCentre();
                const auto now = juce::Time::getCurrentTime();
                juce::MouseEvent press{juce::Desktop::getInstance().getMainMouseSource(), point,
                    juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1, 0, 0, 0, 0,
                    &panel.visual, &panel.visual, now, point, now, 1, false};
                panel.visual.mouseDown(press); panel.visual.mouseDrag(press);
                panel.visual.mouseUp(press);
                cutoffParameter->removeListener(&gestures); resonanceParameter->removeListener(&gestures);
                panel.onBeginEdit = {}; panel.onEndEdit = {};
                require(gestures.starts == 2 && gestures.ends == 2 && begins == 2 && ends == 1,
                        "Graph edits lost balanced host gestures or combined undo scope");
                require(!panel.editing && !panel.visual.dragging, "Graph edit scope stayed open");
            }
            if (kind == UtilityKind::synthFilter) {
                wait([&] { panel.timerCallback(); return panel.responseReady(); });
                require(panel.visual.synthCurve.bins.size() == 16385, "Settled synth curve lost full DSP-derived resolution");
                // Inject an obsolete completion while the displayed A cache is current.
                auto& visual = panel.visual;
                const auto current = visual.curveRequest();
                {
                    std::lock_guard<std::mutex> lock(visual.curveScheduler->mutex);
                    visual.curveTicket->result = {request(123), {{123, true}}};
                    visual.curveTicket->completed = true;
                }
                visual.updateSynthCurve();
                require(visual.synthCurve.request == current && panel.responseReady(), "Old B completion replaced current A cache");
            }
        }
    }
    static void profile() {
        using Clock = std::chrono::steady_clock;
        auto owner = std::make_unique<Processor>();
        UtilityEffectPanel xp(*owner, 1, UtilityKind::xpFilter); xp.setSize(500, 400);
        auto measure = [&](const char* name, int count, auto body) {
            const auto start = Clock::now();
            for (int i = 0; i < count; ++i) body(i);
            const auto ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count() / count;
            std::cout << "PROFILE " << name << " mean_ms=" << ms << '\n';
        };
        measure("previous refresh lookup path", 200, [&](int) {
            const auto snapshot = owner->getModulationSnapshot();
            UtilitySettings old;
            for (size_t i = 0; i < utilityParameterCount; ++i) {
                const auto& d = utilityParameters[i];
                old.values[i] = std::clamp(toneui::visualParameter(*owner, utilityParameterId(1, i), d.initial, &snapshot), d.min, d.max);
            }
            require(old[UtilityParameter::xpCutoff] > 0, "Invalid previous refresh profile");
        });
        measure("coherent cached-binding refresh", 200, [&](int) { xp.visual.refresh(); });
        measure("XP 1400 response evaluations only", 200, [&](int) {
            double sum = 0;
            for (int bin = 0; bin <= 1400; ++bin)
                sum += UtilityEffect::response(48000, UtilityKind::xpFilter, 20 * std::pow(1000., double(bin) / 1400), xp.visual.settings).magnitude;
            require(sum > 0, "Invalid XP evaluation profile");
        });
        measure("XP changed software-image paint", 100, [&](int i) { xp.visual.settings[UtilityParameter::xpCutoff] = float(100 + i * 30); render(xp.visual); });
        measure("XP cached software-image paint", 100, [&](int) { render(xp.visual); });
        UtilitySettings settings;
        for (int mode = 0; mode < 13; ++mode) {
            settings[UtilityParameter::synthMode] = float(mode);
            const auto name = "synth full 32768 mode " + std::to_string(mode);
            measure(name.c_str(), 5, [&](int) { const auto curve = UtilityEffect::synthResponseCurve(48000, settings, 32768); require(curve.size() == 16385, "Invalid profile curve"); });
        }
    }
    static void run() { scheduling(); layouts(); controls(); profile(); }
};
} // namespace vocalstack
