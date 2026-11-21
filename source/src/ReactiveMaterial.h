#pragma once

#include "VisualTheme.h"
#include "ReactiveFxVisualModels.h"

namespace vocalstack::reactiveVisual {

// GUI-only shared Compressor/Gate lane. Geometry is rebuilt only for changed
// snapshot content/revision or bounds; colour/theme changes do not rebuild it.
class HistoryField {
public:
    struct Colours { juce::Colour background, grid, trace, text; };
    static juce::Rectangle<float> plotBounds(juce::Rectangle<float> bounds) noexcept {
        return bounds.withTrimmedLeft(28).withTrimmedRight(5)
            .withTrimmedTop(17).withTrimmedBottom(17);
    }
    static float yFor(float db, juce::Rectangle<float> plot) noexcept {
        return plot.getY() + std::clamp(std::isfinite(db) ? db : 0.f, 0.f,
            GainReductionField::maximumDb) / GainReductionField::maximumDb * plot.getHeight();
    }
    size_t geometryBuildCount() const noexcept { return builds; }
    void paint(juce::Graphics& g, juce::Rectangle<float> bounds,
               const GainReductionField& field, Colours colours) {
        const auto plot = plotBounds(bounds);
        if (plot.isEmpty()) return;
        if (!cached || bounds != cachedBounds || field.sequence != previous.sequence
            || field.epoch != previous.epoch || field.nodeId != previous.nodeId
            || field.available != previous.available || field.db != previous.db) {
            cached = true; cachedBounds = bounds; previous = field; ++builds;
            trace.clear(); fill.clear();
            const float step = plot.getWidth() / float(GainReductionField::columns);
            bool connected = false;
            for (size_t i = 0; i < GainReductionField::columns; ++i) {
                const float db = field.db[i];
                if (!field.available || !std::isfinite(db) || db < 0) { connected = false; continue; }
                const float x = plot.getX() + float(i) * step, y = yFor(db, plot);
                if (connected) trace.lineTo(x, y); else trace.startNewSubPath(x, y);
                trace.lineTo(x + step, y); connected = true;
                fill.addRectangle(x, plot.getY(), step, y - plot.getY());
            }
        }
        g.setColour(colours.background); g.fillRect(bounds);
        g.setColour(colours.grid); g.drawRect(bounds.reduced(.5f), 1.f);
        g.setFont(juce::Font(readableFontOptions(9.f)));
        g.setColour(colours.text);
        g.drawText("GR dB | 20 ms peak", bounds.withHeight(15), juce::Justification::centredLeft);
        for (int db : {0, 12, 24, 36}) {
            const float y = yFor(float(db), plot);
            g.setColour(colours.grid.withAlpha(.45f)); g.drawHorizontalLine(int(y), plot.getX(), plot.getRight());
            g.setColour(colours.text);
            g.drawText(juce::String(db), juce::Rectangle<float>(bounds.getX(), y - 6, 24, 12), juce::Justification::centredRight);
        }
        const auto labels = bounds.withY(plot.getBottom() + 2).withHeight(14);
        g.drawText("-4 s", labels.withX(plot.getX()).withWidth(32), juce::Justification::centredLeft);
        g.drawText("NOW", labels.withX(plot.getRight() - 32).withWidth(32), juce::Justification::centredRight);
        if (!field.available) {
            g.drawText("No live GR history", plot, juce::Justification::centred);
            return;
        }
        const juce::Graphics::ScopedSaveState saved(g);
        g.reduceClipRegion(plot.getSmallestIntegerContainer());
        g.setColour(colours.trace.withAlpha(.12f)); g.fillPath(fill);
        g.setColour(colours.trace); g.strokePath(trace, juce::PathStrokeType(1.f));
    }
private:
    GainReductionField previous;
    juce::Rectangle<float> cachedBounds;
    juce::Path trace, fill;
    bool cached = false;
    size_t builds = 0;
};

// GUI-only: the decorative raster stays coarse. Measured geometry is composed
// separately so improving readability never multiplies fluid/FFT processing.
class ReactiveMaterial {
public:
    static constexpr int width = 48, height = 24;
    static constexpr int maxDisplayColumns = 1024;
    struct Colours { juce::Colour base, input, output, baseline; };
    static int displayColumns(float widthInPixels) noexcept {
        return std::clamp(int(std::ceil(widthInPixels / 1.5f)), 1, maxDisplayColumns);
    }

    void paint(juce::Graphics& g, juce::Rectangle<float> bounds,
               juce::Rectangle<float> plot, const WaveformField& field,
               Colours colours, double decorativeSeconds) {
        if (bounds.isEmpty() || plot.isEmpty()) return;
        prepare(colours, decorativeSeconds); draw(g, bounds);
        const juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(plot.getSmallestIntegerContainer());
        const int count = displayColumns(plot.getWidth());
        std::array<WaveformField::Band, maxDisplayColumns> input{}, wet{};
        // Display interpolation only: 128 measured history windows, not new
        // samples. Every original extremum remains a vertex in the envelope.
        for (int x = 0; x < count; ++x) {
            const float position = float(x) / float(std::max(1, count - 1)) * float(WaveformField::columns - 1);
            const auto first = size_t(position), next = std::min(first + 1, WaveformField::columns - 1);
            const float fraction = position - float(first);
            const auto interpolate = [fraction](const WaveformField::Band& a, const WaveformField::Band& b) {
                return WaveformField::Band{a.negative + fraction * (b.negative - a.negative),
                    a.positive + fraction * (b.positive - a.positive), a.rms + fraction * (b.rms - a.rms)};
            };
            auto band = WaveformField::Column{interpolate(field.history[first].input, field.history[next].input),
                interpolate(field.history[first].wet, field.history[next].wet)};
            // Union measured vertices covered by this display cell, including at
            // narrow widths: interpolation must never discard a brief peak.
            const float halfCell = float(WaveformField::columns - 1) / float(std::max(1, count - 1)) * .5f;
            const int begin = count == 1 ? 0 : std::max(0, int(std::ceil(position - halfCell)));
            const int end = count == 1 ? int(WaveformField::columns - 1)
                : std::min(int(WaveformField::columns - 1), int(std::floor(position + halfCell)));
            const auto merge = [](auto& a, const auto& b) {
                a.negative = std::max(a.negative, b.negative); a.positive = std::max(a.positive, b.positive);
                a.rms = std::max(a.rms, b.rms);
            };
            for (int i = begin; i <= end; ++i) {
                merge(band.input, field.history[size_t(i)].input); merge(band.wet, field.history[size_t(i)].wet);
            }
            input[size_t(x)] = band.input; wet[size_t(x)] = band.wet;
        }
        shade(g, plot, input, count, colours.input, .24f);
        shade(g, plot, wet, count, colours.output, .68f);
        g.setColour(colours.baseline.withAlpha(.28f));
        g.drawLine(plot.getX(), plot.getCentreY(), plot.getRight(), plot.getCentreY(), .7f);
    }

