/* Copyright 2013-2019 Matt Tytel
 * Copyright 2026 UtilityFilters contributors (scalar adaptation)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Modified from Vital commit 636ca0ef517a4db087a6a08a6a8a5e704e21f836.
 * This is GPL-covered DERIVED CODE, not an independently invented filter.
 * Distributed WITHOUT ANY WARRANTY; see ../../ThirdParty/Vital/LICENSE and
 * ../../ThirdParty/Vital/README.md for upstream files, hashes and modifications.
 *
 * Scalar/double, two-channel extraction of digital_svf, ladder_filter,
 * dirty_filter, diode_filter, sallen_key_filter, comb_filter and formant_filter.
 * Processor/polyphony/SIMD routing removed; sample controls supplied by owner.
 * Original tick ordering, nonlinearities and selected setupFilter mappings are
 * retained. Analytic coefficient evaluation replaces the 2048-entry lookup.
 */
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace vocalstack { namespace vital_filter {
inline constexpr double pi = 3.14159265358979323846;
inline constexpr int modeCount = 13;
inline constexpr std::array<const char*, modeCount> names {{
    "Digital LP12", "Digital HP12", "Digital BP12", "Digital Notch24",
    "Ladder LP24", "Comb", "Formant A/I/U/O",
    "Dirty LP12", "Diode LP24", "Analog LP12",
    "Digital LP24", "Ladder HP24", "Analog HP12"
}};
struct Parameters {
    int mode = 0;
    double cutoff = 1000, resonance = 0, driveDb = 0, feedback = 50;
};
// These are Vital's *poly_float* rational functions, not std::tanh substitutes.
inline double tanh(double x) noexcept {
    const double a = std::abs(x), s = x*x;
    const double n1 = a*.821226666969744f + .893229853513558f;
    const double n2 = s*n1 + 2.45550750702956f;
    const double num = x*(a*2.45550750702956f+n2);
    const double den = std::abs(a*.814642734961073f*x+x)*(s+2.44506634652299f)+2.44506634652299f;
    return num/den;
}
inline double hardTanh(double x) noexcept {
    const double c = std::clamp(x,-double(.66f),double(.66f));
    return c + tanh((x-c)*(1.f/(1.f-.66f)))*(1.f-.66f);
}
inline double algebraicSat(double x) noexcept { return x*(x*x)*(-.9f)/(x*x+3)+x; }
inline double quickTanh(double x) noexcept { const double s=x*x; return x/(s/(3+s*.2f)+1); }
inline double coefficient(double ratio) noexcept {
    const float p=3.14159265358979323846f*static_cast<float>(ratio);
    return std::tan(p/(p+1.f));
}
// futils::exp2 / dbToMagnitude, retaining the upstream polynomial and float
// coefficients (not a std::pow stand-in for the drive mapping).
inline double exp2(double exponent) noexcept {
    const double integer=std::nearbyint(exponent),t=exponent-integer;
    constexpr float c1=static_cast<float>(16970./24483.),c2=static_cast<float>(1960./8161.);
    constexpr float c3=static_cast<float>(1360./24483.),c4=static_cast<float>(80./8161.),c5=static_cast<float>(32./24483.);
    const double cubic=t*(t*(t*c5+c4)+c3)+c2;
    return std::ldexp(t*(t*cubic+c1)+1,static_cast<int>(integer));
}
inline double driveGain(double db) noexcept { return exp2(db*(1.f/6.02059991329f)); }
inline double midiHz(double midi) noexcept { return 440*std::exp2((midi-69)/12); }
inline double flush(double x) noexcept { return std::abs(x)<1.e-30 ? 0 : x; }

struct Pole {
    double state=0, sat=0, current=0;
    double basic(double x,double g) noexcept {
        const double d=g*(x-state); state+=d; current=state; state=flush(state+d); return current;
    }
    // 0 identity, 1 algebraicSat, 2 quickTanh, 3 tanh, 4 hard clamp.
    static double saturate(double x,int type) noexcept {
        switch(type) { case 1:return algebraicSat(x); case 2:return quickTanh(x);
            case 3:return tanh(x); case 4:return std::clamp(x,-1.,1.); default:return x; }
    }
    double tick(double x,double g,int type) noexcept {
        const double d=g*(x-sat); state+=d; current=saturate(state,type);
        state=flush(state+d); sat=flush(saturate(state,type)); return current;
    }
};
struct Digital {
    double s1=0,s2=0,pre1=0,pre2=0;
    static double basic(double x,double g,double r,double drive,
                        const std::array<double,3>& blend,double& a,double& b) noexcept {
        const double c0=1/(g*g+g*r+1),c1=c0*g,c2=c0*g*g,in=drive*x;
        const double v3=in-b,v1=c0*a+c1*v3,v2=b+c1*a+c2*v3;
        a=flush(2*v1-a); b=flush(2*v2-b);
        return blend[0]*in+blend[1]*v1+blend[2]*v2;
    }
    double tick(double x,double g,double r,double drive,const std::array<double,3>& blend,bool db24=false) noexcept {
        if(db24) x=hardTanh(basic(x,g,1,drive,blend,pre1,pre2));
        return hardTanh(basic(x,g,r,db24?1:drive,blend,s1,s2));
    }
};
// Original formant_filter.cpp tables, style 0 corners: bottom A/I, top U/O.
// Each entry: gain dB, resonance percentage, MIDI cutoff. Four formants, not
// the former two-band category stand-in. Morph X uses feedback control, Y=.5.
inline constexpr double vowels[4][4][3] = {
    {{-2,.66,75.7552343327},{-8,.75,84.5454706023},{-9,1,100.08500317},{-10,1,101.645729657}},
    {{0,.8,61.7825925179},{-15,.75,94.049554095},{-17,1,101.03821678},{-20,1,103.618371471}},
    {{0,.7,65.0382167797},{-20,.75,74.3695077237},{-17,1,100.408607741},{-14,1,101.645729657}},
    {{-2,.7,67.349957715},{-6,.75,79.349957715},{-14,1,99.7552343327},{-14,1,101.03821678}}
};
struct DigitalSettings {
    double g=0,r=2,drive=1,post=1;
    std::array<double,3> blend {{0,0,1}};
};
inline DigitalSettings digitalSettings(double sr,double hz,double resonance,double drive,double gain,
                                      int pass,double minQ=.5,double maxQ=16) noexcept {
    DigitalSettings d;
    const double a=resonance*resonance*resonance;
    d.r=1/(minQ+(maxQ-minQ)*a); d.drive=drive/(2*a+1); d.post=std::pow(10.,gain/20)/std::sqrt(drive);
    d.g=std::tan(3.14159265358979323846f*std::min(.499f,static_cast<float>(hz/sr)));
    // LP / HP / BP. Notch uses upstream kNotchPassSwap at pass_blend=1:
    // low=high=1, band=0, no resonance drive compensation.
    if(pass==0)d.blend={{0,0,1}};
    else if(pass==1)d.blend={{1,-d.r,-1}};
    else if(pass==2)d.blend={{0,1,0}};
    else {d.blend={{1,-d.r,0}};d.drive=drive;}
    return d;
}
struct Frame {
    Parameters p;
    double sr=48000,g=0,r=0,drive=1,post=1,normalizer=1,feed=1,period=2,fb=0,scale=1,combG1=0,combG2=0;
    std::array<DigitalSettings,4> digital{};
};
inline Frame makeFrame(double sr,Parameters p) noexcept {
    Frame f; f.sr=sr;
    p.mode=std::clamp(p.mode,0,modeCount-1); p.cutoff=std::clamp(p.cutoff,20.,sr*.45);
    p.resonance=std::clamp(p.resonance,0.,100.); p.driveDb=std::clamp(p.driveDb,0.,24.);
    p.feedback=std::clamp(p.feedback,-95.,95.); f.p=p;
    const double rp=p.resonance*.01,amp=driveGain(p.driveDb),dp=p.driveDb*(1.f/36.f);
    f.g=coefficient(p.cutoff/sr);
    if(p.mode<4 || p.mode==10) {
        f.digital[0]=digitalSettings(sr,p.cutoff,rp,amp,0,p.mode==10?0:p.mode);
    } else if(p.mode==4 || p.mode==11) {
        f.r=double(.001f)+(double(4.1f)-double(.001f))*std::sin(rp*(.5f*3.14159265358979323846f))+dp*rp*5;
        f.drive=amp*(p.mode==11?1:std::sin(rp)+1); f.post=1/std::sqrt((amp-1)*.25+1);
    } else if(p.mode==7) {
        const double tuned=std::clamp(std::sqrt(rp)/std::max(1.,2*f.g*.25f+.97f),0.,1.);
        f.r=double(.1f)+(double(2.15f)-double(.1f))*tuned+dp*.05f;
        f.normalizer=1.4f/(f.r*f.r+1);
        f.feed=1/((1+f.r*(f.g*f.g-f.g))*(f.g+1));
        f.drive=((amp-1)*2+1)/(f.r*f.r*.5+1);
    } else if(p.mode==8) {
        f.r=double(.7f)+(17-double(.7f))*(rp*rp*rp); f.drive=(f.r*.5+1)*amp; f.post=1/std::sqrt(amp);
        f.combG2=coefficient(20/sr);
    } else if(p.mode==9 || p.mode==12) {
        f.r=(2.15f*std::sqrt(rp)+dp*rp*1.1f)/std::max(1.,2*f.g*.09f+.97f);
        f.normalizer=1/(f.r*(f.g*f.g-f.g)+1);
        f.drive=amp/(2*rp+1); f.post=1/std::sqrt(amp);
    } else if(p.mode==5) {
        // Vital kComb, kLowHighBlend, pass_blend=0 (low-pass feedback).
        // Unlike the other families, upstream CombFilter has no drive control.
        const double raw=p.feedback*.01;
        f.fb=raw/std::sqrt(std::abs(raw)+.00001f); f.scale=1-f.fb*f.fb*.5;
        f.period=std::max(2.,sr/p.cutoff);
        f.combG1=coefficient(p.cutoff/sr); f.combG2=coefficient(std::clamp(p.cutoff/16,1.,sr*.45351473923)/sr);
    } else if(p.mode==6) {
        const double x=(p.feedback+95)/190;
        for(std::size_t i=0;i<4;++i) {
            std::array<double,3> v{};
            for(std::size_t j=0;j<3;++j) v[j]=.5*((1-x)*(vowels[0][i][j]+vowels[2][i][j])+x*(vowels[1][i][j]+vowels[3][i][j]));
            const double hz=std::clamp(midiHz(v[2])*p.cutoff/1000,1.,sr*.45);
            // FormantManager uses bounds 4..30, pass_blend=1, unit drive.
            f.digital[i]=digitalSettings(sr,hz,v[1]*rp,1,v[0],2,4,30);
        }
        // Adapter exposes drive as an input trim here; upstream FormantFilter
        // ignores FilterState.drive. This is not a new saturation substitute.
        f.drive=amp;
    }
    return f;
}
struct Channel {
    std::array<Pole,8> poles{};
    std::array<Digital,4> digital{};
    std::vector<double> memory;
    std::size_t offset=0;
    void prepare(double sr) { memory.assign(static_cast<std::size_t>(std::ceil(sr/20))+16,0.); reset(); }
    void reset() noexcept { poles={};digital={};offset=0;std::fill(memory.begin(),memory.end(),0.); }
    double delayed(double period) const noexcept {
        // Memory::get ordering and Catmull matrix from upstream memory.h and
        // poly_utils.h. offset is the most recently written sample; push below
        // advances first. Preserve that convention, including its +1 sample.
        const auto n=memory.size(),whole=static_cast<std::size_t>(period);
        const double t=1-(period-static_cast<double>(whole)),t2=t*t,t3=t2*t;
        const auto at=[&](std::size_t j){return memory[(offset+n-whole+n-2+j)%n];};
        return at(0)*(-.5*t3+t2-.5*t)+at(1)*(1.5*t3-2.5*t2+1)+
               at(2)*(-1.5*t3+2*t2+.5*t)+at(3)*(.5*t3-.5*t2);
    }
    double tick(double input,const Frame& f) noexcept {
        auto& a=poles;
        const int mode=f.p.mode;
        if(mode<4 || mode==10) {
            const auto& d=f.digital[0];
            return digital[0].tick(input,d.g,d.r,d.drive,d.blend,mode==10 || mode==3)*d.post;
        }
        if(mode==4 || mode==11) {
            const double g1=f.g*1.66f,g2=g1*g1,g3=g1*g2;
            const double fs1=a[3].sat+g1*a[2].sat,fs2=fs1+g2*a[1].sat,fs=fs2+g3*a[0].sat;
            const double in=tanh(input*f.drive-f.r*fs);
            double x=in;
            for(std::size_t j=0;j<4;++j)x=a[j].tick(x,f.g,1);
            if(mode==11)x=in-4*a[0].current+6*a[1].current-4*a[2].current+a[3].current;
            return x*f.post;
        }
        if(mode==7) {
            const double s1=f.normalizer*input,o1=a[0].basic(s1,f.g),o2=a[1].basic(o1,f.g);
            const double pass=o2;
            const double feedback=a[3].sat+pass+f.g*(pass-a[2].sat);
            const double loop=tanh(f.drive*pass+f.r*f.feed*feedback);
            const double o3=a[2].tick(loop,f.g,2); a[3].tick(loop-o3,f.g,2);
            return loop*(1.f/1.4f);
        }
        if(mode==8) {
            // Diode k12Db style with blend=0: HP input section has zero wet
            // amount, so its states cannot affect output. Feedback HP remains.
            const double in=tanh((f.drive*input-f.r*a[6].sat)*.5);
            const double feedbackInput=in+a[4].sat;
            const double hp=a[2].basic(feedbackInput,f.combG2);
            a[3].tick(feedbackInput-hp,f.g,3);
            a[4].tick((a[3].current+a[5].sat)*.5,f.g,0);
            a[5].tick((a[4].current+a[6].sat)*.5,f.g,0);
            return a[6].tick(a[5].current,f.g,4)*f.post;
        }
        if(mode==9 || mode==12) {
            const double feedback=(2*f.g-f.g*f.g-1)*a[0].state+(1-f.g)*a[1].state;
            const double in=tanh((f.drive*input-f.r*feedback)*f.normalizer);
            const double o1=a[0].basic(in,f.g),o2=a[1].basic(o1,f.g);
            return (mode==12?in-2*o1+o2:o2)*f.post;
        }
        if(mode==5) {
            const double read=delayed(f.period);
            const double combine=f.scale*input+read*f.fb;
            const double lp=a[0].basic(combine,f.combG1);
            const double result=lp-a[1].basic(lp,f.combG2);
            if(++offset==memory.size())offset=0;
            memory[offset]=flush(hardTanh(result)); return result;
        }
        double total=0;
        for(std::size_t j=0;j<4;++j) {
            const auto& d=f.digital[j]; total+=digital[j].tick(input*f.drive,d.g,d.r,d.drive,d.blend)*d.post;
        }
        return total;
    }
};
class Filter {
public:
    void prepare(double sr) { sr_=sr; for(auto& c:channels_)c.prepare(sr); reset(); }
    void reset() noexcept {for(auto& c:channels_)c.reset();period_=0;}
    std::array<double,2> process(double left,double right,Parameters p) noexcept {
        Frame f=makeFrame(sr_,p);
        if(period_==0)period_=f.period;
        else period_+=(1-std::exp(-1/(.03*sr_)))*(f.period-period_);
        f.period=period_;
        std::array<double,2> out{{channels_[0].tick(left,f),channels_[1].tick(right,f)}};
        // Exceptional overflow recovery only. No normal-signal output limiter.
        for(std::size_t c=0;c<2;++c)if(!std::isfinite(out[c])){channels_[c].reset();out[c]=0;}
        return out;
    }
private:
    double sr_=48000,period_=0;
    std::array<Channel,2> channels_;
};
}} // namespace vocalstack::vital_filter
