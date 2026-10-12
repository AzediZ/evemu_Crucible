"""
derive.py - turns the static data, the market history and the killmails into the mktCal* tables
(doc/code_and_design_notes/market-sim/PLAN.md section 4):  type classes, robust price/volume/volatility statistics,
recursive bill-of-materials costs, the gap filling for types without history, regional and weekday shape, meta
ratios, losses and fit baskets.  Stdlib only;  every loop runs over sorted keys, so the output is deterministic.
"""

import collections
import math
import os
import sys

from history import FORGE, HUBS

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'bulkdata'))

import serverdata  # noqa: E402

# ---- type classes (mktCalType.simClass)
EXCLUDED, MINED, REFINED, MFG_T1, MFG_T2, MFG_T3, COMPONENT, MOON, PI, LOOT, FACTION, SALVAGE, VENDOR, NOT_SIM = range(14)
CLASS_NAMES = ['excluded', 'mined', 'refined', 'mfgT1', 'mfgT2', 'mfgT3', 'component', 'reaction/moon', 'PI',
               'loot/named', 'faction/LP', 'salvage/datacore/decryptor', 'npcVendor', 'notSimulated']
# ---- price sources (mktCalType.priceSource)
SRC_NONE, SRC_HISTORY, SRC_BOM, SRC_META, SRC_REFINE, SRC_GROUP, SRC_BASE, SRC_VENDOR = range(8)
SOURCE_NAMES = ['none', 'history', 'BOM', 'meta-parent', 'refine value', 'group median', 'basePrice', 'vendor']

PLEX = 29668
GROUP_GAME_TIME = 943                 # PLEX's group
CAT_APPAREL = 30                      # NeX apparel
CAT_CELESTIAL, CAT_MATERIAL, CAT_SHIP, CAT_MODULE, CAT_CHARGE, CAT_BLUEPRINT = 2, 4, 6, 7, 8, 9
CAT_SKILL, CAT_COMMODITY, CAT_DRONE, CAT_IMPLANT, CAT_DEPLOYABLE, CAT_STRUCTURE = 16, 17, 18, 20, 22, 23
CAT_REACTION, CAT_ASTEROID, CAT_SUBSYSTEM, CAT_RELIC, CAT_DECRYPTOR = 24, 25, 32, 34, 35
CAT_INFRASTRUCTURE, CAT_SOVEREIGNTY, CAT_PI_STRUCTURE, CAT_PI_RESOURCE, CAT_PI_COMMODITY = 39, 40, 41, 42, 43
GROUP_MINERAL, GROUP_ICE_PRODUCT, GROUP_SHUTTLE, GROUP_BOOSTER, GROUP_LEASE, GROUP_FUEL_BLOCK = 18, 423, 31, 303, 652, 1136
GROUP_HARVESTABLE_CLOUD = 711
MINERALS = (34, 35, 36, 37, 38, 39, 40, 11399)

# crpNPCCorporationTrades types of these categories/groups are NPC vendor goods:  skills, blueprints, reactions, trade
# goods (commodities), structures, sovereignty structures and upgrades, containers and outpost platforms, command
# centers, shuttles.  (Modules, charges and PI products in that table were not NPC-seeded in Crucible.)
VENDOR_CATEGORIES = frozenset([CAT_BLUEPRINT, CAT_SKILL, CAT_REACTION, CAT_COMMODITY, CAT_STRUCTURE,
                               CAT_INFRASTRUCTURE, CAT_SOVEREIGNTY, CAT_CELESTIAL, CAT_PI_STRUCTURE])
VENDOR_GROUPS = frozenset([GROUP_SHUTTLE])
MOON_GROUPS = frozenset([427, 428, 429, 712, 974])   # moon materials, intermediate, composite, biochemical, polymers
COMPONENT_GROUPS = frozenset([332, 334, 873, 913, 964])  # R.A.M./R.Db tools, T2, capital, adv. capital, T3 components
SALVAGE_GROUPS = frozenset([333, 526, 528, 530, 732, 733, 734, 735, 754, 880, 886, 966])  # datacores, decryptors,
#   metal scraps, sleeper artifacts/materials/components, salvaged materials, rogue drone components, ancient salvage
FACTION_GROUPS = frozenset([370, 409, 493, GROUP_LEASE])  # tags, insignias, overseer effects, starbase charters (LP)
FACTION_META = frozenset([3, 4, 5, 6])  # storyline, faction, officer, deadspace

# category defaults: demand price elasticity (PLAN.md 6.3) and final-consumption share where killmails don't tell
ELASTICITY = {CAT_SHIP: 0.8, CAT_MODULE: 1.0, CAT_DRONE: 1.0, CAT_SUBSYSTEM: 1.0, CAT_CHARGE: 1.2,
              CAT_IMPLANT: 1.5, CAT_STRUCTURE: 0.8, CAT_DEPLOYABLE: 0.8, CAT_SKILL: 0.5, CAT_BLUEPRINT: 0.5}
