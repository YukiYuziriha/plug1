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
#include <string_view>
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
    struct Point { int32 offset; ParamValue value; };
    ParamID id = 0;
    std::vector<Point> points;
    uint32 refs = 1, readMask = 0;
    tresult PLUGIN_API queryInterface(const TUID iid, void** out) override {
        *out = nullptr;
        if (equal(iid, IParamValueQueue_iid) || equal(iid, FUnknown_iid)) { *out = static_cast<IParamValueQueue*>(this); addRef(); return kResultOk; }
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return ++refs; }
    uint32 PLUGIN_API release() override { return --refs; }
    ParamID PLUGIN_API getParameterId() override { return id; }
    int32 PLUGIN_API getPointCount() override { return int32(points.size()); }
    tresult PLUGIN_API getPoint(int32 index, int32& sampleOffset, ParamValue& parameterValue) override {
        if (index < 0 || index >= getPointCount()) return kInvalidArgument;
        if (index < 32) readMask |= uint32(1) << index;
        const auto& point = points[size_t(index)];
        sampleOffset = point.offset; parameterValue = point.value; return kResultOk;
    }
    tresult PLUGIN_API addPoint(int32 sampleOffset, ParamValue parameterValue, int32& index) override {
        index = getPointCount(); points.push_back({sampleOffset, parameterValue}); return kResultOk;
    }
};
struct ParameterChanges final : IParameterChanges {
    std::array<ParameterQueue, 2> queues;
    int32 count = 0;
    uint32 refs = 1;
    tresult PLUGIN_API queryInterface(const TUID iid, void** out) override {
        *out = nullptr;
        if (equal(iid, IParameterChanges_iid) || equal(iid, FUnknown_iid)) { *out = static_cast<IParameterChanges*>(this); addRef(); return kResultOk; }
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return ++refs; }
    uint32 PLUGIN_API release() override { return --refs; }
    int32 PLUGIN_API getParameterCount() override { return count; }
    IParamValueQueue* PLUGIN_API getParameterData(int32 index) override {
        return index >= 0 && index < count ? &queues[size_t(index)] : nullptr;
    }
    IParamValueQueue* PLUGIN_API addParameterData(const ParamID& id, int32& index) override {
        for (index = 0; index < count; ++index)
            if (queues[size_t(index)].id == id) return &queues[size_t(index)];
        if (size_t(count) == queues.size()) return nullptr;
        queues[size_t(count)].id = id;
        return &queues[size_t(count++)];
    }
    ParameterQueue& addQueue(ParamID id) {
        int32 index = 0;
        auto* queue = addParameterData(id, index);
        check(queue != nullptr, "callback host parameter queue capacity exceeded");
        return static_cast<ParameterQueue&>(*queue);
    }
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
Stream xmlState(std::string_view xml) {
    Stream result; result.bytes.resize(xml.size()+9);
    const uint32 magic=0x21324356, length=uint32(xml.size());
    std::memcpy(result.bytes.data(),&magic,4); std::memcpy(result.bytes.data()+4,&length,4);
    std::memcpy(result.bytes.data()+8,xml.data(),xml.size()); return result;
}
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
    return xmlState(xml);
}
// This host keeps the two VST3 objects connected for their entire lifetime.
// A restart callback only queues work: the host stops audio after returning to
// its event loop, never re-enters setActive from inside restartComponent.
struct LatencyHost final : IComponentHandler {
    Host application;
    IComponent* component = nullptr;
    IAudioProcessor* processor = nullptr;
    IEditController* controller = nullptr;
    IConnectionPoint* audioConnection = nullptr;
    IConnectionPoint* controlConnection = nullptr;
    uint32 refs = 1;
    const DWORD messageThread = GetCurrentThreadId();
    int requests = 0, activations = 0;
    uint32 latencyAtRequest = 0;
    bool pending = false, processing = false, active = false, inProcess = false;
    bool componentInitialized = false, controllerInitialized = false;
    bool callbackOnMessageThread = true, callbackOutsideProcess = true, duplicateRequest = false;
    std::array<std::array<float, 512>, 2> main{}, donor{}, output{};
    std::array<float*, 2> mainPointers{main[0].data(), main[1].data()};
    std::array<float*, 2> donorPointers{donor[0].data(), donor[1].data()};
    std::array<float*, 2> outputPointers{output[0].data(), output[1].data()};
    AudioBusBuffers inputs[2]{}, outputs{};
    ProcessData data{};

