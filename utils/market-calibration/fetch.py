#!/usr/bin/env python3
"""
fetch.py - downloads the EVE Ref (data.everef.net) market-history and killmail archives the calibration uses, into a
local cache outside the repo.  Files already in the cache are kept (and checked);  nothing is downloaded twice.

    python -I utils/market-calibration/fetch.py --history <dir> [--killmails <dir>]
                                                [--from 2011-12-01] [--to 2012-05-21] [--killmail-days 28]
    python -I utils/market-calibration/fetch.py --history <dir> --killmails <dir> --manifest utils/market-calibration/manifest.json

--history        cache folder for market-history-YYYY-MM-DD.csv.bz2 (every day of the window)
--killmails      cache folder for killmails-YYYY-MM-DD.tar.bz2 (a sample of days, see --killmail-days)
--killmail-days  how many killmail days:  every k-th day of the window from its first day, k = window / n rounded
                 down;  the default 28 days at k = 6 cycle through every weekday four times
--manifest       fetch exactly the files a manifest.json lists, and check their sha256 (reproduces a committed run)

Every file is checked to decompress completely before it is moved into place.  Each download is recorded in
<dir>/fetch-log.json (url, HTTP Last-Modified, ETag, fetch time);  the generator copies Last-Modified into the
manifest.  Requests carry a descriptive User-Agent and are spaced out;  please don't lower the delay.  Stdlib only.
"""

import argparse
import bz2
import datetime
import hashlib
import json
import os
import sys
import tarfile
import time
import urllib.error
import urllib.request

BASE_URL = 'https://data.everef.net'
USER_AGENT = 'evemu-market-calibration/1.0 (+https://github.com/EvEmu-Project/evemu_Crucible; utils/market-calibration)'
DELAY = 0.5
LOG_NAME = 'fetch-log.json'


def history_name(day):
    return 'market-history-%s.csv.bz2' % day


def history_url(day):
    return '%s/market-history/%s/%s' % (BASE_URL, day[:4], history_name(day))


def killmail_name(day):
    return 'killmails-%s.tar.bz2' % day


def killmail_url(day):
    return '%s/killmails/%s/%s' % (BASE_URL, day[:4], killmail_name(day))


def days(first, last):
    d = datetime.date.fromisoformat(first)
    end = datetime.date.fromisoformat(last)
    out = []
    while d <= end:
        out.append(d.isoformat())
        d += datetime.timedelta(days=1)
    return out


def killmail_days(first, last, n):
    """every k-th day of the window from its first day, n days"""
    all_days = days(first, last)
    k = max(1, len(all_days) // max(1, n))
    return all_days[::k][:n]


def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def check_archive(path):
    """raises when the file doesn't decompress completely"""
    if path.endswith('.tar.bz2'):
        with tarfile.open(path, 'r:bz2') as tf:
            for m in tf:
                if m.isfile():
                    tf.extractfile(m).read()
    else:
        with bz2.open(path, 'rb') as f:
            while f.read(1 << 20):
                pass


def load_log(folder):
    p = os.path.join(folder, LOG_NAME)
    if os.path.exists(p):
        with open(p, encoding='utf-8') as f:
            return json.load(f)
    return {}


def save_log(folder, log):
    p = os.path.join(folder, LOG_NAME)
    with open(p + '.tmp', 'w', encoding='utf-8', newline='\n') as f:
        json.dump(log, f, indent=1, sort_keys=True)
        f.write('\n')
    os.replace(p + '.tmp', p)


def head(url, name, log):
    """records Last-Modified / ETag of a file that is already cached but has no log entry"""
    try:
        req = urllib.request.Request(url, method='HEAD', headers={'User-Agent': USER_AGENT})
        with urllib.request.urlopen(req, timeout=60) as r:
            log[name] = {'url': url, 'lastModified': r.headers.get('Last-Modified'), 'etag': r.headers.get('ETag'),
                         'fetched': None}
        time.sleep(DELAY)
    except Exception as e:  # noqa: BLE001 - informational only
        print('  %s: HEAD failed: %s' % (url, e))


def fetch(url, folder, name, log, want_sha=None, tries=4):
    path = os.path.join(folder, name)
    if os.path.exists(path):
        if want_sha and sha256(path) != want_sha:
            print('  %s: sha256 differs from the manifest (the archive may have been updated in place)' % name)
        if name not in log:
            head(url, name, log)
        return path, False
    tmp = path + '.part'
    for attempt in range(tries):
        try:
            req = urllib.request.Request(url, headers={'User-Agent': USER_AGENT})
            with urllib.request.urlopen(req, timeout=120) as r, open(tmp, 'wb') as f:
                while True:
                    chunk = r.read(1 << 16)
                    if not chunk:
                        break
                    f.write(chunk)
                headers = r.headers
            check_archive(tmp)
            os.replace(tmp, path)
            log[name] = {'url': url, 'lastModified': headers.get('Last-Modified'), 'etag': headers.get('ETag'),
                         'fetched': datetime.datetime.now(datetime.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')}
            if want_sha and sha256(path) != want_sha:
                print('  %s: sha256 differs from the manifest (the archive may have been updated in place)' % name)
            time.sleep(DELAY)
            return path, True
        except urllib.error.HTTPError as e:
            if e.code == 404:
                print('  %s: not found (404)' % url)
                return None, False
            err = e
        except Exception as e:  # noqa: BLE001 - network, truncated archive: retry
            err = e
        if os.path.exists(tmp):
            os.remove(tmp)
        time.sleep(DELAY * 4 * (attempt + 1))
    raise SystemExit('%s: failed after %d tries: %s' % (url, tries, err))


def run(jobs, folder):
    if not jobs:
        return
    os.makedirs(folder, exist_ok=True)
    log = load_log(folder)
    got = 0
    for i, (url, name, want) in enumerate(jobs):
        _, new = fetch(url, folder, name, log, want)
        if new:
            got += 1
            if got % 10 == 0:
                save_log(folder, log)
                print('  %s: %d/%d' % (folder, i + 1, len(jobs)))
    save_log(folder, log)
    print('%s: %d files, %d downloaded' % (folder, len(jobs), got))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--history', required=True)
    ap.add_argument('--killmails')
    ap.add_argument('--from', dest='first', default='2011-12-01')
    ap.add_argument('--to', dest='last', default='2012-05-21')
    ap.add_argument('--killmail-days', type=int, default=28)
    ap.add_argument('--manifest')
    a = ap.parse_args()
    if a.manifest:
        with open(a.manifest, encoding='utf-8') as f:
            man = json.load(f)
        hist = [(s['url'], s['file'], s['sha256']) for s in man['sources'] if s['kind'] == 'market-history']
        kms = [(s['url'], s['file'], s['sha256']) for s in man['sources'] if s['kind'] == 'killmails']
    else:
        hist = [(history_url(d), history_name(d), None) for d in days(a.first, a.last)]
        kms = [(killmail_url(d), killmail_name(d), None) for d in killmail_days(a.first, a.last, a.killmail_days)]
    run(hist, a.history)
    if a.killmails:
        run(kms, a.killmails)
    elif kms and a.manifest:
        print('the manifest lists %d killmail archives;  pass --killmails to fetch them' % len(kms), file=sys.stderr)


if __name__ == '__main__':
    main()