ELASTICITY_INTERMEDIATE = 0.5          # raw and intermediate goods (derived demand only)
ELASTICITY_LUXURY = 1.5                # faction/LP goods
CONSUME_DEFAULT = {CAT_SHIP: 0.3, CAT_MODULE: 0.1, CAT_DRONE: 0.1, CAT_SUBSYSTEM: 0.1, CAT_IMPLANT: 0.1,
                   CAT_DEPLOYABLE: 0.3, CAT_STRUCTURE: 0.3, CAT_SOVEREIGNTY: 0.3, CAT_CELESTIAL: 0.3,
                   CAT_INFRASTRUCTURE: 0.3, CAT_PI_STRUCTURE: 0.3}
CONSUME_CHARGE = 0.8                   # ammunition is fired;  killmails only show what was loaded
CONSUME_USED_UP = 1.0                  # skills, blueprints, reactions, boosters, charters, fuel blocks
CONSUME_TRADE_GOODS = 0.5
KILLMAIL_MIN_UNITS = 3                 # observed units below this use the group default
FIT_MIN_LOSSES = 5
FIT_MAX_ITEMS = 25
FIT_MIN_QTY = 0.02

# per class:  stock elasticity theta, quote half-spread h, target stock cover in days (PLAN.md 6.2 defaults)
CLASS_SHAPE = {
    EXCLUDED: (0.35, 0.06, 5), MINED: (0.25, 0.015, 3), REFINED: (0.25, 0.015, 3), MFG_T1: (0.35, 0.06, 5),
    MFG_T2: (0.5, 0.06, 7), MFG_T3: (0.5, 0.06, 7), COMPONENT: (0.25, 0.04, 5), MOON: (0.25, 0.015, 4),
    PI: (0.25, 0.03, 4), LOOT: (0.7, 0.12, 10), FACTION: (0.7, 0.12, 10), SALVAGE: (0.25, 0.04, 10),
    VENDOR: (0.0, 0.06, 30), NOT_SIM: (0.35, 0.06, 5)}
HALF_SPREAD_SHIP = 0.04
HALF_SPREAD_IMPLANT = 0.12

HISTORY_MIN_DAYS = 5
VOL_MIN, VOL_MAX = 0.005, 0.6
UNITS_WINSOR = 5.0                     # a day's units are capped at this many times the median trading day
REGION_MIN_ACTIVE = 0.2                # mktCalRegion keeps cells this active, or in a hub region
RATIO_MIN, RATIO_MAX = 0.1, 10.0
T2_ME = -4                             # invented BPCs (Crucible)
NAMED_RATIO_MIN, NAMED_RATIO_MAX = 0.5, 1000.0  # named (meta 1-4) price / parent from the metaLevel fit
REFINE_CLASSES = frozenset([MINED, REFINED, SALVAGE, LOOT])  # raw and reprocessable loot get 0.95 x refine value
THIN_MAX_DEX = 1.0                     # thin history further than this from the group-median estimate is not used


