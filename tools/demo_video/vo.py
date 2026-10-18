import os, subprocess, sys, wave
import numpy as np
sys.path.insert(0, os.environ['S'] + '/pylib'); import imageio_ffmpeg
ff = imageio_ffmpeg.get_ffmpeg_exe(); S = os.environ['S']; F = S + '/frames/'; A = S + '/vo/'; os.makedirs(A, exist_ok=True)
R = '/Users/parthbansal/alif/e8-hackathon'; SR = 48000
LINES = [
 "Sound Sense. A small box for deaf and hard-of-hearing people, that hears the sounds that matter at home, and runs entirely on the Alif E8.",
 "Every start runs a self-test: microphones, the neural processor, saved sounds, the vibration sensor and the lights. Ready in about one second.",
 "The home screen shows what it hears, live: a spectrogram, the room level, and the three best matches.",
 "In a quiet room, the N P U rests. The C P U is awake just twelve percent of the time. Nothing is ever recorded.",
 "When a sound you care about happens, you get a full-screen alert, with an icon, a word, the time, and flashing lights.",
 "A knock is felt through the table, and heard. A microphone alone can't tell your door from a knock on T V.",
 "Urgent sounds, like breaking glass, strobe once a second, and stay until you press.",
 "Ignore it for thirty seconds, and it escalates.",
 "When it isn't sure, it says so. Maybe.",
 "If a sound repeats, it shows as still sounding, instead of a new alert every time.",
 "Every home sounds different, so you teach it your own sounds, right on the device, with eight examples. Each one is checked.",
 "It tests the new sound before saving. No laptop, no retraining. Saved to flash in a quarter of a second.",
 "A loud sound it doesn't know? It asks: what was that? One press, and you can teach it.",
 "It even notices when someone is talking nearby.",
 "Today's timeline lists every sound, with repeats merged and felt knocks marked.",
 "In settings, turn sounds on or off, set how urgent they are, and rename them.",
 "Press down on any alert to snooze that sound for ten minutes.",
 "Night mode keeps the screen dark, while important alerts still wake it.",
 "Under the hood: our own PyTorch model, quantized to int eight, with all fourteen operators on the Ethos U eighty-five. About one millisecond per run.",
 "Sound Sense learns your home, feels your door, and keeps your sounds to itself.",
]
frames = sorted(p for p in os.listdir(F) if p.endswith('.png') and p[0].isdigit())
assert len(frames) == len(LINES), (len(frames), len(LINES))
vo, durs = [], []
for i, t in enumerate(LINES):
    a = A + f'{i:02d}.aiff'; w = A + f'{i:02d}.wav'
    subprocess.run(['say', '-v', 'Samantha', '-r', '178', '-o', a, t], check=True)
    subprocess.run([ff, '-y', '-loglevel', 'error', '-i', a, '-ar', str(SR), '-ac', '1', w], check=True)
    with wave.open(w) as f: x = np.frombuffer(f.readframes(f.getnframes()), np.int16).astype(np.float32) / 32768
    vo.append(x); durs.append(max(3.2, len(x) / SR + 0.9))
total = sum(durs); n = int(total * SR)
voice = np.zeros(n, np.float32); t0 = 0.0
for x, d in zip(vo, durs):
    s = int((t0 + 0.35) * SR); voice[s:s + len(x)] += x[: n - s]; t0 += d
# soft ambient pad: C - Am - F - G, 4 s per chord, sine partials, slow fades
tt = np.arange(n) / SR; music = np.zeros(n, np.float32)
chords = [[261.6, 329.6, 392.0], [220.0, 261.6, 329.6], [174.6, 220.0, 261.6], [196.0, 246.9, 293.7]]
seg = 4.0
for k in range(int(total / seg) + 1):
    a, b = int(k * seg * SR), min(n, int((k + 1) * seg * SR + 1.0 * SR))
    if a >= n: break
    tl = tt[a:b] - k * seg; env = np.minimum(1, tl / 1.2) * np.minimum(1, np.maximum(0, (seg + 1.0 - tl) / 1.2))
    for fq in chords[k % 4]:
        music[a:b] += env * (np.sin(2 * np.pi * fq / 2 * tt[a:b]) + 0.3 * np.sin(2 * np.pi * fq * tt[a:b])) / 6
music *= np.minimum(1, tt / 2.0) * np.minimum(1, (total - tt) / 3.0)
mix = voice * 0.9 + music * 0.10 / max(1e-6, np.abs(music).max())
mix /= max(1.0, np.abs(mix).max() / 0.95)
with wave.open(S + '/mix.wav', 'w') as f:
    f.setnchannels(1); f.setsampwidth(2); f.setframerate(SR); f.writeframes((mix * 32767).astype(np.int16).tobytes())
with open(F + 'list_vo.txt', 'w') as f:
    for p, d in zip(frames, durs): f.write(f"file '{F + p}'\nduration {d:.3f}\n")
    f.write(f"file '{F + frames[-1]}'\n")
out = R + '/video_out/ui_montage_voiceover.mp4'
subprocess.run([ff, '-y', '-loglevel', 'error', '-f', 'concat', '-safe', '0', '-i', F + 'list_vo.txt', '-i', S + '/mix.wav',
                '-vf', 'fps=30,format=yuv420p', '-c:v', 'libx264', '-crf', '18', '-c:a', 'aac', '-b:a', '192k', '-shortest', out], check=True)
print(out, round(os.path.getsize(out) / 1e6, 1), 'MB,', round(total, 1), 's')
