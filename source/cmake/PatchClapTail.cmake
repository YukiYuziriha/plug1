# The pinned wrapper converts tail seconds through a signed int. Frozen captures
# need CLAP's infinite-tail sentinel, and long finite delays must not overflow.
set(wrapper "${clap_juce_SOURCE_DIR}/src/wrapper/clap-juce-wrapper.cpp")
file(READ "${wrapper}" contents)
set(before [=[        return uint32_t(
            juce::roundToIntAccurate((double)sampleRate() * processor->getTailLengthSeconds()));]=])
set(after [=[        const double samples = (double)sampleRate() * processor->getTailLengthSeconds();
        if (!std::isfinite(samples) || samples >= (double)INT32_MAX)
            return INT32_MAX; // CLAP: >= INT32_MAX means infinite tail.
        return samples > 0 ? (uint32_t)std::ceil(samples) : 0;]=])
string(FIND "${contents}" "${after}" patched)
if(patched EQUAL -1)
    string(FIND "${contents}" "${before}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "Pinned CLAP tail implementation changed; review the infinite-tail fix")
    endif()
    string(REPLACE "${before}" "${after}" contents "${contents}")
    file(WRITE "${wrapper}" "${contents}")
endif()
