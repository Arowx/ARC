#!/usr/bin/env python3
"""Even out a folder of sounds for a game: trim silence, level the loudness, limit peaks, add tiny fades.

    python tools/audio_prep.py sfx   art/sfx_raw   assets/sfx              sound effects (-20 dB mean, peaks <= 0.7)
    python tools/audio_prep.py voice art/voice_raw assets/voice            speech (loudness-normalised to -17 LUFS)
    python tools/audio_prep.py loop  art/music_raw assets/music            loops/music: level only, no trim or fades
    python tools/audio_prep.py sfx   art/sfx_raw   assets/sfx --loop wind,rain    these files are treated as loops

Every audio file in IN (.mp3 .wav .ogg .flac .m4a) becomes OUT/<name>.mp3 (or --format wav), mono 44.1 kHz unless
--stereo. A file whose name is listed in --loop (or mode loop) is only levelled: trimming or fading a seamless loop
would break the seam.

  sfx    trim leading/trailing silence (below -55 dB), set the mean level to -20 dB (--level), limit peaks to 0.7,
         5 ms fade in, 20 ms fade out. 128 kbps.
  voice  trim silence (below -50 dB), loudnorm to -17 LUFS (--level), true peak -1.5 dB. 96 kbps.
  loop   loudnorm to -22 LUFS (--level), true peak -3 dB. 128 kbps.

Needs ffmpeg on PATH. (From the Babel project's sfx_prep.sh and voice_prep.sh.)
"""
import argparse, os, re, shutil, subprocess, sys

EXTS = ('.mp3', '.wav', '.ogg', '.flac', '.m4a', '.aac', '.opus')


def mean_volume(path, pre=()):
    """mean level in dB, measured after the filters in pre (the trim, so silence doesn't count)"""
    p = subprocess.run(['ffmpeg', '-hide_banner', '-i', path, '-af', ','.join(list(pre) + ['volumedetect']), '-f', 'null', '-'], capture_output=True, text=True)
    m = re.search(r'mean_volume:\s*(-?[\d.]+) dB', p.stderr)
    return float(m.group(1)) if m else None


def trim(threshold):
    s = 'silenceremove=start_periods=1:start_threshold=%sdB' % threshold
    return [s, 'areverse', s, 'areverse']


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('mode', choices=['sfx', 'voice', 'loop'])
    ap.add_argument('src', help='input folder (or one file)')
    ap.add_argument('out', help='output folder')
    ap.add_argument('--level', type=float, default=None, help='target: sfx mean dB (default -20); voice/loop LUFS (-17 / -22)')
    ap.add_argument('--loop', default='', help='comma-separated names (without extension) to treat as loops')
    ap.add_argument('--format', choices=['mp3', 'wav'], default='mp3')
    ap.add_argument('--stereo', action='store_true', help='keep two channels (default mono)')
    a = ap.parse_args()
    if not shutil.which('ffmpeg'): sys.exit('ffmpeg not found on PATH')
    files = [a.src] if os.path.isfile(a.src) else sorted(os.path.join(a.src, f) for f in os.listdir(a.src) if f.lower().endswith(EXTS))
    if not files: sys.exit('no audio files in ' + a.src)
    os.makedirs(a.out, exist_ok=True)
    loops = {n.strip() for n in a.loop.split(',') if n.strip()}
    for f in files:
        name = os.path.splitext(os.path.basename(f))[0]
        mode = 'loop' if name in loops else a.mode
        if mode == 'loop':
            chain = ['loudnorm=I=%g:TP=-3' % (a.level if a.level is not None and a.mode == 'loop' else -22)]; kbps = '128k'
        elif mode == 'voice':
            chain = trim(-50) + ['loudnorm=I=%g:TP=-1.5:LRA=11' % (a.level if a.level is not None else -17)]; kbps = '96k'
        else:
            mean = mean_volume(f, trim(-55))
            if mean is None: print('skip %s: could not measure it' % f); continue
            gain = (a.level if a.level is not None else -20.0) - mean
            chain = trim(-55) + ['volume=%.2fdB' % gain, 'alimiter=limit=0.7:level=false',
                                 'afade=t=in:d=0.005', 'areverse', 'afade=t=in:d=0.02', 'areverse']; kbps = '128k'
        chain.append('aresample=44100')
        dst = os.path.join(a.out, name + '.' + a.format)
        cmd = ['ffmpeg', '-v', 'error', '-y', '-i', f, '-af', ','.join(chain)]
        if not a.stereo: cmd += ['-ac', '1']
        cmd += ['-b:a', kbps] if a.format == 'mp3' else ['-c:a', 'pcm_s16le']
        r = subprocess.run(cmd + [dst], capture_output=True, text=True)
        if r.returncode: print('failed %s: %s' % (f, r.stderr.strip()[-300:])); continue
        print('%-28s %s -> %s' % (os.path.basename(f), mode, dst))


if __name__ == '__main__':
    main()
