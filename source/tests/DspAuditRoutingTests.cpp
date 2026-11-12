// Current routing integration contract; no processor/JUCE dependency.
#include "Routing.h"
#include <cstdio>
#include <memory>
#include <stdexcept>
using namespace vocalstack;
void check(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
int main() {
    try {
        for(double sr : {8000.,44100.,48000.,96000.,192000.,384000.}) for(float wet : {0.f,1.f}) {
            auto routing=std::make_unique<RoutingExecutor>(); routing->prepare(sr);
            Graph graph; RoutingNode fx{100,NodeType::reverb}; fx.mix=wet;
            fx.creative[CreativeParameter::reverbPredelay]=40;
            fx.creative[CreativeParameter::reverbDuck]=0;
            graph.nodes={{1,NodeType::lead},fx,{2,NodeType::output}};
            graph.links={{1,100,0},{100,2,0}};
            check(bool(routing->requestGraph(graph)),"endpoint graph rejected");
            RoutingControls controls;
            for(int i=0;i<int(sr*.2);++i) routing->process({},controls);
            for(int i=0;i<int(sr*.04);++i) {
                RoutingSources source; source.lead={i==0 ? .25f:0.f,i==0 ? -.125f:0.f};
                const auto y=routing->process(source,controls);
                check(std::isfinite(y.left) && std::isfinite(y.right),"nonfinite graph endpoint");
                if(wet==0) check(y.left==source.lead.left && y.right==source.lead.right,"0% wet is not exact dry");
                else check(y.left==0 && y.right==0,"100% wet contains hidden direct feed");
            }
        }
        std::puts("DSP routing endpoints passed: exact dry / true delayed wet, six rates");
    } catch(const std::exception& e) { std::fprintf(stderr,"FAIL: %s\n",e.what()); return 1; }
}
