# Apply after the existing pinned main-layout patch. IDs 0..2 retain their
# original meanings. Each added group selects mono/stereo optional auxiliary in.
set(wrapper "${clap_juce_SOURCE_DIR}/src/wrapper/clap-juce-wrapper.cpp")
file(READ "${wrapper}" contents)
string(FIND "${contents}" "// Vocal Stack optional auxiliary layouts v1" patched)
if(patched EQUAL -1)
    set(before "uint32_t audioPortsConfigCount() const noexcept override { return 3; }")
    string(FIND "${contents}" "${before}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "Pinned CLAP port configuration changed; review sidechain layouts")
    endif()
    string(REPLACE "${before}" "uint32_t audioPortsConfigCount() const noexcept override { return 9; } // Vocal Stack optional auxiliary layouts v1" contents "${contents}")
    string(REPLACE "const bool monoInput = index != 0;" "const auto mainLayout = index % 3;\n        const auto auxiliaryLayout = index / 3;\n        const bool monoInput = mainLayout != 0;" contents "${contents}")
    string(REPLACE "const bool monoOutput = index == 2;" "const bool monoOutput = mainLayout == 2;" contents "${contents}")
    string(REPLACE "snprintf(config->name, sizeof(config->name), \"%s\", names[index]);" "snprintf(config->name, sizeof(config->name), \"%s / Sidechain %s\", names[mainLayout], auxiliaryLayout == 0 ? \"Off\" : auxiliaryLayout == 1 ? \"Mono\" : \"Stereo\");" contents "${contents}")
    string(REPLACE "config->input_port_count = config->output_port_count = 1;" "config->input_port_count = auxiliaryLayout == 0 ? 1 : 2;\n        config->output_port_count = 1;" contents "${contents}")
    string(REPLACE "layout.inputBuses.set(0, configId == 0 ? stereo : mono);" "layout.inputBuses.set(0, configId % 3 == 0 ? stereo : mono);\n        layout.inputBuses.set(1, configId / 3 == 0 ? juce::AudioChannelSet::disabled() : configId / 3 == 1 ? mono : stereo);" contents "${contents}")
    string(REPLACE "layout.outputBuses.set(0, configId == 2 ? mono : stereo);" "layout.outputBuses.set(0, configId % 3 == 2 ? mono : stereo);" contents "${contents}")
    # This plugin has one mandatory main bus and one trailing optional input.
    # Keep the ordinary main port IDs and the auxiliary ID stable across layouts.
    string(REPLACE "return (uint32_t)processor->getBusCount(isInput);" "uint32_t count = 0;\n        for (int i = 0; i < processor->getBusCount(isInput); ++i)\n            if (processor->getBus(isInput, i)->isEnabled()) ++count;\n        return count;" contents "${contents}")
    string(REPLACE "const auto bus = processor->getBus(isInput, (int)index);\n        const auto busLayout" "const auto bus = processor->getBus(isInput, (int)index);\n        if (bus == nullptr || !bus->isEnabled()) return false;\n        const auto busLayout" contents "${contents}")
    file(WRITE "${wrapper}" "${contents}")
endif()
