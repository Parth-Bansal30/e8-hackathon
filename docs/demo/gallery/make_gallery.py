#!/usr/bin/env python3
"""Build the Devpost gallery images (1500x1000, 3:2) in docs/demo/gallery/.

    python3 docs/demo/gallery/make_gallery.py [path/to/dejavu/fonts]

Inputs: the UI simulator renders in docs/demo/screens/ (make -C tools/ui_sim) and the numbers
below, copied from docs/GATES-results.md and the README. Needs Pillow and the DejaVu Sans TTFs
(the UI's font); pass their folder if they aren't found.
"""
import glob
import os
import sys

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
SCREENS = os.path.join(HERE, "..", "screens")
W, H = 1500, 1000

BG = (12, 15, 21)
CARD = (26, 31, 41)
LINE = (52, 60, 76)
INK = (236, 240, 245)
MUTED = (150, 160, 178)
BLUE = (0, 114, 178)
SKY = (86, 180, 233)
GREEN = (0, 158, 115)
ORANGE = (213, 94, 0)
YELLOW = (230, 159, 0)

RENDER_NOTE = "Screens rendered by the UI simulator from the firmware's UI code; values on them are samples."


def font_dir():
    if len(sys.argv) > 1:
        return sys.argv[1]
    pats = ["/Library/Fonts/DejaVuSans.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            os.path.expanduser("~/Library/Fonts/DejaVuSans.ttf"),
            os.path.expanduser("~/**/mpl-data/fonts/ttf/DejaVuSans.ttf")]
    for p in pats:
        hits = glob.glob(p, recursive=True)
        if hits:
            return os.path.dirname(hits[0])
    sys.exit("DejaVuSans.ttf not found; pass its folder as the first argument")


FD = font_dir()


def F(size, bold=False):
    return ImageFont.truetype(os.path.join(FD, "DejaVuSans-Bold.ttf" if bold else "DejaVuSans.ttf"), size)


def fit(d, text, size, maxw, bold=False, minsize=12):
    """The largest font no bigger than size that fits text in maxw pixels."""
    while size > minsize and d.textlength(text, font=F(size, bold)) > maxw:
        size -= 1
    return F(size, bold)


def canvas():
    im = Image.new("RGB", (W, H), BG)
    return im, ImageDraw.Draw(im)


def header(d, title, sub):
    d.text((48, 36), title, font=F(46, True), fill=INK)
    d.text((50, 98), sub, font=F(24), fill=MUTED)


