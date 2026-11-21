#pragma once

#include "../src/ReactiveMaterial.h"
#include "../src/CreativeEffects.h"
#include "../src/CompressorPlot.h"
#include "ToneUiTests.h"
#include "GuiTestDisplay.h"
#include <limits>
#include <stdexcept>

namespace vocalstack::compressorHistoryTests {
inline void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
// Fixture deliberately does not claim production telemetry exists.
struct Frame {
    std::array<float, 200> gainReductionHistory{};
    size_t gainReductionHistoryUsed = 0, gainReductionHistoryBinSamples = 960;
    float gainReductionDb = 0;
    bool hasGainReduction = true;
    double sampleRate = 48000;
    uint64_t sequence = 1, epoch = 1;
    uint32_t nodeId = 7;
};
inline void measuredMappingAndScale() {
    using reactiveVisual::GainReductionField;
    using reactiveVisual::HistoryField;
    Frame frame;
    frame.gainReductionHistoryUsed = 2;
    frame.gainReductionHistory[198] = 12;
    frame.gainReductionHistory[199] = 48;
    frame.gainReductionDb = 48;
    const auto field = GainReductionField::from(frame, true);
    require(field.available && field.db[197] < 0 && field.db[198] == 12 && field.db[199] == 48,
            "GR history must preserve right alignment, dB units and unclipped measurements");
    require(field.currentDb == 48, "Numeric GR must not clip at the graph ceiling");
    const juce::Rectangle<float> plot(0, 10, 200, 90);
    require(HistoryField::yFor(0, plot) == 10 && HistoryField::yFor(36, plot) == 100
            && HistoryField::yFor(12, plot) == 40 && HistoryField::yFor(48, plot) == 100,
            "GR must grow downward on a fixed 0..36 dB scale");
    require(!GainReductionField::from(frame, false).available, "Bypass/stale data retained GR");
    frame.hasGainReduction = false;
    require(!GainReductionField::from(frame, true).available, "Invalid producer accepted");
    frame.hasGainReduction = true;
    frame.gainReductionHistory[199] = std::numeric_limits<float>::quiet_NaN();
    require(GainReductionField::from(frame, true).db[199] < 0, "Nonfinite GR became measured unity");
    frame.gainReductionHistoryBinSamples = 1920;
    require(!GainReductionField::from(frame, true).available, "Wrong timebase labelled as four seconds");
    for (double rate : {44100., 48000., 96000.}) {
        frame.sampleRate = rate;
        frame.gainReductionHistoryBinSamples = size_t(std::round(rate * .020));
        require(GainReductionField::from(frame, true).available, "Supported sample-rate timebase rejected");
    }
}
inline bool samePixels(const juce::Image& a, const juce::Image& b) {
    if (a.getBounds() != b.getBounds()) return false;
    for (int y = 0; y < a.getHeight(); ++y) for (int x = 0; x < a.getWidth(); ++x)
        if (a.getPixelAt(x, y) != b.getPixelAt(x, y)) return false;
    return true;
}
// Pure offscreen composition; no native-window/second-monitor validation claim.
inline void sharedPixelsAndCache() {
    using namespace reactiveVisual;
    HistoryField compressor, gate;
    Frame frame; frame.gainReductionHistoryUsed = 200;
    for (size_t i = 0; i < 200; ++i) frame.gainReductionHistory[i] = float(i % 37);
    auto field = GainReductionField::from(frame, true);
    const HistoryField::Colours colours{juce::Colour(0xff141a20), juce::Colour(0xff384650),
                                        juce::Colour(0xff54d6c1), juce::Colour(0xff81909a)};
    const auto render = [&](HistoryField& renderer, const GainReductionField& data, int width = 420) {
        juce::Image image(juce::Image::RGB, width, 100, true); juce::Graphics g(image);
        renderer.paint(g, {0, 0, float(width), 100}, data, colours); return image;
    };
    const auto first = render(compressor, field);
    require(samePixels(first, render(gate, field)), "Shared Compressor/Gate renderer differs in pixels");
    require(samePixels(first, render(compressor, field)) && compressor.geometryBuildCount() == 1,
            "Repeated paint rebuilt geometry or invented history motion");
    field.sequence++;
    render(compressor, field);
    require(compressor.geometryBuildCount() == 2, "Telemetry revision did not invalidate cache");
    render(compressor, field, 300);
    require(compressor.geometryBuildCount() == 3, "Resize did not invalidate cache");
    field.epoch++; render(compressor, field);
    require(compressor.geometryBuildCount() == 4, "Reset epoch did not invalidate cache");
    const auto empty = render(compressor, {});
    require(!samePixels(first, empty), "Cleared GR lane retained measured attenuation");
}
inline void actualDspSnapshotBridge() {
    using reactiveVisual::GainReductionField;
    require(GainReductionField::HasHistory<FxMonitorFrame>::value,
            "INTEGRATION REQUIRED: FxMonitorFrame has no measured GR history contract");
    CreativeSettings settings;
    using P = CreativeParameter;
    settings[P::compressorThreshold] = -24;
    settings[P::compressorRatio] = 4;
    settings[P::compressorMakeup] = 0;
    CreativeEffect fx; fx.prepare(48000, CreativeKind::compressor);
    FxMonitor monitor; monitor.reset(48000); monitor.retain(0);
    float peak = 0;
    for (int i = 0; i < 48000; ++i) {
        const float input = .5f * std::sin(float(i) * .1308996939f);
        const auto output = fx.process(input, -input, settings);
        const float measured = fx.gainReductionDb();
        peak = std::max(peak, measured);
        monitor.beginSample(); monitor.setGainReduction(0, measured);
        monitor.capture(0, 7, input, -input, output[0], output[1], 0, {}, true, output[0], output[1], -1, true);
    }
    FxMonitorFrame snapshot;
    require(monitor.read(0, snapshot), "No actual DSP monitor snapshot");
    const auto field = GainReductionField::from(snapshot, true);
    require(field.available && field.currentDb > 1, "Actual DSP GR did not reach snapshot consumer");
    float historyPeak = 0;
    for (float db : field.db) historyPeak = std::max(historyPeak, db);
    require(historyPeak > 1 && historyPeak <= peak + .001f,
            "Snapshot history missing actual gain reduction or exceeds measured DSP peak");
    require(snapshot.hasWet && snapshot.history.back().inputRms > snapshot.history.back().wetRms,
            "Measured PCM input/wet energy did not reach compressor history");
    for (int i=0;i<288000;++i) {
        const auto output=fx.process(0,0,settings);
        monitor.beginSample(); monitor.setGainReduction(0,fx.gainReductionDb());
        monitor.capture(0,7,0,0,output[0],output[1],0,{},true,output[0],output[1],-1,true);
    }
    require(monitor.read(0,snapshot) && snapshot.history.back().inputRms==0 && snapshot.history.back().wetRms==0,
            "Silence retained measured energy");
    require(reactiveVisual::GainReductionField::from(snapshot,true).currentDb<.01f,"Silence retained GR");
    monitor.reset(48000);
    require(!monitor.read(0, snapshot), "Reset exposed old measured GR");
    monitor.release(0);
}
inline void unifiedPlot() {
    // Offscreen pixel invariants, not golden screenshots or desktop FPS.
    const CompressorPlot::Colours colours{juce::Colour(0xff151b23), juce::Colour(0xff46505c),
        juce::Colour(0xff8d9eaa), juce::Colour(0xff50dfb9), juce::Colour(0xffeff7ff),
        juce::Colour(0xffffc36a), juce::Colour(0xff9faab8)};
    CompressorPlot renderer;
    FxMonitorFrame frame; frame.hasWet=true; frame.historyUsed=128;
    frame.hasGainReduction=true; frame.gainReductionHistoryUsed=200;
    for(size_t i=0;i<128;++i) { frame.history[i].inputRms=.3f; frame.history[i].wetRms=.05f; }
    for(size_t i=0;i<200;++i) frame.gainReductionHistory[i]=float(i%28);
    const auto gr=reactiveVisual::GainReductionField::from(frame,true);
    for(int width : {158,174,220,360,700}) {
        const juce::Rectangle<float> bounds(7,9,float(width),190);
        const auto plot=CompressorPlot::plotBounds(bounds);
        require(bounds.contains(plot) && plot.getHeight()>100, "Unified plot split into lanes or escaped frame");
        require(plot == bounds.withTrimmedTop(32).withTrimmedBottom(36).withTrimmedLeft(34).withTrimmedRight(32),
                "Unified plot lost its enlarged axis/legend reservations");
        require(CompressorPlot::levelY(-72,plot)==plot.getBottom() && CompressorPlot::levelY(0,plot)==plot.getY(), "Level scale is not fixed -72..0 dBFS");
        require(CompressorPlot::timeX(4,plot)==plot.getX() && CompressorPlot::timeX(0,plot)==plot.getRight(), "History is not fixed four seconds");
        require(CompressorPlot::timeX(2.56f,plot)>plot.getX()+plot.getWidth()*.35f, "PCM history stretched to four seconds");
        require(CompressorPlot::grY(0,plot)==plot.getY() && CompressorPlot::grY(36,plot)==plot.getBottom(), "GR secondary axis not fixed downward");
        const auto render=[&](bool active, bool energy, bool reduction, float makeup=0.f) {
            juce::Image image(juce::Image::RGB,width+20,210,true); juce::Graphics g(image);
            auto data=frame; data.hasWet=energy;
            renderer.paint(g,bounds,data,reduction ? gr : reactiveVisual::GainReductionField{},active,
                {-24,4,6,makeup,1,true},colours); return image;
        };
        const auto idle=render(false,true,true), live=render(true,true,true);
        require(!samePixels(idle,live), "Measured histories did not update pixels");
        require(!samePixels(live,render(true,false,true)), "Energy background is absent or derived only from GR");
        require(!samePixels(live,render(true,true,false)), "Actual GR overlay missing");
        require(!samePixels(live,render(true,true,true,12)), "Static parameter curve missing");
        require(samePixels(idle,render(false,false,false)), "Stale/bypass retained telemetry pixels");
        // Changing signal data cannot overwrite axes/legend/footer or outside clipping.
        for(int y=0;y<210;++y) for(int x=0;x<width+20;++x)
            if(!plot.getSmallestIntegerContainer().contains(x,y))
                require(idle.getPixelAt(x,y)==live.getPixelAt(x,y), "History escaped the one plot or overwrote legend");
        require(samePixels(live,render(true,true,true)), "Paint invented history motion");
        for(int item=0;item<4;++item) {
            int ink=0;
            const int x0=int(bounds.getX()+item*bounds.getWidth()/4.f);
            for(int y=int(bounds.getY()+14);y<int(bounds.getY()+27);++y)
                for(int x=x0+9;x<int(x0+bounds.getWidth()/4.f);++x)
                    ink+=idle.getPixelAt(x,y)!=juce::Colours::black;
            require(ink>6,"Legend entry missing at narrow plot width");
        }
    }
    // Coincident transfer and GR must retain both solid and dashed identities.
    auto overlap=gr;
    for(size_t i=0;i<overlap.db.size();++i) overlap.db[i]=36.f*(1.f-float(i)/200.f);
    juce::Image crossed(juce::Image::RGB,420,210,true);
    { juce::Graphics graphics(crossed);
      renderer.paint(graphics,{0,0,420,210},frame,overlap,true,{-24,1,0,0,1,true},colours); }
    int curveInk=0,grInk=0;
    const auto region=CompressorPlot::plotBounds({0,0,420,210}).toNearestInt().reduced(2);
    for(int y=region.getY();y<region.getBottom();++y) for(int x=region.getX();x<region.getRight();++x) {
        const auto p=crossed.getPixelAt(x,y);
        curveInk+=p.getRed()>100 && p.getGreen()>100 && p.getBlue()>100
            && std::abs(int(p.getRed())-int(p.getBlue()))<30;
        grInk+=p.getRed()>110 && p.getRed()>p.getGreen()*1.1f && p.getGreen()>p.getBlue()*1.3f;
    }
    require(curveInk>20 && grInk>20,"Overlapping paths lost solid transfer or contrasting dashed GR");
    require(CompressorPlot::rmsDb(0)==-72 && std::abs(CompressorPlot::rmsDb(.1f)+20)<.001f, "RMS is not measured dBFS");
}
inline void nativeTelemetryLifecycle() {
    guiTestDisplay::enumerate();
    auto owner=std::make_unique<Processor>(); owner->setMotionPercent(0);
    Graph graph{{{1,NodeType::lead},{100,NodeType::compressor},{13,NodeType::output}},{{1,100,0},{100,13,0}}};
    juce::String error; require(owner->setRoutingGraph(graph,error),"Compressor lifecycle graph rejected");
    toneUiTests::write(*owner,"levelOn",0); toneUiTests::write(*owner,"fx1On",1);
    toneUiTests::write(*owner,"fx1Mix",100); toneUiTests::write(*owner,"fx1compressor_threshold",-30);
    owner->prepareToPlay(48000,256);
    {
        CreativeEffectVisual visual(*owner,1,CreativeKind::compressor);
        visual.setSize(370,238); guiTestDisplay::attach(visual); visual.refreshAt(0);
        juce::AudioBuffer<float> audio(2,256); juce::MidiBuffer midi; int sample=0;
        const auto process=[&] {
            for(int block=0;block<200;++block) {
                for(int i=0;i<256;++i,++sample) {
                    const float x=.4f*std::sin(float(sample)*.13f);
                    audio.setSample(0,i,x); audio.setSample(1,i,-x);
                }
                owner->processBlock(audio,midi);
            }
        };
        process(); visual.refreshAt(1000);
        require(visual.hasMeasuredActivity() && visual.measuredWetRms()>0 && visual.measuredCompressionHistory().available,
                "Production visual did not consume actual compressor PCM/GR");
        const auto first=visual.measuredSequence(); process(); visual.refreshAt(2000);
        require(visual.measuredSequence()>first,"Production visual did not update telemetry");
        visual.refreshAt(2301);
        require(!visual.hasMeasuredActivity() && !visual.measuredCompressionHistory().available,"Stopped history did not clear");
        process(); visual.refreshAt(3000); owner->reset(); visual.refreshAt(3001);
        require(!visual.hasMeasuredActivity() && visual.measuredWetRms()==0 && !visual.measuredCompressionHistory().available,
                "Processor reset retained visual telemetry");
        process(); toneUiTests::write(*owner,"fx1On",0); process(); visual.refreshAt(4000);
        require(!visual.measuredCompressionHistory().available,"Bypass retained visual compression");
        visual.removeFromDesktop();
    }
    owner->releaseResources();
}
inline void run() {
    measuredMappingAndScale(); sharedPixelsAndCache(); actualDspSnapshotBridge(); unifiedPlot(); nativeTelemetryLifecycle();
}
} // namespace vocalstack::compressorHistoryTests
