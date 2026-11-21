#pragma once
#include "SpectralMorphVisual.h"
#include "MainBusFxRack.h"
#include "MeasuredFieldUiTests.h"

namespace vocalstack::spectralMorphUiTests {
inline void run(bool render = false) {
    namespace ui = toneUiTests;
    guiTestDisplay::enumerate();
    for (int theme = 0; theme < (render ? 3 : 1); ++theme) for (bool external : {false,true}) {
        auto owner = std::make_unique<Processor>(); owner->setSkin(theme); owner->setMotionPercent(0);
        auto layout = owner->getBusesLayout();
        layout.inputBuses.set(1, external ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled());
        ui::require(owner->setBusesLayout(layout), "Spectral visual optional layout");
        Graph graph{{{1,NodeType::lead},{100,NodeType::spectralMorph},{13,NodeType::output}},{{1,100,0},{100,13,0}}};
        graph.nodes[1].mix=1; graph.nodes[1].spectralMorph.morph=.8f;
                if (render) {
                    RoutingNode mix; mix.id=12; mix.type=NodeType::mix; mix.mainControls=true;
                    graph.nodes.push_back(mix); graph.links.front().source=12; graph.links.push_back({1,12,0});
                }
        if (external) { graph.nodes.push_back({14,NodeType::sidechain}); graph.links.push_back({14,100,1}); }
        else {
            graph.nodes.push_back({101,NodeType::waveshaper}); graph.nodes.back().mix=1;
            graph.nodes.back().waveshaper.driveDb=24; graph.nodes.back().waveshaper.shape=1;
            graph.links.push_back({1,101,0}); graph.links.push_back({101,100,1});
        }
        juce::String error;
        ui::require(owner->setRoutingGraph(graph,error), "Spectral UI graph: " + error);
        ui::write(*owner,"levelOn",0); ui::write(*owner,"doubleOn",0);
        owner->prepareToPlay(48000,256);
        std::unique_ptr<juce::AudioProcessorEditor> editor(owner->createEditor());
        editor->setSize(1280,800); guiTestDisplay::attach(*editor); ui::action(*editor,"Routing");
        if (auto* routing = ui::find<RoutingPanel>(*editor,[](auto&) { return true; })) routing->selectEffect(100);
        if (external) {
            const auto savedLayout = owner->getRoutingLayout();
            const auto mainPosition = savedLayout.getChildWithProperty("id",1);
            const auto sidePosition = savedLayout.getChildWithProperty("id",14);
            ui::require(mainPosition.isValid() && sidePosition.isValid()
                && std::abs(float(mainPosition["y"])-float(sidePosition["y"])) >= 70,
                "SIDECHAIN first-use placement overlaps the main source");
        }
        SpectralMorphPanel panel(*owner,1);
        panel.setLookAndFeel(&editor->getLookAndFeel()); panel.getProperties().set(vocalstackThemeProperty,theme);
        panel.getProperties().set(vocalstackAnimationTimeOverrideProperty,0.0);
        panel.setNeon(theme==1);
                for (int width : {190, 220, 320, 480, 840, 1200}) {
                    panel.setSize(width, 420);
                    auto* viewport = ui::find<juce::Viewport>(panel, [](auto&) { return true; });
                    ui::require(viewport != nullptr, "Morph controls lost responsive viewport");
                    auto* content = viewport->getViewedComponent();
                    std::vector<juce::Rectangle<int>> cells;
                    for (auto* child : content->getChildren()) if (auto* knob = dynamic_cast<ParameterKnob*>(child)) {
                        const auto layout = knob->slider.getLookAndFeel().getSliderLayout(knob->slider);
                        ui::require(content->getLocalBounds().contains(knob->getBounds()), "Morph knob outside scroll content");
                        for (auto previous : cells) ui::require(!previous.intersects(knob->getBounds()), "Morph knob cells overlap");
                        cells.push_back(knob->getBounds());
                        ui::require(knob->getHeight() >= 142 && layout.sliderBounds.getHeight() >= 90,
                            "Morph knob must retain a 90px dial after enlarged text lanes");
                        ui::require(layout.textBoxBounds.getHeight() == 22
                            && knob->getHeight() == layout.sliderBounds.getHeight() + layout.textBoxBounds.getHeight()
                                + knob->captionHeightForWidth(knob->getWidth()),
                            "Morph knob height does not account for its actual caption/value lanes");
                        ui::require(!layout.textBoxBounds.intersects(layout.sliderBounds), "Morph value overlaps dial");
                        const float face = (float(std::min(layout.sliderBounds.getWidth(), layout.sliderBounds.getHeight())) * .5f - 8.f) * 1.55f;
                        ui::require(face >= 45.f, "Morph face is unreadably small");
                        viewport->setViewPosition(0, knob->getY());
                        ui::require(viewport->getLocalBounds().contains(viewport->getLocalArea(knob, knob->getLocalBounds())),
                            "Morph knob is not fully scroll reachable");
                        if (width >= 480) ui::require(knob->getY() == 0, "Wide Morph must keep one full knob row");
                    }
                    ui::require(cells.size() == 4, "Morph control missing");
                }
                panel.setSize(840,420); guiTestDisplay::attach(panel);
                        ParameterKnob beforeKnob(*owner, "fx1MorphAmount", "Morph", "%");
                        beforeKnob.setSize(207,74);
                        const auto beforeLayout=beforeKnob.slider.getLookAndFeel().getSliderLayout(beforeKnob.slider);
                        const float beforeFace=float(std::min(beforeLayout.sliderBounds.getWidth(),beforeLayout.sliderBounds.getHeight()))*.34f*1.55f;
                        auto* afterKnob=ui::find<ParameterKnob>(panel,[](auto&) { return true; });
                        const auto afterLayout=afterKnob->slider.getLookAndFeel().getSliderLayout(afterKnob->slider);
                        const float afterFace=(float(std::min(afterLayout.sliderBounds.getWidth(),afterLayout.sliderBounds.getHeight()))*.5f-8.f)*1.55f;
                        std::cout << "MORPH GEOMETRY before row=74 face=" << beforeFace << " dial=" << beforeLayout.sliderBounds.toString()
                            << "; after row=" << afterKnob->getHeight() << " face=" << afterFace << " dial=" << afterLayout.sliderBounds.toString() << '\n';
                        double snapshotMilliseconds=0;
        if (render) { ui::action(*editor, "Stack"); ui::action(*editor, "Spectral Morph #100"); }
                const juce::String stem="spectral-morph-" + juce::String(external ? "external-" : "internal-") + updateUiTests::themeName(theme);
        juce::AudioBuffer<float> audio(external ? 4 : 2,256); juce::MidiBuffer midi;
        int position=0; uint64_t previous=0; bool captured=false, released=false;
        SpectralMorphSnapshot frozen;
        juce::Image first;
        juce::String csv="frame,samples,revision,frozen,pending,main_peak,donor_peak,result_peak\n";
        for(int frame=0;frame<120;++frame) {
            if(frame==35) ui::write(*owner,"fx1MorphFreeze",1);
            if(frame==85) ui::write(*owner,"fx1MorphFreeze",0);
            if(frame==90) ui::write(*owner,"fx1MorphFFT",2);
            if(frame==95) ui::write(*owner,"fx1MorphMode",1);
            constexpr int blocks[]{1,137,3,251,17,63};
            for(int remaining=1920,block=0;remaining>0;++block) {
                const int count=std::min(remaining,blocks[block%6]); audio.setSize(external ? 4 : 2,count,false,false,true);
                for(int i=0;i<count;++i,++position) {
                    const double t=position/48000.;
                    const double base=frame<60 ? 173. : 311.;
                    double carrier=0;
                    for(int harmonic=1;harmonic<=12;++harmonic) carrier+=std::sin(t*2*std::numbers::pi*base*harmonic)/harmonic;
                    audio.setSample(0,i,float(.12*carrier)); audio.setSample(1,i,float(.1*carrier));
                    if(external) {
                        const double f=frame<50 ? 1300. : frame<85 ? 4300. : 2700.;
                        audio.setSample(2,i,float(.14*std::sin(t*2*std::numbers::pi*f)+.05*std::sin(t*2*std::numbers::pi*500)));
                        audio.setSample(3,i,float(.08*std::cos(t*2*std::numbers::pi*f*1.17)));
                    }
                }
                owner->processBlock(audio,midi); remaining-=count;
            }
            SpectralMorphFrame measured;
            ui::require(owner->readSpectralMorph(1,measured) && measured.nodeId==100
                && measured.spectrum.revision>previous,"Actual spectral telemetry did not advance");
            previous=measured.spectrum.revision;
            if(frame==55) { frozen=measured.spectrum; captured=frozen.frozen; }
            if(frame==75) {
                float difference=0;
                for(size_t k=0;k<frozen.bins;++k) difference=std::max(difference,std::abs(frozen.donor[k]-measured.spectrum.donor[k]));
                ui::require(captured && measured.spectrum.frozen && difference<1.e-5f,"Freeze flag is decorative or held donor spectrum changed");
            }
            if(frame==100) released=!measured.spectrum.frozen;
            panel.refresh();
            const double snapshotStart=juce::Time::getMillisecondCounterHiRes();
                        const auto image=panel.createComponentSnapshot(panel.getLocalBounds());
                        snapshotMilliseconds+=juce::Time::getMillisecondCounterHiRes()-snapshotStart;
            if(frame==20) first=image;
            if(frame==70) ui::require(!updateUiTests::samePixels(first,image),"Measured spectral material did not change with Motion zero");
            const auto peak=[](const auto& bins) { return *std::max_element(bins.begin(),bins.end()); };
            csv+=juce::String(frame)+","+juce::String(position)+","+juce::String(juce::int64(previous))+","+juce::String(int(measured.spectrum.frozen))+","+juce::String(int(measured.spectrum.capturePending))+","+juce::String(peak(measured.spectrum.main),6)+","+juce::String(peak(measured.spectrum.donor),6)+","+juce::String(peak(measured.spectrum.result),6)+"\n";
            if(render) {
                measuredFieldUiTests::save(panel,stem+"-panel-"+juce::String(frame).paddedLeft('0',3)+".png");
                                if (auto* view = ui::find<SpectralMorphPanel>(*editor,[](auto& c){return c.isShowing();})) view->refresh();
                                if (auto* rack = ui::find<MainBusFxRackPanel>(*editor,[](auto& c){return c.isShowing();}))
                                    measuredFieldUiTests::save(*rack,stem+"-stack-row-"+juce::String(frame).paddedLeft('0',3)+".png");
                if(frame==20 || frame==70 || frame==105) {
                    if (auto* view = ui::find<SpectralMorphPanel>(*editor,[](auto& c){return c.isShowing();})) view->refresh();
                    measuredFieldUiTests::save(*editor,stem+"-editor-native-"+juce::String(frame)+".png");
                }
            }
        }
        std::cout << "MORPH panel snapshot average ms=" << snapshotMilliseconds/120 << " (840x420, includes controls/compositing; not desktop FPS)\n";
                ui::require(captured && released,"Actual capture/hold/release did not complete");
        const auto connectedGraph = graph;
        // Disconnect the donor explicitly: presentation must not invent a wire.
        graph.links.erase(std::remove_if(graph.links.begin(),graph.links.end(),[](const auto& l){return l.destination==100 && l.inputPin==1;}),graph.links.end());
        ui::require(owner->setRoutingGraph(graph,error),"Spectral disconnect rejected");
        audio.setSize(external ? 4 : 2,256); audio.clear(); owner->processBlock(audio,midi); panel.refresh();
        if(render) {
            measuredFieldUiTests::save(panel,stem+"-unrouted.png");
            juce::File::getCurrentWorkingDirectory().getChildFile(stem+"-measurements.csv").replaceWithText(csv);
            ui::require(owner->setRoutingGraph(connectedGraph,error),"Expanded capture donor restore");
            ui::write(*owner,"fx1MorphFreeze",1);
            const auto feed = [&] {
                audio.setSize(external ? 4 : 2,256);
                for(int block=0;block<48;++block) {
                    for(int i=0;i<256;++i,++position) {
                        const double t=position/48000.;
                        audio.setSample(0,i,float(.15*std::sin(t*2*std::numbers::pi*440)));
                        audio.setSample(1,i,float(.12*std::sin(t*2*std::numbers::pi*660)));
                        if(external) { audio.setSample(2,i,float(.13*std::sin(t*2*std::numbers::pi*1800))); audio.setSample(3,i,float(.1*std::sin(t*2*std::numbers::pi*3200))); }
                    }
                    owner->processBlock(audio,midi);
                }
            };
            feed();
            panel.setLookAndFeel(nullptr); editor->removeFromDesktop(); editor.reset();
            auto expandedLayout=owner->getRoutingLayout();
            expandedLayout.setProperty("inspectorVisible",false,nullptr);
            expandedLayout.setProperty("zoom",.8f,nullptr); expandedLayout.setProperty("panX",20.f,nullptr); expandedLayout.setProperty("panY",20.f,nullptr);
            for(auto child:expandedLayout) if(child.hasType("NODE")) {
                const int id=child["id"]; child.setProperty("expanded",id==100,nullptr);
                if(id==100) {
                    child.setProperty("x",300.f,nullptr); child.setProperty("y",40.f,nullptr);
                    child.setProperty("expandedX",300.f,nullptr); child.setProperty("expandedY",40.f,nullptr);
                } else if(id==101) {
                    child.setProperty("x",24.f,nullptr); child.setProperty("y",200.f,nullptr);
                    child.setProperty("compactX",24.f,nullptr); child.setProperty("compactY",200.f,nullptr);
                }
            }
            owner->setRoutingLayout(expandedLayout);
            editor.reset(owner->createEditor()); editor->setSize(1280,800); guiTestDisplay::attach(*editor); ui::action(*editor,"Routing");
            auto* expandedView=ui::find<SpectralMorphPanel>(*editor,[](auto& c){return c.isShowing();});
            ui::require(expandedView!=nullptr,"Expanded Morph body not mounted"); expandedView->refresh();
            SpectralMorphFrame captured; ui::require(owner->readSpectralMorph(1,captured) && captured.spectrum.frozen,"Expanded snapshot has no actual held capture");
            measuredFieldUiTests::save(*editor,stem+"-expanded-native.png");
            const auto rackScene=mainBusFx::add(defaultGraph(),NodeType::spectralMorph);
            ui::require(rackScene.changed && owner->setRoutingGraph(rackScene.graph,error),"Unrouted rack capture graph");
            ui::action(*editor,"Stack"); ui::action(*editor,"Spectral Morph #100"); feed();
            auto* rackView=ui::find<SpectralMorphPanel>(*editor,[](auto& c){return c.isShowing();});
            ui::require(rackView!=nullptr,"Main rack Morph body missing"); rackView->refresh();
            measuredFieldUiTests::save(*editor,stem+"-rack-unrouted-native.png");
        }
        panel.setLookAndFeel(nullptr); panel.removeFromDesktop(); editor->removeFromDesktop(); owner->releaseResources();
        std::cout << "Spectral Morph actual PCM/native component capture: " << stem << "; held donor spectrum oracle passed\n";
    }
}
}
