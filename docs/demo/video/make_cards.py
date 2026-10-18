#!/usr/bin/env python3
"""Title card, end card and on-screen text overlays (1920x1080) for the demo video.

    python3 docs/demo/video/make_cards.py [path/to/dejavu/fonts]

Writes title_card.png and end_card.png, plus one transparent overlay_<segment>.png per
"On screen:" line in docs/VIDEO-SCRIPT.md, to lay over the footage as a lower third. Same palette
and font as the gallery (docs/demo/gallery/make_gallery.py). Numbers come from docs/GATES-results.md.
"""
import glob
import os
import re
import sys

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HERE, "..", "..", "VIDEO-SCRIPT.md")
W, H = 1920, 1080
BG = (12, 15, 21)
CARD = (26, 31, 41)
LINE = (52, 60, 76)
INK = (236, 240, 245)
MUTED = (150, 160, 178)
BLUE = (0, 114, 178)
SKY = (86, 180, 233)
GREEN = (0, 158, 115)
YELLOW = (230, 159, 0)
REPO = "github.com/Parth-Bansal30/e8-hackathon"


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


def fit(d, text, size, maxw, bold=False, minsize=14):
    while size > minsize and d.textlength(text, font=F(size, bold)) > maxw:
        size -= 1
    return F(size, bold)


def save(im, name):
    im.save(os.path.join(HERE, name), optimize=True)
    print("wrote", name)


def title_card():
    im = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(im)
    d.rectangle((160, 300, 300, 312), fill=GREEN)
    d.text((156, 340), "Sound Sense", font=F(120, True), fill=INK)
    d.text((160, 500), "Home sound alerts for deaf and hard-of-hearing people", font=F(44), fill=INK)
    d.text((160, 600), "Running entirely on the Alif Ensemble E8 · Cortex-M55 + Ethos-U85", font=F(32, True), fill=SKY)
    d.text((160, 650), "PyTorch · ExecuTorch · Vela", font=F(32, True), fill=SKY)
    d.text((160, 900), "Team HashMap · Arm + ExecuTorch Edge AI hackathon", font=F(30), fill=MUTED)
    save(im, "title_card.png")


def end_card():
    im = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(im)
    d.text((W // 2, 170), "Sound Sense", font=F(96, True), fill=INK, anchor="mm")
    d.text((W // 2, 260), "Learns your home, feels your door, keeps your sounds to itself.", font=F(36), fill=MUTED,
           anchor="mm")
    tiles = [("1.00 ms", "NPU per embedding", BLUE), ("14 of 14", "operators on the Ethos-U85", BLUE),
             ("12.6%", "CPU active, quiet room", GREEN), ("437 ms", "clear match to alert (median)", GREEN),
             ("21 of 22", "knocks felt at 20 cm", YELLOW)]
    tw, th, gx = 320, 230, 26
    x = (W - (len(tiles) * tw + (len(tiles) - 1) * gx)) // 2
    for big, small, col in tiles:
        d.rounded_rectangle((x, 360, x + tw, 360 + th), radius=18, fill=CARD, outline=LINE, width=2)
        d.rectangle((x + 22, 386, x + 82, 393), fill=col)
        d.text((x + 22, 490), big, font=fit(d, big, 64, tw - 44, True), fill=INK, anchor="ls")
        d.text((x + 22, 530), small, font=fit(d, small, 24, tw - 44), fill=MUTED)
        x += tw + gx
    d.text((W // 2, 720), REPO, font=F(44, True), fill=SKY, anchor="mm")
    d.text((W // 2, 785), "Judged version: tag v1.0-judged (commit 32177ea)", font=F(30), fill=INK, anchor="mm")
    d.text((W // 2, 960), "Team HashMap · measured on the DK-E8, release build, 2026-10-18", font=F(26), fill=MUTED,
           anchor="mm")
    save(im, "end_card.png")


def overlays():
    md = open(SCRIPT, encoding="utf-8").read()
    full = md[:md.index("## 3:00 cut")]
    for m in re.finditer(r"^### (\d+)\. ([^(]*?) \(.*?\n(.*?)(?=^### |\Z)", full, re.S | re.M):
        on = re.search(r'\*\*On screen:\*\* "(.*?)"', m.group(3), re.S)
        if not on:
            continue
        text = " ".join(on.group(1).split())
        im = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        d = ImageDraw.Draw(im)
        f = fit(d, text, 46, W - 360, True)
        tw = d.textlength(text, font=f)
        x0, y0 = 120, H - 210
        d.rounded_rectangle((x0, y0, x0 + tw + 100, y0 + 104), radius=18, fill=BG + (225,))
        d.rectangle((x0, y0 + 18, x0 + 10, y0 + 86), fill=GREEN + (255,))
        d.text((x0 + 50, y0 + 52), text, font=f, fill=INK + (255,), anchor="lm")
        slug = re.sub(r"[^a-z0-9]+", "_", m.group(2).lower()).strip("_")
        save(im, f"overlay_{int(m.group(1)):02d}_{slug}.png")


if __name__ == "__main__":
    title_card()
    end_card()
    overlays()