# ---------------------------------------------------------------- small statistics
def median(xs):
    s = sorted(xs)
    n = len(s)
    if not n:
        return None
    return s[n // 2] if n % 2 else 0.5 * (s[n // 2 - 1] + s[n // 2])


def percentile(xs, p):
    s = sorted(xs)
    if not s:
        return None
    x = p * (len(s) - 1)
    i = int(math.floor(x))
    if i + 1 >= len(s):
        return s[-1]
    return s[i] + (s[i + 1] - s[i]) * (x - i)


def weighted_percentile(pairs, p):
    """pairs: (value, weight)"""
    s = sorted(pairs)
    tot = math.fsum(w for _, w in s)
    if tot <= 0:
        return None
    acc = 0.0
    for v, w in s:
        acc += w
        if acc >= p * tot:
            return v
    return s[-1][0]


def mad_sd(xs):
    m = median(xs)
    return 1.4826 * median([abs(x - m) for x in xs])


def ols(xs, ys):
    """(a, b, residual sd) of y = a + b x"""
    n = len(xs)
    mx, my = math.fsum(xs) / n, math.fsum(ys) / n
    sxx = math.fsum((x - mx) ** 2 for x in xs)
    if sxx <= 0:
        return None
    b = math.fsum((x - mx) * (y - my) for x, y in zip(xs, ys)) / sxx
    a = my - b * mx
    resid = math.sqrt(math.fsum((y - a - b * x) ** 2 for x, y in zip(xs, ys)) / n)
    return a, b, resid


def clip(x, lo, hi):
    return lo if x < lo else hi if x > hi else x


def round_half_up(x):
    return int(math.floor(x + 0.5))


# ---------------------------------------------------------------- static data
class Static(object):
    """the server's static tables after a fresh install (sql/base + migrations incl. client-sync)"""

    TABLES = ('invTypes', 'invGroups', 'invCategories', 'invTypeMaterials', 'invMetaTypes', 'invBlueprintTypes',
              'ramTypeRequirements', 'crpNPCCorporationTrades', 'dgmTypeAttributes', 'mapSolarSystems', 'mapRegions')

    def __init__(self, client_types=None):
        tables = {n: serverdata.load_synced(n) for n in self.TABLES}
        self.sources = sorted(set(s for t in tables.values() for s in t.sources))
        self.types = {r['typeID']: r for r in tables['invTypes'].rows}
        self.groups = {r['groupID']: r for r in tables['invGroups'].rows}
        self.cats = {r['categoryID']: r['categoryName'] for r in tables['invCategories'].rows}
        self.meta = {r['typeID']: (r['parentTypeID'], r['metaGroupID']) for r in tables['invMetaTypes'].rows}
        self.bp = {}
        for r in sorted(tables['invBlueprintTypes'].rows, key=lambda r: r['blueprintTypeID']):
            if r['productTypeID'] in self.types:
                self.bp.setdefault(r['productTypeID'], r)
        self.mats = collections.defaultdict(list)
        for r in tables['invTypeMaterials'].rows:
            if r['quantity'] and r['quantity'] > 0:
                self.mats[r['typeID']].append((r['materialTypeID'], r['quantity']))
        for v in self.mats.values():
            v.sort()
        self.reqs = collections.defaultdict(list)
        for r in tables['ramTypeRequirements'].rows:
            if r['activityID'] == 1:
                self.reqs[r['typeID']].append((r['requiredTypeID'], r['quantity'], float(r['damagePerJob'] or 0),
                                               bool(r['extra'])))
        for v in self.reqs.values():
            v.sort()
        self.trades = set(r['typeID'] for r in tables['crpNPCCorporationTrades'].rows)
        self.metalevel = {}
        for r in tables['dgmTypeAttributes'].rows:
            if r['attributeID'] == 633:
                v = r['valueInt'] if r['valueInt'] is not None else r['valueFloat']
                self.metalevel[r['typeID']] = float(v or 0)
        self.systems = {r['solarSystemID']: (float(r['security']), r['regionID'])
                        for r in tables['mapSolarSystems'].rows}
        self.regions = {r['regionID']: r['regionName'] for r in tables['mapRegions'].rows}
        self.known = set(self.types)
        self.market = set(t for t, r in self.types.items() if r['published'] in (1, True) and r['marketGroupID'])
        self.client_diff = None
        if client_types is not None:
            # the client wins:  its type list and market flags define what is known and what is a market type
            known = set(client_types)
            market = set(t for t, (pub, mg) in client_types.items() if pub and mg)
            self.client_diff = {'onlyClient': sorted(known - self.known), 'onlyServer': sorted(self.known - known),
                                'marketOnlyClient': sorted(market - self.market),
                                'marketOnlyServer': sorted(self.market - market)}
            self.known = known & set(self.types)
            self.market = market & set(self.types)

    def group(self, t):
        return self.types[t]['groupID']

    def cat(self, t):
        g = self.groups.get(self.types[t]['groupID']) if t in self.types else None
        return g['categoryID'] if g else None

    def name(self, t):
        return self.types[t]['typeName'] if t in self.types else str(t)

    def portion(self, t):
        return max(1, int(self.types[t]['portionSize'] or 1))

    def base_price(self, t):
        return float(self.types[t]['basePrice'] or 0)

    def tech(self, t):
        b = self.bp.get(t)
        return int(b['techLevel']) if b else 0

    def classify(self, t):
        g, c = self.group(t), self.cat(t)
        if t == PLEX or g == GROUP_GAME_TIME or c == CAT_APPAREL:
            return EXCLUDED
        if t in self.trades and (c in VENDOR_CATEGORIES or g in VENDOR_GROUPS):
            return VENDOR
        if c in (CAT_BLUEPRINT, CAT_SKILL):
            return NOT_SIM          # no NPC sells it (T2 BPOs and a few skills):  players only
        if c in (CAT_PI_RESOURCE, CAT_PI_COMMODITY):
            return PI
        if c == CAT_ASTEROID or g == GROUP_HARVESTABLE_CLOUD:
            return MINED
        if g in (GROUP_MINERAL, GROUP_ICE_PRODUCT):
            return REFINED
        if g in MOON_GROUPS:
            return MOON
        if g in SALVAGE_GROUPS or c in (CAT_RELIC, CAT_DECRYPTOR):
            return SALVAGE
        if g in FACTION_GROUPS:
            return FACTION
        mg = self.meta.get(t, (None, None))[1]
        if mg in FACTION_META:
            return FACTION
        if t in self.bp:
            if not self.obtainable(t):
                return FACTION      # BPC-only (LP, drops, prizes):  supplied like loot, not built
            if g in COMPONENT_GROUPS:
                return COMPONENT
            return {1: MFG_T1, 2: MFG_T2, 3: MFG_T3}.get(self.tech(t), MFG_T1)
        if mg == 1:
            return LOOT
        if c in (CAT_IMPLANT, CAT_SHIP):
            return FACTION          # implants and hardwirings, special hulls without a blueprint:  LP and loot
        return LOOT

    def obtainable(self, t):
        """whether t's blueprint can be had:  T1 and components need a BPO on the market, T2 and T3 a published
        blueprint (invention, reverse engineering)"""
        b = self.types.get(self.bp[t]['blueprintTypeID'])
        if b is None or b['published'] not in (1, True):
            return False
        if self.tech(t) <= 1 or self.group(t) in COMPONENT_GROUPS:
            return bool(b['marketGroupID'])
        return True

    def recycled_materials(self, t):
        """base materials of t's blueprint after the recycled (T1) requirement's materials are taken off"""
        b = self.bp[t]
        base = collections.OrderedDict(self.mats.get(t, []))
        for req, q, dmg, rec in self.reqs.get(b['blueprintTypeID'], []):
            if rec:
                for m, mq in self.mats.get(req, []):
                    if m in base:
                        base[m] = max(0, base[m] - mq * q)
        return [(m, q) for m, q in base.items() if q > 0]

    def bom_lines(self, t):
        """(base materials with waste, extra materials as (type, qty * damage));  per run"""
        b = self.bp[t]
        wf = float(b['wasteFactor'] or 0) / 100.0
        me = T2_ME if self.tech(t) == 2 else 0
        waste = wf / (1 + me) if me >= 0 else wf * (1 - me)
        base = [(m, round_half_up(q * (1 + waste))) for m, q in self.recycled_materials(t)]
        extra = []
        for req, q, dmg, rec in self.reqs.get(b['blueprintTypeID'], []):
            if dmg <= 0 or req not in self.types or self.cat(req) == CAT_SKILL:
                continue
            extra.append((req, q * dmg))
        return base, extra

    def bom_cost(self, t, price_of):
        """cost per unit of t at price_of(input);  None when an input has no price"""
        if t not in self.bp:
            return None
        base, extra = self.bom_lines(t)
        if not base and not extra:
            return None
        cost = 0.0
        for m, q in base + extra:
            p = price_of(m)
            if p is None or p <= 0:
                return None
            cost += q * p
        return cost / self.portion(t)


# ---------------------------------------------------------------- calibration
class TypeCal(object):
    __slots__ = ('typeID', 'simClass', 'priceSource', 'confidence', 'refPrice', 'priceP10', 'priceP90',
                 'volatility', 'unitsPerDay', 'activeShare', 'lotSize', 'buildCostRef', 'consumeShare', 'elasticity')

    def __init__(self, t):
        self.typeID = t
        for s in self.__slots__[1:]:
            setattr(self, s, 0)


class Calibration(object):
    def __init__(self, st, hist, km, coverage):
        self.st = st
        self.h = hist
        self.km = km
        self.coverage = coverage
        self.present = hist.present
        self.N = len(hist.present)
        self.notes = []
        self.run()

    # ------------------------------------------------------------ history statistics
    def run(self):
        st, h = self.st, self.h
        self.cls = {t: st.classify(t) for t in sorted(st.market)}
        self.by_type = h.by_type()
        self.forge_avg = {}
        for t, regs in self.by_type.items():
            f = regs.get(FORGE)
            if f is not None:
                self.forge_avg[t] = dict(zip(f.day, f.avg))
        self._region_ratios()
        self._type_history()
        self._markups()
        self._meta_ratios()
        self._group_price_ratios()
        self._price_all()
        self._gap_stats()
        self._fill_types()
        self._region_tables()
        self._group_table()
        self._killmail_tables()

    def _region_ratios(self):
        """same-day avg(region)/avg(Forge):  per cell (>= 3 common days) and per region (all types)"""
        h = self.h
        per_region = collections.defaultdict(list)
        self.cell_ratio = {}
        self.region_units = collections.Counter()
        self.region_isk = collections.Counter()
        for (r, t) in sorted(h.cells):
            c = h.cells[(r, t)]
            if self.cls.get(t) == EXCLUDED:
                continue
            self.region_units[r] += sum(c.vol)
            self.region_isk[r] += math.fsum(a * v for a, v in zip(c.avg, c.vol))
            if r == FORGE:
                continue
            f = self.forge_avg.get(t)
            if not f:
                continue
            rs = [a / f[d] for d, a in zip(c.day, c.avg) if d in f]
            per_region[r].extend(rs)
            if len(rs) >= 3:
                self.cell_ratio[(r, t)] = median(rs)
        self.region_ratio = {FORGE: 1.0}
        for r in sorted(per_region):
            if len(per_region[r]) >= 20:
                self.region_ratio[r] = median(per_region[r])
        # a region with too little overlap with Forge:  the median region
        self.region_ratio_default = median(list(self.region_ratio.values()))

    def _type_history(self):
        """per type:  Forge days, history price and band, volatility, units, lot size"""
        N, present = self.N, self.present
        self.hist = {}
        for t in sorted(self.cls):
            regs = self.by_type.get(t, {})
            if not regs:
                continue
            f = regs.get(FORGE)
            fdays = len(f.day) if f is not None else 0
            units = collections.Counter()
            rows = 0
            for r in sorted(regs):
                c = regs[r]
                rows += len(c.day)
                for d, v in zip(c.day, c.vol):
                    units[d] += v
            if fdays >= HISTORY_MIN_DAYS:
                avgs = list(f.avg)
                ref, p10, p90 = median(avgs), percentile(avgs, 0.1), percentile(avgs, 0.9)
                kind = 'forge'
            else:
                pairs = []
                for r in sorted(regs):
                    c = regs[r]
                    rr = self.region_ratio.get(r, self.region_ratio_default)
                    pairs.extend((a / rr, v) for a, v in zip(c.avg, c.vol))
                ref = weighted_percentile(pairs, 0.5)
                p10, p90 = weighted_percentile(pairs, 0.1), weighted_percentile(pairs, 0.9)
                kind = 'regions' if rows >= HISTORY_MIN_DAYS else 'thin'
            # volatility:  1.4826 MAD of day-to-day log returns, Forge, else the region with most days
            vol = None
            series = [f] if f is not None else []
            series += [regs[r] for r in sorted(regs, key=lambda r: (-len(regs[r].day), r)) if r != FORGE][:1]
            for c in series:
                pts = list(zip(c.day, c.avg))
                rets = [math.log(b / a) for (d0, a), (d1, b) in zip(pts, pts[1:]) if d1 == d0 + 1]
                if len(rets) >= HISTORY_MIN_DAYS:
                    vol = clip(mad_sd(rets), VOL_MIN, VOL_MAX)
                    break
            # units per day:  every present day (no trade = 0), each day capped at UNITS_WINSOR x the median day
            traded = [units[d] for d in present if units[d] > 0]
            cap = UNITS_WINSOR * median(traded) if traded else 0
            upd = math.fsum(min(units[d], cap) for d in present) / N if N else 0.0
            # lot size:  median units per order (Forge rows, else all)
            src = [f] if f is not None and fdays >= HISTORY_MIN_DAYS else [regs[r] for r in sorted(regs)]
            lots = [v / n for c in src for v, n in zip(c.vol, c.orders) if n > 0]
            self.hist[t] = {'kind': kind, 'forgeDays': fdays, 'rows': rows, 'ref': ref, 'p10': p10, 'p90': p90,
                            'vol': vol, 'units': upd, 'active': fdays / N if N else 0.0,
                            'lot': median(lots) if lots else None,
                            'activeAll': len(traded) / N if N else 0.0}

    def hist_price(self, t, forge_only=False):
        hs = self.hist.get(t)
        if not hs or hs['kind'] == 'thin' or (forge_only and hs['kind'] != 'forge'):
            return None
        return hs['ref']

    def tech_key(self, t):
        return 1 if self.st.tech(t) <= 1 else 2

    def _markups(self):
        """median price / BOM cost of history types whose inputs all have history:  group, category, tech"""
        st = self.st
        by = collections.defaultdict(list)
        self.markup_samples = collections.defaultdict(list)
        for t in sorted(self.cls):
            if self.cls[t] not in (MFG_T1, MFG_T2, MFG_T3, COMPONENT):
                continue
            p = self.hist_price(t, forge_only=True)
            if p is None:
                continue
            c = st.bom_cost(t, lambda m: self.hist_price(m))
            if not c:
                continue
            k = self.tech_key(t)
            by[('g', st.group(t), k)].append(p / c)
            by[('c', st.cat(t), k)].append(p / c)
            by[('t', k)].append(p / c)
            self.markup_samples[(st.cat(t), k)].append((t, p / c))
        self.markup_by = {k: median(v) for k, v in by.items() if len(v) >= 3}

    def markup(self, g, c, k):
        for key in (('g', g, k), ('c', c, k), ('t', k)):
            if key in self.markup_by:
                return self.markup_by[key]
        return 1.1 if k == 1 else 1.5

    def _meta_ratios(self):
        """mktCalMeta:  median price and units ratio of meta variants to their parent, per (metaGroup, category)"""
        st = self.st
        pr, ur = collections.defaultdict(list), collections.defaultdict(list)
        self.named_fit = {}
        named = collections.defaultdict(list)
        for t in sorted(st.meta):
            parent, mg = st.meta[t]
            if t not in self.cls or parent == t or mg is None:
                continue
            p, q = self.hist_price(t, True), self.hist_price(parent, True)
            if p is None or q is None or q <= 0:
                continue
            c = st.cat(t)
            for key in ((mg, c), (mg, 0)):
                pr[key].append(p / q)
                ut, up = self.hist[t]['units'], self.hist[parent]['units']
                if ut > 0 and up > 0:
                    ur[key].append(ut / up)
            if mg == 1 and t in st.metalevel:
                named[st.group(t)].append((st.metalevel[t], math.log(p / q)))
        self.meta_rows = {}
        for key in sorted(pr):
            if len(pr[key]) >= 3:
                self.meta_rows[key] = (median(pr[key]), median(ur[key]) if ur[key] else 1.0)
        for g in sorted(named):
            pts = named[g]
            if len(pts) >= 4:
                fit = ols([x for x, _ in pts], [y for _, y in pts])
                if fit:
                    self.named_fit[g] = fit

    def meta_ratio(self, t):
        st = self.st
        parent, mg = st.meta[t]
        if mg == 1 and st.group(t) in self.named_fit and t in st.metalevel:
            a, b, _ = self.named_fit[st.group(t)]
            return clip(math.exp(a + b * st.metalevel[t]), NAMED_RATIO_MIN, NAMED_RATIO_MAX)
        for key in ((mg, st.cat(t)), (mg, 0)):
            if key in self.meta_rows:
                return self.meta_rows[key][0]
        return None

    def _group_price_ratios(self):
        """median refPrice / basePrice of history types, per group and per category (gap filling rule 6)"""
        st = self.st
        g, c = collections.defaultdict(list), collections.defaultdict(list)
        for t in sorted(self.cls):
            p, b = self.hist_price(t), st.base_price(t)
            if p is not None and b > 0:
                g[st.group(t)].append(p / b)
                c[st.cat(t)].append(p / b)
        self.base_ratio_g = {k: median(v) for k, v in g.items() if len(v) >= 3}
        self.base_ratio_c = {k: median(v) for k, v in c.items() if len(v) >= 3}

    # ------------------------------------------------------------ prices
    def _price_all(self):
        self.price = {}
        self._busy = set()
        for t in sorted(self.cls):
            self.resolve(t)

    def resolve(self, t):
        """(refPrice, priceSource, confidence) or None;  the first applicable rule wins (PLAN.md 4.5)"""
        if t in self.price:
            return self.price[t]
        if t in self._busy or t not in self.st.types:
            return None
        self._busy.add(t)
        try:
            r = self._rules(t)
        finally:
            self._busy.discard(t)
        self.price[t] = r
        return r

    def input_price(self, t):
        r = self.resolve(t)
        return r[0] if r else None

    def _rules(self, t):
        st = self.st
        cls = self.cls.get(t)
        hs = self.hist.get(t)
        N = max(1, self.N)
        base = st.base_price(t)
        if cls == VENDOR and base > 0:
            return (base, SRC_VENDOR, 1.0)
        if hs and hs['kind'] == 'forge':
            return (hs['ref'], SRC_HISTORY, 0.5 + 0.5 * min(1.0, hs['forgeDays'] / N))
        if hs and hs['kind'] == 'regions':
            return (hs['ref'], SRC_HISTORY, 0.3 + 0.2 * min(1.0, hs['rows'] / N))
        if t in st.bp and cls in (MFG_T1, MFG_T2, MFG_T3, COMPONENT, None):
            c = st.bom_cost(t, self.input_price)
            if c:
                k = self.tech_key(t)
                return (c * self.markup(st.group(t), st.cat(t), k), SRC_BOM, 0.5)
        if t in st.meta and st.meta[t][0] != t:
            ratio = self.meta_ratio(t)
            p = self.input_price(st.meta[t][0])
            if ratio and p:
                return (p * ratio, SRC_META, 0.35)
        refine = self.refine_value(t)
        if refine and cls in REFINE_CLASSES:
            return (refine, SRC_REFINE, 0.5)
        ratio = self.base_ratio_g.get(st.group(t), self.base_ratio_c.get(st.cat(t))) if base > 0 else None
        if hs and hs['ref']:
            # a few trades (< 5 days):  used unless they are more than THIN_MAX_DEX from the group-median estimate
            if not ratio or abs(math.log10(hs['ref'] / (base * ratio))) <= THIN_MAX_DEX:
                return (hs['ref'], SRC_HISTORY, 0.15)
        if ratio:
            return (base * ratio, SRC_GROUP, 0.2)
        if base > 0:
            return (base, SRC_BASE, 0.1)
        if hs and hs['ref']:
            return (hs['ref'], SRC_HISTORY, 0.1)
        if refine:
            return (refine, SRC_REFINE, 0.1)    # last resort:  what a reprocessor would pay
        return None

    def refine_value(self, t):
        """0.95 x the value of t's reprocessing output per unit, or None"""
        st = self.st
        if t in st.bp or t not in st.mats:
            return None
        val = 0.0
        for m, q in st.mats[t]:
            p = self.input_price(m)
            if not p:
                return None
            val += q * p
        return 0.95 * val / st.portion(t) if val > 0 else None

    # ------------------------------------------------------------ gap statistics and the type rows
    def _gap_stats(self):
        """medians over history types, per group / category / everything:  units, price, band, volatility, lot"""
        st = self.st
        acc = collections.defaultdict(lambda: collections.defaultdict(list))
        for t in sorted(self.cls):
            hs = self.hist.get(t)
            if not hs or hs['kind'] == 'thin' or not hs['ref']:
                continue
            for key in (('g', st.group(t)), ('c', st.cat(t)), ('all',)):
                a = acc[key]
                a['units'].append(hs['units'])
                a['price'].append(hs['ref'])
                a['p10'].append(hs['p10'] / hs['ref'])
                a['p90'].append(hs['p90'] / hs['ref'])
                if hs['vol'] is not None:
                    a['vol'].append(hs['vol'])
                if hs['lot'] is not None:
                    a['lot'].append(hs['lot'] / st.portion(t))
                a['active'].append(hs['active'])
                if hs['units'] > 0:
                    a['xy'].append((math.log10(hs['ref']), math.log10(hs['units'])))
        self.gap = {}
        for key in sorted(acc, key=str):
            a = acc[key]
            g = {k: median(v) for k, v in a.items() if k != 'xy' and v}
            fit = ols([x for x, _ in a['xy']], [y for _, y in a['xy']]) if len(a['xy']) >= 8 else None
            if fit:
                g['volA'], g['volB'], g['volResid'] = fit
            self.gap[key] = g

    def gapv(self, t, k):
        st = self.st
        for key in (('g', st.group(t)), ('c', st.cat(t)), ('all',)):
            g = self.gap.get(key)
            if g and k in g:
                return g[k]
        return None

    def vol_fit(self, g, c):
        for key in (('g', g), ('c', c), ('all',)):
            v = self.gap.get(key)
            if v and 'volB' in v:
                return v['volA'], v['volB'], v['volResid']
        return 0.0, 0.0, 0.0

    def _fill_types(self):
        st, km = self.st, self.km
        self.types = {}
        hull_rate = {t: n / km.days / self.coverage for t, n in km.hull.items()} if km.days else {}
        item_rate = {t: (a[0] + a[1]) / km.days / self.coverage for t, a in km.item_total.items()} if km.days else {}
        item_seen = {t: a[0] + a[1] for t, a in km.item_total.items()}
        self.km_consume = {}
        for t in sorted(self.cls):
            r = TypeCal(t)
            r.simClass = self.cls[t]
            res = self.price.get(t)
            hs = self.hist.get(t)
            if res:
                r.refPrice, r.priceSource, r.confidence = res
            elif r.simClass != EXCLUDED:
                r.simClass = NOT_SIM
            ref = r.refPrice
            if hs and hs['ref'] and r.priceSource == SRC_HISTORY:
                r.priceP10, r.priceP90 = hs['p10'], hs['p90']
            elif ref:
                r.priceP10 = ref * (self.gapv(t, 'p10') or 0.8)
                r.priceP90 = ref * (self.gapv(t, 'p90') or 1.25)
            if r.priceSource == SRC_VENDOR:
                r.priceP10 = r.priceP90 = ref
            r.priceP10, r.priceP90 = min(r.priceP10, ref), max(r.priceP90, ref)
            r.volatility = (hs['vol'] if hs and hs['vol'] is not None else self.gapv(t, 'vol')) or 0.1
            # the group's median units, scaled by price^volB:  the estimate for types that never traded in the window,
            # and a cap for thin ones (a single bulk trade would otherwise set the rate)
            med_u, med_p = self.gapv(t, 'units'), self.gapv(t, 'price')
            _, b, _ = self.vol_fit(st.group(t), st.cat(t))
            est = med_u * (ref / med_p) ** b if med_u and med_p and ref else None
            if hs:
                r.unitsPerDay, r.activeShare = hs['units'], hs['active']
                if hs['activeAll'] * self.N < HISTORY_MIN_DAYS and est is not None:
                    r.unitsPerDay = min(r.unitsPerDay, est)
            elif est is not None:
                r.unitsPerDay = est
                r.activeShare = 0.0
            ps = st.portion(t)
            lot = hs['lot'] if hs and hs['lot'] is not None else (self.gapv(t, 'lot') or 1.0) * ps
            r.lotSize = max(ps, round_half_up(lot / ps) * ps)
            c = st.bom_cost(t, self.input_price)
            r.buildCostRef = c or 0.0
            r.elasticity = self.elasticity(t)
            r.consumeShare = self._consume(t, r.unitsPerDay, hull_rate, item_rate, item_seen)
            self.types[t] = r
        # group default consumeShare for the types without killmail evidence (group median, else category default)
        gm = collections.defaultdict(list)
        for t, v in self.km_consume.items():
            gm[st.group(t)].append(v)
        self.group_consume = {g: median(v) for g, v in gm.items()}
        for t in sorted(self.types):
            r = self.types[t]
            if r.consumeShare is None:
                r.consumeShare = self.group_consume.get(st.group(t), CONSUME_DEFAULT.get(st.cat(t), 0.0))

    def elasticity(self, t):
        st = self.st
        cls, c = self.cls[t], st.cat(t)
        if cls in (MINED, REFINED, COMPONENT, MOON, PI, SALVAGE):
            return ELASTICITY_INTERMEDIATE
        if cls == FACTION or st.group(t) == GROUP_BOOSTER:
            return ELASTICITY_LUXURY
        return ELASTICITY.get(c, 1.0)

    def _consume(self, t, units, hull_rate, item_rate, item_seen):
        """final-consumption share of traded units;  None = use the group default (filled in afterwards)"""
        st = self.st
        cls, c, g = self.cls[t], st.cat(t), st.group(t)
        if cls in (MINED, REFINED, COMPONENT, MOON, PI, SALVAGE, EXCLUDED):
            return 0.0
        if c == CAT_CHARGE:
            return CONSUME_CHARGE
        if c in (CAT_SKILL, CAT_BLUEPRINT, CAT_REACTION) or g in (GROUP_BOOSTER, GROUP_LEASE, GROUP_FUEL_BLOCK):
            return CONSUME_USED_UP
        if c == CAT_COMMODITY:
            return CONSUME_TRADE_GOODS if cls == VENDOR else 0.0
        if c not in CONSUME_DEFAULT:
            return 0.0
        seen = self.km.hull.get(t, 0) + item_seen.get(t, 0)
        rate = hull_rate.get(t, 0.0) + item_rate.get(t, 0.0)
        if seen >= KILLMAIL_MIN_UNITS and units > 0:
            v = clip(rate / units, 0.01, 1.0)
            self.km_consume[t] = v
            return v
        return None

    # ------------------------------------------------------------ regions
    def _region_tables(self):
        st, h, N = self.st, self.h, max(1, self.N)
        tot_u = math.fsum(self.region_units.values())
        tot_i = math.fsum(self.region_isk.values())
        self.region_default = {}
        for r in sorted(self.region_units):
            self.region_default[r] = (self.region_ratio.get(r, self.region_ratio_default),
                                      self.region_units[r] / tot_u if tot_u else 0.0,
                                      self.region_isk[r] / tot_i if tot_i else 0.0)
        self.region_rows = {}
        type_units = collections.Counter()
        for (r, t), c in h.cells.items():
            type_units[t] += sum(c.vol)
        for (r, t) in sorted(h.cells):
            rt = self.types.get(t)
            if rt is None or rt.simClass in (EXCLUDED, NOT_SIM):
                continue
            c = h.cells[(r, t)]
            active = len(c.day) / N
            if active < REGION_MIN_ACTIVE and r not in HUBS:
                continue
            if r == FORGE:
                ratio = 1.0
            elif (r, t) in self.cell_ratio:
                ratio = self.cell_ratio[(r, t)]
            elif rt.refPrice > 0:
                ratio = median(c.avg) / rt.refPrice
            else:
                ratio = self.region_default[r][0]
            share = sum(c.vol) / type_units[t] if type_units[t] else 0.0
            self.region_rows[(r, t)] = (clip(ratio, RATIO_MIN, RATIO_MAX), share, active)

    # ------------------------------------------------------------ groups
    def _group_table(self):
        st, h = self.st, self.h
        groups = sorted(set(st.group(t) for t in self.types))
        # daily ISK per group / category / all (calendar index), for the weekday shape
        isk_g = collections.defaultdict(lambda: collections.Counter())
        isk_c = collections.defaultdict(lambda: collections.Counter())
        isk_all = collections.Counter()
        days_g = collections.defaultdict(set)
        for (r, t) in sorted(h.cells):
            if t not in self.types or self.types[t].simClass == EXCLUDED:
                continue
            c = h.cells[(r, t)]
            g, cat = st.group(t), st.cat(t)
            for d, a, v in zip(c.day, c.avg, c.vol):
                isk_g[g][d] += a * v
                isk_c[cat][d] += a * v
                isk_all[d] += a * v
                days_g[g].add(d)
        wd_all = self._weekday(isk_all) or [1.0] * 7
        wd_c = {c: self._weekday(s) for c, s in isk_c.items()}
        self.group_rows = {}
        self.weekday_source = collections.Counter()
        N = max(1, self.N)
        for g in groups:
            members = [t for t in self.types if st.group(t) == g]
            cat = self.st.groups[g]['categoryID']
            cls_count = collections.Counter(self.types[t].simClass for t in members)
            cls = sorted(cls_count.items(), key=lambda kv: (-kv[1], kv[0]))[0][0]
            theta, h_spread, cover = CLASS_SHAPE[cls]
            if cat == CAT_SHIP and cls not in (VENDOR, FACTION):
                h_spread = HALF_SPREAD_SHIP
            if cat == CAT_IMPLANT:
                h_spread = HALF_SPREAD_IMPLANT
            el = median([self.types[t].elasticity for t in members])
            cs = median([self.types[t].consumeShare for t in members])
            a, b, resid = self.vol_fit(g, cat)
            wd = None
            if len(days_g.get(g, ())) >= 0.9 * N:
                wd = self._weekday(isk_g[g])
                if wd:
                    self.weekday_source['group'] += 1
            if wd is None and wd_c.get(cat):
                wd = wd_c[cat]
                self.weekday_source['category'] += 1
            if wd is None:
                wd = wd_all
                self.weekday_source['all'] += 1
            self.group_rows[g] = (cat, el, theta, h_spread, cover, cs, a, b, resid,
                                  self.markup(g, cat, 1), self.markup(g, cat, 2)) + tuple(wd)

    def _weekday(self, series):
        """weekday multipliers (Mon..Sun, mean 1):  median of day / centred 7-day mean, over full weeks of data"""
        h = self.h
        present = set(self.present)
        f = collections.defaultdict(list)
        for d in self.present:
            win = list(range(d - 3, d + 4))
            if any(x not in present for x in win):
                continue
            m = math.fsum(series.get(x, 0.0) for x in win) / 7.0
            if m > 0:
                f[h.weekday(d)].append(series.get(d, 0.0) / m)
        if len(f) < 7 or min(len(v) for v in f.values()) < 5:
            return None
        w = [median(f[k]) for k in range(7)]
        s = math.fsum(w) / 7.0
        return [x / s for x in w] if s > 0 else None

    # ------------------------------------------------------------ killmails
    def _killmail_tables(self):
        st, km = self.st, self.km
        self.loss_rows = {}
        self.fit_rows = {}
        if not km.days:
            return
        for (hull, b) in sorted(km.losses):
            self.loss_rows[(hull, b)] = km.losses[(hull, b)] / km.days
        per_hull = collections.defaultdict(list)
        for (hull, t) in sorted(km.items):
            de, dr = km.items[(hull, t)]
            n = km.hull[hull]
            r = self.types.get(t)
            if n < FIT_MIN_LOSSES or r is None or r.simClass == EXCLUDED or de + dr <= 0:
                continue
            q = (de + dr) / n
            if q < FIT_MIN_QTY:
                continue
            per_hull[hull].append((-(q * r.refPrice), t, q, de / (de + dr)))
        for hull in sorted(per_hull):
            for _, t, q, ds in sorted(per_hull[hull])[:FIT_MAX_ITEMS]:
                self.fit_rows[(hull, t)] = (q, ds)
