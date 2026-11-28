# Borrow Spatial EQ Gain/Depth queues for one callback. Keep JUCE's normal
# parameter notifications, original process context, and single MIDI/audio call.
set(wrapper "${juce_SOURCE_DIR}/modules/juce_audio_plugin_client/juce_audio_plugin_client_VST3.cpp")
file(READ "${wrapper}" contents)

macro(vocalstack_replace_vst3_anchor)
    string(FIND "${contents}" "${before}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "Pinned JUCE VST3 Spatial EQ automation anchor changed: ${before}")
    endif()
    string(REPLACE "${before}" "${after}" contents "${contents}")
endmacro()

if(NOT contents MATCHES "VOCALSTACK_VST3_SPATIAL_EQ_AUTOMATION")
    set(before "#include <juce_audio_plugin_client/VST3/juce_VST3ModuleInfo.h>")
    set(after [=[#include <juce_audio_plugin_client/VST3/juce_VST3ModuleInfo.h>
#if VOCALSTACK_VST3_SPATIAL_EQ_AUTOMATION
 #include "Vst3SpatialEqAutomation.h"
#endif]=])
    vocalstack_replace_vst3_anchor()

    set(before "    void processParameterChanges (Vst::IParameterChanges& paramChanges)")
    set(after [=[    void processParameterChanges (Vst::IParameterChanges& paramChanges
   #if VOCALSTACK_VST3_SPATIAL_EQ_AUTOMATION
                                  , vocalstack::Vst3SpatialEqAutomation& spatialAutomation, int samples
   #endif
                                  )]=])
    vocalstack_replace_vst3_anchor()

    set(before [=[                    if (auto* param = comPluginInstance->getParamForVSTParamID (vstParamID))
                        setValueAndNotifyIfChanged (*param, (float) change->value);]=])
    set(after [=[                    if (auto* param = comPluginInstance->getParamForVSTParamID (vstParamID))
                    {
                       #if VOCALSTACK_VST3_SPATIAL_EQ_AUTOMATION
                        if (samples > 0)
                            vocalstack::captureVst3SpatialEqAutomation (spatialAutomation, *param, paramQueue,
                                [] (void* queue, int index, vocalstack::Vst3SpatialEqAutomation::Point& point) noexcept
                                {
                                    Steinberg::int32 offset = 0;
                                    if (static_cast<Vst::IParamValueQueue*> (queue)->getPoint (index, offset, point.value) != kResultTrue)
                                        return false;
                                    point.sample = offset;
                                    return true;
                                }, numPoints, samples, { change->offsetSamples, change->value });
                       #endif
                        setValueAndNotifyIfChanged (*param, (float) change->value);
                    }]=])
    vocalstack_replace_vst3_anchor()

    set(before [=[        if (data.inputParameterChanges != nullptr)
            processParameterChanges (*data.inputParameterChanges);]=])
    set(after [=[       #if VOCALSTACK_VST3_SPATIAL_EQ_AUTOMATION
        vocalstack::Vst3SpatialEqAutomation spatialAutomation;
        if (data.inputParameterChanges != nullptr)
            processParameterChanges (*data.inputParameterChanges, spatialAutomation,
                data.numSamples <= processSetup.maxSamplesPerBlock ? data.numSamples : 0);
       #else
        if (data.inputParameterChanges != nullptr)
            processParameterChanges (*data.inputParameterChanges);
       #endif]=])
    vocalstack_replace_vst3_anchor()

    set(before [=[            if      (processSetup.symbolicSampleSize == Vst::kSample32) processAudio<float>  (data);
            else if (processSetup.symbolicSampleSize == Vst::kSample64) processAudio<double> (data);]=])
    set(after [=[           #if VOCALSTACK_VST3_SPATIAL_EQ_AUTOMATION
            if      (processSetup.symbolicSampleSize == Vst::kSample32) processAudio<float>  (data, spatialAutomation);
            else if (processSetup.symbolicSampleSize == Vst::kSample64) processAudio<double> (data, spatialAutomation);
           #else
            if      (processSetup.symbolicSampleSize == Vst::kSample32) processAudio<float>  (data);
            else if (processSetup.symbolicSampleSize == Vst::kSample64) processAudio<double> (data);
           #endif]=])
    vocalstack_replace_vst3_anchor()

    set(before "    void processAudio (Vst::ProcessData& data)")
    set(after [=[    void processAudio (Vst::ProcessData& data
   #if VOCALSTACK_VST3_SPATIAL_EQ_AUTOMATION
                       , vocalstack::Vst3SpatialEqAutomation& spatialAutomation
   #endif
                       )]=])
    vocalstack_replace_vst3_anchor()

    set(before [=[            const ScopedLock sl (pluginInstance->getCallbackLock());

            pluginInstance->setNonRealtime (data.processMode == Vst::kOffline);]=])
    set(after [=[            const ScopedLock sl (pluginInstance->getCallbackLock());
           #if VOCALSTACK_VST3_SPATIAL_EQ_AUTOMATION
            const vocalstack::ScopedVst3SpatialEqAutomation spatialScope (*pluginInstance, spatialAutomation);
           #endif

            pluginInstance->setNonRealtime (data.processMode == Vst::kOffline);]=])
    vocalstack_replace_vst3_anchor()
    file(WRITE "${wrapper}" "${contents}")
endif()

target_include_directories(VocalStack_VST3 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(VocalStack_VST3 PRIVATE VOCALSTACK_VST3_SPATIAL_EQ_AUTOMATION=1)
