#include <clap/clap.h>

#if defined(_WIN32)
 #define WIN32_LEAN_AND_MEAN
 #define NOMINMAX
 #include <windows.h>
#else
 #include <dlfcn.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

void expect(bool result, const char* message) {
    if (!result)
        throw std::runtime_error(message);
}

struct Library {
    explicit Library(const std::filesystem::path& path) {
#if defined(_WIN32)
        handle = LoadLibraryW(path.c_str());
        expect(handle != nullptr, "Windows could not load the CLAP DLL");
        entry = reinterpret_cast<const clap_plugin_entry_t*>(GetProcAddress(handle, "clap_entry"));
#else
        handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!handle)
            throw std::runtime_error(dlerror());
        entry = static_cast<const clap_plugin_entry_t*>(dlsym(handle, "clap_entry"));
#endif
        expect(entry != nullptr && entry->init(path.string().c_str()), "CLAP entry initialization failed");
    }
    ~Library() {
        entry->deinit();
#if defined(_WIN32)
        FreeLibrary(handle);
#else
        dlclose(handle);
#endif
    }
#if defined(_WIN32)
    HMODULE handle = nullptr;
#else
    void* handle = nullptr;
#endif
    const clap_plugin_entry_t* entry = nullptr;
};

void CLAP_ABI noRequest(const clap_host_t*) {}
const void* CLAP_ABI noExtension(const clap_host_t*, const char*) { return nullptr; }

struct Instance {
    explicit Instance(const clap_plugin_factory_t& factory) {
        const auto* descriptor = factory.get_plugin_descriptor(&factory, 0);
        expect(descriptor && std::string_view(descriptor->id) == "audio.vocalstack.effect", "Unexpected CLAP ID");
        expect(descriptor->name && std::string_view(descriptor->name) == "HEAVEN by lil grass block",
               "Unexpected CLAP display name");
        plugin = factory.create_plugin(&factory, &host, descriptor->id);
        expect(plugin && plugin->init(plugin), "CLAP plugin initialization failed");
    }
    ~Instance() { plugin->destroy(plugin); }

    template <typename Extension>
    const Extension& get(const char* id) const {
        const auto* extension = static_cast<const Extension*>(plugin->get_extension(plugin, id));
        expect(extension != nullptr, id);
        return *extension;
    }

    static Instance& from(const clap_host_t* host) { return *static_cast<Instance*>(host->host_data); }
    static void CLAP_ABI requestRestart(const clap_host_t* host) { ++from(host).restartRequests; }
    static void CLAP_ABI latencyChanged(const clap_host_t* host) { ++from(host).latencyChanges; }
    static const void* CLAP_ABI extension(const clap_host_t*, const char* id) {
        static const clap_host_latency_t latency{latencyChanged};
        return std::strcmp(id, CLAP_EXT_LATENCY) == 0 ? &latency : nullptr;
    }
    int restartRequests = 0, latencyChanges = 0;
    clap_host_t host{CLAP_VERSION, this, "Vocal Stack Test", "Vocal Stack", "", "1.0",
                    extension, requestRestart, noRequest, noRequest};
    const clap_plugin_t* plugin = nullptr;
};

struct Events {
    std::vector<clap_event_param_value_t> items;
    clap_input_events_t input{
        this,
        [](const clap_input_events_t* self) -> uint32_t {
            return static_cast<uint32_t>(static_cast<const Events*>(self->ctx)->items.size());
        },
        [](const clap_input_events_t* self, uint32_t index) -> const clap_event_header_t* {
            return &static_cast<const Events*>(self->ctx)->items[index].header;
        }};
    clap_output_events_t output{
        this, [](const clap_output_events_t*, const clap_event_header_t*) { return true; }};

