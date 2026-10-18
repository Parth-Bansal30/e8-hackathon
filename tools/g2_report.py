#!/usr/bin/env python3
"""G2 report from the knock bench's RTT log (firmware/imu_bench; docs/HANDOFF-gates.md G2).

The bench prints one line per event ("[G2] trial N t=<ms> FELT+HEARD dt=..", "HEARD ONLY",
"FELT ONLY"); one knock usually gives several (rebounds, ringing). This groups events into knocks
(gaps under --gap-ms), splits the log into sets at each "trial N is:" line (a bounced centre press
that skips a trial still starts one set), and per set reports:
  knocks, % felt (any IMU event in the group), % heard, and for groups with a FELT+HEARD pair the
  audio-minus-IMU time of the first pair: median, and % within +-10 ms of 0 and of the median.
It also checks the stats lines for the sample rate and missed samples, and prints the G2 verdict
against the HANDOFF criteria when the sets are labelled with --sets.

    tools/g2_report.py docs/gates/g2_rtt.log --sets 20cm,50cm,100cm,phone_air,phone_table,typing,cup,walk
"""
import argparse
import json
import re
import statistics as st

EV = re.compile(r"\[G2\] trial (\d+) t=(\d+) (FELT\+HEARD|HEARD ONLY|FELT ONLY)(?: dt=([+-][\d.]+) ms)?")
TRIAL = re.compile(r"\[G2\] trial (\d+) is: (.*)")
STATS = re.compile(r"\[G2\] stats: imu ([\d.]+) Hz, missed (\d+)")


def parse(path):
    sets, cur, rates, missed = [], None, [], []
    for line in open(path, errors="replace"):
        m = TRIAL.search(line)
        if m:
            if cur is not None and not cur["events"]:
                cur["name"] = m.group(2)          # bounced press: keep one set
                continue
            cur = dict(trial=int(m.group(1)), name=m.group(2), events=[])
            sets.append(cur)
            continue
        m = EV.search(line)
        if m and cur is not None:
            cur["events"].append((int(m.group(2)), m.group(3), float(m.group(4)) if m.group(4) else None))
            continue
        m = STATS.search(line)
        if m:
            rates.append(float(m.group(1)))
            missed.append(int(m.group(2)))
    return sets, rates, missed


def knocks(events, gap_ms):
    groups = []
    for e in sorted(events):
        if groups and e[0] - groups[-1][-1][0] < gap_ms:
            groups[-1].append(e)
        else:
            groups.append([e])
    out = []
    for g in groups:
        kinds = {k for _, k, _ in g}
        pair = next((d for _, k, d in g if k == "FELT+HEARD"), None)
        out.append(dict(t=g[0][0], n_events=len(g), felt=bool(kinds & {"FELT+HEARD", "FELT ONLY"}),
                        heard=bool(kinds & {"FELT+HEARD", "HEARD ONLY"}), dt=pair))
    return out


def summarise(ks):
    n = len(ks)
    dts = [k["dt"] for k in ks if k["dt"] is not None]
    med = st.median(dts) if dts else None
    return dict(knocks=n, felt=sum(k["felt"] for k in ks), heard=sum(k["heard"] for k in ks),
                felt_pct=100.0 * sum(k["felt"] for k in ks) / n if n else 0.0,
                paired=len(dts), dt_median=med,
                within10_of_0_pct=100.0 * sum(abs(d) <= 10 for d in dts) / len(dts) if dts else None,
                within10_of_median_pct=100.0 * sum(abs(d - med) <= 10 for d in dts) / len(dts) if dts else None,
                minutes=(ks[-1]["t"] - ks[0]["t"]) / 60000.0 if n > 1 else 0.0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log")
    ap.add_argument("--gap-ms", type=float, default=400, help="events closer than this are one knock")
    ap.add_argument("--sets", default="", help="labels for the sets after the warm-up, in order")
    ap.add_argument("--json")
    a = ap.parse_args()
    sets, rates, missed = parse(a.log)
    labels = a.sets.split(",") if a.sets else []
    res = {}
    print(f"IMU rate {min(rates):.1f}..{max(rates):.1f} Hz over {len(rates)} stats lines, missed total {sum(missed)}"
          if rates else "no stats lines")
    for i, s in enumerate(sets):
        lab = labels[i - 1] if 0 < i <= len(labels) else f"trial{s['trial']}"
        ks = knocks(s["events"], a.gap_ms)
        r = summarise(ks)
        r["name"] = s["name"]
        res[lab] = r
        al = (f"dt median {r['dt_median']:+.1f} ms, within 10 ms of 0 {r['within10_of_0_pct']:.0f}%, of median "
              f"{r['within10_of_median_pct']:.0f}%" if r["paired"] else "no pairs")
        print(f"{lab:12s} ({s['name']}): {r['knocks']} groups, felt {r['felt']} ({r['felt_pct']:.0f}%), heard {r['heard']}, "
              f"paired {r['paired']}; {al}")
    v = {}
    if rates:
        v["rate_ok"] = min(rates) >= 800 and sum(missed) == 0
    if "50cm" in res:
        v["felt_50cm_ok"] = res["50cm"]["felt_pct"] >= 95
    if "phone_air" in res:
        v["heard_only_ok"] = res["phone_air"]["felt_pct"] <= 10
    dist = [res[k] for k in ("20cm", "50cm", "100cm") if k in res and res[k]["paired"]]
    if dist:
        v["aligned_ok"] = all(r["within10_of_median_pct"] >= 95 for r in dist)
    if v:
        print("criteria:", ", ".join(f"{k} {'yes' if ok else 'NO'}" for k, ok in v.items()))
    if len(v) == 4:
        go = all(v.values())
        soft = not go and res.get("20cm", {}).get("felt_pct", 0) >= 95
        print("G2 verdict:", "GO" if go else "SOFT" if soft else "NO-GO")
        res["verdict"] = dict(criteria=v, result="GO" if go else "SOFT" if soft else "NO-GO")
    else:
        print("G2 verdict: incomplete (needs the 20cm, 50cm, 100cm and phone_air sets)")
    if a.json:
        json.dump(dict(rates=rates, missed=sum(missed), sets=res), open(a.json, "w"), indent=1)


if __name__ == "__main__":
    main()