    tresult PLUGIN_API queryInterface(const TUID interfaceId, void** out) override {
        *out = nullptr;
        if (equal(interfaceId, IComponentHandler_iid) || equal(interfaceId, FUnknown_iid)) {
            *out = static_cast<IComponentHandler*>(this); addRef(); return kResultOk;
        }
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return ++refs; }
    uint32 PLUGIN_API release() override { return --refs; }
    tresult PLUGIN_API beginEdit(ParamID) override { return kResultOk; }
    tresult PLUGIN_API performEdit(ParamID, ParamValue) override { return kResultOk; }
    tresult PLUGIN_API endEdit(ParamID) override { return kResultOk; }
    tresult PLUGIN_API restartComponent(int32 flags) override {
        if ((flags & kLatencyChanged) != 0) {
            ++requests;
            duplicateRequest |= pending;
            pending = true;
            latencyAtRequest = processor->getLatencySamples();
            callbackOnMessageThread &= GetCurrentThreadId() == messageThread;
            callbackOutsideProcess &= !inProcess;
        }
        return kResultOk;
    }

    LatencyHost(IPluginFactory& factory, const TUID cid, double rate, int channels, bool morph,
                const Stream* preset = nullptr) {
        try { initialize(factory, cid, rate, channels, morph, preset); }
        catch (...) { shutdown(); throw; }
    }
    void initialize(IPluginFactory& factory, const TUID cid, double rate, int channels, bool morph,
                    const Stream* preset) {
        check(factory.createInstance(cid, IComponent_iid, reinterpret_cast<void**>(&component)) == kResultOk && component,
              "callback host component creation failed");
        check(component->initialize(&application) == kResultOk, "callback host component initialization failed");
        componentInitialized = true;
        check(component->queryInterface(IAudioProcessor_iid, reinterpret_cast<void**>(&processor)) == kResultOk && processor,
              "callback host processor missing");
        TUID controllerClass{};
        check(component->getControllerClassId(controllerClass) == kResultOk, "callback host controller class missing");
        check(factory.createInstance(controllerClass, IEditController_iid, reinterpret_cast<void**>(&controller)) == kResultOk && controller,
              "callback host controller creation failed");
        check(controller->initialize(&application) == kResultOk, "callback host controller initialization failed");
        controllerInitialized = true;
        check(controller->setComponentHandler(this) == kResultOk, "callback host handler installation failed");
        check(component->queryInterface(IConnectionPoint_iid, reinterpret_cast<void**>(&audioConnection)) == kResultOk && audioConnection
            && controller->queryInterface(IConnectionPoint_iid, reinterpret_cast<void**>(&controlConnection)) == kResultOk && controlConnection,
              "callback host connection points missing");
        check(audioConnection->connect(controlConnection) == kResultOk && controlConnection->connect(audioConnection) == kResultOk,
              "callback host controller/processor connection failed");
        const auto arrangement = channels == 1 ? SpeakerArr::kMono : SpeakerArr::kStereo;
        SpeakerArrangement in[]{arrangement, arrangement}, out[]{arrangement};
        check(processor->setBusArrangements(in, 2, out, 1) == kResultOk, "callback host layout rejected");
        check(component->activateBus(kAudio, kInput, 0, true) == kResultOk
            && component->activateBus(kAudio, kInput, 1, true) == kResultOk
            && component->activateBus(kAudio, kOutput, 0, true) == kResultOk, "callback host bus activation failed");
        // Disabled Morph with zero Mix gives an exact dry oracle even when on.
        // The canonical graph contains the real Sidechain Blend node.
        auto initial = preset != nullptr ? *preset : state(morph ? 0 : 3);
        std::string xml(initial.bytes.begin() + 8, initial.bytes.end() - 1);
        if (preset == nullptr && morph) {
            const auto on = xml.find("id=\"fx1On\" value=\"1\"");
            check(on != std::string::npos, "callback Morph state fixture missing");
            xml.replace(on, std::strlen("id=\"fx1On\" value=\"1\""), "id=\"fx1On\" value=\"0\"");
            const auto mix = xml.find("id=\"fx1Mix\" value=\"100\"");
            check(mix != std::string::npos, "callback Morph Mix fixture missing");
            xml.replace(mix, std::strlen("id=\"fx1Mix\" value=\"100\""), "id=\"fx1Mix\" value=\"0\"");
        } else if (preset == nullptr) {
            // Start bypassed, rather than testing the unrelated asymptotic
            // bypass gain ramp with a sample-exact latency oracle.
            xml.insert(xml.find("</VocalStack>"), "<PARAM id=\"bypass\" value=\"1\"/>");
        }
        initial = xmlState(xml);
        check(component->setState(&initial) == kResultOk, "callback host initial state failed");
        Stream synchronized;
        check(component->getState(&synchronized) == kResultOk, "callback host state synchronization read failed");
        synchronized.cursor = 0;
        check(controller->setComponentState(&synchronized) == kResultOk, "callback host controller state synchronization failed");
        ProcessSetup setup{};
        setup.processMode = kOffline; setup.symbolicSampleSize = kSample32;
        setup.maxSamplesPerBlock = 512; setup.sampleRate = rate;
        check(processor->setupProcessing(setup) == kResultOk && component->setActive(true) == kResultOk,
              "callback host initial activation failed");
        active = true;
        check(processor->getLatencySamples() == 0, "callback host must start without latency");
        check(processor->setProcessing(true) == kResultOk, "callback host processing start failed");
        processing = true;
        inputs[0].numChannels = inputs[1].numChannels = outputs.numChannels = channels;
        inputs[0].channelBuffers32 = mainPointers.data(); inputs[1].channelBuffers32 = donorPointers.data();
        outputs.channelBuffers32 = outputPointers.data();
        data.processMode = kOffline; data.symbolicSampleSize = kSample32;
        data.numInputs = 2; data.numOutputs = 1; data.inputs = inputs; data.outputs = &outputs;
        if (preset == nullptr) stable(0);
    }
    ~LatencyHost() { shutdown(); }
    void shutdown() {
        if (processing) processor->setProcessing(false);
        if (active) component->setActive(false);
        if (controller) controller->setComponentHandler(nullptr);
        if (controlConnection && audioConnection) {
            controlConnection->disconnect(audioConnection); audioConnection->disconnect(controlConnection);
        }
        if (controlConnection) controlConnection->release();
        if (audioConnection) audioConnection->release();
        if (controllerInitialized) controller->terminate();
        if (controller) controller->release();
        if (processor) processor->release();
        if (componentInitialized) component->terminate();
        if (component) component->release();
    }
    ParamID parameter(const char* title) {
        for (int32 i = 0; i < controller->getParameterCount(); ++i) {
            ParameterInfo info{};
            if (controller->getParameterInfo(i, info) != kResultOk) continue;
            std::string name;
            for (auto c : info.title) { if (!c) break; name.push_back(char(c)); }
            if (name == title) return info.id;
        }
        throw std::runtime_error(std::string("callback host parameter missing: ") + title);
    }
    void process(int samples, IParameterChanges* changes = nullptr) {
        data.numSamples = samples; data.inputParameterChanges = changes;
        inProcess = true;
        const auto result = processor->process(data);
        inProcess = false; data.inputParameterChanges = nullptr;
        check(result == kResultOk, "callback host audio processing failed");
        for (int c = 0; c < outputs.numChannels; ++c) for (int i = 0; i < samples; ++i)
            check(std::isfinite(output[size_t(c)][size_t(i)]), "callback host nonfinite output");
    }
    ParamValue normalizedValue(ParamID id, double plain) {
        // JUCE's continuous VST3 plain range is normalized, not the displayed
        // engineering units. Ask the released controller to parse its own text.
        const auto text = std::to_string(plain);
        String128 display{};
        for (size_t i = 0; i < text.size() && i < 127; ++i) display[i] = char16(text[i]);
        ParamValue normalized = 0;
        check(controller->getParamValueByString(id, display, normalized) == kResultOk,
              "callback host controller value conversion failed");
        return normalized;
    }
    void automate(const char* title, double plain) {
        const auto id = parameter(title);
        const auto normalized = normalizedValue(id, plain);
        check(controller->setParamNormalized(id, normalized) == kResultOk, "callback host controller edit failed");
        ParameterChanges changes;
        changes.addQueue(id).points = {{0, normalized}};
        for (auto& c : main) c.fill(0);
        process(137, &changes); // Host edits reach DSP through the actual VST3 queue.
        check(std::abs(controller->getParamNormalized(id) - normalized) < 1.e-6, "callback controller edit not retained");
    }
    void pump() {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            check(message.message != WM_QUIT, "callback host received WM_QUIT");
            TranslateMessage(&message); DispatchMessageW(&message);
        }
    }
    void impulse(uint32 latency) {
        check(processor->getLatencySamples() == latency, "callback host compensation changed before activation");
        // Flush earlier audio without transport reset or lifecycle calls.
        for (auto& c : main) c.fill(0);
        for (uint32 n = 0; n < 8192; n += 512) process(512);
        constexpr std::array<int, 5> blocks{1, 137, 3, 511, 17};
        for (int position = 0, block = 0; position < int(latency) + 600; ++block) {
            const int count = std::min(blocks[size_t(block) % blocks.size()], int(latency) + 600 - position);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < count; ++i)
                main[size_t(c)][size_t(i)] = position + i == 0 ? (c ? -.25f : .4f) : 0.f;
            process(count);
            for (int c = 0; c < outputs.numChannels; ++c) for (int i = 0; i < count; ++i) {
                const float expected = position + i == int(latency) ? (c ? -.25f : .4f) : 0.f;
                if (std::abs(output[size_t(c)][size_t(i)] - expected) >= 2.e-5f) {
                    std::cerr << "VST3 callback impulse latency=" << latency << " sample=" << position + i
                              << " channel=" << c << " expected=" << expected
                              << " actual=" << output[size_t(c)][size_t(i)] << '\n';
                    check(false, "callback host exact impulse delay differs from active compensation");
                }
            }
            check(processor->getLatencySamples() == latency, "callback host processing installed pending latency");
            position += count;
        }
    }
    void stable(uint32 latency) {
        const int before = requests, restarts = activations;
        const auto deadline = GetTickCount64() + 250;
        do {
            pump();
            for (auto& c : main) c.fill(0);
            process(137);
            check(!pending && requests == before && activations == restarts, "callback host duplicate/looping latency restart");
            check(processor->getLatencySamples() == latency, "callback host latency did not remain stable");
            Sleep(2);
        } while (GetTickCount64() < deadline);
    }
    void transition(const char* title, double value, uint32 before, uint32 after) {
        std::cout << "VST3 callback " << title << '=' << value << ": " << before << " -> " << after << std::endl;
        const int previous = requests, previousActivations = activations;
        automate(title, value);
        check(processor->getLatencySamples() == before, "automation changed compensation before callback");
        if (before == after) { stable(before); check(requests == previous, "no-op edit requested latency restart"); return; }
        const auto deadline = GetTickCount64() + 1500;
        while (!pending && GetTickCount64() < deadline) { pump(); Sleep(2); }
        check(pending && requests == previous + 1, "missing actual VST3 kLatencyChanged callback");
        check(callbackOnMessageThread && callbackOutsideProcess && !duplicateRequest,
              "VST3 latency callback duplicated or arrived inside audio/off message thread");
        check(latencyAtRequest == before && processor->getLatencySamples() == before,
              "VST3 latency callback must expose old compensation until host activation");
        impulse(before); // Pending audio must still use the old plan, not only the old number.
        check(activations == previousActivations, "host activated without servicing callback");
        // This is the only transition restart, and cannot run without a real request.
        check(pending, "host restart has no latency callback");
        pending = false;
        check(processor->setProcessing(false) == kResultOk, "callback host stop failed"); processing = false;
        check(component->setActive(false) == kResultOk, "callback host deactivate failed"); active = false;
        check(processor->getLatencySamples() == before, "deactivation changed compensation before activation");
        check(component->setActive(true) == kResultOk, "callback host reactivate failed"); active = true;
        ++activations;
        check(processor->getLatencySamples() == after, "requested activation did not install exact new compensation");
        check(processor->setProcessing(true) == kResultOk, "callback host resume failed"); processing = true;
        check(requests == previous + 1 && !pending, "activation synchronously requested another restart");
        impulse(after);
        stable(after); // Real timers/messages plus audio must not produce a restart loop.
        check(requests == previous + 1 && activations == previousActivations + 1,
              "latency transition did not have exactly one callback-driven activation");
    }
};