    void paintSpectrum(juce::Graphics& g, juce::Rectangle<float> bounds,
                       const SpectrumField& field, juce::Colour base,
                       const std::array<juce::Colour, 3>& colours, double decorativeSeconds) {
        if (bounds.isEmpty()) return;
        prepare({base, colours[0], colours[2], colours[1]}, decorativeSeconds); draw(g, bounds);
        const juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(bounds.getSmallestIntegerContainer());
        // All measured bins are retained. Thin paths interpolate display geometry
        // only; no invented spectral measurements or extra analysis FFT.
        for (size_t s = 0; s < 3; ++s) {
            juce::Path trace;
            for (size_t x = 0; x < SpectrumField::columns; ++x) {
                const float raw = field.levels[s][x];
                const float level = std::isfinite(raw) ? std::clamp(raw, 0.f, 1.f) : 0.f;
                const float px = bounds.getX() + float(x) * (bounds.getWidth() - 1) / float(SpectrumField::columns - 1);
                const float py = bounds.getBottom() - 1 - level * (bounds.getHeight() - 2);
                if (x == 0) trace.startNewSubPath(px, py); else trace.lineTo(px, py);
            }
            auto fill = trace;
            fill.lineTo(bounds.getRight() - 1, bounds.getBottom());
            fill.lineTo(bounds.getX(), bounds.getBottom()); fill.closeSubPath();
            g.setColour(colours[s].withAlpha(.065f)); g.fillPath(fill);
            g.setColour(colours[s].withAlpha(.9f)); g.strokePath(trace, juce::PathStrokeType(1.f));
        }
    }
private:
    static void shade(juce::Graphics& g, juce::Rectangle<float> plot,
                      const std::array<WaveformField::Band, maxDisplayColumns>& bands,
                      int count, juce::Colour tint, float strength) {
        const float centre = plot.getCentreY(), extent = plot.getHeight() * .46f;
        const float step = plot.getWidth() / float(count);
        juce::Path envelope, core, rims;
        envelope.startNewSubPath(plot.getX(), centre);
        core.startNewSubPath(plot.getX(), centre);
        bool positive = false, negative = false;
        for (int x = 0; x < count; ++x) {
            const auto& band = bands[size_t(x)];
            const float left = plot.getX() + float(x) * step, right = left + step;
            const float top = centre - std::ceil(band.positive * extent);
            envelope.lineTo(left, top); envelope.lineTo(right, top);
            const float rmsTop = std::max(top, centre - band.rms * extent);
            core.lineTo(left, rmsTop); core.lineTo(right, rmsTop);
            if (band.positive > 0) {
                if (positive) rims.lineTo(left, top); else rims.startNewSubPath(left, top);
                rims.lineTo(right, top);
            }
            positive = band.positive > 0;
        }
        for (int x = count - 1; x >= 0; --x) {
            const auto& band = bands[size_t(x)];
            const float left = plot.getX() + float(x) * step, right = left + step;
            const float bottom = centre + std::ceil(band.negative * extent);
            envelope.lineTo(right, bottom); envelope.lineTo(left, bottom);
            const float rmsBottom = std::min(bottom, centre + band.rms * extent);
            core.lineTo(right, rmsBottom); core.lineTo(left, rmsBottom);
            if (band.negative > 0) {
                if (negative) rims.lineTo(right, bottom); else rims.startNewSubPath(right, bottom);
                rims.lineTo(left, bottom);
            }
            negative = band.negative > 0;
        }
        envelope.closeSubPath(); core.closeSubPath();
        // A handful of connected paths, not thousands of tiny draw calls.
        g.setColour(tint.withAlpha(strength * .16f)); g.fillPath(envelope);
        g.setColour(tint.withAlpha(strength * .24f)); g.fillPath(core);
        g.setColour(tint.withAlpha(strength)); g.strokePath(rims, juce::PathStrokeType(1.f));
    }
    void prepare(Colours colours, double seconds) {
        if (image.isNull()) image = juce::Image(juce::Image::RGB, width, height, false);
        juce::Graphics raster(image);
        background.paint(raster, {0, 0, width, height}, colours.base, colours.input, seconds, .25f, width);
    }
    void draw(juce::Graphics& g, juce::Rectangle<float> bounds) {
        juce::Graphics::ScopedSaveState state(g);
        g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);
        g.drawImage(image, bounds);
    }
    FluidBackground background;
    juce::Image image;
};

} // namespace vocalstack::reactiveVisual
