cmake_minimum_required(VERSION 3.24)
project(VocalStackAutomaticTunerTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(VOCALSTACK_DSP_DEPS "${CMAKE_CURRENT_LIST_DIR}/../../build/_deps" CACHE PATH "Existing pinned Signalsmith checkouts")
add_executable(vocalstack_tuning_tests AutomaticTunerTests.cpp)
target_include_directories(vocalstack_tuning_tests PRIVATE
    ../../src
    "${VOCALSTACK_DSP_DEPS}/signalsmith-stretch-src/include"
    "${VOCALSTACK_DSP_DEPS}/signalsmith-linear-src/include")
if(MSVC)
    target_compile_options(vocalstack_tuning_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(vocalstack_tuning_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
enable_testing()
add_test(NAME VocalStackAutomaticTuner COMMAND vocalstack_tuning_tests)
