"""join.py clip1 [clip2 ...] -> video_out/sound_sense_demo.mp4: title, the clips (with sound), the UI montage body, end card."""
import os, subprocess, sys
sys.path.insert(0, os.environ['S'] + '/pylib'); import imageio_ffmpeg
ff = imageio_ffmpeg.get_ffmpeg_exe(); F = os.environ['S'] + '/frames/'; R = '/Users/parthbansal/alif/e8-hackathon'
T = os.environ['S'] + '/parts/'; os.makedirs(T, exist_ok=True)
V = 'scale=1920:1080:force_original_aspect_ratio=decrease,pad=1920:1080:(ow-iw)/2:(oh-ih)/2:color=0x0E1220,fps=30,format=yuv420p,setsar=1'
def still(png, dur, out):
    subprocess.run([ff, '-y', '-loglevel', 'error', '-loop', '1', '-t', str(dur), '-i', png, '-f', 'lavfi', '-t', str(dur), '-i', 'anullsrc=r=48000:cl=stereo',
                    '-vf', V, '-c:v', 'libx264', '-crf', '18', '-c:a', 'aac', '-shortest', out], check=True)
def clip(src, out):
    has_a = subprocess.run([ff, '-i', src], capture_output=True, text=True).stderr.count('Audio:') > 0
    cmd = [ff, '-y', '-loglevel', 'error', '-i', src] + ([] if has_a else ['-f', 'lavfi', '-i', 'anullsrc=r=48000:cl=stereo'])
    cmd += ['-vf', V, '-c:v', 'libx264', '-crf', '20', '-c:a', 'aac', '-ar', '48000', '-ac', '2'] + ([] if has_a else ['-shortest']) + [out]
    subprocess.run(cmd, check=True)
parts = []
still(F + '00.png', 3.0, T + 'a_title.mp4'); parts.append(T + 'a_title.mp4')
for i, c in enumerate(sys.argv[1:]):
    o = T + f'b_clip{i}.mp4'; clip(c, o); parts.append(o)
frames = sorted(p for p in os.listdir(F) if p.endswith('.png'))[1:]
for i, p in enumerate(frames):
    o = T + f'c_{i:02d}.mp4'; still(F + p, 4.0 if i == len(frames) - 1 else 3.2, o); parts.append(o)
with open(T + 'list.txt', 'w') as f:
    for p in parts: f.write(f"file '{p}'\n")
out = R + '/video_out/sound_sense_demo.mp4'
subprocess.run([ff, '-y', '-loglevel', 'error', '-f', 'concat', '-safe', '0', '-i', T + 'list.txt', '-c', 'copy', out], check=True)
print(out, round(os.path.getsize(out) / 1e6, 1), 'MB')
