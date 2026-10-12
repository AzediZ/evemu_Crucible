"""
history.py - streams the EVE Ref market-history day files (market-history-YYYY-MM-DD.csv.bz2:  date, region_id,
type_id, average, highest, lowest, volume, order_count) into per (region, type) daily series.  Stdlib only.

Rows are dropped when the type is unknown to the Crucible client (later back-filled IDs such as 44992, the modern
PLEX), when it isn't a market type (published with a marketGroupID: faction, officer and deadspace modules the
client can't list), when the region is unknown, or when average or volume is not positive.  highest and lowest are
not read:  they are equal to average in most rows (see PLAN.md section 4.1).
"""

import bz2
import collections
import csv
import datetime
import io
import os
from array import array

FORGE = 10000002
HUBS = (10000002, 10000043, 10000032, 10000030, 10000042)  # The Forge, Domain, Sinq Laison, Heimatar, Metropolis


class Cell(object):
    """one region's daily series of one type:  calendar day index into History.days, average, volume, order count"""
    __slots__ = ('day', 'avg', 'vol', 'orders')

    def __init__(self):
        self.day = array('H')
        self.avg = array('d')
        self.vol = array('q')
        self.orders = array('q')


class History(object):
    def __init__(self):
        self.days = []            # ISO dates of every window day, ascending (cells index this list)
        self.present = []         # indices into days that have a file
        self.missing = []         # window days without a file
        self.cells = {}           # (regionID, typeID) -> Cell
        self.files = []           # [{'file', 'path', 'date', 'rows', 'kept'}]
        self.dropped = collections.Counter()      # reason -> rows
        self.unknown_types = collections.Counter()    # typeID -> rows (not in the client's invTypes)
        self.nonmarket_types = collections.Counter()  # typeID -> rows (known, but not a market type)
        self.date_mismatch = 0

    def weekday(self, i):
        return datetime.date.fromisoformat(self.days[i]).weekday()

    def by_type(self):
        """typeID -> {regionID: Cell}"""
        out = collections.defaultdict(dict)
        for (r, t), c in self.cells.items():
            out[t][r] = c
        return out


def window(first, last):
    d = datetime.date.fromisoformat(first)
    end = datetime.date.fromisoformat(last)
    while d <= end:
        yield d.isoformat()
        d += datetime.timedelta(days=1)


def read(folder, first, last, known, market, regions):
    """known: typeIDs the client has;  market: the market typeIDs;  regions: known regionIDs"""
    h = History()
    for di, day in enumerate(window(first, last)):
        h.days.append(day)
        name = 'market-history-%s.csv.bz2' % day
        path = os.path.join(folder, name)
        if not os.path.exists(path):
            h.missing.append(day)
            continue
        h.present.append(di)
        rows = kept = 0
        with bz2.open(path, 'rb') as raw:
            f = io.TextIOWrapper(raw, encoding='utf-8', newline='')
            rd = csv.reader(f)
            head = next(rd)
            col = {c.strip(): i for i, c in enumerate(head)}
            i_date, i_reg, i_type = col['date'], col['region_id'], col['type_id']
            i_avg, i_vol, i_ord = col['average'], col['volume'], col['order_count']
            cells = h.cells
            for r in rd:
                rows += 1
                try:
                    tid = int(r[i_type])
                    reg = int(r[i_reg])
                    avg = float(r[i_avg])
                    vol = int(float(r[i_vol]))
                    orders = int(float(r[i_ord]))
                except (ValueError, IndexError):
                    h.dropped['unparsable'] += 1
                    continue
                if tid not in known:
                    h.unknown_types[tid] += 1
                    h.dropped['type unknown to the client'] += 1
                    continue
                if tid not in market:
                    h.nonmarket_types[tid] += 1
                    h.dropped['not a market type'] += 1
                    continue
                if reg not in regions:
                    h.dropped['unknown region'] += 1
                    continue
                if not (avg > 0) or vol <= 0:
                    h.dropped['average or volume not positive'] += 1
                    continue
                if r[i_date][:10] != day:
                    h.date_mismatch += 1
                c = cells.get((reg, tid))
                if c is None:
                    c = cells[(reg, tid)] = Cell()
                c.day.append(di)
                c.avg.append(avg)
                c.vol.append(vol)
                c.orders.append(max(0, orders))
                kept += 1
        h.files.append({'file': name, 'path': path, 'date': day, 'rows': rows, 'kept': kept})
    return h
