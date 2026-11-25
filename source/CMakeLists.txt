cmake_minimum_required(VERSION 3.24)
project(VocalStack VERSION 1.14.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

include(FetchContent)
option(VOCALSTACK_BUILD_PLUGIN "Build the CLAP and VST3 plugins" ON)
option(VOCALSTACK_BUILD_TESTS "Build the DSP and host validation tests" ON)

FetchContent_Declare(signalsmith-linear
    GIT_REPOSITORY https://github.com/Signalsmith-Audio/linear.git
    GIT_TAG 7bb1c8f36794d6980080ce817c91f8da8fe56a66)
FetchContent_Declare(signalsmith-stretch
    GIT_REPOSITORY https://github.com/Signalsmith-Audio/signalsmith-stretch.git
    GIT_TAG 88061fd4eda56a2353c5d65c74aa3f4ccf76f9da
    GIT_SUBMODULES "")
FetchContent_MakeAvailable(signalsmith-linear signalsmith-stretch)

add_library(vocalstack_dsp STATIC src/VocalEngine.cpp)
set_target_properties(vocalstack_dsp PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(vocalstack_dsp PUBLIC src)
target_link_libraries(vocalstack_dsp PUBLIC signalsmith-stretch)

if(VOCALSTACK_BUILD_PLUGIN)
    FetchContent_Declare(JUCE
        URL https://codeload.github.com/juce-framework/JUCE/tar.gz/refs/tags/8.0.12
        URL_HASH SHA256=dec1a8baee5aaec4502b717a421b850e257ab7e9c2efd03766185ec1957cf102)
    FetchContent_Declare(clap_juce
        GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
        GIT_TAG 6bde108329ff9fc24404b79b8b397261a75cf6a8
        PATCH_COMMAND ${CMAKE_COMMAND} -DSOURCE_DIRECTORY=<SOURCE_DIR>
            -DPATCH_FILE=${CMAKE_CURRENT_SOURCE_DIR}/cmake/clap-ports.patch
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/PatchClap.cmake)
    FetchContent_MakeAvailable(JUCE clap_juce)
    include(cmake/PatchClapTail.cmake)
    include(cmake/PatchClapSidechain.cmake)
    include(cmake/PatchClapGlitchClock.cmake)

    juce_add_plugin(VocalStack
        COMPANY_NAME "lil grass block"
        BUNDLE_ID audio.vocalstack.effect
        PLUGIN_MANUFACTURER_CODE VoSt
        PLUGIN_CODE Vs01
        PRODUCT_NAME "HEAVEN by lil grass block"
        FORMATS VST3
        VST3_CATEGORIES Fx Tools
        VST3_AUTO_MANIFEST FALSE
        NEEDS_MIDI_INPUT TRUE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE)
    juce_add_binary_data(VocalStackFonts SOURCES
        assets/PixelifySans.ttf
        assets/OxaniumMedium.ttf
        assets/OxaniumSemiBold.ttf)
    target_link_libraries(VocalStack PUBLIC VocalStackFonts)
    target_sources(VocalStack PRIVATE src/PluginProcessor.cpp src/PluginEditor.cpp src/MeterPanel.cpp)
    target_compile_definitions(VocalStack PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)
    target_link_libraries(VocalStack PRIVATE
        vocalstack_dsp juce::juce_audio_processors juce::juce_recommended_config_flags)
    include(cmake/PatchVst3Activation.cmake)
    clap_juce_extensions_plugin(TARGET VocalStack
        CLAP_ID "audio.vocalstack.effect"
        CLAP_FEATURES audio-effect pitch-shifter utility stereo mono
        CLAP_PROCESS_EVENTS_RESOLUTION_SAMPLES 1)
    if(WIN32)
        # JUCE 8.0.12 omits this input dependency, leaving PE versions stale on incremental builds.
        set(vocalstack_version_info "${CMAKE_CURRENT_BINARY_DIR}/VocalStack_artefacts/JuceLibraryCode")
        add_custom_command(OUTPUT "${vocalstack_version_info}/VocalStack_resources.rc"
            APPEND DEPENDS "${vocalstack_version_info}/Info.txt")
        target_sources(VocalStack_CLAP PRIVATE $<TARGET_OBJECTS:VocalStack_rc_lib>)
    endif()
endif()

if(VOCALSTACK_BUILD_TESTS)
    enable_testing()
    add_executable(vocalstack_spectralmorph_benchmark tests/SpectralMorphBenchmark.cpp)
    target_include_directories(vocalstack_spectralmorph_benchmark PRIVATE src)
    add_executable(vocalstack_spectralmorph_tests tests/SpectralMorphTests.cpp)
    target_include_directories(vocalstack_spectralmorph_tests PRIVATE src)
    add_test(NAME VocalStackSpectralMorph COMMAND vocalstack_spectralmorph_tests)
    add_executable(vocalstack_spectralrouting_tests tests/SpectralRoutingTests.cpp)
    target_link_libraries(vocalstack_spectralrouting_tests PRIVATE vocalstack_dsp)
    add_test(NAME VocalStackSpectralRouting COMMAND vocalstack_spectralrouting_tests)
    add_executable(vocalstack_sidechainsource_tests tests/SidechainSourceTests.cpp)
    target_link_libraries(vocalstack_sidechainsource_tests PRIVATE vocalstack_dsp)
    add_test(NAME VocalStackSidechainSource COMMAND vocalstack_sidechainsource_tests)
    add_executable(vocalstack_factoryfx_tests tests/FactoryFxTests.cpp)
    target_include_directories(vocalstack_factoryfx_tests PRIVATE src)
    add_test(NAME VocalStackFactoryFx COMMAND vocalstack_factoryfx_tests)
    add_executable(vocalstack_creative_tests tests/CreativeEffectsTests.cpp)
    target_include_directories(vocalstack_creative_tests PRIVATE src)
    add_test(NAME VocalStackCreative COMMAND vocalstack_creative_tests)
    add_executable(vocalstack_creativetail_tests tests/CreativeTailTests.cpp)
    target_include_directories(vocalstack_creativetail_tests PRIVATE src)
    add_test(NAME VocalStackCreativeTail COMMAND vocalstack_creativetail_tests)
    # Informational, run separately to avoid timings polluted by parallel CTest jobs.
    add_executable(vocalstack_effects_benchmark tests/EffectsBenchmark.cpp)
    target_include_directories(vocalstack_effects_benchmark PRIVATE src)
    add_executable(vocalstack_utilityfilters_tests tests/UtilityFiltersTests.cpp)
    target_include_directories(vocalstack_utilityfilters_tests PRIVATE src)
    add_test(NAME VocalStackUtilityFilters COMMAND vocalstack_utilityfilters_tests)
    add_executable(vocalstack_dsp_tests tests/DspTests.cpp)
    target_link_libraries(vocalstack_dsp_tests PRIVATE vocalstack_dsp)
    add_test(NAME VocalStackDSP COMMAND vocalstack_dsp_tests)
    add_executable(vocalstack_chorale_tests tests/ChoraleTests.cpp)
    target_include_directories(vocalstack_chorale_tests PRIVATE src)
    add_test(NAME VocalStackChorale COMMAND vocalstack_chorale_tests)
    add_executable(vocalstack_fxmonitor_tests tests/FxMonitorTests.cpp)
    target_link_libraries(vocalstack_fxmonitor_tests PRIVATE vocalstack_dsp)
    add_test(NAME VocalStackFxMonitor COMMAND vocalstack_fxmonitor_tests)
    foreach(suite IN ITEMS RoutingBackend RoutingActivation VocalLatency VoiceBackend FxAutomation PitchResolution FormantRatio ToneEffects Multiband LeadMixBackend WaveshaperExpansion MainBusFxRack WetPocket DspAudit DspAuditRouting GrainSnapshot)
        string(TOLOWER "${suite}" suite_lower)
        add_executable(vocalstack_${suite_lower}_tests tests/${suite}Tests.cpp)
        target_link_libraries(vocalstack_${suite_lower}_tests PRIVATE vocalstack_dsp)
        add_test(NAME VocalStack${suite} COMMAND vocalstack_${suite_lower}_tests)
    endforeach()

    # Optional pinned-history comparison; ordinary source archives need no Git/Python.
    option(VOCALSTACK_TEST_BASELINE "Compare default DSP with the pre-routing Git revision" OFF)
    if(VOCALSTACK_TEST_BASELINE)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/prepare-backend-baseline.py"
            "${CMAKE_CURRENT_BINARY_DIR}/backend-tests/baseline" RESULT_VARIABLE baseline_result)
        if(NOT baseline_result EQUAL 0)
            message(FATAL_ERROR "Could not prepare the pinned baseline DSP")
        endif()
        add_executable(vocalstack_baseline_tests tests/BaselineCompatibilityTests.cpp
            "${CMAKE_CURRENT_BINARY_DIR}/backend-tests/baseline/VocalEngine.cpp")
        target_include_directories(vocalstack_baseline_tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/backend-tests")
        target_link_libraries(vocalstack_baseline_tests PRIVATE vocalstack_dsp)
        add_test(NAME VocalStackBaseline COMMAND vocalstack_baseline_tests)
    endif()
    if(VOCALSTACK_BUILD_PLUGIN)
        add_executable(vocalstack_clap_smoke tests/ClapSmoke.cpp)
        target_compile_definitions(vocalstack_clap_smoke PRIVATE VOCALSTACK_EXPECTED_VERSION="${PROJECT_VERSION}")
        target_link_libraries(vocalstack_clap_smoke PRIVATE clap-core ${CMAKE_DL_LIBS})
        add_test(NAME VocalStackCLAP COMMAND vocalstack_clap_smoke $<TARGET_FILE:VocalStack_CLAP>)

        juce_add_console_app(VocalStackTypographyTests PRODUCT_NAME "HEAVEN Typography Tests")
        target_sources(VocalStackTypographyTests PRIVATE tests/TypographyTests.cpp)
        target_include_directories(VocalStackTypographyTests PRIVATE src)
        target_link_libraries(VocalStackTypographyTests PRIVATE VocalStackFonts
            juce::juce_gui_basics juce::juce_recommended_config_flags)
        add_test(NAME VocalStackTypography COMMAND VocalStackTypographyTests)

        juce_add_console_app(VocalStackEditorTests PRODUCT_NAME "Vocal Stack Editor Tests")
        target_sources(VocalStackEditorTests PRIVATE tests/EditorTests.cpp)
        target_link_libraries(VocalStackEditorTests PRIVATE VocalStack vocalstack_dsp
            juce::juce_audio_processors clap_juce_extensions juce::juce_recommended_config_flags)
        add_test(NAME VocalStackEditor COMMAND VocalStackEditorTests)
        add_test(NAME VocalStackControlLayoutUpdate COMMAND VocalStackEditorTests --filter "Control layout update")
        add_test(NAME VocalStackCompressorHistory COMMAND VocalStackEditorTests --filter "Compressor measured")
        add_test(NAME VocalStackUtilityVisualUpdate COMMAND VocalStackEditorTests --filter "Utility visual telemetry")
        set_tests_properties(VocalStackEditor VocalStackControlLayoutUpdate VocalStackCompressorHistory VocalStackUtilityVisualUpdate
            PROPERTIES RESOURCE_LOCK VocalStackNativeGui)

        juce_add_console_app(VocalStackModulationTests PRODUCT_NAME "Vocal Stack Modulation Tests")
        target_sources(VocalStackModulationTests PRIVATE tests/ModulationBackendTests.cpp)
        target_include_directories(VocalStackModulationTests PRIVATE src)
        target_link_libraries(VocalStackModulationTests PRIVATE juce::juce_core juce::juce_recommended_config_flags)
        add_test(NAME VocalStackModulation COMMAND VocalStackModulationTests)

        juce_add_console_app(VocalStackModulationProcessorTests PRODUCT_NAME "Vocal Stack Modulation Processor Tests")
        target_sources(VocalStackModulationProcessorTests PRIVATE tests/ModulationProcessorTests.cpp)
        target_link_libraries(VocalStackModulationProcessorTests PRIVATE VocalStack vocalstack_dsp
            juce::juce_audio_processors clap_juce_extensions juce::juce_recommended_config_flags)
        add_test(NAME VocalStackModulationProcessor COMMAND VocalStackModulationProcessorTests)

        if(WIN32)
            add_executable(vocalstack_vst3_sidechain_smoke tests/Vst3SidechainSmoke.cpp)
            target_include_directories(vocalstack_vst3_sidechain_smoke PRIVATE
                "${juce_SOURCE_DIR}/modules/juce_audio_processors_headless/format_types/VST3_SDK")
            add_dependencies(vocalstack_vst3_sidechain_smoke VocalStack_VST3)
            add_test(NAME VocalStackVST3Sidechain COMMAND vocalstack_vst3_sidechain_smoke $<TARGET_FILE:VocalStack_VST3>)
        endif()

        juce_add_console_app(VocalStackSidechainProcessorTests PRODUCT_NAME "Vocal Stack Sidechain Processor Tests")
        target_sources(VocalStackSidechainProcessorTests PRIVATE tests/SidechainProcessorTests.cpp)
        target_link_libraries(VocalStackSidechainProcessorTests PRIVATE VocalStack vocalstack_dsp
            juce::juce_audio_processors clap_juce_extensions juce::juce_recommended_config_flags)
        add_test(NAME VocalStackSidechainProcessor COMMAND VocalStackSidechainProcessorTests)

        juce_add_console_app(VocalStackRoutingSplitterStateTests PRODUCT_NAME "Vocal Stack Routing Splitter State Tests")
        target_sources(VocalStackRoutingSplitterStateTests PRIVATE tests/RoutingSplitterStateTests.cpp)
        target_link_libraries(VocalStackRoutingSplitterStateTests PRIVATE VocalStack vocalstack_dsp
            juce::juce_audio_processors clap_juce_extensions juce::juce_recommended_config_flags)
        add_test(NAME VocalStackRoutingSplitterState COMMAND VocalStackRoutingSplitterStateTests)

        juce_add_console_app(VocalStackMainBusFxRackUiTests PRODUCT_NAME "Vocal Stack Main Bus FX Rack UI Tests")
        target_sources(VocalStackMainBusFxRackUiTests PRIVATE tests/MainBusFxRackUiTests.cpp)
        target_link_libraries(VocalStackMainBusFxRackUiTests PRIVATE VocalStack vocalstack_dsp
            juce::juce_audio_processors clap_juce_extensions juce::juce_recommended_config_flags)
        add_test(NAME VocalStackMainBusFxRackUi COMMAND VocalStackMainBusFxRackUiTests)

        juce_add_console_app(VocalStackGrainSnapshotUiTests PRODUCT_NAME "Vocal Stack Grain Snapshot UI Tests")
        target_sources(VocalStackGrainSnapshotUiTests PRIVATE tests/GrainSnapshotUiTests.cpp)
        target_link_libraries(VocalStackGrainSnapshotUiTests PRIVATE VocalStack vocalstack_dsp
            juce::juce_audio_processors clap_juce_extensions juce::juce_recommended_config_flags)
        add_test(NAME VocalStackGrainSnapshotUi COMMAND VocalStackGrainSnapshotUiTests)
        set_tests_properties(VocalStackMainBusFxRackUi VocalStackGrainSnapshotUi
            PROPERTIES RESOURCE_LOCK VocalStackNativeGui)

        juce_add_console_app(VocalStackFactoryPresetAudioTests PRODUCT_NAME "Vocal Stack Factory Preset Audio Tests")
        target_sources(VocalStackFactoryPresetAudioTests PRIVATE tests/FactoryPresetAudioTests.cpp)
        target_link_libraries(VocalStackFactoryPresetAudioTests PRIVATE VocalStack vocalstack_dsp
            juce::juce_audio_processors clap_juce_extensions juce::juce_recommended_config_flags)
        add_test(NAME VocalStackFactoryPresetValidation COMMAND VocalStackFactoryPresetAudioTests --validate)
        foreach(preset RANGE 0 29)
            add_test(NAME VocalStackFactoryPresetAudio${preset} COMMAND VocalStackFactoryPresetAudioTests ${preset})
        endforeach()

        # Informational only; explicitly opt in after stopping concurrent builds/tests.
        juce_add_console_app(VocalStackReleasePerformanceProbe PRODUCT_NAME "Vocal Stack Release Performance Probe")
        target_sources(VocalStackReleasePerformanceProbe PRIVATE tests/ReleasePerformanceProbe.cpp)
        target_link_libraries(VocalStackReleasePerformanceProbe PRIVATE VocalStack vocalstack_dsp
            juce::juce_audio_processors clap_juce_extensions juce::juce_recommended_config_flags)
    endif()
endif()
