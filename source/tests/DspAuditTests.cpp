#include "CreativeEffects.h"
#include <cstdio>
#include <stdexcept>
#include <type_traits>
#include <vector>
#include <chrono>
#include <cstdlib>
#include <new>
thread_local bool audioCheck=false;
unsigned audioAllocations=0,audioFrees=0;
void* operator new(std::size_t n) { if(audioCheck) ++audioAllocations; if(auto p=std::malloc(n ? n:1)) return p; throw std::bad_alloc(); }
void operator delete(void* p) noexcept { if(audioCheck && p) ++audioFrees; std::free(p); }
void operator delete(void* p,std::size_t) noexcept { ::operator delete(p); }
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete[](void* p,std::size_t) noexcept { ::operator delete(p); }
using namespace vocalstack;
using P = CreativeParameter;
constexpr double pi = 3.14159265358979323846;
constexpr std::array<double,6> rates{8000,44100,48000,96000,192000,384000};
constexpr std::array<CreativeKind,5> kinds{CreativeKind::delay,CreativeKind::reverb,
    CreativeKind::compressor,CreativeKind::bitcrusher,CreativeKind::granular};
void check(bool b, const char* m) { if(!b) throw std::runtime_error(m); }
void finite(std::array<float,2> y) { check(std::isfinite(y[0]) && std::isfinite(y[1]), "nonfinite output"); }
float tone(int i,double sr) { return .2f*float(std::sin(2*pi*997*i/sr)); }
void snapshotCheck(const CreativeEffect::GrainSnapshot& s) {
    check(s.count<=32,"grain capacity");
    check(s.captureHead>=0 && s.captureHead<1 && s.validFraction>=0 && s.validFraction<=1,"capture bounds");
    unsigned slots=0;
    for(unsigned i=0;i<s.count;++i) { const auto& g=s.grains[i];
        check(g.position>=0 && g.position<1 && g.progress>=0 && g.progress<1,"grain coordinates");
        check(std::isfinite(g.envelope) && g.envelope>=0 && g.envelope<=1.00001f,"grain envelope");
        check(g.slot<32 && !(slots & (1u<<g.slot)),"grain slot identity"); slots|=1u<<g.slot;
        check(std::abs(g.step)>=.249 && std::abs(g.step)<=2.001,"grain direction/pitch");
    }
}
void boundaries() {
    creative_detail::Ring r;
    check(r.at(0)==0 && r.read(1)==0,"unprepared ring"); r.push(1);
    r.allocate(8); for(int i=0;i<8;++i) r.push(float(i));
    check(r.at(-.5)==3.5f && r.at(7.5)==3.5f,"fractional wrap");
    check(r.at(8000001.5)==1.5f,"bounded absolute wrap");
    check(r.at(std::numeric_limits<double>::quiet_NaN())==0,"invalid read");
    check(r.read(std::numeric_limits<double>::infinity())==0,"invalid delay");
}
void partitionAndEndpoints() {
    for(double sr:rates) for(auto kind:kinds) {
        CreativeEffect a,b; CreativeSettings settings; a.prepare(sr,kind); b.prepare(sr,kind);
        constexpr int blocks[]{1,7,31,2,129,511,3};
        int position=0,block=0; const int total=int(sr*.55);
        auto automation = [](int i) {
            CreativeSettings s;
            const int event=i/137;
            for(std::size_t p=0;p<creativeParameterCount;++p) {
                const auto& d=creativeParameters[p];
                s.values[p]=event%3==0 ? std::numeric_limits<float>::quiet_NaN() : (event%2 ? d.min:d.max);
            }
            return s;
        };
        std::vector<std::array<float,2>> reference(total);
        for(int i=0;i<total;++i) {
            const float l=tone(i,sr), r=i%3 ? l:-l;
            reference[i]=a.process(l,r,automation(i),i%2 ? 20:400);
        }
        while(position<total) {
            const int count=std::min(blocks[block++%7],total-position);
            for(int j=0;j<count;++j,++position) {
                settings=automation(position);
                const float l=tone(position,sr),r=position%3 ? l:-l;
                const auto y=b.process(l,r,settings,position%2 ? 20:400);
                finite(y); check(reference[position]==y,"partition sequence mismatch");
                if(kind==CreativeKind::granular && position%113==0) snapshotCheck(b.grainSnapshot());
            }
        }
        a.reset(); b.reset();
        for(int i=0;i<256;++i) { const auto x=a.process(0,0,settings); check(x[0]==0 && x[1]==0,"reset/silence"); }
        for(int i=0;i<512;++i) { auto x=a.process(tone(i,sr),0,settings); auto y=b.process(tone(i,sr),0,settings);
            // a advanced on silence, so reset again before the reproducibility test below.
            finite(x); finite(y); }
        a.reset(); b.prepare(sr,kind);
        for(int i=0;i<1024;++i) check(a.process(tone(i,sr),0,settings)==b.process(tone(i,sr),0,settings),"reset/prepare replay");
        finite(a.process(std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN(),settings));
    }
}
void reverbLevel() {
    CreativeEffect fx; CreativeSettings s; fx.prepare(48000,CreativeKind::reverb); s[P::reverbDuck]=0;
    double in=0,out=0; float peak=0;
    for(int i=0;i<240000;++i) {
        const float x=tone(i,48000); auto y=fx.process(x,x,s);
        if(i>=96000) { in+=x*x; out+=(y[0]*y[0]+y[1]*y[1])*.5; peak=std::max(peak,std::max(std::abs(y[0]),std::abs(y[1]))); }
    }
    const double db=10*std::log10(out/in);
    // Independently recorded pre-change fixture: -9.33273 dB; fixed 4x output
    // gain is +12.04120 dB. Tolerance covers ordinary compiler/libm variation.
    check(std::abs(db-2.70847)<.08,"reverb tonal calibration fixture");
    check(peak<.66f && peak>.6f,"reverb tonal headroom fixture");
    for(double sr:rates) {
        fx.prepare(sr,CreativeKind::reverb); s[P::reverbPredelay]=40; s[P::reverbEarly]=100;
        for(int i=0;i<int(sr*.04);++i) {
            auto y=fx.process(i==0 ? 1.f:0.f, i==0 ? -1.f:0.f,s);
            check(y[0]==0 && y[1]==0,"wet contains hidden direct feed");
        }
    }
}
void grainHistoryAndFreeze() {
    static_assert(std::is_trivially_copyable<CreativeEffect::GrainSnapshot>::value,"bridge needs fixed value data");
    for(double sr:rates) {
        CreativeEffect fx; CreativeSettings s; fx.prepare(sr,CreativeKind::granular);
        s[P::granularPitch]=0; s[P::granularReverse]=1; s[P::granularPosition]=1500;
        s[P::granularDensity]=80; s[P::granularSize]=250; s[P::granularJitter]=0;
        s[P::granularSpread]=0; s[P::granularLowCut]=20; s[P::granularHighCut]=20000;
        for(int i=0;i<int(sr*.1);++i) fx.process(tone(i,sr),tone(i,sr),s);
        // Only 100ms is recorded. Original code blindly addressed 1500ms of
        // lookback (zeros), despite usable captured audio being available.
        s[P::granularFreeze]=1;
        double energy=0; float maximumStep=0,previous=0;
        for(int i=0;i<int(sr*.5);++i) {
            auto y=fx.process(0,0,s); finite(y); energy+=y[0]*y[0];
            if(i>0) maximumStep=std::max(maximumStep,std::abs(y[0]-previous)); previous=y[0];
            if(i%97==0) snapshotCheck(fx.grainSnapshot());
        }
        auto frozen=fx.grainSnapshot(); check(frozen.frozen && !frozen.capturing,"freeze flag");
        check(frozen.capturedFrames<unsigned(sr*.14),"capture continued while frozen");
        check(energy/sr>1e-6,"partial-history freeze lost usable capture");
        check(maximumStep<.25f,"boundary transition spike");
        const auto captured=frozen.capturedFrames;
        s[P::granularFreeze]=0;
        for(int i=0;i<int(sr*.2);++i) finite(fx.process(tone(i,sr),0,s));
        check(fx.grainSnapshot().capturedFrames>captured,"unfreeze capture");
        fx.reset(); s[P::granularFreeze]=1;
        for(int i=0;i<int(sr*.3);++i) { auto y=fx.process(.5f,.5f,s); check(y[0]==0 && y[1]==0,"empty frozen capture"); }
        check(fx.grainSnapshot().capturedFrames==0,"reset capture count");
    }
}
void grainTransitionRetirement() {
    CreativeEffect fx; CreativeSettings s; fx.prepare(48000,CreativeKind::granular);
    s[P::granularPitch]=12; s[P::granularSize]=250; s[P::granularDensity]=80;
    s[P::granularPosition]=s[P::granularSpread]=s[P::granularJitter]=0;
    for(int i=0;i<19200;++i) fx.process(tone(i,48000),tone(i,48000),s);
    s[P::granularFreeze]=1;
    bool sawRetirement=false; float previous=0,maximumStep=0;
    for(int i=0;i<14400;++i) {
        const auto y=fx.process(0,0,s); const auto snap=fx.grainSnapshot(); snapshotCheck(snap);
        for(unsigned j=0;j<snap.count;++j) sawRetirement|=snap.grains[j].holdingBoundary;
        if(i>0) maximumStep=std::max(maximumStep,std::abs(y[0]-previous)); previous=y[0];
    }
    check(sawRetirement,"freeze did not exercise live forward grain boundary");
    check(maximumStep<.08f,"forward freeze transition discontinuity");
}
void compressorMeter() {
    CreativeEffect fx; CreativeSettings s; fx.prepare(48000,CreativeKind::compressor);
    s[P::compressorThreshold]=-40; s[P::compressorKnee]=0; s[P::compressorMakeup]=0;
    for(int i=0;i<10000;++i) {
        const float x=(i%200<100 ? .5f:.05f); auto y=fx.process(x,x,s);
        const float actual=-20.f*std::log10(y[0]/x);
        check(std::abs(actual-fx.gainReductionDb())<.0001f,"meter differs from applied compressor gain");
        check(y[0]==y[1],"stereo detector link");
    }
    s[P::compressorRatio]=1; fx.reset();
    for(int i=0;i<1024;++i) check(fx.process(.25f,-.125f,s)==std::array<float,2>{.25f,-.125f},"ratio-one transfer");
}
void transferAndRealtime() {
    for(double sr:rates) for(float knee : {0.f,6.f}) for(float ratio : {1.f,4.f,20.f}) {
        CreativeEffect fx; CreativeSettings s; fx.prepare(sr,CreativeKind::compressor);
        s[P::compressorThreshold]=-20; s[P::compressorRatio]=ratio; s[P::compressorKnee]=knee;
        s[P::compressorSidechainHP]=20; s[P::compressorAttack]=.1f; s[P::compressorRelease]=10;
        const float amplitude=knee==0 ? .25f:.1f; std::array<float,2> y{};
        for(int i=0;i<int(sr*.2);++i) y=fx.process(i%2 ? amplitude:-amplitude,0,s);
        // Exact steady-state one-pole HP response at Nyquist, followed by the
        // independent static dB hard/soft-knee transfer. Not a DSP self-render.
        const double alpha=1-std::exp(-2*pi*20/sr);
        const double detector=amplitude*2*(1-alpha)/(2-alpha);
        const double over=20*std::log10(detector)+20, slope=1-1/ratio;
        const double reduction=knee>0 && over>-knee*.5 && over<knee*.5
            ? slope*(over+knee*.5)*(over+knee*.5)/(2*knee):slope*std::max(0.,over);
        check(std::abs(std::abs(y[0])/amplitude-std::pow(10.,-reduction/20))<.0002,"static compressor transfer");
    }
    volatile float realtimeSink=0;
    for(auto kind:kinds) {
        CreativeEffect fx; CreativeSettings s; fx.prepare(48000,kind);
        audioCheck=true;
        for(int i=0;i<10000;++i) { const auto y=fx.process(.1f,-.1f,s); const auto snap=fx.grainSnapshot();
            realtimeSink=y[0]+float(snap.count)+fx.gainReductionDb(); }
        fx.reset(); audioCheck=false;
    }
    (void)realtimeSink;
    check(audioAllocations==0 && audioFrees==0,"audio process/snapshot/reset allocated/freed");
}
void timing() {
    volatile float sink=0;
    for(auto kind:kinds) {
        CreativeEffect fx; CreativeSettings s; fx.prepare(48000,kind);
        s[P::granularSize]=250; s[P::granularDensity]=80;
        for(int i=0;i<48000;++i) fx.process(tone(i,48000),tone(i,48000),s);
        for(int block : {1,17,64,511}) {
            double total=0,worst=0;
            for(int k=0;k<512;++k) {
                const auto begin=std::chrono::steady_clock::now();
                for(int j=0;j<block;++j) { const auto y=fx.process(.1f,-.05f,s); sink=y[0]+fx.gainReductionDb(); }
                const auto snapshot=fx.grainSnapshot(); sink=float(snapshot.count)+sink;
                const double us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-begin).count();
                total+=us; worst=std::max(worst,us);
            }
            std::printf("timing kind=%d block=%d mean_us=%.3f max_us=%.3f (not a deadline guarantee)\n",int(kind),block,total/512,worst);
        }
    }
    (void)sink;
}
int main() {
    try { boundaries(); partitionAndEndpoints(); reverbLevel(); grainHistoryAndFreeze(); grainTransitionRetirement(); compressorMeter(); transferAndRealtime();
        std::puts("DSP audit regressions passed: six rates, boundaries, endpoints, freeze/history, calibration, applied GR"); timing(); }
    catch(const std::exception& e) { std::fprintf(stderr,"FAIL: %s\n",e.what()); return 1; }
}
