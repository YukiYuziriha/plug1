// Standalone, JUCE-independent audit. Compile with C++17; see run-dsp-audit.py.
#include "CreativeEffects.h"
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>
using namespace vocalstack;
using P = CreativeParameter;
constexpr double pi = 3.14159265358979323846;
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
struct Noise {
    unsigned state = 0x12345678;
    float next() { state ^= state << 13; state ^= state >> 17; state ^= state << 5;
        return (float(state >> 8) / 8388608.f - 1.f) * .2f; }
};
void measure(const char* audioPath) {
    std::FILE* audio = audioPath ? std::fopen(audioPath, "wb") : nullptr;
    if (audioPath && !audio) throw std::runtime_error("cannot open audio output");
    std::puts("rate,signal,feature,wet,dry_rms,output_rms,relative_db,peak,tail_rms");
    for (double sr : {8000.,44100.,48000.,96000.,192000.,384000.})
    for (int signal = 0; signal < 5; ++signal)
    for (int feature = 0; feature < 6; ++feature) {
        CreativeEffect fx; fx.prepare(sr, CreativeKind::reverb); CreativeSettings s;
        s[P::reverbDuck] = 0;
        if (feature == 1) s[P::reverbDuck] = 6;
        if (feature == 2) s[P::reverbSwell] = 500;
        if (feature == 3) s[P::reverbSaturation] = 12;
        if (feature == 4) s[P::reverbPitchAmount] = 100;
        if (feature == 5) s[P::reverbEarly] = 0;
        Noise noise; double dry = 0, tail = 0;
        std::array<double,5> energy{}, peak{};
        for (int i = 0; i < int(sr * 8); ++i) {
            float l = noise.next(), r = signal == 1 ? noise.next() : l;
            if (signal == 2) l = r = .2f * float(std::sin(2*pi*997*i/sr));
            if (signal == 3) { const double t = std::fmod(i/sr, .37);
                l = r = float(.35*std::exp(-t*24)*(std::sin(2*pi*173*i/sr)+.3*std::sin(2*pi*1091*i/sr))); }
            if (signal == 4) { l = i == 0 ? 1.f : 0.f; r = l; }
            if (i >= int(sr*5)) l = r = 0;
            auto y = fx.process(l,r,s);
            if (audio && sr == 48000 && signal == 3 && feature == 0) std::fwrite(y.data(),sizeof(float),2,audio);
            if ((signal == 4 || i >= int(sr*2)) && i < int(sr*5)) {
                dry += (l*l+r*r)*.5;
                for (int k=0;k<5;++k) { const float w = k*.25f;
                    const double a=l*(1-w)+y[0]*w, b=r*(1-w)+y[1]*w;
                    energy[k]+=(a*a+b*b)*.5; peak[k]=std::max(peak[k],std::max(std::abs(a),std::abs(b))); }
            }
            if(i >= int(sr*5)) tail += (y[0]*y[0]+y[1]*y[1])*.5;
            require(std::isfinite(y[0]) && std::isfinite(y[1]), "Nonfinite measurement");
        }
        for(int k=0;k<5;++k) std::printf("%.0f,%d,%d,%.2f,%.9g,%.9g,%.6g,%.9g,%.9g\n",sr,signal,feature,k*.25,
            std::sqrt(dry/(sr*(signal==4 ? 5:3))),std::sqrt(energy[k]/(sr*(signal==4 ? 5:3))),10*std::log10(std::max(1e-30,energy[k])/std::max(1e-30,dry)),peak[k],std::sqrt(tail/(sr*3)));
    }
    if(audio) std::fclose(audio);
}
void decay() {
    std::puts("rate,size,target_seconds,T20_extrapolated_seconds,impulse_energy,peak");
    for(double sr : {44100.,48000.,96000.}) for(float size : {0.f,100.f}) for(float target : {.15f,2.8f,20.f}) {
        CreativeEffect fx; CreativeSettings s; fx.prepare(sr,CreativeKind::reverb);
        s[P::reverbDuck]=0; s[P::reverbModulation]=0; s[P::reverbEarly]=0;
        s[P::reverbSize]=size; s[P::reverbDecay]=target;
        // Broadband output-energy decay, NOT a certified midband RT60. The
        // actual damping/output filters remain at defaults deliberately.
        const int samples=int(sr*std::max(3.,double(target)*2));
        std::vector<double> energy(samples); double peak=0;
        for(int i=0;i<samples;++i) { const auto y=fx.process(i==0 ? 1.f:0.f,i==0 ? 1.f:0.f,s);
            energy[i]=(y[0]*y[0]+y[1]*y[1])*.5; peak=std::max(peak,double(std::max(std::abs(y[0]),std::abs(y[1])))); }
        for(int i=samples-2;i>=0;--i) energy[i]+=energy[i+1];
        double st=0,sd=0,stt=0,std=0,n=0;
        for(int i=0;i<samples;++i) {
            const double db=10*std::log10(std::max(1e-300,energy[i])/energy[0]);
            if(db<=-5 && db>=-25) { const double t=i/sr; st+=t; sd+=db; stt+=t*t; std+=t*db; ++n; }
        }
        const double slope=(n*std-st*sd)/(n*stt-st*st);
        require(n>2 && slope<0,"decay fit failed");
        std::printf("%.0f,%.0f,%.9g,%.9g,%.9g,%.9g\n",sr,size,target,-60/slope,energy[0],peak);
    }
}
int main(int argc,char** argv) {
    try { if(argc>1 && std::string(argv[1])=="--decay") decay();
        else if(argc>1) measure(argc>2 ? argv[2]:nullptr); else std::puts("Audit probe ready"); }
    catch(const std::exception& e) { std::fprintf(stderr,"FAIL: %s\n",e.what()); return 1; }
}
