"""
killmails.py - streams the EVE Ref killmail day archives (killmails-YYYY-MM-DD.tar.bz2, one ESI-format JSON file per
killmail) into losses per (victim type, security band) and fit baskets per victim type.  Stdlib only.

Fit items are the top-level victim items in fitting flags:  low/med/high slots (11-34, charges loaded in a module
share its flag), the drone bay (87), implants (89, capsules), rigs (92-99) and subsystems (125-132).  The Crucible-era
killmails were imported from the old killmail API, which didn't record slots:  every fitted module, rig, subsystem
and loaded charge has flag 0 there (cargo is 5, drones 87, implants 89), so flag 0 counts as fitted.  Cargo and
container contents are not part of a fit and are left out.
"""

import collections
import datetime
import json
import os
import tarfile

FIT_FLAGS = frozenset([0] + list(range(11, 35)) + [87, 89] + list(range(92, 100)) + list(range(125, 133)))
WSPACE_REGION = 11000000

BAND_HIGH, BAND_LOW, BAND_NULL, BAND_WSPACE = 0, 1, 2, 3


def security_level(sec):
    """the client's displayed security (CrimeWatch::SecurityLevel):  0 < s < 0.05 shows 0.1, halves away from 0"""
    if 0.0 < sec < 0.05:
        sec = 0.05
    x = sec * 10.0
    lvl = (int(x + 0.5) if x >= 0 else -int(-x + 0.5)) / 10.0
    return 0.0 if lvl == 0 else lvl


def band(sec, region):
    if region >= WSPACE_REGION:
        return BAND_WSPACE
    lvl = security_level(sec)
    if lvl <= 0.0:
        return BAND_NULL
    if lvl < 0.45:
        return BAND_LOW
    return BAND_HIGH


class Killmails(object):
    def __init__(self):
        self.files = []                              # [{'file', 'path', 'date', 'rows'}]
        self.days = 0
        self.count = 0
        self.npc_only = 0
        self.losses = collections.Counter()          # (victim typeID, band) -> killmails
        self.hull = collections.Counter()            # victim typeID -> killmails
        self.items = collections.defaultdict(lambda: [0, 0])     # (victim, typeID) -> [destroyed, dropped]
        self.item_total = collections.defaultdict(lambda: [0, 0])  # typeID -> [destroyed, dropped], fit flags
        self.bands = collections.Counter()
        self.dropped = collections.Counter()


def read(folder, days, known, systems):
    """days: ISO dates to read (missing archives are skipped);  known: typeIDs the client has;
    systems: solarSystemID -> (security, regionID)"""
    k = Killmails()
    if not folder:
        return k
    for day in days:
        name = 'killmails-%s.tar.bz2' % day
        path = os.path.join(folder, name)
        if not os.path.exists(path):
            continue
        n = 0
        with tarfile.open(path, 'r:bz2') as tf:
            for m in tf:
                if not m.isfile() or not m.name.endswith('.json'):
                    continue
                km = json.load(tf.extractfile(m))
                n += 1
                v = km.get('victim') or {}
                st = v.get('ship_type_id')
                sysrow = systems.get(km.get('solar_system_id'))
                if st not in known:
                    k.dropped['victim type unknown'] += 1
                    continue
                if sysrow is None:
                    k.dropped['solar system unknown'] += 1
                    continue
                b = band(*sysrow)
                k.count += 1
                k.bands[b] += 1
                k.losses[(st, b)] += 1
                k.hull[st] += 1
                if all('character_id' not in a for a in km.get('attackers', [])):
                    k.npc_only += 1
                for it in v.get('items', []):
                    if it.get('flag') not in FIT_FLAGS:
                        continue
                    t = it.get('item_type_id')
                    if t not in known:
                        k.dropped['item type unknown'] += 1
                        continue
                    de, dr = int(it.get('quantity_destroyed', 0)), int(it.get('quantity_dropped', 0))
                    a = k.items[(st, t)]
                    a[0] += de
                    a[1] += dr
                    a = k.item_total[t]
                    a[0] += de
                    a[1] += dr
        k.files.append({'file': name, 'path': path, 'date': day, 'rows': n})
        k.days += 1
    return k


def archive_days(folder, first, last):
    """the ISO dates of the killmail archives in folder that fall inside the window"""
    if not folder or not os.path.isdir(folder):
        return []
    out = []
    for fn in os.listdir(folder):
        if fn.startswith('killmails-') and fn.endswith('.tar.bz2'):
            d = fn[len('killmails-'):-len('.tar.bz2')]
            try:
                datetime.date.fromisoformat(d)
            except ValueError:
                continue
            if first <= d <= last:
                out.append(d)
    return sorted(out)
