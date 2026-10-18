"""Neural voiceover (Piper) for the UI montage, conversational script, music ducked under the voice.
Usage: S=<workdir> PYTHONPATH=$S/piperlib python vo2.py [voice] ; voice: lessac (default) or ryan."""
import os, subprocess, sys, wave
import numpy as np
S = os.environ['S']; sys.path.insert(0, S + '/pylib'); import imageio_ffmpeg
from piper import PiperVoice, SynthesisConfig
ff = imageio_ffmpeg.get_ffmpeg_exe(); F = S + '/frames/'; R = '/Users/parthbansal/alif/e8-hackathon'; SR = 48000
name = sys.argv[1] if len(sys.argv) > 1 else 'lessac'
voice = PiperVoice.load(f"{S}/voices/en_US-{name}-high.onnx")
cfg = SynthesisConfig(length_scale=1.06, noise_scale=0.6, noise_w_scale=0.75)
LINES = [
 "Meet Sound Sense. It's a small device that listens for the sounds that matter at home, and shows them, for people who are deaf or hard of hearing. Everything runs right here, on the Alif E8 board.",
 "Every time it starts, it checks itself. The microphones, the neural processor, saved sounds, the vibration sensor, and the lights. It's ready in about a second.",
 "This is what it hears, live. A spectrogram, how loud the room is, and its three best guesses.",
 "When the room is quiet, the neural processor rests. The processor is awake only about twelve percent of the time. And nothing is ever recorded.",
 "When a sound you care about happens, like your doorbell, you can't miss it. A full-screen alert, a clear icon, the time, and flashing lights.",
 "Here's something a microphone alone can't do. A knock on your door is felt through the table, and heard. A knock on TV? Only heard. So it knows the difference.",
 "Urgent sounds, like breaking glass, flash once a second, and stay on screen until you respond.",
 "And if nobody responds, it gets more insistent.",
 "When it isn't sure, it tells you. Maybe a baby crying? You decide.",
 "If a sound keeps going, it says so, instead of alerting you again and again.",
 "Every home sounds different, so you can teach it your own sounds, right on the device. Just make the sound eight times, and it checks each one.",
 "It tests what it learned before saving. No laptop. No retraining. And it's stored in about a quarter of a second.",
 "Hear something it doesn't know? It asks, what was that? One press, and you can teach it.",
 "It even lets you know when someone's talking nearby.",
 "The timeline shows everything it heard today, with repeats grouped together.",
 "In settings, you choose which sounds matter, how urgent they are, and what they're called.",
 "Need a break from one sound? Snooze it for ten minutes.",
 "At night, the screen stays dark, but important alerts still wake it up.",
 "Under the hood, it's our own model, running entirely on the Ethos U eighty-five neural processor, in about one millisecond.",
 "Sound Sense. It learns your home, feels your door, and keeps your sounds to itself.",
]
frames = sorted(p for p in os.listdir(F) if p.endswith('.png') and p[0].isdigit())
assert len(frames) == len(LINES), (len(frames), len(LINES))
def speak(text):
    parts = []
    for ch in voice.synthesize(text, syn_config=cfg):
        parts += [ch.audio_float_array.astype(np.float32), np.zeros(int(0.28 * ch.sample_rate), np.float32)]
        sr = ch.sample_rate
    x = np.concatenate(parts[:-1])
    n = int(len(x) * SR / sr)                                  # resample to 48 kHz
    return np.interp(np.linspace(0, len(x) - 1, n), np.arange(len(x)), x).astype(np.float32)
vo, durs = [], []
for i, t in enumerate(LINES):
    x = speak(t); x /= max(1e-6, np.abs(x).max()) / 0.9
    vo.append(x); durs.append(max(3.2, len(x) / SR + 1.0))
total = sum(durs); n = int(total * SR); tt = np.arange(n) / SR
voice_tr = np.zeros(n, np.float32); t0 = 0.0
for x, d in zip(vo, durs):
    s = int((t0 + 0.4) * SR); voice_tr[s:s + len(x)] += x[: n - s]; t0 += d
music = np.zeros(n, np.float32); seg = 4.0
chords = [[261.6, 329.6, 392.0], [220.0, 261.6, 329.6], [174.6, 220.0, 261.6], [196.0, 246.9, 293.7]]
for k in range(int(total / seg) + 1):
    a, b = int(k * seg * SR), min(n, int((k + 1) * seg * SR + 1.0 * SR))
    if a >= n: break
    tl = tt[a:b] - k * seg; env = np.minimum(1, tl / 1.2) * np.minimum(1, np.maximum(0, (seg + 1.0 - tl) / 1.2))
    for fq in chords[k % 4]:
        music[a:b] += env * (np.sin(2 * np.pi * fq / 2 * tt[a:b]) + 0.3 * np.sin(2 * np.pi * fq * tt[a:b])) / 6
music *= np.minimum(1, tt / 2.0) * np.minimum(1, (total - tt) / 3.0); music /= max(1e-6, np.abs(music).max())
# duck the music under the voice: smoothed voice envelope -> gain 0.16 when silent, 0.07 when speaking
env = np.convolve(np.abs(voice_tr), np.ones(int(0.25 * SR)) / int(0.25 * SR), mode='same')
gain = 0.16 - 0.09 * np.minimum(1, env / 0.05)
mix = voice_tr * 0.92 + music * gain
mix /= max(1.0, np.abs(mix).max() / 0.95)
wav = f"{S}/mix_{name}.wav"
with wave.open(wav, 'w') as f:
    f.setnchannels(1); f.setsampwidth(2); f.setframerate(SR); f.writeframes((mix * 32767).astype(np.int16).tobytes())
with open(F + 'list_vo2.txt', 'w') as f:
    for p, d in zip(frames, durs): f.write(f"file '{F + p}'\nduration {d:.3f}\n")
    f.write(f"file '{F + frames[-1]}'\n")
out = f"{R}/video_out/sound_sense_ui_narrated_{name}.mp4"
subprocess.run([ff, '-y', '-loglevel', 'error', '-f', 'concat', '-safe', '0', '-i', F + 'list_vo2.txt', '-i', wav,
                '-vf', 'fps=30,format=yuv420p', '-c:v', 'libx264', '-crf', '18', '-c:a', 'aac', '-b:a', '192k', '-shortest', out], check=True)
print(out, round(os.path.getsize(out) / 1e6, 1), 'MB,', round(total, 1), 's')