    void set(clap_id id, double value, uint32_t time = 0) {
        clap_event_param_value_t event{};
        event.header = {sizeof(event), time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
        event.param_id = id;
        event.note_id = event.port_index = event.channel = event.key = -1;
        event.value = value;
        items.push_back(event);
    }
};

struct Stream {
    std::vector<uint8_t> bytes;
    size_t cursor = 0;
    clap_ostream_t output{
        this, [](const clap_ostream_t* stream, const void* data, uint64_t count) -> int64_t {
            auto& bytes = static_cast<Stream*>(stream->ctx)->bytes;
            const auto* first = static_cast<const uint8_t*>(data);
            const auto chunk = std::min<uint64_t>(count, 17);
            bytes.insert(bytes.end(), first, first + chunk);
            return static_cast<int64_t>(chunk);
        }};
    clap_istream_t input{
        this, [](const clap_istream_t* stream, void* data, uint64_t count) -> int64_t {
            auto& state = *static_cast<Stream*>(stream->ctx);
            const auto chunk = std::min({count, uint64_t{13}, uint64_t{state.bytes.size() - state.cursor}});
            if (chunk != 0) {
                std::memcpy(data, state.bytes.data() + state.cursor, static_cast<size_t>(chunk));
                state.cursor += static_cast<size_t>(chunk);
            }
            return static_cast<int64_t>(chunk);
        }};
};

clap_param_info_t getParameter(const Instance& instance, std::string_view name) {
    const auto& params = instance.get<clap_plugin_params_t>(CLAP_EXT_PARAMS);
    for (uint32_t i = 0; i < params.count(instance.plugin); ++i) {
        clap_param_info_t info{};
        if (params.get_info(instance.plugin, i, &info) && name == info.name)
            return info;
    }
    throw std::runtime_error("Missing parameter: " + std::string(name));
}

double currentValue(const Instance& instance, clap_id id) {
    double value = 0;
    expect(instance.get<clap_plugin_params_t>(CLAP_EXT_PARAMS).get_value(instance.plugin, id, &value),
           "Parameter value unavailable");
    return value;
}

void checkHostControls(const clap_plugin_factory_t& factory) {
    Instance instance(factory);
    constexpr std::array<std::string_view, 10> names{
        "Leveler", "Mix", "Human", "Width", "Formant",
        "Leveler Active", "Doubler Active", "Formant Active", "Bypass", "Output"
    };
    const auto& params = instance.get<clap_plugin_params_t>(CLAP_EXT_PARAMS);
    expect(params.count(instance.plugin) == 2457, "Expected 2447 legacy parameters, Sidechain Blend, and nine Sidechain card parameters");
    for (uint32_t i = 0; i < params.count(instance.plugin); ++i) {
        clap_param_info_t info{};
        expect(params.get_info(instance.plugin, i, &info), "Host parameter metadata missing");
        expect((info.flags & CLAP_PARAM_IS_AUTOMATABLE) != 0, "Appended host parameter cannot be automated");
        if (i < names.size()) expect(names[i] == info.name, "Original parameter index/name changed");
        if (i == 10) expect(std::string_view(info.name) == "Advanced Doubler", "Legacy Advanced index changed");
        if (i == 2447) expect(std::string_view(info.name) == "Sidechain Blend" && info.default_value == info.min_value,
                              "Sidechain Blend must stay at index 2447 and default to 0%");
        if (i >= 11 && i < 43) {
            constexpr std::array<std::string_view, 8> fields{"Active", "Gain", "Pan", "Delay", "Detune", "Formant", "Depth", "Rate"};
            const auto expected = "Voice " + std::to_string(1 + (i - 11) / 8) + " " + std::string(fields[(i - 11) % 8]);
            expect(expected == info.name, "Original per-voice parameter index/name changed");
        }
        if (i >= 43 && i < 46) {
            constexpr std::array<std::string_view, 3> choraleNames{"Chorale Active", "Chorale Mix", "Chorale Motion"};
            expect(choraleNames[i - 43] == info.name, "Original Chorale index/name changed");
        }
        if (i >= 94 && i < 94 + 15 * 3) {
            constexpr std::array<std::string_view, 3> fields{"Active", "Mix", "Chorale Motion"};
            const auto field = (i - 94) % 3;
            const auto expected = "FX " + std::to_string(1 + (i - 94) / 3) + " " + std::string(fields[field]);
            expect(expected == info.name, ("FX slot parameter index/name mismatch: " + expected).c_str());
        }
    }
    const auto advanced = getParameter(instance, "Advanced Doubler");
    const auto chorale = getParameter(instance, "Chorale Active");
    expect(advanced.default_value == advanced.min_value && chorale.default_value == chorale.min_value,
           "Advanced and Chorale must default off");
    const auto& remote = instance.get<clap_plugin_remote_controls_t>(CLAP_EXT_REMOTE_CONTROLS);
    expect(remote.count(instance.plugin) == 2, "Expected Main and Output remote controls pages");
    clap_remote_controls_page_t page{};
    expect(remote.get(instance.plugin, 0, &page) && !page.is_for_preset &&
           std::string_view(page.page_name) == "HEAVEN by lil grass block", "Remote controls page was not advertised");
    for (size_t i = 0; i < names.size(); ++i) {
        const auto info = getParameter(instance, names[i]);
        expect((info.flags & CLAP_PARAM_IS_AUTOMATABLE) != 0, "Host parameter cannot be automated");
        if (i < CLAP_REMOTE_CONTROLS_COUNT)
            expect(page.param_ids[i] == info.id, "Remote control maps to the wrong host parameter");
    }
    expect(remote.get(instance.plugin, 1, &page) && std::string_view(page.page_name) == "Output",
           "Output remote controls page is missing");
    expect(page.param_ids[0] == getParameter(instance, "Output").id &&
           page.param_ids[1] == getParameter(instance, "Bypass").id,
           "Output remote controls are mapped incorrectly");
    for (size_t i = 2; i < CLAP_REMOTE_CONTROLS_COUNT; ++i)
        expect(page.param_ids[i] == CLAP_INVALID_ID, "An unused Output remote slot is not empty");
    std::cout << "2457 automatable parameters and two remote pages verified; original indices unchanged\n";
}

void checkState(const clap_plugin_factory_t& factory) {
    Instance source(factory), target(factory);
    const auto formant = getParameter(source, "Formant");
    Events events;
    const double value = formant.min_value + 0.75 * (formant.max_value - formant.min_value);
    events.set(formant.id, value);
    const auto output = getParameter(source, "Output");
    events.set(output.id, output.max_value);
    const auto& params = source.get<clap_plugin_params_t>(CLAP_EXT_PARAMS);
    for (uint32_t i = 10; i < params.count(source.plugin); ++i) {
        clap_param_info_t info{};
        expect(params.get_info(source.plugin, i, &info), "Missing appended state parameter");
        events.set(info.id, i % 2 == 0 ? info.max_value : info.min_value);
    }
    source.get<clap_plugin_params_t>(CLAP_EXT_PARAMS).flush(source.plugin, &events.input, &events.output);
    expect(std::abs(currentValue(source, formant.id) - formant.default_value) > 0.1,
           "Host parameter change was ignored");

    Stream state;
    expect(source.get<clap_plugin_state_t>(CLAP_EXT_STATE).save(source.plugin, &state.output), "State save failed");
    const auto& targetState = target.get<clap_plugin_state_t>(CLAP_EXT_STATE);
    expect(targetState.load(target.plugin, &state.input), "State load failed");
    expect(currentValue(source, formant.id) == currentValue(target, formant.id), "State parameter did not roundtrip");
    expect(currentValue(source, output.id) == currentValue(target, output.id), "Output gain state did not roundtrip");
    for (uint32_t i = 10; i < params.count(source.plugin); ++i) {
        clap_param_info_t info{};
        expect(params.get_info(source.plugin, i, &info), "Missing appended state metadata");
        expect(std::abs(currentValue(source, info.id) - currentValue(target, info.id)) < 0.0001,
               "Advanced/Chorale state did not roundtrip");
    }
    Stream invalid;
    invalid.bytes.assign(10, 41);
    expect(!targetState.load(target.plugin, &invalid.input), "Invalid state was accepted");
}

void checkLayout(const clap_plugin_factory_t& factory, clap_id configId, double sampleRate) {
    constexpr uint32_t block = 137;
    Instance instance(factory);
    const auto* plug = instance.plugin;
    const auto& configs = instance.get<clap_plugin_audio_ports_config_t>(CLAP_EXT_AUDIO_PORTS_CONFIG);
    expect(configs.count(plug) == 9, "Expected three main layouts times sidechain off/mono/stereo");
    clap_audio_ports_config_t config{};
    expect(configs.get(plug, configId, &config) && configs.select(plug, config.id), "Could not select port configuration");

    const auto& ports = instance.get<clap_plugin_audio_ports_t>(CLAP_EXT_AUDIO_PORTS);
    clap_audio_port_info_t inInfo{}, outInfo{};
    expect(ports.get(plug, 0, true, &inInfo) && ports.get(plug, 0, false, &outInfo), "Cannot read ports");
    expect(inInfo.channel_count == config.main_input_channel_count &&
           outInfo.channel_count == config.main_output_channel_count, "Port layout metadata mismatch");

    expect(ports.count(plug, true) == config.input_port_count
        && ports.count(plug, false) == 1, "Optional port count mismatch");
    clap_audio_port_info_t auxiliaryInfo{};
    const uint32_t auxiliaryChannels = configId / 3;
    if (auxiliaryChannels) {
        expect(ports.get(plug, 1, true, &auxiliaryInfo), "Missing external sidechain port");
        expect(auxiliaryInfo.channel_count == auxiliaryChannels
            && (auxiliaryInfo.flags & CLAP_AUDIO_PORT_IS_MAIN) == 0
            && auxiliaryInfo.in_place_pair == CLAP_INVALID_ID,
            "Sidechain channel / main-flag / in-place metadata mismatch");
    } else {
        expect(ports.count(plug, true) == 1, "Disabled sidechain advertised as active");
    }
    const auto bypass = getParameter(instance, "Bypass");
    expect((bypass.flags & CLAP_PARAM_IS_BYPASS) != 0, "Missing CLAP bypass flag");
    Events events;
    events.set(bypass.id, bypass.max_value);
    const auto outputGain = getParameter(instance, "Output");
    events.set(outputGain.id, outputGain.max_value);
    const auto& params = instance.get<clap_plugin_params_t>(CLAP_EXT_PARAMS);
    params.flush(plug, &events.input, &events.output);
    events.items.clear();

    const auto& render = instance.get<clap_plugin_render_t>(CLAP_EXT_RENDER);
    expect(render.set(plug, CLAP_RENDER_OFFLINE), "Offline mode rejected");
    expect(plug->activate(plug, sampleRate, 1, block), "Activation failed");
    const auto latency = instance.get<clap_plugin_latency_t>(CLAP_EXT_LATENCY).get(plug);
    expect(latency == 0u, "CLAP default graph should report zero latency in bypass");
    expect(plug->start_processing(plug), "Processing did not start");

    std::array<std::array<float, block>, 2> input{}, output{};
    std::array<float*, 2> inputPointers{input[0].data(), input[1].data()};
    std::array<float*, 2> outputPointers{output[0].data(), output[1].data()};
    std::array<std::array<float, block>, 2> auxiliary{};
    auxiliary[0].fill(.71f); auxiliary[1].fill(-.53f);
    std::array<float*, 2> auxiliaryPointers{auxiliary[0].data(), auxiliary[1].data()};
    std::array<clap_audio_buffer_t, 2> inputBuses{{
        {inputPointers.data(), nullptr, inInfo.channel_count, 0, 0},
        {auxiliaryPointers.data(), nullptr, auxiliaryChannels, 0, 0}}};
    clap_audio_buffer_t outputBus{outputPointers.data(), nullptr, outInfo.channel_count, 0, 0};
    clap_process_t process{};
    process.frames_count = block;
    process.audio_inputs = inputBuses.data();
    process.audio_outputs = &outputBus;
    process.audio_inputs_count = auxiliaryChannels ? 2 : 1;
    process.audio_outputs_count = 1;
    process.in_events = &events.input;
    process.out_events = &events.output;
    uint32_t position = 0;
    for (; position < latency + 3 * block; position += block) {
        input[0][5] = position == 0 ? 0.40f : 0.0f;
        input[1][5] = position == 0 ? -0.25f : 0.0f;
        process.steady_time = position;
        expect(plug->process(plug, &process) != CLAP_PROCESS_ERROR, "Offline render failed");
        for (uint32_t channel = 0; channel < outInfo.channel_count; ++channel)
            for (uint32_t i = 0; i < block; ++i) {
                const auto impulse = inInfo.channel_count == 2 && channel == 1 ? -0.25f : 0.40f;
                const auto expected = position + i == latency + 5 ? impulse : 0.0f;
                expect(std::abs(output[channel][i] - expected) < 0.000001f, "Bypass latency or channels incorrect");
            }
    }

    const auto formant = getParameter(instance, "Formant");
    events.set(bypass.id, bypass.min_value, 19);
    const auto advanced = getParameter(instance, "Advanced Doubler");
    const auto chorale = getParameter(instance, "Chorale Active");
    const auto voiceFormant = getParameter(instance, "Voice 1 Formant");
    events.set(advanced.id, advanced.max_value, 37);
    events.set(chorale.id, chorale.max_value, 49);
    events.set(voiceFormant.id, voiceFormant.max_value, 63);
    events.set(formant.id, formant.max_value, 76);
    events.set(outputGain.id, outputGain.min_value, 98);
    process.steady_time = position;
    expect(plug->process(plug, &process) != CLAP_PROCESS_ERROR, "Host automation processing failed");
    plug->stop_processing(plug);
    plug->deactivate(plug);
    expect(std::abs(currentValue(instance, formant.id) - formant.max_value) < 0.001, "Automation did not apply");
    expect(currentValue(instance, advanced.id) == advanced.max_value, "Advanced automation did not apply");
    expect(currentValue(instance, chorale.id) == chorale.max_value, "Chorale automation did not apply");
    expect(std::abs(currentValue(instance, voiceFormant.id) - voiceFormant.max_value) < 0.001,
           "Per-voice formant automation did not apply");
    expect(std::abs(currentValue(instance, outputGain.id) - outputGain.min_value) < 0.001,
           "Output gain automation did not apply");
    std::cout << config.name << ", " << sampleRate << " Hz, latency " << latency << " samples\n";
}

void checkSpectralDonor(const clap_plugin_factory_t& factory, clap_id configuration, double rate) {
    Instance instance(factory); const auto* plug = instance.plugin;
    const auto& configs = instance.get<clap_plugin_audio_ports_config_t>(CLAP_EXT_AUDIO_PORTS_CONFIG);
    expect(configs.select(plug, configuration), "CLAP spectral bus selection failed");
    const std::string xml = R"(<VocalStack><PARAM id="levelOn" value="0"/><PARAM id="doubleOn" value="0"/><PARAM id="formantOn" value="0"/><PARAM id="fx1Mix" value="100"/><PARAM id="fx1MorphAmount" value="100"/><PARAM id="fx1MorphSmoothing" value="0"/><RoutingGraph><Node id="1" type="0"/><Node id="14" type="22"/><Node id="100" type="23" mix="1"/><Node id="13" type="5"/><Link source="1" destination="100" inputPin="0"/><Link source="14" destination="100" inputPin="1"/><Link source="100" destination="13" inputPin="0"/></RoutingGraph></VocalStack>)";
    Stream preset; preset.bytes.resize(xml.size()+9);
    const uint32_t magic=0x21324356, length=uint32_t(xml.size());
    std::memcpy(preset.bytes.data(),&magic,4); std::memcpy(preset.bytes.data()+4,&length,4);
    std::memcpy(preset.bytes.data()+8,xml.data(),xml.size());
    const auto& state = instance.get<clap_plugin_state_t>(CLAP_EXT_STATE);
    // The fresh default graph is zero-latency. Loading Morph while active
    // must not change host compensation until reactivation.
    expect(plug->activate(plug,rate,1,512), "CLAP baseline activation failed");
    const auto& activationLatency = instance.get<clap_plugin_latency_t>(CLAP_EXT_LATENCY);
    expect(activationLatency.get(plug) == 0u, "CLAP fresh instance should report zero latency");
    expect(state.load(plug,&preset.input), "CLAP routed Morph state load failed");
    plug->on_main_thread(plug);
    expect(instance.restartRequests > 0, "CLAP pending Morph did not request automatic host reactivation");
    const int requestsBeforeActivation = instance.restartRequests;
    expect(activationLatency.get(plug) == 0u, "CLAP active state changed host latency before reactivation");
    plug->deactivate(plug);
    const uint32_t mainChannels = configuration%3 == 0 ? 2 : 1;
    const uint32_t outChannels = configuration%3 == 2 ? 1 : 2;
    const uint32_t auxiliaryChannels = configuration/3;
    constexpr uint32_t maximum = 512;
    expect(plug->activate(plug,rate,1,maximum), "CLAP Morph activation failed");
    const auto& latencyExtension = instance.get<clap_plugin_latency_t>(CLAP_EXT_LATENCY);
    const uint32_t latency = latencyExtension.get(plug);
    plug->on_main_thread(plug);
    expect(instance.restartRequests == requestsBeforeActivation, "CLAP activation caused a restart loop");
    expect(latency == 4096u, "CLAP Morph-only activation should report 4096 samples");
    expect(plug->start_processing(plug), "CLAP Morph start failed");
    std::array<std::array<float,maximum>,2> main{},aux{},out{};
    main[0].fill(.1f); main[1].fill(-.1f); aux[0].fill(-.04f); aux[1].fill(.07f);
    std::array<float*,2> mp{main[0].data(),main[1].data()}, ap{aux[0].data(),aux[1].data()}, op{out[0].data(),out[1].data()};
    std::array<clap_audio_buffer_t,2> input{{{mp.data(),nullptr,mainChannels,0,0},{ap.data(),nullptr,auxiliaryChannels,0,0}}};
    clap_audio_buffer_t output{op.data(),nullptr,outChannels,0,0};
    Events events; clap_process_t process{}; process.audio_inputs=input.data(); process.audio_inputs_count=auxiliaryChannels ? 2 : 1;
    process.audio_outputs=&output; process.audio_outputs_count=1; process.in_events=&events.input; process.out_events=&events.output;
    constexpr std::array<uint32_t,7> blocks{1,137,3,511,17,63,257}; double error=0;
    for(uint32_t position=0,block=0;position<latency+16000;++block) {
        process.frames_count=blocks[block%blocks.size()]; process.steady_time=position;
        expect(plug->process(plug,&process)!=CLAP_PROCESS_ERROR, "CLAP routed Morph process failed");
        if(position>latency+12000) for(uint32_t c=0;c<outChannels;++c) for(uint32_t i=0;i<process.frames_count;++i) {
            const float expected = auxiliaryChannels==0 ? 0.f : outChannels==1 && auxiliaryChannels==2 ? .055f
                : c==0 || auxiliaryChannels==1 ? (c==1 && mainChannels==2 ? -.04f : .04f) : mainChannels==2 ? -.07f : .07f;
            error=std::max(error,std::abs(double(out[c][i]-expected)));
        }
        position+=process.frames_count;
    }
    // Exercise sample-offset parameter splits on the live two-input graph.
    const auto fft=getParameter(instance,"FX 1 Spectral FFT Size");
    const auto freeze=getParameter(instance,"FX 1 Spectral Freeze");
    events.set(fft.id,fft.max_value,17); events.set(freeze.id,freeze.max_value,31); process.frames_count=137;
    expect(plug->process(plug,&process)!=CLAP_PROCESS_ERROR, "CLAP spectral event sub-block failed");
    expect(latencyExtension.get(plug)==latency, "CLAP FFT automation changed host latency");
    plug->stop_processing(plug); plug->deactivate(plug);
    Stream saved; expect(state.save(plug,&saved.output), "CLAP spectral state save failed");
    expect(state.load(plug,&saved.input), "CLAP spectral state recall failed");
    expect(plug->activate(plug,rate,1,maximum), "CLAP recalled activation failed");
    expect(latencyExtension.get(plug) == latency, "CLAP recalled Morph latency");
    const std::string noMorphXml = "<VocalStack><PARAM id=\"levelOn\" value=\"0\"/>"
        "<PARAM id=\"doubleOn\" value=\"0\"/><PARAM id=\"formantOn\" value=\"0\"/></VocalStack>";
    Stream noMorph; noMorph.bytes.resize(noMorphXml.size() + 9);
    const uint32_t noMorphLength = uint32_t(noMorphXml.size());
    std::memcpy(noMorph.bytes.data(), &magic, 4); std::memcpy(noMorph.bytes.data() + 4, &noMorphLength, 4);
    std::memcpy(noMorph.bytes.data() + 8, noMorphXml.data(), noMorphXml.size());
    expect(state.load(plug, &noMorph.input), "CLAP active Morph removal failed");
    expect(latencyExtension.get(plug) == latency, "CLAP Morph removal changed compensation while active");
    plug->deactivate(plug);
    expect(plug->activate(plug,rate,1,maximum), "CLAP no-Morph reactivation failed");
    expect(latencyExtension.get(plug) == 0u, "CLAP default graph reactivation should restore zero latency");
    plug->deactivate(plug);
    expect(error<2.e-3, "CLAP routed external donor/main-phase/absence/channel oracle failed");
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: vocalstack_clap_smoke <plugin.clap>\n";
        return 2;
    }
    try {
        Library library(std::filesystem::absolute(argv[1]));
        const auto* factory = static_cast<const clap_plugin_factory_t*>(
            library.entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
        expect(factory && factory->get_plugin_count(factory) == 1, "Unexpected CLAP factory");
        const auto* descriptor = factory->get_plugin_descriptor(factory, 0);
        expect(descriptor && descriptor->version
                   && std::string_view(descriptor->version) == VOCALSTACK_EXPECTED_VERSION,
               "CLAP descriptor version does not match the source release");
        std::cout << "CLAP version " << descriptor->version << " verified\n";
        checkHostControls(*factory);
        checkState(*factory);
        for (clap_id configuration = 0; configuration < 9; ++configuration)
            for (double rate : {44100., 48000., 96000.}) { checkLayout(*factory, configuration, rate); checkSpectralDonor(*factory, configuration, rate); }
        std::cout << "CLAP state, latency, bypass, automation and offline layout checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
