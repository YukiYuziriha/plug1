#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <pluginterfaces/base/ipluginbase.h>
#include <pluginterfaces/base/ibstream.h>
#include <pluginterfaces/vst/ivstcomponent.h>
#include <pluginterfaces/vst/ivstaudioprocessor.h>
#include <pluginterfaces/vst/ivsthostapplication.h>
#include <pluginterfaces/vst/ivsteditcontroller.h>
#include <pluginterfaces/vst/ivstparameterchanges.h>
#include <pluginterfaces/vst/ivstmessage.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace Steinberg;
using namespace Steinberg::Vst;
namespace {
void check(bool x, const char* reason) { if (!x) throw std::runtime_error(reason); }
bool equal(const TUID a, const TUID b) { return std::memcmp(a,b,16) == 0; }
struct Host final : IHostApplication {
    std::atomic<uint32> refs{1};
    tresult PLUGIN_API queryInterface(const TUID iid, void** out) override {
        *out = nullptr;
        if (equal(iid,IHostApplication_iid) || equal(iid,FUnknown_iid)) { *out = static_cast<IHostApplication*>(this); addRef(); return kResultOk; }
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return ++refs; }
    uint32 PLUGIN_API release() override { return --refs; }
    tresult PLUGIN_API getName(String128 name) override {
        const char* text = "Vocal Stack Offline VST3 Test"; int i=0;
        for (; text[i] && i<127; ++i) name[i] = char16(text[i]); name[i]=0; return kResultOk;
    }
    tresult PLUGIN_API createInstance(TUID, TUID, void** out) override { *out=nullptr; return kNoInterface; }
};
struct ParameterQueue final : IParamValueQueue {
    ParamID id = 0; ParamValue value = 0; int32 offset = 17; uint32 refs = 1;
    tresult PLUGIN_API queryInterface(const TUID iid, void** out) override {
        *out = nullptr;
        if (equal(iid, IParamValueQueue_iid) || equal(iid, FUnknown_iid)) { *out = static_cast<IParamValueQueue*>(this); addRef(); return kResultOk; }
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return ++refs; }
    uint32 PLUGIN_API release() override { return --refs; }
    ParamID PLUGIN_API getParameterId() override { return id; }
    int32 PLUGIN_API getPointCount() override { return 1; }
    tresult PLUGIN_API getPoint(int32 index, int32& sampleOffset, ParamValue& parameterValue) override {
        if (index != 0) return kInvalidArgument;
        sampleOffset = offset; parameterValue = value; return kResultOk;
    }
    tresult PLUGIN_API addPoint(int32 sampleOffset, ParamValue parameterValue, int32& index) override {
        offset = sampleOffset; value = parameterValue; index = 0; return kResultOk;
    }
};
struct ParameterChanges final : IParameterChanges {
    ParameterQueue queue; uint32 refs = 1;
    tresult PLUGIN_API queryInterface(const TUID iid, void** out) override {
        *out = nullptr;
        if (equal(iid, IParameterChanges_iid) || equal(iid, FUnknown_iid)) { *out = static_cast<IParameterChanges*>(this); addRef(); return kResultOk; }
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return ++refs; }
    uint32 PLUGIN_API release() override { return --refs; }
    int32 PLUGIN_API getParameterCount() override { return 1; }
    IParamValueQueue* PLUGIN_API getParameterData(int32 index) override { return index == 0 ? &queue : nullptr; }
    IParamValueQueue* PLUGIN_API addParameterData(const ParamID& id, int32& index) override { queue.id = id; index = 0; return &queue; }
};
ParamID fftParameterId(IPluginFactory& factory, IComponent& component, Host& host) {
    TUID controllerClass{};
    check(component.getControllerClassId(controllerClass) == kResultOk, "VST3 controller class unavailable");
    IEditController* controller = nullptr;
    check(factory.createInstance(controllerClass, IEditController_iid, reinterpret_cast<void**>(&controller)) == kResultOk && controller,
          "VST3 automation controller creation failed");
    check(controller->initialize(&host) == kResultOk, "VST3 automation controller initialization failed");
    IConnectionPoint* audioConnection = nullptr; IConnectionPoint* controlConnection = nullptr;
    check(component.queryInterface(IConnectionPoint_iid, reinterpret_cast<void**>(&audioConnection)) == kResultOk && audioConnection,
          "VST3 component connection point unavailable");
    check(controller->queryInterface(IConnectionPoint_iid, reinterpret_cast<void**>(&controlConnection)) == kResultOk && controlConnection,
          "VST3 controller connection point unavailable");
    check(audioConnection->connect(controlConnection) == kResultOk && controlConnection->connect(audioConnection) == kResultOk,
          "VST3 component/controller connection failed");
    bool found = false; ParamID id = 0;
    const auto parameterCount = controller->getParameterCount();
    for (int32 index = 0; index < controller->getParameterCount(); ++index) {
        ParameterInfo info{};
        if (controller->getParameterInfo(index, info) != kResultOk) continue;
        std::string title;
        for (const auto character : info.title) { if (!character) break; title.push_back(char(character)); }
        if (title == "FX 1 Spectral FFT Size") { id = info.id; found = true; break; }
    }
    controlConnection->disconnect(audioConnection); audioConnection->disconnect(controlConnection);
    controlConnection->release(); audioConnection->release();
    controller->terminate(); controller->release();
    if (!found) std::cerr << "VST3 controller parameter count=" << parameterCount << '\n';
    check(found, "VST3 FFT automation parameter metadata missing");
    return id;
}
struct Stream final : IBStream {
    std::vector<char> bytes; int64 cursor=0; uint32 refs=1;
    tresult PLUGIN_API queryInterface(const TUID iid, void** out) override {
        *out=nullptr;
        if (equal(iid,IBStream_iid) || equal(iid,FUnknown_iid)) { *out=static_cast<IBStream*>(this); addRef(); return kResultOk; }
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return ++refs; }
    uint32 PLUGIN_API release() override { return --refs; }
    tresult PLUGIN_API read(void* data, int32 n, int32* done) override {
        const auto count = int32(std::min<int64>(std::max(0,n), int64(bytes.size())-cursor));
        if (count) std::memcpy(data,bytes.data()+cursor,size_t(count)); cursor+=count;
        if (done) *done=count; return count ? kResultOk : kResultFalse;
    }
    tresult PLUGIN_API write(void* data, int32 n, int32* done) override {
        if (n<0) return kInvalidArgument;
        bytes.resize(size_t(cursor+n)); if (n) std::memcpy(bytes.data()+cursor,data,size_t(n)); cursor+=n;
        if (done) *done=n; return kResultOk;
    }
    tresult PLUGIN_API seek(int64 position, int32 mode, int64* result) override {
        const int64 base = mode==kIBSeekCur ? cursor : mode==kIBSeekEnd ? int64(bytes.size()) : 0;
        if (base+position<0 || base+position>int64(bytes.size())) return kInvalidArgument;
        cursor=base+position; if(result) *result=cursor; return kResultOk;
    }
    tresult PLUGIN_API tell(int64* position) override { if(!position) return kInvalidArgument; *position=cursor; return kResultOk; }
};
Stream state(int variant) {
    std::string xml = "<VocalStack><PARAM id=\"levelOn\" value=\"0\"/><PARAM id=\"doubleOn\" value=\"0\"/><PARAM id=\"formantOn\" value=\"0\"/>"
        "<PARAM id=\"bypass\" value=\"" + std::string(variant==1 ? "1" : "0") + "\"/>"
        "<PARAM id=\"globalMix\" value=\"" + std::string(variant==2 ? "0" : "100") + "\"/>"
        "<PARAM id=\"fx1Mix\" value=\"100\"/><PARAM id=\"fx1On\" value=\"1\"/>"
        "<PARAM id=\"fx1MorphAmount\" value=\"100\"/><PARAM id=\"fx1MorphSmoothing\" value=\"0\"/>"
        "<RoutingGraph><Node id=\"1\" type=\"0\"/><Node id=\"14\" type=\"22\"/>"
        "<Node id=\"100\" type=\"23\" mix=\"1\"/><Node id=\"13\" type=\"5\"/>"
        "<Link source=\"1\" destination=\"100\" inputPin=\"0\"/><Link source=\"14\" destination=\"100\" inputPin=\"1\"/>"
        "<Link source=\"100\" destination=\"13\" inputPin=\"0\"/></RoutingGraph></VocalStack>";
    if (variant == 3) xml = "<VocalStack><PARAM id=\"levelOn\" value=\"0\"/><PARAM id=\"doubleOn\" value=\"0\"/><PARAM id=\"formantOn\" value=\"0\"/></VocalStack>";
    Stream result; result.bytes.resize(xml.size()+9);
    const uint32 magic=0x21324356, length=uint32(xml.size());
    std::memcpy(result.bytes.data(),&magic,4); std::memcpy(result.bytes.data()+4,&length,4);
    std::memcpy(result.bytes.data()+8,xml.data(),xml.size()); return result;
}
void render(IPluginFactory& factory, const TUID cid, double rate, int mainChannels, int outputChannels, int auxChannels, int variant) {
    Host host; IComponent* component=nullptr; IAudioProcessor* processor=nullptr;
    check(factory.createInstance(cid,IComponent_iid,reinterpret_cast<void**>(&component)) == kResultOk && component,"VST3 component creation failed");
    check(component->initialize(&host)==kResultOk,"VST3 initialize failed");
    check(component->queryInterface(IAudioProcessor_iid,reinterpret_cast<void**>(&processor))==kResultOk,"VST3 processor query failed");
    check(component->getBusCount(kAudio,kInput)==2 && component->getBusCount(kAudio,kOutput)==1,"VST3 optional bus not exposed");
    BusInfo auxiliary{}; check(component->getBusInfo(kAudio,kInput,1,auxiliary)==kResultOk,"VST3 auxiliary metadata unavailable");
    check(auxiliary.busType==kAux,"VST3 auxiliary advertised as main");
    SpeakerArrangement inputs[]{mainChannels==1 ? SpeakerArr::kMono : SpeakerArr::kStereo,
        auxChannels==1 ? SpeakerArr::kMono : auxChannels==2 ? SpeakerArr::kStereo : SpeakerArr::kEmpty};
    SpeakerArrangement outputs[]{outputChannels==1 ? SpeakerArr::kMono : SpeakerArr::kStereo};
    check(processor->setBusArrangements(inputs,2,outputs,1)==kResultOk,"VST3 bus arrangements rejected");
    check(component->activateBus(kAudio,kInput,0,true)==kResultOk && component->activateBus(kAudio,kOutput,0,true)==kResultOk,"VST3 main activation failed");
    check(component->activateBus(kAudio,kInput,1,auxChannels!=0)==kResultOk,"VST3 auxiliary activation failed");
    ProcessSetup setup{}; setup.processMode=kOffline; setup.symbolicSampleSize=kSample32; setup.maxSamplesPerBlock=512; setup.sampleRate=rate;
    check(processor->setupProcessing(setup)==kResultOk && component->setActive(true)==kResultOk,"VST3 baseline prepare failed");
    check(processor->getLatencySamples() == 0u, "VST3 fresh instance should report zero latency");
    auto preset=state(variant); check(component->setState(&preset)==kResultOk,"VST3 component state load failed");
    check(processor->getLatencySamples() == 0u, "VST3 active state changed host compensation early");
    check(component->setActive(false)==kResultOk && component->setActive(true)==kResultOk,"VST3 Morph reactivation failed");
    const auto latency=processor->getLatencySamples(); check(latency == 4096u, "VST3 Morph-only activation should report 4096 samples");
    check(processor->setProcessing(true)==kResultOk,"VST3 processing activation failed");
    constexpr int maximum=512;
    std::array<std::array<float,maximum>,2> main{},aux{},out{};
    std::array<float*,2> mainPointers{main[0].data(),main[1].data()}, auxPointers{aux[0].data(),aux[1].data()}, outPointers{out[0].data(),out[1].data()};
    AudioBusBuffers in[2]{},output{};
    in[0].numChannels=mainChannels; in[0].channelBuffers32=mainPointers.data();
    in[1].numChannels=auxChannels; in[1].channelBuffers32=auxPointers.data();
    output.numChannels=outputChannels; output.channelBuffers32=outPointers.data();
    ProcessData data{}; data.processMode=kOffline; data.symbolicSampleSize=kSample32; data.numInputs=2; data.numOutputs=1; data.inputs=in; data.outputs=&output;
    constexpr std::array<int,7> blocks{1,137,3,511,17,63,257};
    double maximumError=0;
    const int length=int(latency)+16000;
    for(int position=0,block=0;position<length;++block) {
        const int count=std::min(blocks[size_t(block%blocks.size())],length-position); data.numSamples=count;
        for(int i=0;i<count;++i) {
            main[0][size_t(i)]=variant ? (position+i==5 ? .4f : 0.f) : .1f;
            main[1][size_t(i)]=variant ? (position+i==5 ? -.25f : 0.f) : -.1f;
            aux[0][size_t(i)]=-.04f; aux[1][size_t(i)]=.07f;
        }
        check(processor->process(data)==kResultOk,"VST3 processing failed");
        for(int c=0;c<outputChannels;++c) for(int i=0;i<count;++i) {
            const int t=position+i;
            check(std::isfinite(out[size_t(c)][size_t(i)]),"VST3 nonfinite audio");
            if(variant) {
                const float expected=t==int(latency)+5 ? (outputChannels==1 && mainChannels==2 ? .075f : mainChannels==2 && c==1 ? -.25f : .4f) : 0.f;
                maximumError=std::max(maximumError,std::abs(double(out[size_t(c)][size_t(i)]-expected)));
            } else if(t>int(latency)+12000) {
                // Mono output is the documented average of processed L/R,
                                // not an implicit selection of the donor's left channel.
                                const float left = auxChannels == 0 ? 0.f : .04f;
                const float right = auxChannels == 0 ? 0.f : (auxChannels == 1 ? .04f : .07f) * (mainChannels == 2 ? -1.f : 1.f);
                const float expected = outputChannels == 1 ? .5f * (left + right) : c == 0 ? left : right;
                maximumError=std::max(maximumError,std::abs(double(out[size_t(c)][size_t(i)]-expected)));
            }
        }
        position+=count;
    }
    // Real VST3 input queues, not a preset-only or direct Processor simulation.
    // State inspection proves the wrapper consumed each event; no sample-accurate
    // FFT-switch timing claim is made beyond latency stability and finite audio.
    ParameterChanges changes; changes.queue.id = fftParameterId(factory, *component, host);
    data.inputParameterChanges = &changes; data.numSamples = 137;
    for (int fft = 0; fft < 3; ++fft) {
        changes.queue.value = fft * .5;
        check(processor->process(data) == kResultOk, "VST3 FFT parameter-queue processing failed");
        check(processor->getLatencySamples() == latency, "VST3 FFT automation changed fixed compensation");
        for (int c = 0; c < outputChannels; ++c) for (int i = 0; i < data.numSamples; ++i) {
            const float actual = out[size_t(c)][size_t(i)];
            check(std::isfinite(actual), "VST3 FFT automation produced nonfinite audio");
            const float left = auxChannels == 0 ? 0.f : .04f;
            const float right = auxChannels == 0 ? 0.f : (auxChannels == 1 ? .04f : .07f) * (mainChannels == 2 ? -1.f : 1.f);
            const float expected = variant ? 0.f : outputChannels == 1 ? .5f * (left + right) : c == 0 ? left : right;
            check(std::abs(actual - expected) < (variant ? 2.e-5f : 2.e-3f), "VST3 FFT automation disturbed aligned steady-state output");
        }
        Stream observed; check(component->getState(&observed) == kResultOk, "VST3 FFT state inspection failed");
        const std::string xml(observed.bytes.begin(), observed.bytes.end());
        const auto parameter = xml.find("id=\"fx1MorphFFT\"");
        check(parameter != std::string::npos, "VST3 FFT missing from saved component state");
        const auto start = xml.rfind('<', parameter), end = xml.find('>', parameter);
        const auto value = xml.find("value=\"", start);
        check(value != std::string::npos && value < end && std::strtod(xml.c_str() + value + 7, nullptr) == fft,
              "VST3 wrapper did not apply the actual FFT automation queue");
    }
    data.inputParameterChanges = nullptr;
    processor->setProcessing(false); component->setActive(false);
    Stream saved; check(component->getState(&saved)==kResultOk && saved.bytes.size()>8,"VST3 state save failed");
    saved.cursor=0; check(component->setState(&saved)==kResultOk,"VST3 state recall failed");
    check(component->setActive(true)==kResultOk, "VST3 recalled Morph activation failed");
    check(processor->getLatencySamples() == latency, "VST3 state recall changed Morph latency");
    auto noMorph = state(3); check(component->setState(&noMorph)==kResultOk, "VST3 active Morph removal failed");
    check(processor->getLatencySamples()==latency, "VST3 removal changed compensation while active");
    check(component->setActive(false)==kResultOk && component->setActive(true)==kResultOk, "VST3 no-Morph reactivation failed");
    check(processor->getLatencySamples() == 0u, "VST3 default graph reactivation should restore zero latency");
    component->setActive(false);
    saved.cursor=0; check(component->setState(&saved)==kResultOk,"VST3 lifecycle Morph restore failed");
    if (rate == 48000 && mainChannels == 1 && outputChannels == 2 && auxChannels == 2 && variant == 0) {
        // Real component lifecycle on one instance, not fresh-instance layout
        // tests: disable/reprepare at 96k, then reconnect/reprepare at 44.1k.
        for (int phase = 0; phase < 2; ++phase) {
            const int auxiliary = phase == 0 ? 0 : 2;
            inputs[1] = phase == 0 ? SpeakerArr::kEmpty : SpeakerArr::kStereo;
            check(processor->setBusArrangements(inputs,2,outputs,1)==kResultOk,"VST3 reconnect arrangement failed");
            check(component->activateBus(kAudio,kInput,1,auxiliary!=0)==kResultOk,"VST3 reconnect activation failed");
            setup.sampleRate = phase == 0 ? 96000 : 44100;
            check(processor->setupProcessing(setup)==kResultOk && component->setActive(true)==kResultOk
                && processor->setProcessing(true)==kResultOk,"VST3 rate-change reprepare failed");
            const uint32 reconfiguredLatency = processor->getLatencySamples();
            check(reconfiguredLatency == latency, "VST3 reprepare changed Morph-only latency");
            in[1].numChannels=auxiliary;
            main[0].fill(.1f); main[1].fill(.1f); aux[0].fill(-.04f); aux[1].fill(.07f);
            data.numSamples=137;
            double reconnectError=0;
            for(uint32 t=0;t<reconfiguredLatency+16000;t+=137) {
                check(processor->process(data)==kResultOk,"VST3 reconnect process failed");
                if(t>reconfiguredLatency+12000) for(int c=0;c<2;++c) for(int i=0;i<137;++i)
                    reconnectError=std::max(reconnectError,std::abs(double(out[size_t(c)][size_t(i)] - (auxiliary ? (c ? .07f : .04f) : 0.f))));
            }
            processor->setProcessing(false); component->setActive(false);
            check(reconnectError<2.e-3,"VST3 disconnect/rate/reconnect retained donor data or substituted main");
        }
    }
    processor->release(); component->terminate(); component->release();
    if (!(maximumError < (variant ? 2.e-5 : 2.e-3)))
        std::cerr << "VST3 case rate=" << rate << " main=" << mainChannels << " output=" << outputChannels
            << " aux=" << auxChannels << " variant=" << variant << " latency=" << latency
            << " maxError=" << maximumError << " final=" << out[0][0] << "," << out[1][0] << '\n';
    check(maximumError<(variant ? 2.e-5 : 2.e-3),"VST3 routed donor / main-phase / dry / bypass latency oracle failed");
}
}
int main(int argc,char** argv) {
    if(argc!=2) return 2;
    try {
        const auto path=std::filesystem::absolute(argv[1]);
        auto module=LoadLibraryW(path.c_str()); check(module!=nullptr,"VST3 module load failed");
        using Entry=bool(*)();
        const auto init=reinterpret_cast<Entry>(GetProcAddress(module,"InitDll"));
        check(!init || init(),"VST3 module initialization failed");
        using Factory=IPluginFactory*(*)();
        const auto entry=reinterpret_cast<Factory>(GetProcAddress(module,"GetPluginFactory"));
        check(entry!=nullptr,"VST3 factory export missing");
        auto* factory=entry(); check(factory!=nullptr,"VST3 factory missing");
        PClassInfo info{}; bool found=false;
        for(int32 i=0;i<factory->countClasses();++i) {
            if(factory->getClassInfo(i,&info)==kResultOk && std::strcmp(info.category,kVstAudioEffectClass)==0) { found=true; break; }
        }
        check(found,"VST3 audio component missing");
        int cases=0;
        for(double rate : {44100.,48000.,96000.}) for(int layout=0;layout<4;++layout)
            for(int aux=0;aux<3;++aux) for(int variant=0;variant<3;++variant) {
                render(*factory,info.cid,rate,layout==0 || layout==3 ? 2 : 1,layout>=2 ? 1 : 2,aux,variant); ++cases;
            }
        factory->release();
        const auto exit=reinterpret_cast<Entry>(GetProcAddress(module,"ExitDll")); if(exit) exit();
        FreeLibrary(module);
        std::cout << "VST3 native component host: " << cases << " bus/rate/Morph/dry/bypass cases passed\n";
        return 0;
    } catch(const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
