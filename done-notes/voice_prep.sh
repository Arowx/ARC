#!/usr/bin/env sh
# Narrator lines: art_src/voice_raw/<key>.mp3 (Ludo createSpeechPreset, voice "George"; see narration.json)
# -> assets/voice/<key>.mp3, levelled to the same loudness with leading/trailing silence trimmed.
cd "$(dirname "$0")/.." && mkdir -p assets/voice
for f in art_src/voice_raw/*.mp3; do
  ffmpeg -v error -y -i "$f" -af "silenceremove=start_periods=1:start_threshold=-50dB,areverse,silenceremove=start_periods=1:start_threshold=-50dB,areverse,loudnorm=I=-17:TP=-1.5:LRA=11,aresample=44100" \
    -ac 1 -b:a 96k "assets/voice/$(basename "$f")"
done
