# A deferred routing change keeps the running latency until the host activates
# the new plan. Forward explicit same-latency notifications to the VST3 host,
# without requesting another restart from inside that activation.
set(wrapper "${juce_SOURCE_DIR}/modules/juce_audio_plugin_client/juce_audio_plugin_client_VST3.cpp")
file(READ "${wrapper}" contents)
if(NOT contents MATCHES "VOCALSTACK_VST3_DEFERRED_LATENCY")
    set(before [=[            if (details.latencyChanged && latencySamples != lastLatencySamples)
            {
                flags |= Vst::kLatencyChanged;
                lastLatencySamples = latencySamples;
            }]=])
    set(after [=[           #if VOCALSTACK_VST3_DEFERRED_LATENCY
            if (details.latencyChanged)
            {
                if (! inLatencyActivation)
                    flags |= Vst::kLatencyChanged;
                lastLatencySamples = latencySamples;
            }
           #else
]=])
    string(FIND "${contents}" "${before}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "Pinned JUCE VST3 latency handler changed")
    endif()
    string(REPLACE "${before}" "${after}${before}\n           #endif" contents "${contents}")
    set(before "    int lastLatencySamples = 0;")
    string(REPLACE "${before}" "   #if VOCALSTACK_VST3_DEFERRED_LATENCY\n    bool inLatencyActivation = false;\n   #endif\n${before}" contents "${contents}")
    set(before "        const auto willBeActive = (state != 0);")
    set(after [=[
       #if VOCALSTACK_VST3_DEFERRED_LATENCY
        auto* latencyController = juceVST3EditController.get();
        const auto wasInLatencyActivation = latencyController != nullptr && latencyController->inLatencyActivation;
        if (latencyController != nullptr)
            latencyController->inLatencyActivation = true;
        const ScopeGuard latencyActivationScope { [&]
        {
            if (latencyController != nullptr)
                latencyController->inLatencyActivation = wasInLatencyActivation;
        } };
       #endif
]=])
    string(FIND "${contents}" "${before}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "Pinned JUCE VST3 activation handler changed")
    endif()
    string(REPLACE "${before}" "${before}\n${after}" contents "${contents}")
    file(WRITE "${wrapper}" "${contents}")
endif()
target_compile_definitions(VocalStack_VST3 PRIVATE VOCALSTACK_VST3_DEFERRED_LATENCY=1)
