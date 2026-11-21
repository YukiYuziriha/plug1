#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "ReactiveFxVisualModels.h"
#include "VisualTheme.h"

namespace vocalstack {
// Independent coordinates share one clipped surface, never a time/input axis.
class CompressorPlot {
public:
    struct Colours { juce::Colour background, border, input, output, transfer, gr, text; };
    struct Model { float threshold, ratio, knee, makeup, mix; bool enabled; };
    static juce::Rectangle<float> plotBounds(juce::Rectangle<float> bounds) {
        return bounds.withTrimmedTop(32).withTrimmedBottom(36).withTrimmedLeft(34).withTrimmedRight(32);
    }
    static float levelY(float db, juce::Rectangle<float> r) {
        return r.getBottom() - std::clamp((db + 72.f) / 72.f, 0.f, 1.f) * r.getHeight();
    }
    static float timeX(float ageSeconds, juce::Rectangle<float> r) {
        return r.getRight() - ageSeconds / 4.f * r.getWidth();
    }
    static float grY(float db, juce::Rectangle<float> r) {
        return r.getY() + std::clamp(db / 36.f, 0.f, 1.f) * r.getHeight();
    }
    static float rmsDb(float rms) {
        return std::isfinite(rms) && rms > 0 ? std::max(-72.f, 20.f * std::log10(rms)) : -72.f;
    }
    static void label(juce::Graphics& g, juce::Rectangle<float> r, const juce::String& s, juce::Colour c,
                      juce::Justification align = juce::Justification::centredLeft) {
        g.setColour(c); g.setFont(juce::Font(pixelFontOptions(12.f))); g.drawText(s, r, align, false);
    }
    void paint(juce::Graphics& g, juce::Rectangle<float> bounds, const FxMonitorFrame& frame,
               const reactiveVisual::GainReductionField& gr, bool active, Model model, Colours c) {
        const auto r = plotBounds(bounds);
        if (r.isEmpty()) return;
        g.setColour(c.background); g.fillRect(r);
        label(g, {bounds.getX(), bounds.getY(), bounds.getWidth()-52, 15},
                      bounds.getWidth()<290 ? "dBFS / RMS" : "Output dBFS / RMS", c.text);
                label(g, {bounds.getRight()-48, bounds.getY(), 48, 15}, "GR dB", c.gr, juce::Justification::centredRight);
        const float cell = bounds.getWidth() / 4.f;
        for (int i = 0; i < 4; ++i) {
            const auto colour = i == 0 ? c.input : i == 1 ? c.output : i == 2 ? c.transfer : c.gr;
            const float x = bounds.getX() + i * cell;
            g.setColour(colour); g.drawLine(x, bounds.getY()+23, x+6, bounds.getY()+23, 1.f);
            label(g, {x+9, bounds.getY()+16, cell-9, 15}, i==0 ? "In" : i==1 ? "Wet" : i==2 ? (cell < 45 ? "Crv" : "Curve") : "GR", colour);
        }
        {
            juce::Graphics::ScopedSaveState saved(g);
            g.reduceClipRegion(r.getSmallestIntegerContainer());
            for (int db : {-72, -54, -36, -18, 0}) {
                const float y = levelY(float(db), r), x = r.getX() + (float(db)+72.f)/72.f*r.getWidth();
                g.setColour(c.border.withAlpha(.35f));
                g.drawHorizontalLine(int(y), r.getX(), r.getRight());
                g.drawVerticalLine(int(x), r.getY(), r.getBottom());
            }
            // Measured 20 ms stereo-power bins occupy their true ages within 4 s.
            // Only 2.56 s is retained by this producer; no resampling/stretching.
            if (active && frame.hasWet && frame.sampleRate > 0 && frame.historyBinSamples > 0) {
                const float seconds = float(frame.historyBinSamples / frame.sampleRate);
                for (int wet = 0; wet < 2; ++wet) {
                    juce::Path trace;
                    bool connected = false;
                    const size_t used = std::min(frame.historyUsed, frame.history.size());
                    for (size_t i = frame.history.size()-used; i < frame.history.size(); ++i) {
                        const float rms = wet ? frame.history[i].wetRms : frame.history[i].inputRms;
                        if (!std::isfinite(rms) || rms < 0) { connected=false; continue; }
                        const float x = timeX(float(frame.history.size()-i)*seconds, r);
                        const float end = timeX(float(frame.history.size()-i-1)*seconds, r);
                        const float y = levelY(rmsDb(rms), r);
                        g.setColour((wet ? c.output : c.input).withAlpha(wet ? .12f : .055f));
                        g.fillRect(juce::Rectangle<float>(x,y,std::max(.5f,end-x),r.getBottom()-y));
                        if (connected) trace.lineTo(x,y); else trace.startNewSubPath(x,y);
                        trace.lineTo(end,y); connected=true;
                    }
                    g.setColour((wet ? c.output : c.input).withAlpha(wet ? .7f : .35f));
                    g.strokePath(trace,juce::PathStrokeType(wet ? 1.f : .8f));
                }
            }
            juce::Path unity, transfer, reduction;
            unity.startNewSubPath(r.getBottomLeft()); unity.lineTo(r.getTopRight());
            g.setColour(c.text.withAlpha(.18f)); g.strokePath(unity,juce::PathStrokeType(.6f));
            for (int i=0;i<=256;++i) {
                const float in=-72.f+72.f*float(i)/256.f, over=in-model.threshold;
                const float slope=1.f-1.f/std::max(1.f,model.ratio);
                const float attenuation=model.knee>0 && over>-model.knee*.5f && over<model.knee*.5f
                    ? slope*std::pow(over+model.knee*.5f,2.f)/(2.f*model.knee) : slope*std::max(0.f,over);
                const float wet=model.enabled ? in-attenuation+model.makeup : in;
                const float out=in+20.f*std::log10((1.f-model.mix)+model.mix*std::pow(10.f,(wet-in)/20.f));
                const float x=r.getX()+float(i)/256.f*r.getWidth(), y=levelY(out,r);
                if(i==0) transfer.startNewSubPath(x,y); else transfer.lineTo(x,y);
            }
            // A quiet backdrop stroke keeps a thin transfer readable at crossings.
            g.setColour(c.background.withAlpha(.85f)); g.strokePath(transfer,juce::PathStrokeType(3.f));
            g.setColour(c.transfer); g.strokePath(transfer,juce::PathStrokeType(1.f));
            bool connected=false;
            if(active && gr.available) for(size_t i=0;i<gr.db.size();++i) {
                if(!std::isfinite(gr.db[i]) || gr.db[i]<0) { connected=false; continue; }
                const float x=timeX(float(gr.db.size()-i)*.020f,r), y=grY(gr.db[i],r);
                if(connected) reduction.lineTo(x,y); else reduction.startNewSubPath(x,y);
                reduction.lineTo(timeX(float(gr.db.size()-i-1)*.020f,r),y); connected=true;
            }
            // Dashed GR also distinguishes coincident paths without relying on hue.
            juce::Path dashed; const float dashes[]{3.f,2.f};
            juce::PathStrokeType(1.f).createDashedStroke(dashed,reduction,dashes,2);

            g.setColour(c.gr); g.fillPath(dashed);
        }
        g.setColour(c.border); g.drawRect(r,.7f);
        for(int db : {-72,-36,0}) {
            const float y=std::clamp(levelY(float(db),r)-7.f,r.getY(),r.getBottom()-14.f);
            label(g,{bounds.getX(),y,31,14},juce::String(db),c.text,juce::Justification::centredRight);
        }
        label(g,{r.getRight()+3,r.getY(),29,14},"0",c.gr);
        label(g,{r.getRight()+3,r.getCentreY()-7,29,14},"18",c.gr);
        label(g,{r.getRight()+3,r.getBottom()-14,29,14},"36",c.gr);
        label(g,{r.getX(),r.getBottom()+2,r.getWidth(),15},r.getWidth()<180 ? "-72 In dBFS" : "-72     Input dBFS",c.transfer);
        label(g,{r.getX(),r.getBottom()+2,r.getWidth(),15},"0",c.transfer,juce::Justification::centredRight);
        label(g,{r.getX(),r.getBottom()+19,r.getWidth(),15},r.getWidth()<180 ? "-4 s" : "-4 s  History",c.gr);
        label(g,{r.getX(),r.getBottom()+19,r.getWidth(),15},"now",c.gr,juce::Justification::centredRight);
    }
};
}