void spatialEqOffsets(IPluginFactory& factory, const TUID cid) {
    constexpr int blockSize = 512, continuation = 389, first = 73, second = 297;
    constexpr int preroll = 4096, automated = blockSize + continuation, total = automated + 1024;
    constexpr double rate = 48000.;
    constexpr float match = 2.e-6f, distinguish = 1.e-5f;
    using Audio = std::array<std::array<float, total>, 2>;
    using Points = std::vector<ParameterQueue::Point>;
    struct Render {
        Audio audio{};
        std::array<std::array<uint32, 2>, 2> reads{}; // [block][Gain/Depth]
    };
    enum class Delivery { queued, explicitSamples, finalAtStart, unchanged, partitionedUnchanged, queuedNoOp };
    struct Fixture { const char* name; bool gain, depth, ramp, flush; };
    constexpr Fixture fixtures[]{
        {"Gain jumps", true, false, false, false}, {"Depth jumps", false, true, false, false},
        {"Gain ramp", true, false, true, false}, {"Depth ramp", false, true, true, false},
        {"simultaneous Gain/Depth ramps", true, true, true, false},
        {"simultaneous Gain/Depth flush", true, true, false, true},
    };
    const auto distance = [](const Audio& a, const Audio& b, int begin, int end) {
        float peak = 0;
        for (size_t c = 0; c < a.size(); ++c) for (int i = begin; i < end; ++i)
            peak = std::max(peak, std::abs(a[c][size_t(i)] - b[c][size_t(i)]));
        return peak;
    };
    int failures = 0;
    for (const auto& fixture : fixtures) {
        try {
            const auto preset = xmlState(std::string(
                "<VocalStack><PARAM id=\"levelOn\" value=\"0\"/><PARAM id=\"doubleOn\" value=\"0\"/>"
                "<PARAM id=\"formantOn\" value=\"0\"/><PARAM id=\"globalMix\" value=\"100\"/>"
                "<PARAM id=\"fx1On\" value=\"1\"/><PARAM id=\"fx1SpatialEqBand1On\" value=\"1\"/>"
                "<PARAM id=\"fx1SpatialEqBand1Frequency\" value=\"900\"/>")
                + (fixture.gain ? "<PARAM id=\"fx1SpatialEqBand1Gain\" value=\"-9\"/><PARAM id=\"fx1SpatialEqBand1Depth\" value=\"60\"/>"
                                : "<PARAM id=\"fx1SpatialEqBand1Gain\" value=\"12\"/><PARAM id=\"fx1SpatialEqBand1Depth\" value=\"0\"/>")
                + "<RoutingGraph><Node id=\"1\" type=\"0\"/><Node id=\"100\" type=\"28\"/><Node id=\"13\" type=\"5\"/>"
                  "<Link source=\"1\" destination=\"100\"/><Link source=\"100\" destination=\"13\"/>"
                  "</RoutingGraph></VocalStack>");
            const auto render = [&](Delivery delivery) {
                LatencyHost host(factory, cid, rate, 2, false, &preset);
                const std::array ids{host.parameter("FX 1 Spatial EQ Band 1 Gain"), host.parameter("FX 1 Spatial EQ Band 1 Depth")};
                std::array<ParamValue, 2> held{};
                std::array<std::array<Points, 2>, 2> schedule;
                for (size_t p = 0; p < ids.size(); ++p) {
                    const bool gain = p == 0;
                    const auto norm = [&](double value) { return host.normalizedValue(ids[p], value); };
                    held[p] = norm(gain ? (fixture.gain ? -9 : 12) : (fixture.gain ? 60 : 0));
                    check(std::abs(host.controller->getParamNormalized(ids[p]) - held[p]) < 1.e-6,
                          "initial normalized value differs from fixture");
                    if (!(gain ? fixture.gain : fixture.depth)) continue;
                    const auto middle = norm(gain ? 12 : 100), end = norm(gain ? 5 : 35);
                    if (fixture.flush) {
                        schedule[0][p] = {{0, middle}};
                    } else if (fixture.ramp) {
                        schedule[0][p] = {{0, norm(gain ? 6 : 25)}, {gain ? first : second, middle}, {blockSize - 1, end}};
                        // No point at offset zero: the previous block's last sample is the -1 anchor.
                        schedule[1][p] = {{gain ? second : continuation - 1, norm(gain ? -12 : 95)}};
                    } else {
                        // Adjacent old/new points encode exact VST3 jumps.
                        schedule[0][p] = {{0, held[p]}, {first - 1, held[p]}, {first, middle},
                            {second - 1, middle}, {second, end}, {blockSize - 1, end}};
                    }
                }
                Render result;
                const auto processSpan = [&](int start, int count, ParameterChanges* changes = nullptr) {
                    for (int i = 0; i < count; ++i) {
                        const double phase = 6.283185307179586 * 900 * (preroll + start + i) / rate;
                        host.main[0][size_t(i)] = float(.06 * std::sin(phase) + .02 * std::cos(1.31 * phase));
                        host.main[1][size_t(i)] = float(.05 * std::cos(.83 * phase) - .02 * std::sin(1.17 * phase));
                    }
                    host.process(count, changes != nullptr && changes->count != 0 ? changes : nullptr);
                    if (start >= 0) for (size_t c = 0; c < result.audio.size(); ++c)
                        std::copy_n(host.output[c].begin(), count, result.audio[c].begin() + start);
                    check(host.processor->getLatencySamples() == 0, "automation changed latency");
                };
                for (int start = -preroll; start < 0; start += blockSize) processSpan(start, blockSize);
                for (size_t block = 0; block < schedule.size(); ++block) {
                    const int start = block == 0 ? 0 : blockSize;
                    const int count = block == 0 ? blockSize : continuation;
                    const auto& points = schedule[block];
                    if (delivery == Delivery::explicitSamples || delivery == Delivery::partitionedUnchanged) {
                        for (int i = 0; i < count; ++i) {
                            ParameterChanges changes;
                            if (delivery == Delivery::explicitSamples) for (size_t p = 0; p < ids.size(); ++p) {
                                if (points[p].empty()) continue;
                                // Evaluate the host curve, then deliver only a single offset-zero value.
                                // The oracle never relies on the plug-in interpreting a nonzero offset.
                                ParameterQueue::Point left{-1, held[p]};
                                auto value = points[p].back().value;
                                for (const auto& right : points[p]) {
                                    if (i <= right.offset) {
                                        value = std::lerp(left.value, right.value, double(i - left.offset) / (right.offset - left.offset));
                                        break;
                                    }
                                    left = right;
                                }
                                changes.addQueue(ids[p]).points = {{0, value}};
                            }
                            processSpan(start + i, 1, &changes);
                        }
                    } else {
                        ParameterChanges changes;
                        for (size_t p = 0; p < ids.size(); ++p) {
                            if (points[p].empty()) continue;
                            if (delivery == Delivery::queued) changes.addQueue(ids[p]).points = points[p];
                            if (delivery == Delivery::queuedNoOp) {
                                auto& queue = changes.addQueue(ids[p]);
                                for (const auto& point : points[p]) queue.points.push_back({point.offset, held[p]});
                            }
                            if (delivery == Delivery::finalAtStart) changes.addQueue(ids[p]).points = {{0, points[p].back().value}};
                        }
                        if (fixture.flush && delivery == Delivery::queued && block == 0) {
                            ProcessData flush{};
                            flush.processMode = kOffline; flush.symbolicSampleSize = kSample32;
                            flush.inputParameterChanges = &changes;
                            check(host.processor->process(flush) == kResultOk, "parameter-only flush failed");
                            processSpan(start, count);
                        } else processSpan(start, count, &changes);
                        if (delivery == Delivery::queued) for (int32 q = 0; q < changes.count; ++q) {
                            const auto& queue = changes.queues[size_t(q)];
                            result.reads[block][queue.id == ids[0] ? 0 : 1] = queue.readMask;
                        }
                    }
                    for (size_t p = 0; p < ids.size(); ++p)
                        if (!points[p].empty()) held[p] = points[p].back().value;
                }
                for (int start = automated; start < total; start += blockSize) processSpan(start, blockSize);
                return result;
            };
            const auto unchanged = render(Delivery::unchanged);
            const auto partitioned = render(Delivery::partitionedUnchanged);
            const auto atStart = render(Delivery::finalAtStart);
            // A flush must behave like an offset-zero edit, without advancing the audio timeline.
            const auto expected = fixture.flush ? atStart : render(Delivery::explicitSamples);
            const auto queued = render(Delivery::queued);
            const float partitionError = distance(partitioned.audio, unchanged.audio, 0, total);
            const float effect = distance(expected.audio, unchanged.audio, 0, total);
            const float earlyControl = distance(expected.audio, atStart.audio, 0, automated);
            const float firstError = distance(queued.audio, expected.audio, 0, blockSize);
            const float nextError = distance(queued.audio, expected.audio, blockSize, automated);
            const float holdError = distance(queued.audio, expected.audio, automated, total);
            const bool jumps = !fixture.ramp && !fixture.flush;
            const float preJumpError = jumps ? distance(queued.audio, unchanged.audio, 0, first) : 0;
            const float noOpError = jumps ? distance(render(Delivery::queuedNoOp).audio, unchanged.audio, 0, total) : 0;
            std::cout << "VST3 Spatial EQ " << fixture.name << " read masks (Gain/Depth)="
                      << queued.reads[0][0] << '/' << queued.reads[0][1] << ',' << queued.reads[1][0] << '/' << queued.reads[1][1]
                      << " first/next/hold error=" << firstError << '/' << nextError << '/' << holdError
                      << " partition=" << partitionError << " reference/unchanged=" << effect
                      << " reference/final-at-start=" << earlyControl
                      << " queued/final-at-start=" << distance(queued.audio, atStart.audio, 0, total);
            if (jumps) std::cout << " pre-jump error=" << preJumpError << " no-op error=" << noOpError;
            std::cout << std::endl;
            check(partitionError <= match && noOpError <= match, "partitioned or no-op audio differs from unchanged audio");
            check(effect > distinguish && (fixture.flush || earlyControl > distinguish),
                  "fixture does not distinguish the automation schedules");
            check(std::max({preJumpError, firstError, nextError, holdError}) <= match,
                  "queued audio did not match the explicit reference");
        } catch (const std::exception& error) {
            std::cerr << "VST3 Spatial EQ " << fixture.name << ": " << error.what() << '\n';
            ++failures;
        }
    }
    if (failures != 0)
        throw std::runtime_error("VST3 Spatial EQ offsets: " + std::to_string(failures) + " fixture(s) failed");
    std::cout << "VST3 Spatial EQ offsets: all fixtures passed\n";
}

