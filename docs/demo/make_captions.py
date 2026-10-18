#!/usr/bin/env python3
"""Caption files (SRT) for the demo video, from the voice-over lines in docs/VIDEO-SCRIPT.md.

    python3 docs/demo/make_captions.py      # writes docs/demo/captions_4min.srt and captions_3min.srt

Each segment's voice-over is split into captions of at most two 42-character lines and spread over
the segment's time slot in proportion to its length. The slots come from the script, so after the
edit nudge the timings to the real footage (any SRT editor, or the video editor's caption track).
"""
import os
import re
import textwrap

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HERE, "..", "VIDEO-SCRIPT.md")
LINE, LINES = 42, 2


def secs(t):
    m, s = t.split(":")
    return int(m) * 60 + int(s)


def stamp(t):
    ms = int(round(t * 1000))
    return f"{ms // 3600000:02d}:{ms // 60000 % 60:02d}:{ms // 1000 % 60:02d},{ms % 1000:03d}"


def full_cut(md):
    """[(n, start, end, vo)] from the '### N. Title (m:ss-m:ss)' sections."""
    segs = []
    for m in re.finditer(r"^### (\d+)\. .*?\((\d+:\d\d)-(\d+:\d\d)\)\n(.*?)(?=^### |^## )", md, re.S | re.M):
        vo = re.search(r'\*\*VO:\*\* "(.*?)"\n', m.group(4), re.S)
        segs.append((int(m.group(1)), secs(m.group(2)), secs(m.group(3)),
                     " ".join(vo.group(1).split()) if vo else ""))
    return segs


def short_cut(md, full):
    """The 3:00 cut table: its own VO where quoted, the full cut's VO for 'Full VO', none for 'No VO'."""
    by_n = {n: vo for n, _, _, vo in full}
    table = md[md.index("## 3:00 cut"):]
    segs = []
    for m in re.finditer(r"^\| (\d+)\. [^|]*\| (\d+:\d\d)-(\d+:\d\d) \| (.*?) \|$", table, re.M):
        n, note = int(m.group(1)), m.group(4)
        quoted = re.search(r'VO: "(.*?)"', note)
        drop = re.search(r'VO: drop "(.*?)"', note)
        if drop:
            vo = by_n[n].replace(drop.group(1), "")
        elif quoted:
            vo = quoted.group(1)
        elif "No VO" in note:
            vo = ""
        else:
            vo = by_n[n]
        segs.append((n, secs(m.group(2)), secs(m.group(3)), " ".join(vo.split())))
    return segs


def split_even(words, k):
    """k runs of words with about equal character counts."""
    total = sum(len(w) + 1 for w in words)
    parts, cur, size = [], [], 0
    for i, w in enumerate(words):
        cur.append(w)
        size += len(w) + 1
        left = len(words) - i - 1
        if len(parts) < k - 1 and size >= total * (len(parts) + 1) / k and left:
            parts.append(cur)
            cur = []
    return parts + [cur]


def chunks(vo):
    """Sentences, each cut into the fewest even captions of at most LINES lines of LINE characters."""
    out = []
    for sent in re.split(r"(?<=[.?!:])\s+", vo):
        words = sent.split()
        k = 1
        while True:
            caps = [textwrap.wrap(" ".join(p), LINE) for p in split_even(words, k)]
            if all(len(c) <= LINES for c in caps):
                break
            k += 1
        out += [" ".join(c) for c in caps]
    merged = []   # a short caption ("Save.") joins its neighbour when both fit on two lines
    for c in out:
        if merged and min(len(c), len(merged[-1])) < 24 and len(textwrap.wrap(merged[-1] + " " + c, LINE)) <= LINES:
            merged[-1] += " " + c
        else:
            merged.append(c)
    return ["\n".join(textwrap.wrap(c, LINE)) for c in merged if c.strip()]


def srt(segs):
    cues = []
    for _, a, b, vo in segs:
        cs = chunks(vo)
        if not cs:
            continue
        total = sum(max(len(c), 30) for c in cs)
        t = a + 0.3
        span = (b - a) - 0.6
        for c in cs:
            d = span * max(len(c), 30) / total
            cues.append((t, t + d - 0.05, c))
            t += d
    return "".join(f"{i}\n{stamp(s)} --> {stamp(e)}\n{c}\n\n" for i, (s, e, c) in enumerate(cues, 1))


def main():
    md = open(SCRIPT, encoding="utf-8").read()
    full = full_cut(md)
    for name, segs in (("captions_4min.srt", full), ("captions_3min.srt", short_cut(md, full))):
        path = os.path.join(HERE, name)
        open(path, "w", encoding="utf-8").write(srt(segs))
        print(f"wrote {name}: {len(segs)} segments, {sum(bool(s[3]) for s in segs)} with voice-over")


if __name__ == "__main__":
    main()
