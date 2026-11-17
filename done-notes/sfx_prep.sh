#!/usr/bin/env sh
# Sound effects: art_src/sfx_raw/<name>.mp3 (Ludo createSoundEffect; prompts in art_src/sfx.json)
# -> assets/sfx/<name>.mp3: silence trimmed, levelled to about -20 dB mean, peaks limited, 5 ms fades.
cd "$(dirname "$0")/.." && mkdir -p assets/sfx
for f in art_src/sfx_raw/*.mp3; do
  case "$(basename "$f")" in wind.mp3)   # a seamless loop: level it only (no trimming or fades, which would break the seam)
    ffmpeg -v error -y -i "$f" -af "loudnorm=I=-22:TP=-3,aresample=44100" -ac 1 -b:a 128k "assets/sfx/wind.mp3"; continue;; esac
  mean=$(ffmpeg -hide_banner -i "$f" -af volumedetect -f null - 2>&1 | sed -n 's/.*mean_volume: \([-0-9.]*\) dB.*/\1/p')
  gain=$(python3 -c "print(round(-20 - ($mean), 2))")
  ffmpeg -v error -y -i "$f" -af "silenceremove=start_periods=1:start_threshold=-55dB,areverse,silenceremove=start_periods=1:start_threshold=-55dB,areverse,volume=${gain}dB,alimiter=limit=0.7:level=false,afade=t=in:d=0.005,areverse,afade=t=in:d=0.02,areverse,aresample=44100" \
    -ac 1 -b:a 128k "assets/sfx/$(basename "$f")"
done
