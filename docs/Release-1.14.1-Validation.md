# HEAVEN 1.14.1

- The plugin header displays the version of the loaded binary below the skin selector, in Stack and Routing and all four skins.
- Voice add/remove and Reset now send the missing latency-change notification to the host. Requests happen outside the control lock, with active latency preserved until host reactivation.

## Validation

CLAP and VST3 Release builds succeeded. Native Sidechain/UI checks passed on the enumerated non-primary display, including visibility of the version label. Host-listener regressions reproduced the missing requests and passed after the fix for all three commands, including no-change controls and duplicate-request prevention.

A separate offline Sidechain regression rendered raw, neutral, Pitch +12 st, Detune +10 cents and Formant +3 st at 48 kHz with mono/stereo and 64/137/512-sample blocks. Raw and neutral were identical; processed output was finite and had no post-warmup zero samples in these runs. This did **not** reproduce or resolve the reported sustained DAW stutter. Bitwig was not controlled or tested. These tests do not establish real-time performance in the user's project.

The previously recorded unrelated `Gate bridge graph` failure in the full Sidechain Processor suite remains; focused checks are reported separately rather than claiming a clean full suite.
