# JUCE 8.0.12: an explicit latency notification requests host reactivation even
# before the processor publishes its new latency. Only VocalStack_VST3 opts in;
# AudioProcessor and console-test listener semantics remain unmodified.
#
# Processor contract: publish pending intent before notifying on the message
# thread, retain current DSP/reported latency until prepareToPlay(), and commit
# there with setLatencySamples(). setActive() notifications refresh the cache
# without requesting a second restart. Coalesce pending requests in the processor;
# neither notification delivery nor restartComponent() acknowledges activation.
function(vocalstack_patch_juce_latency)
    set(wrapper "${juce_SOURCE_DIR}/modules/juce_audio_plugin_client/juce_audio_plugin_client_VST3.cpp")
    file(READ "${wrapper}" contents)
    set(original "${contents}")

    set(before [=[            if (details.latencyChanged && latencySamples != lastLatencySamples)
            {
                flags |= Vst::kLatencyChanged;
                lastLatencySamples = latencySamples;
            }]=])
    set(after [=[           #if VOCALSTACK_VST3_DEFERRED_LATENCY
            // Explicit same-latency notifications request a future activation.
            // prepareToPlay commits that request inside setActive, not via another restart.
            if (details.latencyChanged)
            {
                if (! inLatencyActivation)
                    flags |= Vst::kLatencyChanged;

                lastLatencySamples = latencySamples;
            }
           #else
            if (details.latencyChanged && latencySamples != lastLatencySamples)
            {
                flags |= Vst::kLatencyChanged;
                lastLatencySamples = latencySamples;
            }
           #endif]=])
    vocalstack_replace_juce_latency_block("${contents}" "${before}" "${after}" contents)

    set(before [=[    int lastLatencySamples = 0;
    bool blueCatPatchwork = isBlueCatHost (hostContext.get());]=])
    set(after [=[   #if VOCALSTACK_VST3_DEFERRED_LATENCY
    bool inLatencyActivation = false;
   #endif
    int lastLatencySamples = 0;
    bool blueCatPatchwork = isBlueCatHost (hostContext.get());]=])
    vocalstack_replace_juce_latency_block("${contents}" "${before}" "${after}" contents)

    set(before [=[        const auto willBeActive = (state != 0);

        active = false;]=])
    set(after [=[        const auto willBeActive = (state != 0);

       #if VOCALSTACK_VST3_DEFERRED_LATENCY
        // Cover prepare and release, restoring the guard even under host reentrancy.
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

        active = false;]=])
    vocalstack_replace_juce_latency_block("${contents}" "${before}" "${after}" contents)

    # Validate every anchor before writing, and leave timestamps alone on reconfigure.
    if(NOT contents STREQUAL original)
        file(WRITE "${wrapper}" "${contents}")
    endif()
endfunction()

function(vocalstack_replace_juce_latency_block contents before after output)
    string(FIND "${contents}" "${after}" patched)
    if(NOT patched EQUAL -1)
        set(needle "${after}")
        set(position "${patched}")
    else()
        string(FIND "${contents}" "${before}" position)
        if(position EQUAL -1)
            message(FATAL_ERROR "Pinned JUCE VST3 latency implementation changed; review PatchJuceLatency.cmake")
        endif()
        set(needle "${before}")
    endif()
    string(LENGTH "${needle}" length)
    math(EXPR next "${position} + ${length}")
    string(SUBSTRING "${contents}" ${next} -1 remainder)
    string(FIND "${remainder}" "${needle}" duplicate)
    if(NOT duplicate EQUAL -1)
        message(FATAL_ERROR "Ambiguous JUCE VST3 latency patch anchor; review PatchJuceLatency.cmake")
    endif()
    if(patched EQUAL -1)
        string(REPLACE "${before}" "${after}" contents "${contents}")
    endif()
    set(${output} "${contents}" PARENT_SCOPE)
endfunction()

vocalstack_patch_juce_latency()
