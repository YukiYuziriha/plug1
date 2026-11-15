#pragma once
#include "PluginProcessor.h"
#include "ParameterKnob.h"
#include "ModulationUI.h"
#include "ReactiveMaterial.h"
#include <functional>

namespace vocalstack {
class SpectralMorphPanel final : public juce::Component, public juce::SettableTooltipClient, private juce::Timer {
public:
    SpectralMorphPanel(Processor& processor, size_t fxSlot) : owner(processor), slot(fxSlot) {
        setComponentID("spectralMorphPanel");
        for (size_t field : {size_t(0), size_t(3), size_t(5)}) {
            const auto& d = spectralMorphParameters[field];
            auto knob = std::make_unique<ParameterKnob>(owner, spectralMorphParameterId(slot, field), d.label, d.unit);
            knob->slider.getProperties().set("vocalstackInspectorRotary", true);
            controlsContent.addAndMakeVisible(*knob); knobs.push_back(std::move(knob));
        }
        auto mix = std::make_unique<ParameterKnob>(owner, fxParameterId(slot, "Mix"), "Mix", "%");
        mix->slider.getProperties().set("vocalstackInspectorRotary", true);
                controlsContent.addAndMakeVisible(*mix); knobs.push_back(std::move(mix));
        mode.addItem("Magnitude", 1); mode.addItem("Envelope", 2);
        fft.addItem("1024", 1); fft.addItem("2048", 2); fft.addItem("4096", 3);
        mode.setTooltip("Mode: magnitude transfer / smoothed spectral envelope transfer; main phase is retained");
        fft.setTooltip("FFT size; all resolutions have the same 4096-sample latency");
        freeze.setButtonText("Freeze donor"); freeze.setClickingTogglesState(true);
        freeze.setTooltip("Capture next available donor analysis; hold through disconnect; release returns to live donor. Captured audio is not saved.");
        open.setButtonText("Open Routing"); open.onClick = [this] { if (onOpenRouting) onOpenRouting(); };
        for (auto* c : std::array<juce::Component*, 4>{&mode, &fft, &freeze, &open}) addAndMakeVisible(c);
        modeLink = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(owner.parameters, spectralMorphParameterId(slot, 1), mode);
        fftLink = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(owner.parameters, spectralMorphParameterId(slot, 2), fft);
        freezeLink = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(owner.parameters, spectralMorphParameterId(slot, 4), freeze);
        controlsViewport.setComponentID("spectralMorphControlsViewport");
        controlsContent.setComponentID("spectralMorphControlsContent");
        controlsViewport.setViewedComponent(&controlsContent, false);
                controlsViewport.setScrollBarsShown(true, false);
                addAndMakeVisible(controlsViewport);
                detail::passiveControl(*this); startTimerHz(30);
    }
    ~SpectralMorphPanel() override { stopTimer(); }
    std::function<void()> onOpenRouting;
    void setNeon(bool value) { neon = value; for (auto& k : knobs) k->setNeon(value); repaint(); }
    void setInspectorLayout(bool value) { inspectorLayout = value; resized(); }
    static bool usesWideLayout(int width, bool inspector) { return !inspector && width >= 680; }
    bool usesSideBySideLayout() const { return usesWideLayout(getWidth(), inspectorLayout); }
    juce::Rectangle<int> spectrumBounds() const { return plot; }
    static std::pair<juce::String, juce::String> axisLabels(int width, double sampleRate) {
        const juce::Font font(pixelFontOptions(12.f));
        const auto fits = [&](const juce::String& text) { return juce::GlyphArrangement::getStringWidth(font, text) <= width; };
        const juce::String level = "Peak bins -60..0 dBFS";
        const juce::String frequency = sampleRate > 0 ? "Log Hz: 20 to " + juce::String(int(sampleRate / 2)) : "Waiting for analysis";
        return {fits(level) ? level : juce::String("-60..0 dBFS"), fits(frequency) ? frequency : juce::String("20 Hz..Nyquist")};
    }
    void revealParameter(const juce::String&) {}
    void refresh() {
        const auto graph = owner.getRoutingGraph(); donorWired = false;
        const uint32_t id = uint32_t(99 + slot);
        for (const auto& l : graph.links) if (l.destination == id && l.inputPin == 1) donorWired = true;
        SpectralMorphFrame next;
        const bool valid = owner.readSpectralMorph(slot, next) && next.nodeId == id;
        const auto now = juce::Time::getMillisecondCounterHiRes();
        if (valid && (next.spectrum.revision != revision || next.spectrum.fftSize != frame.fftSize)) {
            revision = next.spectrum.revision; lastUpdate = now; frame = next.spectrum;
        }
        if (!valid || now - lastUpdate > 250) {
            const double analysisRate = frame.sampleRate;
            frame = {}; frame.sampleRate = analysisRate;
        }
        repaint();
    }
    void resized() override {
        auto area = getLocalBounds().reduced(6);
        auto bottom = area.removeFromBottom(24); open.setBounds(bottom.removeFromRight(108));
        juce::Rectangle<int> visual;
        if (usesSideBySideLayout()) {
            const int visualWidth = juce::jlimit(260, 420, area.getWidth() * 2 / 5);
            visual = area.removeFromLeft(visualWidth);
            area.removeFromLeft(12);
        }
        auto top = area.removeFromTop(28);
        mode.setBounds(top.removeFromLeft(std::max(60, top.getWidth() / 3)).reduced(2));
        fft.setBounds(top.removeFromLeft(std::max(60, top.getWidth() / 2)).reduced(2));
        freeze.setBounds(top.reduced(2));
        const int availableWidth = std::max(1, area.getWidth() - controlsViewport.getScrollBarThickness());
        const int columns = std::clamp(availableWidth / 88, 1, 4);
        const int rows = (4 + columns - 1) / columns;
        auto controls = usesSideBySideLayout() ? area
            : area.removeFromBottom(std::min(rows * 138, std::max(0, area.getHeight() - 106)));
        controlsViewport.setBounds(controls);
        controlsContent.setSize(availableWidth, rows * 138);
        const int width = availableWidth / columns;
        for (size_t i = 0; i < knobs.size(); ++i)
            knobs[i]->setBounds(int(i) % columns * width, int(i) / columns * 138, width, 138);
        // Two explicit axis rows keep technical units readable in narrow inspectors.
                plot = (usesSideBySideLayout() ? visual : area).withTrimmedTop(18).withTrimmedBottom(32);
    }
    void paint(juce::Graphics& g) override {
        const auto p = themePalette(*this, neon);
        g.fillAll(p.panel);
        reactiveVisual::SpectrumField field;
        for (size_t col = 0; col < reactiveVisual::SpectrumField::columns; ++col) {
            field.set(0, col, frame.main[col]);
            field.set(1, col, frame.donor[col]);
            field.set(2, col, frame.result[col]);
        }
        material.paintSpectrum(g, plot.toFloat(), field, p.inset, {p.accent, p.secondary, p.text}, fluidAnimationSeconds(*this));
        g.setFont(juce::Font(pixelFontOptions(12.f)));
        const int legendY = plot.getY() - 17;
        const int legendWidth = std::min(145, plot.getWidth() / 3);
        g.setColour(p.accent); g.drawText("MAIN", plot.getX(), legendY, legendWidth, 16, juce::Justification::left);
        g.setColour(p.secondary); g.drawText("DONOR", plot.getX() + legendWidth, legendY, legendWidth, 16, juce::Justification::left);
        g.setColour(p.text); g.drawText(plot.getWidth() < 320 ? "RESULT" : "RESULT / pre Mix", plot.getX() + 2 * legendWidth, legendY, std::max(0, plot.getWidth() - 2 * legendWidth), 16, juce::Justification::left);
        g.setColour(p.muted);
        const auto axes = axisLabels(plot.getWidth(), frame.sampleRate);
        g.drawText(axes.first, plot.getX(), plot.getBottom(), plot.getWidth(), 16, juce::Justification::left);
        g.drawText(axes.second, plot.getX(), plot.getBottom() + 16, plot.getWidth(), 16, juce::Justification::left);
        juce::String status = !donorWired ? "DONOR UNROUTED" : frame.capturePending ? "CAPTURE WAITING" : frame.frozen ? "DONOR FROZEN" : !frame.donorConnected ? "DONOR ROUTED / UNAVAILABLE" : "DONOR ROUTED / LIVE";
        g.setColour(!donorWired ? p.secondary : p.text);
        const int statusWidth = std::max(0, getWidth() - 125);
                const juce::Font statusFont(pixelFontOptions(12.f));
                const auto shortStatus = !donorWired ? "UNROUTED" : frame.capturePending ? "WAITING" : frame.frozen ? "FROZEN" : !frame.donorConnected ? "NO DATA" : "LIVE";
                g.drawText(juce::GlyphArrangement::getStringWidth(statusFont, status) <= statusWidth ? status : juce::String(shortStatus),
                           6, getHeight() - 29, statusWidth, 24, juce::Justification::left);
                setTooltip("MAIN / DONOR / RESULT: measured peak bins, fixed -60..0 dBFS; logarithmic frequency 20 Hz to Nyquist. Result is before Mix. " + status);
        setTooltipText();
    }
private:
    void setTooltipText() {
        open.setTooltip("Choose independent MAIN and DONOR pins. Host sidechain is "
            + juce::String(owner.externalSidechainChannels() ? "configured; cable connectivity is unknown (silence is not a disconnect detector)." : "disabled; it supplies silence."));
    }
    void timerCallback() override { if (isShowing()) refresh(); }
    Processor& owner; size_t slot; bool neon = false, donorWired = false, inspectorLayout = false;
    uint64_t revision = 0; double lastUpdate = 0;
    SpectralMorphSnapshot frame;
    reactiveVisual::ReactiveMaterial material;
    juce::Rectangle<int> plot;
    juce::Component controlsContent;
        PassiveViewport controlsViewport;
        std::vector<std::unique_ptr<ParameterKnob>> knobs;
    juce::ComboBox mode, fft;
    juce::ToggleButton freeze; juce::TextButton open;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeLink, fftLink;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> freezeLink;
};
}
