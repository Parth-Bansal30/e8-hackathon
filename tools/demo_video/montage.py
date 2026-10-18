import os, subprocess, sys
from PIL import Image, ImageDraw, ImageFont
sys.path.insert(0, os.environ['S'] + '/pylib'); import imageio_ffmpeg
R = '/Users/parthbansal/alif/e8-hackathon'; SC = R + '/docs/demo/screens/'; F = os.environ['S'] + '/frames/'
ff = imageio_ffmpeg.get_ffmpeg_exe()
def font(sz, bold=True):
    for p in ['/System/Library/Fonts/Supplemental/Arial Bold.ttf' if bold else '/System/Library/Fonts/Supplemental/Arial.ttf', '/System/Library/Fonts/Helvetica.ttc']:
        if os.path.exists(p): return ImageFont.truetype(p, sz)
    return ImageFont.load_default()
SHOTS = [
 ("14_ready", "Ready in 1.3 s", "Every start self-tests the mics, the NPU, saved sounds, the vibration sensor and the lights"),
 ("01_home", "Live listening", "Spectrogram, room level against its noise floor, and the 3 best matches, 10 times a second"),
 ("16_home_npu_resting", "Quiet room: NPU rests", "The CPU is awake 12.6% of the time (was 100%). Nothing is recorded"),
 ("02_alert_doorbell", "A sound you taught", "Full-screen alert with icon, word and time; the LEDs flash their own pattern"),
 ("03_alert_knock_felt", "Felt here + heard", "The accelerometer feels the knock through the table: a mic alone can't tell a door from a TV"),
 ("04_alert_urgent_a", "Urgent: glass breaking", "Strobes once a second (photosensitivity-safe) and stays until you press"),
 ("17_alert_escalated_a", "Ignored? It escalates", "No response for 30 s: the strobe doubles"),
 ("06_alert_maybe", "Honest when unsure", "A weak match says \"Maybe\", so you can confirm or correct it"),
 ("27_alert_still_sounding", "Still sounding", "Repeats within 15 s are merged into one alert"),
 ("09_teach", "Teach any sound, on the device", "8 examples, each checked: too quiet, clipped, loud room, unlike the others"),
 ("11_teach_done", "Tested before saving", "A profile from the NPU's embeddings, saved to flash in about 250 ms; no retraining"),
 ("24_what_was_that", "\"What was that?\"", "An unknown loud sound gets a banner; UP teaches it right away"),
 ("29_home_talking", "Someone is talking nearby", "Speech is noticed, but nothing is recorded or sent anywhere"),
 ("08_timeline", "Today's timeline", "Every sound with its time; repeats merged, felt knocks marked"),
 ("26_menu_sounds", "Your sounds, your rules", "Turn sounds on or off, set urgency, rename: all on the board"),
 ("30_snoozed", "Snooze", "DOWN on an alert snoozes that sound for 10 minutes; it's still logged"),
 ("22_home_night", "Night mode", "The screen goes dark; important alerts still wake it"),
 ("10_stats", "Under the hood", "Our PyTorch model, int8 via ExecuTorch: all 14 operators on the Ethos-U85, about 1 ms per run"),
]
W, H = 1920, 1080; BG = (14, 18, 32)
lst = []
def add(img, dur):
    p = F + f"{len(lst):02d}.png"; img.save(p); lst.append((p, dur))
def card(path):
    im = Image.open(path).convert('RGB')
    if im.size != (W, H): im = im.resize((W, H))
    return im
add(card(R + '/docs/demo/video/title_card.png'), 3.0)
t1, t2 = font(72), font(40, False)
for name, head, sub in SHOTS:
    im = Image.new('RGB', (W, H), BG); d = ImageDraw.Draw(im)
    s = Image.open(SC + name + '.png').convert('RGB'); s = s.resize((int(s.width * 960 / s.height), 960), Image.LANCZOS)
    im.paste(s, (140, 60)); d.rectangle([136, 56, 140 + s.width + 3, 60 + 960 + 3], outline=(70, 80, 110), width=4)
    x = 140 + s.width + 120; maxw = W - x - 100
    d.text((x, 360), head, font=t1, fill=(255, 255, 255))
    words, line, y = sub.split(), "", 480
    for w in words:
        if d.textlength(line + " " + w, font=t2) > maxw and line:
            d.text((x, y), line, font=t2, fill=(190, 200, 225)); y += 56; line = w
        else: line = (line + " " + w).strip()
    d.text((x, y), line, font=t2, fill=(190, 200, 225))
    d.text((x, H - 110), "Sound Sense · Alif Ensemble E8 · Cortex-M55 + Ethos-U85", font=font(30, False), fill=(110, 120, 150))
    add(im, 3.2)
add(card(R + '/docs/demo/video/end_card.png'), 4.0)
with open(F + 'list.txt', 'w') as f:
    for p, dur in lst: f.write(f"file '{p}'\nduration {dur}\n")
    f.write(f"file '{lst[-1][0]}'\n")
out = R + '/video_out/ui_montage.mp4'
subprocess.run([ff, '-y', '-loglevel', 'error', '-f', 'concat', '-safe', '0', '-i', F + 'list.txt', '-vf', 'fps=30,format=yuv420p', '-c:v', 'libx264', '-crf', '18', out], check=True)
print(out, os.path.getsize(out), 'bytes,', round(sum(d for _, d in lst), 1), 's')
