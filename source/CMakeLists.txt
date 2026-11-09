cmake_minimum_required(VERSION 3.24)
project(VocalStack VERSION 1.2.0 LANGUAGES C CXX)
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

    juce_add_plugin(VocalStack
        COMPANY_NAME "Vocal Stack"
        BUNDLE_ID audio.vocalstack.effect
        PLUGIN_MANUFACTURER_CODE VoSt
        PLUGIN_CODE Vs01
        PRODUCT_NAME "Vocal Stack"
        FORMATS VST3
        VST3_CATEGORIES Fx Tools
        VST3_AUTO_MANIFEST FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE)
    target_sources(VocalStack PRIVATE src/PluginProcessor.cpp src/PluginEditor.cpp src/MeterPanel.cpp)
    target_compile_definitions(VocalStack PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)
    target_link_libraries(VocalStack PRIVATE
        vocalstack_dsp juce::juce_audio_processors juce::juce_recommended_config_flags)
    clap_juce_extensions_plugin(TARGET VocalStack
        CLAP_ID "audio.vocalstack.effect"
        CLAP_FEATURES audio-effect pitch-shifter utility stereo mono
        CLAP_PROCESS_EVENTS_RESOLUTION_SAMPLES 1)
endif()

if(VOCALSTACK_BUILD_TESTS)
    enable_testing()
    add_executable(vocalstack_dsp_tests tests/DspTests.cpp)
    target_link_libraries(vocalstack_dsp_tests PRIVATE vocalstack_dsp)
    add_test(NAME VocalStackDSP COMMAND vocalstack_dsp_tests)
    add_executable(vocalstack_chorale_tests tests/ChoraleTests.cpp)
    target_include_directories(vocalstack_chorale_tests PRIVATE src)
    add_test(NAME VocalStackChorale COMMAND vocalstack_chorale_tests)
    if(VOCALSTACK_BUILD_PLUGIN)
        add_executable(vocalstack_clap_smoke tests/ClapSmoke.cpp)
        target_link_libraries(vocalstack_clap_smoke PRIVATE clap-core ${CMAKE_DL_LIBS})
        add_test(NAME VocalStackCLAP COMMAND vocalstack_clap_smoke $<TARGET_FILE:VocalStack_CLAP>)

        juce_add_console_app(VocalStackEditorTests PRODUCT_NAME "Vocal Stack Editor Tests")
        target_sources(VocalStackEditorTests PRIVATE tests/EditorTests.cpp)
        target_link_libraries(VocalStackEditorTests PRIVATE VocalStack vocalstack_dsp
            juce::juce_audio_processors clap_juce_extensions juce::juce_recommended_config_flags)
        add_test(NAME VocalStackEditor COMMAND VocalStackEditorTests)
    endif()
endif()
