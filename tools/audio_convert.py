#!/usr/bin/env python3
"""Convert audio files (MP3, OGG, FLAC, WAV, ...) to .qoa for ARC.

    python3 tools/audio_convert.py IN [IN ...] --out DIR [--rate 44100] [--mono]
    python3 tools/audio_convert.py assets_src/audio/*.mp3 --out assets/audio

ARC loads MP3, QOA and 16-bit WAV itself. Use this tool for other formats (OGG, FLAC...), or to turn MP3 into QOA
where decoding cost matters (QOA is cheaper to decode). It uses ffmpeg (on PATH) to decode, then
tools/qoa_encode.c to encode. QOA is ~3.2 bits per sample and sounds clean on music and
voice, but buzzy synthetic effects can come out hissy; any file whose QOA signal-to-noise ratio is below
--min-snr (default 30 dB) is written as 16-bit .wav instead; arc_sound_load() reads both.
The encoder is compiled on first use with cc/gcc/clang (or set QOA_ENCODE to a prebuilt binary).
Files that are already up to date are skipped.
"""
import argparse, os, re, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))


def encoder():
    exe = os.environ.get('QOA_ENCODE')
    if exe:
        return exe
    exe = os.path.join(HERE, 'bin', 'qoa_encode' + ('.exe' if os.name == 'nt' else ''))
    src = os.path.join(HERE, 'qoa_encode.c')
    if os.path.exists(exe) and os.path.getmtime(exe) >= os.path.getmtime(src):
        return exe
    cc = next((c for c in (os.environ.get('CC'), 'cc', 'gcc', 'clang') if c and shutil.which(c)), None)
    if not cc:
        sys.exit('no C compiler found to build tools/qoa_encode.c (set CC, or QOA_ENCODE to a built binary)')
    os.makedirs(os.path.dirname(exe), exist_ok=True)
    subprocess.run([cc, '-O2', src, '-o', exe, '-lm'], check=True)
    return exe


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('inputs', nargs='+')
    ap.add_argument('--out', required=True)
    ap.add_argument('--rate', type=int, default=0, help='resample to this rate (default: keep)')
    ap.add_argument('--mono', action='store_true', help='mix down to one channel')
    ap.add_argument('--force', action='store_true', help='convert even if the output is newer')
    ap.add_argument('--min-snr', type=float, default=30.0, help='below this QOA quality (dB), keep a .wav instead (0 = always QOA)')
    a = ap.parse_args()
    if not shutil.which('ffmpeg'):
        sys.exit('ffmpeg not found on PATH (needed to decode MP3/OGG; https://ffmpeg.org)')
    enc = encoder()
    os.makedirs(a.out, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        for src in a.inputs:
            base = os.path.join(a.out, os.path.splitext(os.path.basename(src))[0])
            done = [p for p in (base + '.qoa', base + '.wav') if os.path.exists(p)]
            if not a.force and done and os.path.getmtime(done[0]) >= os.path.getmtime(src):
                print(f'{done[0]}: up to date'); continue
            wav = os.path.join(tmp, 'x.wav')
            cmd = ['ffmpeg', '-v', 'error', '-y', '-i', src, '-c:a', 'pcm_s16le']
            if a.rate: cmd += ['-ar', str(a.rate)]
            if a.mono: cmd += ['-ac', '1']
            subprocess.run(cmd + [wav], check=True)
            for p in done:                        # the old output, whichever format it was
                os.remove(p)
            out = subprocess.run([enc, wav, base + '.qoa'], check=True, capture_output=True, text=True).stdout.strip()
            m = re.search(r'snr ([0-9.]+)', out)
            if m and float(m.group(1)) < a.min_snr:
                os.remove(base + '.qoa'); shutil.copyfile(wav, base + '.wav')
                print(f'{base}.wav: kept as WAV (QOA would be {m.group(1)} dB)')
            else:
                print(out)


if __name__ == '__main__':
    main()
