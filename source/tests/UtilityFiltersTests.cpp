// Standalone C++17, no JUCE, build system or project libraries required.
// MSVC developer prompt, from plug1 (artifacts outside the source tree):
// cl /nologo /std:c++17 /EHsc /O2 /W4 source/tests/UtilityFiltersTests.cpp /Fo:%TEMP%/UtilityFiltersTests.obj /Fe:%TEMP%/UtilityFiltersTests.exe
// %TEMP%/UtilityFiltersTests.exe
// Portable: c++ -std=c++17 -O2 source/tests/UtilityFiltersTests.cpp -o /tmp/UtilityFiltersTests && /tmp/UtilityFiltersTests
#include "../src/UtilityFilters.h"
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4100 4244 4267 4305 4456 4458)
#endif
#include "../../ThirdParty/Vital/reference/Upstream.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <new>
#include <stdexcept>
#if defined(_MSC_VER)
#include <malloc.h>
#endif

namespace {
thread_local bool audioThread = false;
std::atomic<unsigned> allocations{0}, destructions{0};
}
void* operator new(std::size_t n) {
    if (audioThread) ++allocations;
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept {
    if (p && audioThread) ++destructions;
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { ::operator delete(p); }
#if defined(__cpp_aligned_new)
void* operator new(std::size_t n, std::align_val_t a) {
    if (audioThread) ++allocations;
#if defined(_MSC_VER)
    if (void* p = _aligned_malloc(n ? n : 1, static_cast<std::size_t>(a))) return p;
#else
    const auto alignment = static_cast<std::size_t>(a);
    if (void* p = std::aligned_alloc(alignment, ((n ? n : 1)+alignment-1)/alignment*alignment)) return p;
#endif
    throw std::bad_alloc();
}
void operator delete(void* p, std::align_val_t) noexcept {
    if (p && audioThread) ++destructions;
#if defined(_MSC_VER)
    _aligned_free(p);
#else
    std::free(p);
#endif
}
void operator delete(void* p, std::size_t, std::align_val_t a) noexcept { ::operator delete(p,a); }
void* operator new[](std::size_t n, std::align_val_t a) { return ::operator new(n,a); }
void operator delete[](void* p, std::align_val_t a) noexcept { ::operator delete(p,a); }
void operator delete[](void* p, std::size_t, std::align_val_t a) noexcept { ::operator delete(p,a); }
#endif

using namespace vocalstack;
namespace {
using P = UtilityParameter;
constexpr double pi = 3.14159265358979323846;
constexpr std::array<UtilityKind,5> kinds {{UtilityKind::gain, UtilityKind::stereo,
    UtilityKind::gate, UtilityKind::xpFilter, UtilityKind::synthFilter}};
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void near(double a, double b, double tolerance, const char* message) { check(std::abs(a-b) <= tolerance, message); }
void finite(const std::array<float,2>& x) { check(std::isfinite(x[0]) && std::isfinite(x[1]), "Nonfinite output"); }
void descriptors() {
    check(utilityParameterCount == 21, "Parameter index/count contract changed");
    const std::array<const char*,21> ids {{"gain_db", "stereo_width", "stereo_balance", "stereo_swap",
        "stereo_invert_left", "stereo_invert_right", "gate_threshold", "gate_range", "gate_ratio",
        "gate_attack", "gate_hold", "gate_release", "xp_mode", "xp_cutoff", "xp_resonance", "xp_drive",
        "synth_mode", "synth_cutoff", "synth_resonance", "synth_drive", "synth_feedback"}};
    UtilitySettings s;
    for (std::size_t i=0; i<utilityParameterCount; ++i) {
        const auto& d=utilityParameters[i];
        check(std::strcmp(d.id,ids[i])==0, "Parameter ID/order changed");
        check(d.label && *d.label && d.unit && d.min<d.max && d.initial>=d.min && d.initial<=d.max, "Invalid descriptor");
        check(s.values[i]==d.initial, "Wrong default");
        for (std::size_t j=0; j<i; ++j) check(std::strcmp(d.id,utilityParameters[j].id)!=0,"Duplicate ID");
    }
    UtilityEffect cold;
    check(cold.process(1,1,s)==std::array<float,2>{},"Unprepared processing unsafe");
    check(xpFilterConfigurations.size()==15 && synthFilterConfigurations.size()==13,"Missing documented models");
}
void gainStereo() {
    UtilityEffect fx; UtilitySettings s;
    fx.prepare(48000,UtilityKind::gain);
    check(fx.process(128,-256,s)==std::array<float,2>{{128,-256}},"Unity gain clamps normal finite input");
    for (float db : {-119.f,-60.f,6.f,24.f}) {
        s[P::gainDb]=db; fx.reset(); const auto y=fx.process(10,-20,s);
        near(y[0],10*std::pow(10.,db/20.),1.e-4,"Wrong gain mapping");
        near(y[1],-2*y[0],1.e-5,"Gain stereo mismatch");
    }
    s[P::gainDb]=-120; fx.reset(); check(fx.process(1,-1,s)==std::array<float,2>{},"Bottom not exact mute");
    s[P::gainDb]=0; fx.reset(); fx.process(1,1,s); s[P::gainDb]=-120;
    const auto first=fx.process(1,1,s); check(first[0]>.99f && first[0]<1,"Gain change not smoothed");
    for (int i=0;i<20000;++i) fx.process(1,1,s);
    check(fx.process(1,1,s)[0]==0,"Mute never reaches zero");
    fx.prepare(48000,UtilityKind::stereo); s=UtilitySettings{};
    check(fx.process(.7f,-.3f,s)==std::array<float,2>{{.7f,-.3f}},"Stereo default not transparent");
    s[P::stereoWidth]=0; fx.reset(); auto y=fx.process(.7f,-.3f,s);
    near(y[0],.2,1.e-7,"Zero width not mono"); check(y[0]==y[1],"Mono width channels differ");
    s[P::stereoWidth]=200; fx.reset(); y=fx.process(.5f,-.5f,s);
    near(y[0],1,1.e-7,"Side width wrong"); near(y[1],-1,1.e-7,"Side width wrong");
    s=UtilitySettings{}; s[P::stereoSwap]=1; s[P::stereoInvertLeft]=1; fx.reset();
    y=fx.process(.7f,-.3f,s); near(y[0],.3,1.e-7,"Swap/output polarity order wrong"); near(y[1],.7,1.e-7,"Swap wrong");
    for (float balance : {-100.f,100.f}) {
        s=UtilitySettings{}; s[P::stereoBalance]=balance; fx.reset(); y=fx.process(.4f,.4f,s);
        near(y[balance<0?1:0],0,0,"Balance endpoint not muted"); near(y[balance<0?0:1],.4,1.e-7,"Balance boosted surviving side");
    }
}
void gate() {
    UtilityEffect fx; UtilitySettings s;
    s[P::gateThreshold]=-20; s[P::gateRange]=80; s[P::gateRatio]=4;
    s[P::gateHold]=0; s[P::gateAttack]=1; s[P::gateRelease]=10;
    fx.prepare(48000,UtilityKind::gate);
    std::array<float,2> y{};
    for (int i=0;i<24000;++i) y=fx.process(.01f,-.005f,s);
    near(fx.gainReductionDb(),60,.001,"Expander ratio incorrect");
    near(y[0],.00001,1.e-8,"Gate attenuation wrong"); near(y[1],-.5*y[0],1.e-10,"Gate not stereo linked");
    for (int i=0;i<4800;++i) y=fx.process(.001f,1,s);
    check(fx.gainReductionDb()<.001f,"Right side cannot open linked gate");
    s[P::gateHold]=10; s[P::gateRelease]=1; fx.reset(); fx.process(1,1,s);
    for (int i=0;i<480;++i) { fx.process(0,0,s); near(fx.gainReductionDb(),0,1.e-6,"Hold closed early"); }
    for (int i=0;i<480;++i) fx.process(0,0,s);
    check(fx.gainReductionDb()>79,"Release failed after hold");
    s[P::gateHold]=0; s[P::gateAttack]=10; fx.reset();
    for (int i=0;i<4800;++i) fx.process(0,0,s);
    const double closed=fx.gainReductionDb();
    for (int i=0;i<480;++i) fx.process(1,1,s);
    near(fx.gainReductionDb(),closed/std::exp(1.),.01,"Opening attack time constant incorrect");
    s[P::gateRatio]=1; fx.reset();
    check(fx.process(.01f,-.02f,s)==std::array<float,2>{{.01f,-.02f}},"Unity ratio not transparent");
    s[P::gateRatio]=20; s[P::gateRange]=0; fx.reset();
    check(fx.process(.01f,-.02f,s)==std::array<float,2>{{.01f,-.02f}},"Zero range not transparent");
}
void responses() {
    // Direct DFT of actual impulse response, not a second formula for expected
    // coefficients. Covers all models, including fractional comb interpolation,
    // ladder feedback, complex formant summation, cascades, DC and Nyquist.
    for (double sr : {8000.,48000.,192000.}) for (bool xp : {false,true}) {
        for (int mode=0;mode<(xp?15:vital_filter::modeCount);++mode) for (float drive : {0.f,12.f}) {
            UtilitySettings s; UtilityEffect fx;
            s[xp?P::xpDrive:P::synthDrive]=drive;
            const auto kind=xp?UtilityKind::xpFilter:UtilityKind::synthFilter;
            s[xp?P::xpMode:P::synthMode]=static_cast<float>(mode);
            s[xp?P::xpCutoff:P::synthCutoff]=873;
            s[xp?P::xpResonance:P::synthResonance]=65;
            s[P::synthFeedback]=-73;
            fx.prepare(sr,kind);
            const auto curve=xp?std::vector<UtilityResponse>{}:UtilityEffect::synthResponseCurve(sr,s);
            std::array<double,6> hz {{0,100,873,2400,sr*.48,sr*.5}};
            if(!xp)for(auto& h:hz)h=std::round(h/sr*32768)*sr/32768;
            std::array<std::complex<double>,6> sum{}, phase{}, rotation{};
            for (std::size_t j=0;j<hz.size();++j) { phase[j]=1; rotation[j]=std::polar(1.,-2*pi*hz[j]/sr); }
            for (int i=0;i<32768;++i) {
                const auto y=fx.process(i==0?1.e-5f:0,0,s);
                check(y[1]==0,"Filter crosstalk");
                for (std::size_t j=0;j<hz.size();++j) { sum[j]+=static_cast<double>(y[0])*phase[j]/1.e-5; phase[j]*=rotation[j]; }
            }
            for (std::size_t j=0;j<hz.size();++j) {
                const auto r=xp?fx.response(hz[j],s):curve[static_cast<std::size_t>(std::llround(hz[j]/sr*32768))];
                near(std::abs(sum[j]),r.magnitude,2.e-4*(1+r.magnitude),"Response helper disagrees with implemented impulse response");
                check(r.approximate==(!xp || drive>0),"Incorrect approximation flag");
            }
            if(xp){s[P::xpDrive]=6;check(fx.response(1000,s).approximate,"Nonlinear response must be marked approximate");}
        }
    }
    UtilitySettings s;
    // Vital BP is raw band (not the old constant-peak stand-in). Its Q and
    // gain are checked against upstream classes below, not the old formula.
    auto mag=[&s](int mode,double hz) {
        s[P::synthMode]=static_cast<float>(mode);
        return UtilityEffect::response(48000,UtilityKind::synthFilter,hz,s).magnitude;
    };
    check(mag(0,100)>.95 && mag(0,10000)<.01,"LP not low pass");
    check(mag(1,100)<.02 && mag(1,10000)>.95,"HP not high pass");
    check(mag(2,1000)>.49 && mag(2,100)<.15,"Vital BP shape incorrect");
    check(mag(3,1000)<.002,"Vital notch missing");
    for (int m : {4,9,14}) {
        s[P::xpMode]=static_cast<float>(m);
        near(UtilityEffect::response(48000,UtilityKind::xpFilter,3210,s).magnitude,1,1.e-10,"Allpass magnitude changed");
    }
}
// This harness exercises the ORIGINAL setupFilter/process/tick implementations
// compiled from the pinned .h/.cpp files (see reference/generate.py). It never
// calls production makeFrame/tick/nonlinearities to calculate expected results.
void upstreamComparison() {
    namespace ref=vital_reference;
    using SF=ref::SynthFilter;
    unsigned comparisons=0;
    double worstRelative=0,worstAbsolute=0;
    for(double x : {-10.,-1.,-.5,0.,.5,1.,10.}) {
        near(vital_filter::tanh(x),ref::futils::tanh(x).v,1.e-12,"Vital rational tanh changed");
        near(vital_filter::hardTanh(x),ref::futils::hardTanh(x).v,1.e-12,"Vital hardTanh changed");
        near(vital_filter::algebraicSat(x),ref::futils::algebraicSat(x).v,1.e-12,"Vital algebraicSat changed");
        near(vital_filter::quickTanh(x),ref::futils::quickTanh(x).v,1.e-12,"Vital quickTanh changed");
    }
    for(double db : {0.,3.,12.,24.})near(vital_filter::driveGain(db),ref::futils::dbToMagnitude(db).v,1.e-12,"Vital drive conversion changed");
    for(int sr : {8000,48000,192000})for(int mode=0;mode<vital_filter::modeCount;++mode)
    for(double resonance : {0.,40.,70.})for(double drive : {0.,12.})for(double cutoff : {87.,873.,17000.}) {
        cutoff=std::min(cutoff,sr*.45);
        vital_filter::Parameters p{mode,cutoff,resonance,drive,-73};
        const auto frame=vital_filter::makeFrame(sr,p);
        vital_filter::Channel actual; actual.prepare(sr);
        std::unique_ptr<ref::Processor> upstream;
        std::array<ref::DigitalSvf,4> formants;
        if(mode<4 || mode==10)upstream=std::make_unique<ref::DigitalSvf>();
        else if(mode==4 || mode==11)upstream=std::make_unique<ref::LadderFilter>();
        else if(mode==5)upstream=std::make_unique<ref::CombFilter>(sr/20+32);
        else if(mode==7)upstream=std::make_unique<ref::DirtyFilter>();
        else if(mode==8)upstream=std::make_unique<ref::DiodeFilter>();
        else if(mode==9 || mode==12)upstream=std::make_unique<ref::SallenKeyFilter>();
        const auto setup=[&](ref::Processor& f,double hz,double rp,double inputDrive,double gain,int style,double blend){
            f.setSampleRate(sr);f.set(SF::kMidiCutoff,69+12*std::log2(hz/440));
            f.set(SF::kResonance,rp);f.set(SF::kDriveGain,inputDrive);f.set(SF::kGain,gain);
            f.set(SF::kStyle,style);f.set(SF::kPassBlend,blend);
        };
        if(upstream) {
            const int style=(mode==4 || mode==10 || mode==11)?SF::k24Db:mode==3?SF::kNotchPassSwap:SF::k12Db;
            const double blend=(mode==1 || mode==11 || mode==12)?2:(mode==2 || mode==3)?1:0;
            setup(*upstream,cutoff,mode==5?(-.73+1)/2:resonance*.01,drive,0,style,blend);
        } else {
            for(int j=0;j<4;++j) {
                // Original FormantFilter::setupFilter interpolation, style 0,
                // no spread, transpose from cutoff/1000, X from feedback.
                const auto& corners=ref::formant_style2;
                const auto fs=ref::interpolateFormants(corners[2][j],corners[3][j],corners[0][j],corners[1][j],(-73.+95)/190,.5);
                const double hz=std::clamp(440*std::exp2((fs.midi_cutoff.v-69)/12)*cutoff/1000,1.,sr*.45);
                formants[static_cast<std::size_t>(j)].setResonanceBounds(4,30);
                setup(formants[static_cast<std::size_t>(j)],hz,fs.resonance_percent.v*resonance*.01,0,fs.gain.v,SF::k12Db,1);
            }
        }
        double error=0,energy=0,maxError=0;
        const int samples=mode==5?8192:1024;
        for(int i=0;i<samples;++i) {
            // Alternating small/large excitation checks actual nonlinearities,
            // not only a shared linearized transfer or one impulse fixture.
            const double input=(i<samples/2?.03:.8)*(std::sin(.127*i)+.2*std::cos(.037*i));
            double expected=0;
            if(upstream){upstream->set(SF::kReset,i==0?1:0);upstream->set(SF::kAudio,input);upstream->process(1);expected=upstream->output()->buffer[0].v;}
            else for(auto& f:formants){f.set(SF::kReset,i==0?1:0);f.set(SF::kAudio,input*ref::futils::dbToMagnitude(drive).v);f.process(1);expected+=f.output()->buffer[0].v;}
            const double got=actual.tick(input,frame),e=got-expected;
            check(std::isfinite(got) && std::isfinite(expected),"Nonfinite upstream comparison");
            error+=e*e;energy+=expected*expected;maxError=std::max(maxError,std::abs(e));
        }
        ++comparisons;
        worstRelative=std::max(worstRelative,std::sqrt(error/(energy+1.e-12)));
        worstAbsolute=std::max(worstAbsolute,maxError);
        if(!(std::sqrt(error/(energy+1.e-12))<.003 || maxError<1.e-5)) {
            std::cerr<<"Reference mismatch: mode="<<mode<<" sr="<<sr<<" cutoff="<<cutoff<<" resonance="<<resonance<<" drive="<<drive
                     <<" relative RMS="<<std::sqrt(error/(energy+1.e-12))<<" max="<<maxError<<'\n';
            check(false,"Scalar adaptation differs from original upstream filter classes");
        }
    }
    std::cout<<"Vital upstream class comparisons: "<<comparisons<<", worst relative RMS="<<worstRelative
             <<", worst absolute error="<<worstAbsolute<<'\n';
}
void safetyAndRealtime() {
    const float nan=std::numeric_limits<float>::quiet_NaN();
    for (double sr : {8000.,44100.,48000.,96000.,192000.,384000.,0.,std::numeric_limits<double>::infinity()}) {
        for (auto kind:kinds) {
            UtilityEffect fx; UtilitySettings s; fx.prepare(sr,kind);
            for (int i=0;i<128;++i) check(fx.process(0,0,s)==std::array<float,2>{},"Initial silence nonzero");
            std::array<std::array<float,2>,128> reference{};
            fx.reset();
            for (int i=0;i<128;++i) reference[static_cast<std::size_t>(i)]=fx.process(i==0?.1f:0,i==1?-.3f:0,s);
            fx.reset();
            for (int i=0;i<128;++i) check(fx.process(i==0?.1f:0,i==1?-.3f:0,s)==reference[static_cast<std::size_t>(i)],"Reset nondeterministic");
            const int modes=kind==UtilityKind::xpFilter?15:kind==UtilityKind::synthFilter?vital_filter::modeCount:1;
            audioThread=true;
            for (int mode=0;mode<modes;++mode) {
                for (int extreme=0;extreme<3;++extreme) {
                    for (std::size_t p=0;p<s.values.size();++p)
                        s.values[p]=extreme==0?utilityParameters[p].min:extreme==1?utilityParameters[p].max:nan;
                    s[P::xpMode]=static_cast<float>(mode); s[P::synthMode]=static_cast<float>(mode);
                    fx.reset();
                    for (int i=0;i<4096;++i) {
                        if (i==2048) { s[P::xpCutoff]=19000; s[P::synthCutoff]=19000; }
                        const float x=static_cast<float>(.3*std::sin(.3*i));
                        const float input=i==500?nan:i==501?std::numeric_limits<float>::infinity():x;
                        auto y=fx.process(input,input,s);
                        finite(y);
                        if (kind!=UtilityKind::stereo) near(y[0],y[1],.0001,"Identical mono inputs diverged");
                        check(std::abs(y[0])<10000 && std::abs(y[1])<10000,"Unstable filter under extreme automation");
                        if (kind!=UtilityKind::synthFilter && (i%511)==0) check(std::isfinite(fx.response(i,s).magnitude),"Nonfinite graph");
                    }
                }
            }
            // Exercise actual discrete transitions without resets and all
            // continuous targets at irregular sample intervals.
            for (int i=0;i<8192;++i) {
                if (i%127==0) for (std::size_t p=0;p<s.values.size();++p)
                    s.values[p]=((i/127)&1)?utilityParameters[p].min:utilityParameters[p].max;
                finite(fx.process(.2f,-.1f,s));
            }
            finite(fx.process(std::numeric_limits<float>::max(),-std::numeric_limits<float>::max(),s));
            for (int i=0;i<1000;++i) finite(fx.process(0,0,s));
            fx.reset(); check(fx.gainReductionDb()==0,"Meter not reset");
            check(fx.process(0,0,s)==std::array<float,2>{},"Reset left tail");
            audioThread=false;
        }
    }
    check(allocations==0 && destructions==0,"Audio processing/reset allocated or freed memory");
}
} // namespace
int main() {
    try {
        descriptors(); gainStereo(); gate(); upstreamComparison(); responses(); safetyAndRealtime();
        std::cout << "UtilityFilters: all tests passed (including response DFT and zero audio allocations/frees)\n";
        return 0;
    } catch (const std::exception& e) {
        audioThread=false;
        std::cerr << "UtilityFilters: " << e.what() << '\n'; return 1;
    }
}
