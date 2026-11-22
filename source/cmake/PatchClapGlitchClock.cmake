# JUCE sees a smaller block when CLAP splits on automation. Rebase the pinned
# wrapper's playhead to the start of that block (or a later transport event).
set(wrapper "${clap_juce_SOURCE_DIR}/src/wrapper/clap-juce-wrapper.cpp")
file(READ "${wrapper}" contents)
if(NOT contents MATCHES "Vocal Stack CLAP sub-block clock")
    function(glitch_clock_replace before after)
        string(FIND "${contents}" "${before}" found)
        if(found EQUAL -1)
            message(FATAL_ERROR "Pinned CLAP transport changed; review the sub-block clock patch")
        endif()
        string(REPLACE "${before}" "${after}" updated "${contents}")
        set(contents "${updated}" PARENT_SCOPE)
    endfunction()
    glitch_clock_replace([=[            auto posinfo = PositionInfo();]=] [=[            auto posinfo = PositionInfo();
            // Vocal Stack CLAP sub-block clock
            const bool playing = (flags & CLAP_TRANSPORT_IS_PLAYING) != 0;
            const auto rate = sampleRate();
            const auto elapsed = playing ? juce::jmax(0, playHeadBlockOffset - transportAnchorOffset) : 0;]=])
    glitch_clock_replace([=[posinfo.setBpm(transportInfo->tempo);]=]
        [=[posinfo.setBpm(transportInfo->tempo + elapsed * transportInfo->tempo_inc);]=])
    glitch_clock_replace([=[                posinfo.setPpqPosition(1.0 * (double)transportInfo->song_pos_beats /
                                       CLAP_BEATTIME_FACTOR);]=] [=[                const double beatAdvance = (flags & CLAP_TRANSPORT_HAS_TEMPO) && rate > 0
                    ? elapsed * (transportInfo->tempo + 0.5 * (elapsed - 1) * transportInfo->tempo_inc) / (60.0 * rate)
                    : 0.0;
                posinfo.setPpqPosition((double)transportInfo->song_pos_beats / CLAP_BEATTIME_FACTOR + beatAdvance);]=])
    glitch_clock_replace([=[                posinfo.setTimeInSeconds(timeInSeconds);]=]
        [=[                posinfo.setTimeInSeconds(timeInSeconds + (rate > 0 ? elapsed / rate : 0.0));]=])
    glitch_clock_replace([=[posinfo.setTimeInSamples((int64_t)std::llround(timeInSeconds * sampleRate()));]=]
        [=[posinfo.setTimeInSamples((int64_t)std::llround(timeInSeconds * rate) + elapsed);]=])
    glitch_clock_replace([=[        if (process->transport)]=]
        [=[        playHeadBlockOffset = transportAnchorOffset = 0;
        if (process->transport)]=])
    glitch_clock_replace([=[            // process the events in this sub-block]=]
        [=[            playHeadBlockOffset = n;
            // process the events in this sub-block]=])
    glitch_clock_replace([=[            transportInfo = reinterpret_cast<const clap_event_transport *>(event);]=]
        [=[            transportInfo = reinterpret_cast<const clap_event_transport *>(event);
            transportAnchorOffset = (int)event->time;]=])
    glitch_clock_replace([=[    bool hasTransportInfo{false};]=]
        [=[    bool hasTransportInfo{false};
    int playHeadBlockOffset = 0, transportAnchorOffset = 0;]=])
    file(WRITE "${wrapper}" "${contents}")
endif()