void automaticLatencyTransitions(IPluginFactory& factory, const TUID cid) {
    int failures = 0, cases = 0;
    for (double rate : {44100., 48000., 96000.}) for (int channels : {1, 2}) for (bool morph : {true, false}) {
        try {
            std::cout << "VST3 callback fixture rate=" << rate << " channels=" << channels
                      << " graph=" << (morph ? "Morph" : "Sidechain Blend") << std::endl;
            LatencyHost host(factory, cid, rate, channels, morph);
            if (morph) {
                for (int repeat = 0; repeat < 2; ++repeat) {
                    host.transition("FX 1 Active", 1, 0, 4096);
                    host.transition("FX 1 Active", 1, 4096, 4096);
                    host.transition("FX 1 Active", 0, 4096, 0);
                }
            } else {
                // Bypass is only an exact dry-delay oracle, not a latency edit.
                host.transition("Bypass", 1, 0, 0);
                host.transition("Sidechain Blend", 100, 0, 0);
                host.transition("Sidechain Active", 1, 0, 0);
                const uint32 vocalLatency = rate == 96000 ? 2688u : 1344u;
                host.transition("Sidechain Pitch", 12, 0, vocalLatency);
                host.transition("Sidechain Pitch", 0, vocalLatency, 0);
                host.transition("Sidechain Pitch", 12, 0, vocalLatency);
                host.transition("Sidechain Pitch", -5, vocalLatency, vocalLatency);
                host.transition("Sidechain Active", 0, vocalLatency, 0);
                host.transition("Sidechain Pitch", 7, 0, 0); // Process off keeps Pitch dormant.
                host.transition("Sidechain Active", 1, 0, vocalLatency);
                host.transition("Sidechain Blend", 0, vocalLatency, 0);
                host.transition("Sidechain Pitch", 12, 0, 0); // Zero Blend keeps Pitch dormant.
                host.transition("Sidechain Blend", 100, 0, vocalLatency);
                host.transition("Sidechain Blend", 50, vocalLatency, vocalLatency);
                host.transition("Sidechain Pitch", 0, vocalLatency, 0);
            }
            ++cases;
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "VST3 callback failure rate=" << rate << " channels=" << channels
                      << " graph=" << (morph ? "Morph" : "Sidechain Blend") << ": " << error.what() << '\n';
        }
    }
    std::cout << "VST3 automatic latency callbacks: " << cases << " fixtures passed, " << failures << " failed\n";
    check(failures == 0, "released VST3 automatic latency callback regressions failed");
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
    ParameterChanges changes;
    auto& fftQueue = changes.addQueue(fftParameterId(factory, *component, host));
    data.inputParameterChanges = &changes; data.numSamples = 137;
    for (int fft = 0; fft < 3; ++fft) {
        fftQueue.points = {{17, fft * .5}};
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
    const bool offsetsOnly = argc == 3 && std::string_view(argv[2]) == "--spatial-eq-offsets";
    if (argc != 2 && !offsetsOnly) {
        std::cerr << "usage: vocalstack_vst3_sidechain_smoke <module> [--spatial-eq-offsets]\n";
        return 2;
    }
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
        spatialEqOffsets(*factory, info.cid);
        int cases=0;
        if (!offsetsOnly) {
            automaticLatencyTransitions(*factory, info.cid);
            for(double rate : {44100.,48000.,96000.}) for(int layout=0;layout<4;++layout)
                for(int aux=0;aux<3;++aux) for(int variant=0;variant<3;++variant) {
                    render(*factory,info.cid,rate,layout==0 || layout==3 ? 2 : 1,layout>=2 ? 1 : 2,aux,variant); ++cases;
                }
        }
        factory->release();
        const auto exit=reinterpret_cast<Entry>(GetProcAddress(module,"ExitDll")); if(exit) exit();
        FreeLibrary(module);
        if (!offsetsOnly)
            std::cout << "VST3 native component host: " << cases << " bus/rate/Morph/dry/bypass cases passed\n";
        return 0;
    } catch(const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