def footer(d, text, y=H - 40):
    d.text((W // 2, y), text, font=F(18), fill=MUTED, anchor="mm")


def screen(name, h):
    im = Image.open(os.path.join(SCREENS, name)).convert("RGB")
    w = round(im.width * h / im.height)
    im = im.resize((w, h), Image.LANCZOS)
    mask = Image.new("L", im.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, w - 1, h - 1], radius=18, fill=255)
    return im, mask


def screens_row(im, d, items, top=150, h=720):
    """items: [(file, label)]; lays the screens out evenly with a label under each."""
    shots = [screen(f, h) for f, _ in items]
    gap = 40
    total = sum(s.width for s, _ in shots) + gap * (len(shots) - 1)
    x = (W - total) // 2
    for (s, m), (_, label) in zip(shots, items):
        d.rounded_rectangle([x - 2, top - 2, x + s.width + 1, top + h + 1], radius=20, outline=LINE, width=2)
        im.paste(s, (x, top), m)
        d.text((x + s.width // 2, top + h + 28), label, font=F(22, True), fill=INK, anchor="mm")
        x += s.width + gap


def save(im, name):
    im.save(os.path.join(HERE, name), optimize=True)
    print("wrote", name)


def gallery_screens(name, title, sub, items, note=RENDER_NOTE):
    im, d = canvas()
    header(d, title, sub)
    screens_row(im, d, items)
    footer(d, note)
    save(im, name)


def box(d, xy, title, lines=(), fill=CARD, outline=LINE, tsize=24, lsize=19, title_fill=INK):
    x0, y0, x1, y1 = xy
    d.rounded_rectangle(xy, radius=14, fill=fill, outline=outline, width=2)
    d.text((x0 + 16, y0 + 12), title, font=fit(d, title, tsize, x1 - x0 - 32, True), fill=title_fill)
    y = y0 + 16 + tsize + 8
    for ln in lines:
        d.text((x0 + 16, y), ln, font=fit(d, ln, lsize, x1 - x0 - 32), fill=MUTED if fill == CARD else INK)
        y += lsize + 8


def arrow(d, a, b, fill=SKY, width=4):
    import math
    d.line([a, b], fill=fill, width=width)
    ang = math.atan2(b[1] - a[1], b[0] - a[0])
    L = 14
    p1 = (b[0] - L * math.cos(ang - 0.45), b[1] - L * math.sin(ang - 0.45))
    p2 = (b[0] - L * math.cos(ang + 0.45), b[1] - L * math.sin(ang + 0.45))
    d.polygon([b, p1, p2], fill=fill)


def architecture():
    im, d = canvas()
    header(d, "How Sound Sense works", "Everything runs on the Alif Ensemble E8 DevKit. Audio is never stored and never leaves the board.")

    # sensors
    box(d, (40, 190, 300, 300), "2 PDM mics", ["16 kHz, both checked", "at every boot"])
    box(d, (40, 470, 300, 580), "BMI323 IMU", ["800 Hz FIFO over I3C", "805 samples/s, 0 missed"])

    # M55
    d.rounded_rectangle((350, 160, 920, 760), radius=18, outline=BLUE, width=3)
    d.text((370, 172), "Cortex-M55 (HP, 400 MHz)", font=F(26, True), fill=SKY)
    box(d, (370, 220, 900, 310), "Log-mel features + onset detector", ["40 bands every 20 ms: 0.30 ms per update"])
    box(d, (370, 330, 900, 440), "Cosine match against profiles", ["9 built-in + up to 8 taught; own threshold each",
                                                                  "\"Maybe: x?\" when close to it"])
    box(d, (370, 460, 900, 550), "Felt vs heard", ["Knock felt by the IMU within ±100 ms of the sound?"])
    box(d, (370, 570, 625, 740), "Teach", ["5 examples, checked", "-> new profile", "in milliseconds"])
    box(d, (645, 570, 900, 740), "UI", ["Alerts, timeline,", "live view, stats,", "Ready self-test"])

    # U85
    d.rounded_rectangle((1030, 160, 1460, 470), radius=18, fill=(10, 40, 62), outline=SKY, width=3)
    d.text((1050, 172), "Ethos-U85-256 NPU", font=F(26, True), fill=SKY)
    y = 216
    for ln, b in [("DS-CNN sound encoder, int8", True), ("49x40 log-mel -> 64-d embedding", False),
                  ("219k params, 43.2 M MACs", False), ("All 14 operators on the NPU", True),
                  ("1.00 ms per embedding", True), ("Run by ExecuTorch on the M55;", False),
                  ("idle when the room is quiet", False)]:
        d.text((1050, y), ln, font=fit(d, ln, 21, 390, b), fill=INK if b else MUTED)
        y += 34

    # outputs + memory
    box(d, (1030, 500, 1460, 600), "Outputs", ["480x800 MIPI DSI LCD, 2 RGB LEDs", "LED pattern per sound, 1 Hz urgent strobe"])
    box(d, (1030, 620, 1460, 760), "Memory", ["MRAM: model (.pte 240 KB), fonts, cold code", "ITCM 97.6%: all hot code",
                                              "OSPI flash: taught sounds, A/B + CRC32"])

    # arrows
    arrow(d, (300, 245), (368, 265))
    arrow(d, (300, 525), (368, 505))
    arrow(d, (900, 265), (1028, 265))          # patch -> NPU
    arrow(d, (1028, 385), (902, 385))          # embedding -> match
    d.text((965, 250), "int8 patch", font=F(16), fill=MUTED, anchor="mb")
    d.text((965, 370), "64-d embedding", font=F(15), fill=MUTED, anchor="mb")
    arrow(d, (900, 655), (1028, 560))          # UI -> outputs

    # ExecuTorch path + power
    d.rounded_rectangle((40, 790, 1460, 900), radius=14, fill=CARD, outline=LINE, width=2)
    d.text((60, 802), "Model path", font=F(22, True), fill=INK)
    d.text((60, 838), "PyTorch -> PT2E int8 (EthosUQuantizer) -> EthosUPartitioner -> Vela (ethos-u85-256) -> .pte in MRAM -> ExecuTorch on the M55 -> U85",
           font=F(19), fill=MUTED)
    d.text((60, 868), "Power: the M55 sleeps (WFI) between audio blocks; the live encoder runs only above the room's floor + 6 dB.",
           font=F(19), fill=MUTED)
    footer(d, "Numbers measured on the board, release build, 2026-10-18 (docs/GATES-results.md).", y=H - 50)
    save(im, "02_architecture.png")


def numbers():
    im, d = canvas()
    header(d, "Measured on the board", "DK-E8, M55-HP at 400 MHz, Ethos-U85-256, release build, 2026-10-18")
    tiles = [
        ("1.00 ms", "NPU per embedding", "1.03 ms with the runner", BLUE),
        ("14 of 14", "operators on the NPU", "0 on the CPU", BLUE),
        ("12.6%", "CPU active, quiet room", "was 100%; NPU idle", GREEN),
        ("0 of 1960", "feature values differ", "board vs Python", GREEN),
        ("75.8%", "5-way 5-shot, int8", "10 classes never trained on", YELLOW),
        ("21 of 22", "knocks felt at 20 cm", "IMU at 805 samples/s", YELLOW),
        ("0 dropped", "audio blocks in 1,101", "flash saves while listening", GREEN),
        ("2.2 ms", "restore at boot", "taught sounds, OSPI flash", BLUE),
        ("1.5 s", "reset to Ready", "self-test included", BLUE),
        ("~0.85 s", "sound start to alert", "target was 0.5 s", ORANGE),
    ]
    cols, tw, th, gx, gy = 5, 268, 270, 18, 26
    x0 = (W - (cols * tw + (cols - 1) * gx)) // 2
    for i, (big, l1, l2, col) in enumerate(tiles):
        x = x0 + (i % cols) * (tw + gx)
        y = 195 + (i // cols) * (th + gy)
        d.rounded_rectangle((x, y, x + tw, y + th), radius=16, fill=CARD, outline=LINE, width=2)
        d.rectangle((x + 18, y + 22, x + 70, y + 28), fill=col)
        d.text((x + 18, y + 110), big, font=fit(d, big, 50, tw - 36, True), fill=INK, anchor="ls")
        d.text((x + 18, y + 160), l1, font=fit(d, l1, 21, tw - 36, True), fill=INK)
        d.text((x + 18, y + 196), l2, font=fit(d, l2, 19, tw - 36), fill=MUTED)
    d.text((W // 2, 835), "Limits: built-ins 54% live from a laptop speaker (88% offline); taught sounds 38% accepted offline at 5 examples.",
           font=F(19), fill=MUTED, anchor="mm")
    footer(d, "Method and logs: docs/GATES-results.md in the repo.", y=H - 60)
    save(im, "03_numbers.png")


def felt_vs_heard():
    im, d = canvas()
    header(d, "Felt vs heard", "The microphones say what the sound was. The accelerometer says whether it touched this table.")
    s, m = screen("03_alert_knock_felt.png", 720)
    x = 90
    d.rounded_rectangle([x - 2, 148, x + s.width + 1, 148 + 721], radius=20, outline=LINE, width=2)
    im.paste(s, (x, 150), m)
    bx = x + s.width + 70
    rows = [
        ("Knock on the table", "Felt here + heard", "Full-screen alert", BLUE),
        ("Knock sound on the TV", "Heard only (TV?)", "Banner only, no full alert", YELLOW),
        ("Table felt it, encoder unsure", "Maybe: Knock?", "Certain only when both agree", MUTED),
    ]
    y = 170
    for what, tag, result, col in rows:
        d.rounded_rectangle((bx, y, W - 70, y + 170), radius=16, fill=CARD, outline=LINE, width=2)
        d.rectangle((bx, y + 20, bx + 8, y + 150), fill=col)
        d.text((bx + 32, y + 22), what, font=F(28, True), fill=INK)
        d.text((bx + 32, y + 72), "Shows: " + tag, font=F(23), fill=INK)
        d.text((bx + 32, y + 112), result, font=F(21), fill=MUTED)
        y += 196
    d.text((bx, y + 16), "BMI323 read at 800 Hz through its FIFO.", font=F(21), fill=MUTED)
    d.text((bx, y + 50), "Knocks felt at 20 cm: 21 of 22. Place it on the table by the door.", font=F(21), fill=MUTED)
    footer(d, "Left: UI simulator render (the board's card reads \"Knock\"); its numbers are samples.")
    save(im, "05_felt_vs_heard.png")


def cover():
    im, d = canvas()
    d.text((70, 120), "Sound Sense", font=F(76, True), fill=INK)
    lines = ["Home sound alerts for deaf and", "hard-of-hearing people."]
    y = 230
    for ln in lines:
        d.text((72, y), ln, font=F(34), fill=INK)
        y += 48
    pts = ["Learns your own sounds on the device", "Feels a knock on its table (IMU + mics)",
           "Encoder on the Ethos-U85: 1.00 ms", "Nothing recorded, nothing sent"]
    y = 380
    for p in pts:
        d.ellipse((76, y + 10, 92, y + 26), fill=GREEN)
        d.text((108, y), p, font=F(26), fill=INK)
        y += 52
    d.text((72, 640), "Alif Ensemble E8 · Cortex-M55 + Ethos-U85", font=F(22, True), fill=SKY)
    d.text((72, 674), "PyTorch · ExecuTorch · Vela", font=F(22, True), fill=SKY)
    items = ["03_alert_knock_felt.png", "11_teach_done.png"]
    h = 640
    x = W - 40 - 2 * round(480 * h / 800) - 30
    for f in items:
        s, m = screen(f, h)
        d.rounded_rectangle([x - 2, 168, x + s.width + 1, 168 + h + 1], radius=20, outline=LINE, width=2)
        im.paste(s, (x, 170), m)
        x += s.width + 30
    footer(d, RENDER_NOTE)
    save(im, "01_cover.png")


def main():
    cover()
    architecture()
    numbers()
    gallery_screens("04_teach.png", "Teach it your own sounds",
                    "Five examples on the device, each checked; a test match; saved to flash. No laptop, no retraining.",
                    [("09_teach.png", "Each example checked"), ("11_teach_done.png", "Test match, then Save"),
                     ("02_alert_doorbell.png", "Your sound, recognised")])
    felt_vs_heard()
    gallery_screens("06_self_test.png", "Ready at every start",
                    "Mics, NPU against a stored reference, saved sounds, IMU and lights: ready 1.5 s after reset (measured).",
                    [("14_ready.png", "All checks pass"), ("15_not_ready.png", "A failure stays on screen"),
                     ("16_home_npu_resting.png", "Quiet room: NPU resting")])
    gallery_screens("07_alerts_timeline.png", "Honest alerts that learn from mistakes",
                    "Unsure shows as \"Maybe\". LEFT marks a wrong alert, and that sound becomes a counter-example.",
                    [("06_alert_maybe.png", "Low confidence: \"Maybe\""), ("02_alert_doorbell.png", "LEFT: wrong sound"),
                     ("23_timeline_marked.png", "Marked on today's timeline")])
    gallery_screens("08_more.png", "More on the board, no app",
                    "Unknown loud sounds offer to be taught. A settings menu for night mode, mics off and forgetting sounds.",
                    [("24_what_was_that.png", "\"What was that?\" UP teaches it"), ("19_menu.png", "Settings (DOWN)"),
                     ("22_home_night.png", "Night mode: dark after 15 s")])


if __name__ == "__main__":
    main()
