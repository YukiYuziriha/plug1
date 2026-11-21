// SPDX-License-Identifier: GPL-3.0-or-later
// Test-only framework replacement for the retained, pinned Vital filter classes.
// Not used by production DSP. A single SIMD lane is emulated in double precision;
// original float constants are retained. Source-derived futils and Memory below
// carry Matt Tytel's Copyright 2013-2019 and GPL-3.0-or-later (see ../LICENSE).
#pragma once
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <memory>
#include <vector>
#define force_inline inline
#define JUCE_LEAK_DETECTOR(x)
#define VITAL_ASSERT(x) assert(x)
namespace vital_reference {
using mono_float=float;
inline constexpr float kPi=3.14159265358979323846f;
inline constexpr float kMinNyquistMult=.45351473923f;
inline constexpr float kNotesPerOctave=12;
inline constexpr int kNumFormants=4;
struct poly_mask {
    bool value=false;
    poly_mask()=default; poly_mask(bool x):value(x){}
    bool anyMask()const{return value;} int sum()const{return value?1:0;} bool operator[](int)const{return value;}
    friend poly_mask operator~(poly_mask a){return !a.value;}
};
struct poly_float {
    double v=0;
    static constexpr int kSize=1;
    poly_float()=default; poly_float(double x):v(x){}
    double operator[](int)const{return v;}
    friend poly_float operator+(poly_float a,poly_float b){return a.v+b.v;}
    friend poly_float operator-(poly_float a,poly_float b){return a.v-b.v;}
    friend poly_float operator*(poly_float a,poly_float b){return a.v*b.v;}
    friend poly_float operator/(poly_float a,poly_float b){return a.v/b.v;}
    friend poly_float operator-(poly_float a){return -a.v;}
    friend poly_float operator&(poly_float a,poly_mask b){return b.value?a:poly_float(0);}
    poly_float& operator+=(poly_float a){v+=a.v;return *this;}
    poly_float& operator-=(poly_float a){v-=a.v;return *this;}
    poly_float& operator*=(poly_float a){v*=a.v;return *this;}
    poly_float& operator/=(poly_float a){v/=a.v;return *this;}
    static poly_float abs(poly_float x){return std::abs(x.v);}
    static poly_float min(poly_float a,poly_float b){return std::min(a.v,b.v);}
    static poly_float max(poly_float a,poly_float b){return std::max(a.v,b.v);}
    static poly_mask lessThan(poly_float a,poly_float b){return a.v<b.v;}
};
namespace constants {
inline const poly_mask kFullMask{true};
enum FilterModel { kAnalog,kComb,kDigital,kDirty,kLadder,kDiode,kFormant,kPhase };
}
namespace utils {
inline poly_float mulAdd(poly_float a,poly_float b,poly_float c){return a+b*c;}
inline poly_float maskLoad(poly_float a,poly_float b,poly_mask m){return m.value?b:a;}
inline poly_float min(poly_float a,poly_float b){return poly_float::min(a,b);}
inline poly_float max(poly_float a,poly_float b){return poly_float::max(a,b);}
inline float max(float a,float b){return std::max(a,b);}
inline poly_float clamp(poly_float x,poly_float lo,poly_float hi){return std::clamp(x.v,lo.v,hi.v);}
inline poly_float sqrt(poly_float x){return std::sqrt(x.v);}
inline poly_float sin(poly_float x){return std::sin(x.v);}
inline poly_float tan(poly_float x){return std::tan(x.v);}
inline poly_float interpolate(poly_float a,poly_float b,poly_float t){return a+(b-a)*t;}
inline poly_float midiNoteToFrequency(poly_float x){return 440*std::exp2((x.v-69)/12);}
inline poly_float frequencyToMidiNote(poly_float x){return 69+12*std::log2(x.v/440);}
inline poly_float ratioToMidiTranspose(poly_float x){return 12*std::log2(x.v);}
inline poly_float dbToMagnitude(poly_float x){return std::pow(10.,x.v/20);}
inline bool isFinite(poly_float x){return std::isfinite(x.v);}
template<class T> T pass(T x){return x;}
}
namespace futils {
inline poly_float tanh(poly_float value) {
    poly_float abs_value=poly_float::abs(value),square=value*value;
    poly_float part_num1=abs_value*.821226666969744f+.893229853513558f;
    poly_float part_num2=square*part_num1+2.45550750702956f;
    poly_float num=value*(abs_value*2.45550750702956f+part_num2);
    poly_float part_den=poly_float::abs(abs_value*.814642734961073f*value+value);
    poly_float den=part_den*(square+2.44506634652299f)+2.44506634652299f;
    return num/den;
}
inline poly_float hardTanh(poly_float value) {
    constexpr float h=.66f;
    poly_float clamped=poly_float::max(poly_float::min(value,h),-h);
    return clamped+tanh((value-clamped)*(1.f/(1.f-h)))*(1.f-h);
}
inline poly_float quickTanh(poly_float value){poly_float s=value*value;return value/(s/(3.f+s*.2f)+1.f);}
inline poly_float algebraicSat(poly_float value){poly_float s=value*value;return value*s*(-.9f)/(s+3.f)+value;}
inline poly_float pow(poly_float a,poly_float b){return std::pow(a.v,b.v);}
inline poly_float exp2(poly_float exponent){
    static constexpr float c0=1,c1=static_cast<float>(16970./24483.),c2=static_cast<float>(1960./8161.);
    static constexpr float c3=static_cast<float>(1360./24483.),c4=static_cast<float>(80./8161.),c5=static_cast<float>(32./24483.);
    const double integer=std::nearbyint(exponent.v);
    poly_float t=exponent-poly_float(integer),int_pow=std::ldexp(1.,static_cast<int>(integer));
    poly_float cubic=t*(t*(t*c5+c4)+c3)+c2;
    poly_float interpolate=t*(t*cubic+c1)+c0;
    return int_pow*interpolate;
}
inline poly_float midiOffsetToRatio(poly_float a){return exp2(a*(1.f/12.f));}
inline poly_float dbToMagnitude(poly_float x){return exp2(x*(1.f/6.02059991329f));}
}
// Intentional reference deviation: evaluate upstream coefficient function
// directly rather than its interpolated LUT. Production does likewise.
template<float(*Function)(float),int Size> struct OneDimLookup {
    poly_float cubicLookup(poly_float x)const{return Function(static_cast<float>(x.v));}
};
struct Buffer { poly_float buffer[128]{}; };
struct Input {
    Buffer storage; Buffer* source=&storage;
    poly_float at(int n)const{return source->buffer[n];}
};
class Processor {
public:
    Processor(int,int){}
    virtual ~Processor()=default;
    virtual Processor* clone()const{return nullptr;}
    virtual void process(int){}
    virtual void processWithInput(const poly_float*,int){}
    virtual void reset(poly_mask){}
    virtual void hardReset(){}
    Input* input(int i){return &inputs_[static_cast<std::size_t>(i)];}
    Buffer* output(){return &out_;}
    bool inputMatchesBufferSize(int)const{return true;}
    int getSampleRate()const{return sr_;}
    void setSampleRate(int sr){sr_=sr;}
    poly_mask getResetMask(int i){return input(i)->at(0).v!=0;}
    void set(int i,double value){input(i)->source->buffer[0]=value;}
private:
    std::array<Input,16> inputs_{}; Buffer out_; int sr_=48000;
};
// Test-only single-lane implementation of upstream Memory::get: explicit four
// Catmull weights, power-of-two buffer, push-advances-first convention retained.
class Memory {
public:
    static constexpr float kMinPeriod=2;
    explicit Memory(int n){std::size_t size=16;while(size<static_cast<std::size_t>(n))size*=2;data.assign(size,0);}
    int getSize()const{return static_cast<int>(data.size());}
    int getMaxPeriod()const{return getSize()-3;}
    void clearMemory(int,poly_mask m){if(m.value)std::fill(data.begin(),data.end(),0);}
    void push(poly_float x){offset=(offset+1)%data.size();data[offset]=x.v;}
    poly_float get(poly_float past)const {
        const int index=static_cast<int>(past.v);const double t=index-past.v+1,t2=t*t,t3=t2*t;
        const std::array<double,4> weights{{t2-.5*t3-.5*t,1+1.5*t3-2.5*t2,.5*t+2*t2-1.5*t3,.5*t3-.5*t2}};
        double result=0;
        for(int i=0;i<4;++i){const auto at=(offset+2*data.size()-static_cast<std::size_t>(index)-2+static_cast<std::size_t>(i))%data.size();result+=weights[static_cast<std::size_t>(i)]*data[at];}
        return result;
    }
private:
    std::vector<double> data;std::size_t offset=0;
};
namespace cr {
struct Value {double v;explicit Value(double x):v(x){} poly_float value()const{return v;}};
}
} // namespace vital_reference
