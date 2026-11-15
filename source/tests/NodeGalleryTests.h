#pragma once
#include "SpectralMorphUiTests.h"

// Audit-only fixture: production components and processor, never injected telemetry.
namespace vocalstack::nodeGalleryTests {
namespace ui = toneUiTests;
struct Entry { const char* slug; const char* name; NodeType type; uint32_t id; int voice = 0; bool main = false; };
inline const std::vector<Entry> entries {
    {"chorale","Chorale",NodeType::chorale,100},
    {"waveshaper","Waveshaper",NodeType::waveshaper,100},
    {"eq8","EQ8",NodeType::equalizer,100},
    {"multiband","Multiband",NodeType::crossover,100},
    {"delay","Delay",NodeType::delay,100},
    {"reverb","Reverb",NodeType::reverb,100},
    {"compressor","Compressor",NodeType::compressor,100},
    {"bitcrusher","Bitcrusher",NodeType::bitcrusher,100},
    {"granular","Granular",NodeType::granular,100},
    {"spectral-morph","Spectral Morph",NodeType::spectralMorph,100},
    {"convolution","Convolution",NodeType::convolution,100},
    {"saturator","Saturator",NodeType::saturator,100},
    {"gain","Gain",NodeType::gain,100},
    {"stereo","Stereo",NodeType::stereo,100},
    {"gate","Gate",NodeType::gate,100},
    {"xp-filter","XP Filter",NodeType::xpFilter,100},
    {"synth-filter","Synth Filter",NodeType::synthFilter,100},
    {"main-chorale","Main Chorale",NodeType::chorale,12,0,true},
    {"lead","Lead",NodeType::lead,1},
    {"voice-1","Voice 1",NodeType::voice,2,0},
    {"voice-2","Voice 2",NodeType::voice,3,1},
    {"voice-3","Voice 3",NodeType::voice,4,2},
    {"voice-4","Voice 4",NodeType::voice,5,3},
    {"voice-5","Voice 5",NodeType::voice,6,4},
    {"voice-6","Voice 6",NodeType::voice,7,5},
    {"voice-7","Voice 7",NodeType::voice,8,6},
    {"voice-8","Voice 8",NodeType::voice,9,7},
    {"sidechain","SIDECHAIN",NodeType::sidechain,14},
    {"dry-wet","Dry / Wet",NodeType::mix,200},
    {"splitter","Splitter",NodeType::splitter,300},
    {"main-mix","Lead / Stack",NodeType::mix,11,0,true},
    {"output","Output",NodeType::output,13}
};
inline const char* model(const Entry& e) {
    switch(e.type) {
        case NodeType::spectralMorph: return "Measured MAIN / DONOR / result spectra; real Freeze hold/release";
        case NodeType::reverb: case NodeType::convolution: case NodeType::saturator:
            return "Measured input / wet history + parameter-derived foreground";
        case NodeType::compressor: return "ONE plot: measured In/Wet RMS + dashed actual GR; solid static transfer; independent axes";
        case NodeType::delay: return "Parameter-timed phase illustration; not measured echoes";
        case NodeType::granular: return "Intentionally stable parameter envelope; NOT live grains/audio";
        case NodeType::bitcrusher: return "Static parameter-derived quantization schematic";
        case NodeType::waveshaper: return "Parameter transfer + measured stereo PRE / POST waveforms";
        case NodeType::equalizer: return "Parameter EQ response + measured stereo PRE / POST FFT";
        case NodeType::crossover: return "Parameter crossover curves + measured input FFT / band levels";
        case NodeType::xpFilter: case NodeType::synthFilter:
            return "Parameter response + measured PRE / POST FFT; Synth curve is approximate";
        case NodeType::gate: return "Actual input/output RMS, smoothed threshold and wet GR, 20 ms / 4 seconds";
        case NodeType::stereo: return "Actual final L/R RMS and correlation; persisted post-Mix Mono Audition";
        case NodeType::gain: return "Measured PRE / POST meters; default full Wet with compatible legacy blend";
        default: return "Production controls/information only; no invented audio visualization";
    }
}
inline Graph graph(const Entry& e) {
    if(e.id >= 100 && e.id != 200 && e.id != 300) {
        Graph g{{{1,NodeType::lead},{11,NodeType::mix},{e.id,e.type},{13,NodeType::output}},
            {{1,11,0},{11,e.id,0},{e.id,13,0}}};
        g.nodes[1].mainControls=true; g.nodes[2].mix=1;
        if(e.type==NodeType::spectralMorph) { g.nodes.push_back({14,NodeType::sidechain}); g.links.push_back({14,e.id,1}); }
        return g;
    }
    auto g=defaultGraph();
    if(e.type==NodeType::sidechain) { g.nodes.push_back({14,NodeType::sidechain}); }
    if(e.id==200 || e.id==300) {
        g.nodes.push_back({e.id,e.type});
        g.links.erase(std::remove_if(g.links.begin(),g.links.end(),[](auto l){return l.source==1;}),g.links.end());
        g.links.push_back({1,e.id,0}); g.links.push_back({e.id,11,0});
    }
    return g;
}
inline void refresh(juce::Component& c,double milliseconds) {
    if(auto* v=dynamic_cast<CreativeEffectVisual*>(&c)) v->refreshAt(milliseconds);
    if(auto* v=dynamic_cast<SpectralMorphPanel*>(&c)) v->refresh();
    for(auto* child:c.getChildren()) if(child->isVisible()) refresh(*child,milliseconds);
}
inline void choices(juce::Component& root,juce::Array<juce::var>& result) {
    if(auto* combo=dynamic_cast<juce::ComboBox*>(&root); combo && combo->isShowing()) {
        auto* object=new juce::DynamicObject; object->setProperty("parameter",combo->getComponentID());
        object->setProperty("selected",combo->getText()); juce::Array<juce::var> items;
        for(int i=0;i<combo->getNumItems();++i) items.add(combo->getItemText(i));
        object->setProperty("choices",items); result.add(juce::var(object));
    }
    for(auto* child:root.getChildren()) if(child->isVisible()) choices(*child,result);
}
inline void run(const juce::String& only, int theme = 1, bool coordinated = false) {
    guiTestDisplay::enumerate();
    for(const auto& e:entries) {
        if(only.isNotEmpty() && only!=e.slug) continue;
        for(bool rack:{false,true}) {
            if(rack && !mainBusFx::eligible(e.type)) continue;
            auto owner=std::make_unique<Processor>(); owner->setSkin(theme); owner->setMotionPercent(0);
            auto buses=owner->getBusesLayout(); buses.inputBuses.set(1,juce::AudioChannelSet::stereo());
            ui::require(owner->setBusesLayout(buses),"Gallery auxiliary bus");
            juce::String error; ui::require(owner->setRoutingGraph(graph(e),error),"Gallery graph: "+error);
            for(int i=0;i<8;++i) { ui::write(*owner,"voice"+juce::String(i+1)+"Present",1); ui::write(*owner,"voice"+juce::String(i+1)+"On",1); }
            ui::write(*owner,"levelOn",0); ui::write(*owner,"doubleOn",1); ui::write(*owner,"advancedOn",1);
            ui::write(*owner,"choraleOn",e.main && e.type==NodeType::chorale ? 1.f : 0.f);
            if(e.id==100) { ui::write(*owner,"fx1On",1); ui::write(*owner,"fx1Mix",100); }
            if(e.type==NodeType::spectralMorph) ui::write(*owner,"fx1MorphAmount",80);
            if(e.type==NodeType::compressor) ui::write(*owner,"fx1compressor_threshold",-30);
            if(coordinated && e.type==NodeType::gate) ui::write(*owner,utilityParameterId(1,size_t(UtilityParameter::gateThreshold)),-24);
            if(e.type==NodeType::reverb) { ui::write(*owner,"fx1reverb_decay",3); ui::write(*owner,"fx1reverb_duck",0); }
            if(e.type==NodeType::saturator) ui::write(*owner,"fx1saturator_drive",12);
            auto layout=owner->getRoutingLayout(); layout.setProperty("selectedNode",int(e.id),nullptr);
            layout.removeProperty("selectedVoiceSlot",nullptr); layout.setProperty("inspectorVisible",true,nullptr);
            layout.setProperty("inspectorWidth",400,nullptr); owner->setRoutingLayout(layout);
            owner->prepareToPlay(48000,256);
            std::unique_ptr<juce::AudioProcessorEditor> editor(owner->createEditor());
            editor->setSize(1280,900); guiTestDisplay::attach(*editor);
            editor->getProperties().set(vocalstackAnimationTimeOverrideProperty,0.0);
            ui::action(*editor,rack ? "Stack" : "Routing");
            auto* routing=ui::find<RoutingPanel>(*editor,[](auto&){return true;});
            auto* rackPanel=ui::find<MainBusFxRackPanel>(*editor,[](auto&){return true;});
            ui::require(routing && rackPanel,"Production gallery surfaces missing");
            if(rack) {
                rackPanel->sync();
                const auto label=juce::String(e.name)+" #"+juce::String(e.id);
                // Main Chorale is initially the first/only tab; independent FX use the same production action.
                if(!e.main) ui::action(*editor,label);
            }
            const juce::String stem=juce::String(e.slug)+(rack ? "-rack" : "-inspector") + (coordinated ? "-" + updateUiTests::themeName(theme) : juce::String());
            juce::AudioBuffer<float> audio(4,256); juce::MidiBuffer midi;
            juce::String csv="frame,input_rms,output_rms,fx_sequence,wet_rms,morph_revision,frozen,pixel_changed\n";
            juce::Image previous; int position=0,changed=0; bool held=false,released=false,tail=false;
            SpectralMorphSnapshot frozen;
            for(int frame=0;frame<100;++frame) {
                if(coordinated) {
                    if(e.type==NodeType::stereo && (frame==30 || frame==75)) ui::write(*owner,stereoMonoParameterId(1),frame==30 ? 1.f : 0.f);
                    if(e.type==NodeType::gain && (frame==30 || frame==75)) ui::write(*owner,"fx1Mix",frame==30 ? 37.f : 100.f);
                    if(e.type==NodeType::output) ui::write(*owner,"outputGain",-24.f + 36.f * float(frame) / 99.f);
                    if(e.type==NodeType::mix) ui::write(*owner,e.main ? juce::String("double") : localMixParameterId(e.id),float(frame) * 100.f / 99.f);
                }
                if(e.type==NodeType::spectralMorph && frame==30) ui::write(*owner,"fx1MorphFreeze",1);
                if(e.type==NodeType::spectralMorph && frame==75) ui::write(*owner,"fx1MorphFreeze",0);
                double inputEnergy=0,outputEnergy=0;
                for(int remaining=1920;remaining>0;) {
                    int n=std::min(remaining,256); audio.setSize(4,n,false,false,true);
                    for(int i=0;i<n;++i,++position) {
                        const double t=position/48000.;
                        const bool tailEffect=e.type==NodeType::reverb || e.type==NodeType::convolution;
                        const double envelope=(coordinated && (e.type==NodeType::gate || e.type==NodeType::compressor)) ? (frame % 30 < 18 ? .4 : .004) : tailEffect ? (t<.7 ? .42*std::sin(std::numbers::pi*t/.7) : 0.)
                            : t>3.2 ? 0. : (.12+.22*std::pow(std::sin(t*5),2));
                        const double base=t<1.6 ? 173. : 311.;
                        float a=float(envelope*(std::sin(t*2*std::numbers::pi*base)+.23*std::sin(t*2*std::numbers::pi*1700)));
                        float b=float(envelope*.73*std::sin(t*2*std::numbers::pi*base*1.3));
                        audio.setSample(0,i,a); audio.setSample(1,i,b); inputEnergy+=a*a+b*b;
                        const double donor=frame<45 ? 1300. : 4300.;
                        audio.setSample(2,i,float(.16*std::sin(t*2*std::numbers::pi*donor)));
                        audio.setSample(3,i,float(.11*std::sin(t*2*std::numbers::pi*donor*1.17)));
                    }
                    owner->processBlock(audio,midi);
                    for(int c=0;c<2;++c) for(int i=0;i<n;++i) {
                        const float sample=audio.getSample(c,i); ui::require(std::isfinite(sample),"Nonfinite gallery PCM"); outputEnergy+=sample*sample;
                    }
                    remaining-=n;
                }
                juce::Thread::sleep(40); juce::Timer::callPendingTimersSynchronously();
                refresh(*editor,position/48.);
                FxMonitorFrame fx; const bool hasFx=owner->readFxMonitor(e.main ? 0 : 1,fx);
                SpectralMorphFrame morph; const bool hasMorph=e.type==NodeType::spectralMorph && owner->readSpectralMorph(1,morph);
                if(hasMorph && frame==40) { frozen=morph.spectrum; held=frozen.frozen; }
                if(hasMorph && frame==65) {
                    float difference=0; for(size_t k=0;k<frozen.bins;++k) difference=std::max(difference,std::abs(frozen.donor[k]-morph.spectrum.donor[k]));
                    ui::require(held && morph.spectrum.frozen && difference<1.e-5f,"Real donor Freeze changed while held");
                }
                if(hasMorph && frame==90) released=!morph.spectrum.frozen;
                if(coordinated && frame==90 && (e.type==NodeType::gate || e.type==NodeType::compressor)) {
                    ui::require(hasFx && fx.hasGainReduction && fx.gainReductionHistoryUsed > 100, "Approval capture lacks actual four-second GR producer history");
                    if(e.type==NodeType::gate) ui::require(fx.utilityHistory.used > 100, "Approval Gate capture lacks measured threshold/levels");
                                        if(e.type==NodeType::compressor) ui::require(fx.hasWet && fx.historyUsed==128 && fx.history.back().inputRms>0 && fx.history.back().wetRms>0,
                                            "Compressor shared plot lacks actual input/wet energy");
                }
                if(coordinated && frame==60 && e.type==NodeType::stereo) {
                    ui::require(hasFx && std::abs(fx.output[0].back()-fx.output[1].back()) < 1.e-5f, "Approval Stereo capture is not actual final-output mono");
                }
                if(hasFx && frame>25 && fx.hasWet && fx.history.back().inputRms==0 && fx.wetRms>1.e-5f) tail=true;
                juce::Image image;
                if(rack) image=rackPanel->createComponentSnapshot(rackPanel->getLocalBounds());
                else {
                    // Include the actual title, enable switch, scroll surface and explanatory footer.
                    const auto bounds=routing->getLocalBounds().withLeft(routing->getWidth()-412).withTrimmedTop(44);
                    image=routing->createComponentSnapshot(bounds);
                }
                const bool difference=previous.isValid() && !updateUiTests::samePixels(previous,image);
                changed+=int(difference); previous=image;
                juce::Image labelled(juce::Image::RGB,image.getWidth(),image.getHeight()+44,true);
                { juce::Graphics g(labelled); g.fillAll(juce::Colour(0xff10121a)); g.setColour(juce::Colours::white); g.setFont(13.f);
                g.drawText(juce::String(e.name)+(rack ? " | STACK MAIN BUS FX" : " | RIGHT INSPECTOR"),8,1,image.getWidth()-16,21,juce::Justification::centredLeft);
                g.setFont(10.f); g.drawText(model(e),8,22,image.getWidth()-16,20,juce::Justification::centredLeft); g.drawImageAt(image,0,44); }
                const auto file=juce::File::getCurrentWorkingDirectory().getChildFile(stem+"-"+juce::String(frame).paddedLeft('0',3)+".png");
                ui::require(!file.existsAsFile() || file.deleteFile(),"Cannot replace gallery PNG");
                juce::FileOutputStream stream(file); ui::require(stream.openedOk() && juce::PNGImageFormat().writeImageToStream(labelled,stream),"Gallery PNG");
                csv+=juce::String(frame)+","+juce::String(std::sqrt(inputEnergy/3840),8)+","+juce::String(std::sqrt(outputEnergy/3840),8)+","+juce::String(juce::int64(hasFx ? fx.sequence : 0))+","+juce::String(hasFx ? fx.wetRms : 0,8)+","+juce::String(juce::int64(hasMorph ? morph.spectrum.revision : 0))+","+juce::String(int(hasMorph && morph.spectrum.frozen))+","+juce::String(int(difference))+"\n";
                if(frame==45) {
                                    measuredFieldUiTests::save(*editor,stem+"-context.png");
                                    if(e.type==NodeType::compressor) {
                                        auto* visual=ui::find<CreativeEffectVisual>(rack ? static_cast<juce::Component&>(*rackPanel) : static_cast<juce::Component&>(*routing),
                                            [](auto& v){ return v.isShowing(); });
                                        ui::require(visual && visual->hasMeasuredActivity(),"Compressor production shared plot missing live telemetry");
                                        measuredFieldUiTests::save(*visual,stem+"-plot.png");
                                    }
                                }
            }
            if(e.type==NodeType::spectralMorph) ui::require(held && released && changed>0,"Morph hold/release/activity");
            if(e.type==NodeType::reverb || e.type==NodeType::convolution) ui::require(tail && changed>0,"Actual wet tail missing");
            juce::File::getCurrentWorkingDirectory().getChildFile(stem+"-measurements.csv").replaceWithText(csv);
            auto* metadata=new juce::DynamicObject; juce::var data(metadata);
            metadata->setProperty("theme",updateUiTests::themeName(theme)); metadata->setProperty("coordinated_update",coordinated);
                        metadata->setProperty("slug",e.slug); metadata->setProperty("name",e.name);
            metadata->setProperty("node_type",int(e.type)); metadata->setProperty("node_id",int(e.id));
            metadata->setProperty("view",rack ? "rack" : "inspector"); metadata->setProperty("model",model(e));
            metadata->setProperty("rack_supported",mainBusFx::eligible(e.type));
            metadata->setProperty("changed_frames",changed); metadata->setProperty("actual_tail_verified",tail);
            juce::Array<juce::var> options; choices(rack ? static_cast<juce::Component&>(*rackPanel) : static_cast<juce::Component&>(*routing),options);
            metadata->setProperty("model_choices",options);
            juce::File::getCurrentWorkingDirectory().getChildFile(stem+".json").replaceWithText(juce::JSON::toString(data));
            std::cout<<"GALLERY "<<stem<<" frames=100 fps=25 changed="<<changed<<"/99 tail="<<tail<<" model="<<model(e)<<std::endl;
            editor->removeFromDesktop(); editor.reset(); owner->releaseResources();
        }
    }
}
}
