#include "MeterPanel.h"
#include <algorithm>
#include <cmath>

namespace vocalstack {
namespace {
using namespace juce;

Colour rgb(uint32_t hex) { return Colour(0xff000000u | hex); }

void text(Graphics& g, const String& value, Rectangle<float> area, Colour colour,
          float size, bool bold = false) {
    g.setFont(Font(FontOptions(size, bold ? Font::bold : Font::plain)));
    g.setColour(colour);
    g.drawText(value, area, Justification::centred, false);
}

constexpr float floorDb = -60.0f, ceilingDb = 6.0f;
constexpr std::array<int, 9> scaleDb{-60, -48, -36, -24, -18, -12, -6, 0, 6};

float sampleDb(float gain) {
    return std::isfinite(gain) && gain > 0.0f
        ? std::max(-160.0f, 20.0f * std::log10(gain)) : -160.0f;
}

String signedDb(float db) {
    if (std::abs(db) < 0.05f)
        db = 0.0f;
    return (db >= 0.0f ? "+" : "") + String(db, 1);
}

String maximumText(float peak) {
    return "MAX " + (peak > 0.0f && std::isfinite(peak) ? signedDb(sampleDb(peak)) : String("-inf"));
}
}

MeterPanel::MeterPanel() {
    setName("Input and output sample meters");
    setTitle("Sample meters; click to clear peaks and clips");
    setTooltip("All meters are dBFS. RMS fill = signal energy; white PK = sample peak; "
               "amber HOLD = DSP-supplied 1.5 s sample-peak hold.\n"
               "MAX = highest sample peak in the active channels since reset. "
               "Red CLIP = latched sample overload. MONO uses one M lane, stereo uses L/R.\n"
               "LVL = leveler gain correction, not the Output trim.\n"
               "Click anywhere here to request clearing "
               "held peaks, maximum peaks and clip latches. Live RMS/PK continue.");
    setOpaque(true);
    setMouseCursor(MouseCursor::PointingHandCursor);
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
}

void MeterPanel::setMeters(const Meters& snapshot) {
    meters = snapshot;
    repaint();
}

void MeterPanel::setNeon(bool enabled) {
    if (neon != enabled) {
        neon = enabled;
        repaint();
    }
}

void MeterPanel::mouseDown(const MouseEvent& event) {
    if (event.mods.isLeftButtonDown() && onReset)
        onReset();
}


void MeterPanel::paint(Graphics& g) {
    g.fillAll(rgb(neon ? 0x090d17 : 0x292b2a));
    const auto background = rgb(neon ? 0x111827 : 0x252826);
    const auto border = rgb(neon ? 0x2b3a53 : 0x50564f);
    const auto foreground = rgb(neon ? 0xf1f5ff : 0xf4f4ea);
    const auto muted = rgb(neon ? 0x9eafc8 : 0xbabdb3);
    const auto good = rgb(neon ? 0x64f5e3 : 0x9ac37d);
    const auto holdColour = rgb(neon ? 0xf8de6b : 0xf5a24b);
    const auto overload = rgb(0xff726e);
    const auto track = rgb(neon ? 0x090d17 : 0x171b18);
    const float radius = neon ? 0.0f : 5.0f;

    // One geometry for both skins. The editor supplies a 244 x 246 panel.
    Graphics::ScopedSaveState state(g);
    g.addTransform(AffineTransform::scale(static_cast<float>(getWidth()) / 244.0f,
                                         static_cast<float>(getHeight()) / 246.0f));
    const Rectangle<float> frame{0.5f, 0.5f, 243.0f, 245.0f};
    g.setColour(background);
    g.fillRoundedRectangle(frame, radius);
    g.setColour(hasKeyboardFocus(false) ? good : border);
    g.drawRoundedRectangle(frame, radius, hasKeyboardFocus(false) ? 2.0f : 1.0f);
    g.setColour(good);
    g.fillRect(6.0f, 1.0f, 232.0f, 2.0f);

    constexpr float top = 66.0f, bottom = 190.0f;
    const auto yForDb = [](float db) {
        return bottom - (jlimit(floorDb, ceilingDb, db) - floorDb)
                        / (ceilingDb - floorDb) * (bottom - top);
    };
    text(g, "SAMPLE", {99, 8, 46, 14}, muted, 9.5f, true);
    text(g, "dBFS", {99, 25, 46, 15}, foreground, 11.0f, true);

    for (const int db : scaleDb) {
        const auto y = yForDb(static_cast<float>(db));
        g.setColour(db == 0 ? muted.withAlpha(0.65f) : border);
        for (const float x : {18.0f, 158.0f})
            g.drawHorizontalLine(roundToInt(y), x, x + 68.0f);
        text(g, db > 0 ? "+" + String(db) : String(db), {102, y - 5.0f, 40, 10},
             db >= 0 ? holdColour : muted, 9.5f, db == 0);
    }

    const auto drawGroup = [&](const std::array<MeterReading, 2>& readings,
                               int channelCount, float x, const String& name) {
        const int channels = jlimit(0, 2, channelCount);
        float maximum = 0.0f;
        bool clipped = false;
        for (int i = 0; i < channels; ++i) {
            const auto& reading = readings[static_cast<size_t>(i)];
            if (std::isfinite(reading.maxPeak))
                maximum = std::max(maximum, reading.maxPeak);
            clipped = clipped || reading.clipped;
        }
        text(g, name + (channels == 1 ? " / MONO" : channels == 2 ? " / STEREO" : " / OFF"),
             {x, 8, 88, 16}, foreground, 11.0f, true);
        text(g, channels == 0 ? String("MAX --") : maximumText(maximum),
             {x, 27, 88, 16}, clipped ? overload : foreground, 11.0f, true);

        for (int i = 0; i < channels; ++i) {
            const auto& reading = readings[static_cast<size_t>(i)];
            const float laneX = x + (channels == 1 ? 35.0f : 18.0f + 34.0f * i);
            text(g, channels == 1 ? "M" : i == 0 ? "L" : "R",
                 {laneX - 3, 45, 24, 15}, muted, 10.0f, true);
            const Rectangle<float> lane{laneX, top, 18.0f, bottom - top};
            g.setColour(track);
            g.fillRect(lane);

            const auto rmsDb = sampleDb(reading.rms);
            const auto rmsY = yForDb(rmsDb);
            g.setColour(good);
            g.fillRect(Rectangle<float>{laneX, rmsY, 18.0f, bottom - rmsY});
            if (rmsDb > -12.0f) {
                g.setColour(holdColour);
                g.fillRect(Rectangle<float>{laneX, rmsY, 18.0f, yForDb(-12.0f) - rmsY});
            }
            if (rmsDb > 0.0f) {
                g.setColour(overload);
                g.fillRect(Rectangle<float>{laneX, rmsY, 18.0f, yForDb(0.0f) - rmsY});
            }
            // Keep calibration visible over both the track and the RMS fill.
            for (const int db : scaleDb) {
                g.setColour(db == 0 ? foreground.withAlpha(0.6f) : background.withAlpha(0.65f));
                g.drawHorizontalLine(roundToInt(yForDb(static_cast<float>(db))), laneX, laneX + 18);
            }
            if (std::isfinite(reading.heldPeak) && reading.heldPeak > 0.0f) {
                g.setColour(holdColour);
                g.drawLine(laneX - 4, yForDb(sampleDb(reading.heldPeak)),
                           laneX + 22, yForDb(sampleDb(reading.heldPeak)), 2.0f);
            }
            if (std::isfinite(reading.peak) && reading.peak > 0.0f) {
                g.setColour(foreground);
                g.drawLine(laneX - 1, yForDb(sampleDb(reading.peak)),
                           laneX + 19, yForDb(sampleDb(reading.peak)), 1.4f);
            }
        }

        const Rectangle<float> clipBadge{x + 20, 197, 48, 14};
        g.setColour(clipped ? overload : track);
        g.fillRoundedRectangle(clipBadge, 2.0f);
        text(g, "CLIP", clipBadge, clipped ? rgb(0x17171c) : muted, 10.0f, true);
    };
    drawGroup(meters.input, meters.inputChannels, 8.0f, "IN");
    drawGroup(meters.output, meters.outputChannels, 148.0f, "OUT");
    text(g, "CLEAR", {99, 197, 46, 14}, muted, 9.5f, true);

    g.setColour(good);
    g.fillRect(13.0f, 218.0f, 8.0f, 8.0f);
    text(g, "RMS", {24, 215, 30, 14}, muted, 10.0f);
    g.setColour(foreground);
    g.drawLine(65.0f, 222.0f, 76.0f, 222.0f, 1.4f);
    text(g, "PK", {80, 215, 21, 14}, muted, 10.0f);
    g.setColour(holdColour);
    g.drawLine(114.0f, 222.0f, 130.0f, 222.0f, 2.0f);
    text(g, "HOLD 1.5s", {133, 215, 97, 14}, muted, 10.0f);

    g.setColour(border);
    g.drawHorizontalLine(231, 8.0f, 236.0f);
    const auto correction = std::isfinite(meters.correctionDb) ? meters.correctionDb : 0.0f;
    text(g, "LVL " + signedDb(correction) + " dB  /  leveler", {8, 232, 228, 13},
         foreground, 10.5f, true);
}

} // namespace vocalstack
