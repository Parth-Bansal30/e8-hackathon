# Demo video from the UI screens

These scripts build the 1080p demo videos from `docs/demo/screens/` (simulator renders of the real UI) and the
title and end cards in `docs/demo/video/`. Their output goes to `video_out/`, which is kept out of git.

- `montage.py`: 18 screens with captions, plus the title and end cards. Output: `video_out/ui_montage.mp4`.
- `vo.py`: the same slides with a macOS `say` voiceover (voice Samantha) and a generated ambient pad, mixed
  low. Each slide stays up as long as its line. Output: `video_out/ui_montage_voiceover.mp4`.
- `vo2.py [lessac|ryan]`: the same slides with a neural voice (Piper, `en_US-lessac-high` or
  `en_US-ryan-high`), a conversational script with short pauses, and the pad dipping under the voice.
  Output: `video_out/sound_sense_ui_narrated_<voice>.mp4` (about 2:30 with Lessac, 2:18 with Ryan).
- `join.py clip1 [clip2 ...]`: the title, then your footage with its sound, then the montage and the end
  card. Output: `video_out/sound_sense_demo.mp4`.

**Setup.** The scripts need a working folder `S`. `montage.py` writes its frames to `$S/frames/`, and the
other two scripts read them from there, so run `montage.py` first. `S` also holds a bundled ffmpeg:

```bash
export S=/tmp/sv && mkdir -p $S/frames && python3 -m pip install --target $S/pylib imageio-ffmpeg
```

For `vo2.py`, also `python3 -m pip install --target $S/piperlib piper-tts` and put the voice files
(`en_US-<voice>-high.onnx` and `.onnx.json`, from the rhasspy/piper-voices release) in `$S/voices/`. Run it
with `PYTHONPATH=$S/piperlib`.
