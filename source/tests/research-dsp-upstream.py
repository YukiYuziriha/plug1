"""Optional public-source audit fetch; no audio/project data is transmitted.

Print pinned source SHA256 and relevant code excerpts. No vendoring, build,
installation, or files written. Run manually when reviewing DSP-Audit.md.
"""
import hashlib
import urllib.request

references = [
    ('pichenettes/eurorack', 'f4cfa7697095a28d292bff758beb51deb49b5a86',
     [('clouds/dsp/grain.h', ('void Start(', 'RenderEnvelope', 'OverlapAdd')),
      ('clouds/dsp/granular_sample_player.h', ('kMaxNumGrains', 'gain_normalization =', 'eaten_by_play_head', 'available -=')),
      ('clouds/dsp/audio_buffer.h', ('void Write(', 'Read(', 'head_'))]),
    ('juce-framework/JUCE', '51a8a6d7aeae7326956d747737ccf1575e61e209',
     [('modules/juce_audio_basics/utilities/juce_Reverb.h',
       ('AGPL', 'wetScaleFactor', 'gain =', 'combTunings', 'left[i]  =', 'buffer[index]'))]),
    ('michaelwillis/dragonfly-reverb', '440ec7b3b3db2fec34b9f80cf4890102ca14d1c3',
     [('plugins/dragonfly-hall-reverb/DSP.cpp', ('setdryr', 'processreplace', 'lateLevel  *')),
      ('common/freeverb/progenitor2.cpp', ('GNU General', 'while(count--', 'input diffusion', 'cross loop signal')),
      ('common/freeverb/COPYING', ('GNU GENERAL', 'Version 2')),
      ('LICENSE', ('GNU GENERAL', 'Version 3'))]),
    ('signalsmith-audio/signalsmith-stretch', '88061fd4eda56a2353c5d65c74aa3f4ccf76f9da',
     [('signalsmith-stretch.h', ('inputLatency()', 'outputLatency()', 'void configure(', 'stft.configure')),
      ('LICENSE.txt', ('MIT License', 'Copyright'))]),
]
for repo, revision, files in references:
    for path, needles in files:
        url = f'https://raw.githubusercontent.com/{repo}/{revision}/{path}'
        data = urllib.request.urlopen(url, timeout=30).read()
        print(f'\n{url}\nSHA256 {hashlib.sha256(data).hexdigest()}')
        lines = data.decode().splitlines()
        selected = set()
        for i, line in enumerate(lines):
            if any(needle in line for needle in needles):
                selected.update(range(max(0, i-1), min(len(lines), i+7)))
        for i in sorted(selected):
            print(f'{i+1}: {lines[i]}')
