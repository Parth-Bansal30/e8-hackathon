# Demo video from the UI screens

These scripts build the 1080p demo videos from `docs/demo/screens/` (simulator renders of the real UI) and the
title and end cards in `docs/demo/video/`. Their output goes to `video_out/`, which is kept out of git.

- `montage.py`: 18 screens with captions, plus the title and end cards. Output: `video_out/ui_montage.mp4`.
- `vo.py`: the same slides with a macOS `say` voiceover (voice Samantha) and a generated ambient pad, mixed
  low. Each slide stays up as long as its line. Output: `video_out/ui_montage_voiceover.mp4`.
- `join.py clip1 [clip2 ...]`: the title, then your footage with its sound, then the montage and the end
  card. Output: `video_out/sound_sense_demo.mp4`.

**Setup.** The scripts need a working folder `S`. `montage.py` writes its frames to `$S/frames/`, and the
other two scripts read them from there, so run `montage.py` first. `S` also holds a bundled ffmpeg:

```bash
export S=/tmp/sv && mkdir -p $S/frames && python3 -m pip install --target $S/pylib imageio-ffmpeg
```
