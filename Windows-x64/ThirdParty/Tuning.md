# Tuning integration notice (v1.12.0)

This notice supplements, and does not replace or edit, the retained donor files
and audit in [MXTune](MXTune/README.md).

## Copyright, license and original sources

- Detector donor: Liu An Lin's MXTune at commit
  `ea73804baff9e483d70b0056ebc91523402210eb`,
  <https://github.com/liuanlin-mx/MXTune>.
  `MXTune/pitch_detector_talent.cpp` and `.h` retain Copyright (C) 2021–2023
  Liu An Lin and **GPL-3.0-or-later** notices. They are retained for source and
  provenance, not compiled as separate translation units.
- MXTune traces its detector to Thomas A. Baran's Autotalent 0.2. The retained
  `MXTune/autotalent-0.2-notice.txt` preserves the GPL-2.0-or-later source grant;
  selecting GPLv3 is permitted. Keep `MXTune/COPYING-autotalent` and
  `MXTune/LICENSE` with that notice. The donor audit records the upstream archive
  and SHA256. Do not discard the earlier provenance because MXTune is GPL3+.
- The actual detector adaptation is embedded in
  `source/src/AutomaticTuner.h`, with its GPL-3.0-or-later attribution header.
  The full editable algorithm accompanies source distributions, not only an
  interface, downloaded binary or independently installed dependency.
- Signalsmith Stretch and Linear retain their MIT notices in
  `Signalsmith-Stretch.txt` and `Signalsmith-Linear.txt`. Their exact pins are
  recorded in `source/CMakeLists.txt` and `docs/Licensing.md`. The tuner uses
  these existing dependencies; it does not use MXTune's shifter, FFTW or
  PureData/mayer FFT.

## Integration changes and scope

The adaptation uses preallocated RAII storage, Signalsmith FFT, linked stereo
analysis, guarded silence/finite handling, interpolated peak estimation and
new note selection, stability and correction control. One linked stereo
Signalsmith shifter supplies the tuned source, with real spectral-envelope
formant compensation. The engine aligns clean/tuned sources and blends them
before each existing voice renderer; there is only one shared tuner before
fan-out. No modifications to Signalsmith donor sources are required.

The MXTune README's standalone-module statements describe the original isolated
module handoff. In the 1.12.0 integration, `VocalEngine`, processor state/latency
handling and the themed Tuning tab now connect that module to the plugin.
Use `docs/Tuning.md` for current behavior, not the standalone handoff's statement
that no engine/UI/package files changed.

The combined plugin retains the project's GPLv3 and JUCE's AGPLv3 terms through
section 13's combination permission, not a blanket relicensing of donor code.
Preserve all copyright, license and modification notices. A complete
corresponding-source package must include `ThirdParty/MXTune` in full,
`AutomaticTuner.h`, settings/integration/UI code, build scripts, tests and the
pinned dependency sources. An older source ZIP without the MXTune adaptation
cannot serve as the complete corresponding source for 1.12.0.

The software is without warranty under the applicable licenses. No proprietary
Antares code or equivalence claim is involved. Real-vocal listening remains
unverified; synthetic checks are not a sonic certification.
