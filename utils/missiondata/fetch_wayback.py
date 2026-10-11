#!/usr/bin/env python3
"""
fetch_wayback.py - download the eve-survival.org mission reports as the Wayback Machine archived them
                   before the Crucible release (2011-11-29).

    python -I fetch_wayback.py [--cutoff 20111129] [--out cache/] [--delay 4]

  - one CDX query lists every archived eve-survival.org/wikka.php page up to the cutoff (cache/cdx.txt)
  - the index pages (MissionReports, MissionReportsLevel1..5, MissionReportsMining, CourierMissionsList) give the
    report page list
  - every report page is downloaded raw (the 'id_' url form, no wayback toolbar) at its latest snapshot <= cutoff
    into cache/<Page>.html, with the snapshot timestamp in cache/<Page>.ts
  - pages without such a snapshot are listed in cache/missing.txt (they are taken from the 2017 GitHub mirror,
    disabled by default, if --mirror is given)

Throttled to one request per --delay seconds, 503s/timeouts retried with backoff (8, 16, 32, 64 s), resumable
(existing cache files are skipped).  Standard library only.
"""

import argparse
import os
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

CDX = ("http://web.archive.org/cdx/search/cdx?url=eve-survival.org/wikka.php&matchType=prefix"
       "&from=20100101&to={cutoff}&filter=statuscode:200&fl=timestamp,original")
RAW = "http://web.archive.org/web/{ts}id_/http://eve-survival.org/wikka.php?wakka={page}"
MIRROR = "https://cyberrodent.github.io/eve-survival-mission-report/{page}.html"

INDEX_PAGES = ["MissionReports", "MissionReportsLevel1", "MissionReportsLevel2", "MissionReportsLevel3",
               "MissionReportsLevel4", "MissionReportsLevel5", "MissionReportsMining", "CourierMissionsList"]

# wiki pages that are never mission reports
SKIP = re.compile(r"^(HomePage|PageIndex|RecentChanges|RecentlyCommented|UserSettings|TextSearch|WantedPages|"
                  r"OrphanedPages|OwnedPages|MyPages|MyChanges|Category\w*|MissionReports\w*|CourierMissionsList|"
                  r"FormattingRules|WikkaDocumentation|SandBox|Ships\w*|NPC\w*|Faction\w*|Damage\w*|Guide\w*)$", re.I)

_last = [0.0]


def fetch(url, delay, tries=5):
    wait = 8
    for attempt in range(tries):
        gap = time.time() - _last[0]
        if gap < delay:
            time.sleep(delay - gap)
        _last[0] = time.time()
        try:
            req = urllib.request.Request(url, headers={"User-Agent": "evemu-missiondata/1.0 (offline import)"})
            with urllib.request.urlopen(req, timeout=60) as rsp:
                return rsp.read()
        except urllib.error.HTTPError as e:
            if e.code == 404:
                return None
            print("  http %d on %s, retry in %ds" % (e.code, url, wait), flush=True)
        except Exception as e:  # timeouts, resets
            print("  %s on %s, retry in %ds" % (type(e).__name__, url, wait), flush=True)
        time.sleep(wait)
        wait *= 2
    return None


def page_of(original):
    """'http://eve-survival.org:80/wikka.php?wakka=Blockade4an' -> ('Blockade4an', extra-params?)"""
    q = urllib.parse.urlsplit(original).query
    m = re.match(r"wakka=([A-Za-z0-9]+)(.*)$", q)
    if m is None:
        return None, True
    rest = m.group(2)
    # '/history', '/revisions', '&show_comments=1' ... are not the page itself
    return m.group(1), (rest != "")


def load_cdx(out, cutoff, delay):
    path = os.path.join(out, "cdx.txt")
    if not os.path.exists(path):
        data = fetch(CDX.format(cutoff=cutoff), delay)
        if data is None:
            sys.exit("CDX query failed")
        with open(path, "wb") as f:
            f.write(data)
    snaps = {}      # page lower -> [(ts, page, plain)]
    with open(path, encoding="utf8", errors="replace") as f:
        for line in f:
            parts = line.split()
            if len(parts) != 2 or parts[0] > cutoff + "999999":
                continue
            page, extra = page_of(parts[1])
            if page is None:
                continue
            snaps.setdefault(page.lower(), []).append((parts[0], page, not extra))
    return snaps


def latest(snaps, page):
    """latest plain snapshot <= cutoff, else the latest with extra params (show_comments...)"""
    lst = snaps.get(page.lower(), [])
    plain = sorted(s for s in lst if s[2])
    if plain:
        return plain[-1][0]
    any_ = sorted(lst)
    return any_[-1][0] if any_ else None


def save(out, page, ts, data):
    with open(os.path.join(out, page + ".html"), "wb") as f:
        f.write(data)
    with open(os.path.join(out, page + ".ts"), "w") as f:
        f.write(ts)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cutoff", default="20111129")
    ap.add_argument("--out", default="cache")
    ap.add_argument("--delay", type=float, default=4.0)
    ap.add_argument("--mirror", action="store_true", help="take pages without a snapshot from the 2017 mirror")
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)

    snaps = load_cdx(args.out, args.cutoff, args.delay)
    print("%d archived wiki pages <= %s" % (len(snaps), args.cutoff), flush=True)

    pages = []
    for idx in INDEX_PAGES:
        cached = os.path.join(args.out, idx + ".html")
        if not os.path.exists(cached):
            ts = latest(snaps, idx)
            if ts is None:
                print("no snapshot of index %s" % idx, flush=True)
                continue
            data = fetch(RAW.format(ts=ts, page=idx), args.delay)
            if data is None or b'id="content"' not in data:
                print("index %s unusable" % idx, flush=True)
                continue
            save(args.out, idx, ts, data)
        html = open(cached, encoding="latin1").read()
        for m in re.finditer(r"wikka\.php\?wakka=([A-Za-z0-9]+)\"", html):
            p = m.group(1)
            if not SKIP.match(p) and p not in pages:
                pages.append(p)
    print("%d report pages linked from the indexes" % len(pages), flush=True)
    with open(os.path.join(args.out, "pages.txt"), "w") as f:
        f.write("\n".join(pages) + "\n")

    missing = []
    for n, page in enumerate(pages, 1):
        if os.path.exists(os.path.join(args.out, page + ".html")):
            continue
        ts = latest(snaps, page)
        if ts is None:
            missing.append(page)
            if args.mirror:
                data = fetch(MIRROR.format(page=page), 1.0)
                if data is not None:
                    save(args.out, page, "mirror2017", data)
            continue
        data = fetch(RAW.format(ts=ts, page=page), args.delay)
        if data is None or b'id="content"' not in data:
            # truncated/odd snapshot: try the next older one
            older = sorted(s[0] for s in snaps.get(page.lower(), []) if s[0] < ts)
            data = None
            for ots in reversed(older[-3:]):
                d = fetch(RAW.format(ts=ots, page=page), args.delay)
                if d is not None and b'id="content"' in d:
                    data, ts = d, ots
                    break
        if data is None:
            missing.append(page)
            print("[%d/%d] %s: no usable snapshot" % (n, len(pages), page), flush=True)
            continue
        save(args.out, page, ts, data)
        print("[%d/%d] %s @ %s" % (n, len(pages), page, ts), flush=True)

    with open(os.path.join(args.out, "missing.txt"), "w") as f:
        f.write("\n".join(missing) + ("\n" if missing else ""))
    print("done, %d missing" % len(missing), flush=True)


if __name__ == "__main__":
    main()
