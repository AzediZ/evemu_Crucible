/*
    ------------------------------------------------------------------------------------
    LICENSE:
    ------------------------------------------------------------------------------------
    This file is part of EVEmu: EVE Online Server Emulator
    Copyright 2006 - 2021 The EVEmu Team
    For the latest information visit https://evemu.dev
    ------------------------------------------------------------------------------------
    This program is free software; you can redistribute it and/or modify it under
    the terms of the GNU Lesser General Public License as published by the Free Software
    Foundation; either version 2 of the License, or (at your option) any later
    version.

    This program is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License along with
    this program; if not, write to the Free Software Foundation, Inc., 59 Temple
    Place - Suite 330, Boston, MA 02111-1307, USA, or go to
    http://www.gnu.org/copyleft/lesser.txt.
    ------------------------------------------------------------------------------------
*/


#include "eve-test.h"

#include <cmath>
#include <cstdint>
#include <vector>

#include "EVE_Loot.h"

namespace {
    int failures = 0;

    void Check(bool ok, const char* what)
    {
        ::printf("%s %s\n", (ok ? "  ok  " : "  FAIL"), what);
        if (!ok)
            ++failures;
    }

    bool Near(double a, double b, double eps)
    {
        return (std::fabs(a - b) <= eps);
    }

    uint32_t QuantityOf(const std::vector<Loot::Drop>& drops, uint32_t typeID)
    {
        for (auto& cur : drops)
            if (cur.typeID == typeID)
                return cur.quantity;
        return 0;
    }

    Loot::Item MakeItem(uint32_t typeID, double chance, uint32_t minQty = 1, uint32_t maxQty = 1, uint8_t metaLevel = 0)
    {
        return Loot::Item{typeID, metaLevel, chance, minQty, maxQty};
    }
}

int npcai_LootRollTest( int /*argc*/, char* /*argv*/[] )
{
    ::printf("\nNPC loot rolls\n");
    using namespace Loot;

    // the rng:  mt19937's first output for its default seed is 3499211612 on every standard library
    {
        Rng rng;
        Check(Near(rng.Unit(), 3499211612.0 / 4294967296.0, 1e-12), "rng: default seed starts at mt19937's 3499211612");
        Rng a(1234), b(1234), c(4321);
        bool same(true), differ(false);
        for (int i = 0; i < 100; ++i) {
            int64_t va(a.Int(0, 1000)), vb(b.Int(0, 1000)), vc(c.Int(0, 1000));
            same = same and (va == vb);
            differ = differ or (va != vc);
        }
        Check(same, "rng: one seed, one sequence");
        Check(differ, "rng: another seed, another sequence");
    }

    // Int:  both ends included, nothing outside, a reversed range gives lo
    {
        Rng rng(7);
        bool inside(true), lo(false), hi(false);
        for (int i = 0; i < 10000; ++i) {
            int64_t v(rng.Int(-6, 5));
            inside = inside and (v >= -6) and (v <= 5);
            lo = lo or (v == -6);
            hi = hi or (v == 5);
        }
        Check(inside, "Int(-6, 5): never outside");
        Check(lo and hi, "Int(-6, 5): both ends come up");
        Check(rng.Int(9, 3) == 9, "Int(9, 3): a reversed range gives lo");
        Check(!rng.Chance(0.0) and rng.Chance(1.0), "Chance: 0 never, 1 always");
    }

    // the rolls:  entityEquipmentMin/Max.  a deadspace frigate (-6 .. 5) has an empty wreck 7 times in 12
    {
        Rng rng(11);
        const int n(120000);
        int zero(0), neg(0), over(0);
        for (int i = 0; i < n; ++i) {
            int32_t count(RollCount(rng, -6, 5));
            if (count == 0)
                ++zero;
            if (count < 0)
                ++neg;
            if (count > 5)
                ++over;
        }
        Check(neg == 0 and over == 0, "RollCount(-6, 5): 0 .. 5, never negative");
        Check(Near(static_cast<double>(zero) / n, 7.0 / 12.0, 0.01), "RollCount(-6, 5): no rolls 7 times in 12");
        Check(RollCount(rng, 1, 1) == 1, "RollCount(1, 1): an Angel Rogue always rolls once");
        Check(RollCount(rng, 0, 0) == 0, "RollCount(0, 0): no rolls");
        int32_t swapped(RollCount(rng, 8, 6));
        Check(swapped >= 6 and swapped <= 8, "RollCount(8, 6): a reversed range is swapped");
        Check(RollCount(rng, 5000, 5000) == MaxRolls, "RollCount: capped at MaxRolls");
    }

    // quantities
    {
        Rng rng(3);
        Check(RollQuantity(rng, 100, 100) == 100, "RollQuantity(100, 100): a stack of 100 T1 ammo");
        Check(RollQuantity(rng, 0, 0) == 1, "RollQuantity(0, 0): never under 1");
        bool one(false), seven(false), inside(true);
        for (int i = 0; i < 2000; ++i) {
            uint32_t q(RollQuantity(rng, 1, 7));
            one = one or (q == 1);
            seven = seven or (q == 7);
            inside = inside and (q >= 1) and (q <= 7);
        }
        Check(inside and one and seven, "RollQuantity(1, 7): 1 .. 7, both ends come up");
    }

    // weighted picks:  a group's share of the rolls is its groupDropChance over the pool's total
    {
        Rng rng(5);
        std::vector<double> w{0.0, 0.1, 0.3};
        int counts[3] = {0, 0, 0};
        for (int i = 0; i < 40000; ++i)
            ++counts[PickWeighted(rng, w)];
        Check(counts[0] == 0, "PickWeighted: weight 0 never comes up");
        Check(Near(static_cast<double>(counts[2]) / counts[1], 3.0, 0.15), "PickWeighted: 0.3 comes up 3 times as often as 0.1");
        std::vector<double> none{0.0, -1.0};
        Check(PickWeighted(rng, none) == none.size(), "PickWeighted: nothing to pick gives size()");
    }

    // the pool
    {
        std::vector<Item> ammo{MakeItem(178, 1.0, 100, 100)};
        std::vector<Item> empty;
        std::vector<Item> never{MakeItem(484, 0.0)};
        Params params{5, 5, 0};

        // a group without items (all unpublished) can't eat rolls
        std::vector<Group> pool{Group{87, 0.3, &ammo}, Group{118, 0.3, &empty}, Group{999, 0.3, nullptr}};
        Rng rng(1);
        std::vector<Drop> drops;
        Roll(rng, params, pool, drops);
        Check(drops.size() == 1 and QuantityOf(drops, 178) == 500, "Roll: 5 rolls of a sure ammo stack merge into one stack of 500");

        // an item with chance 0 never drops, and its rolls give nothing
        std::vector<Group> pool2{Group{7, 0.3, &never}};
        drops.clear();
        Roll(rng, params, pool2, drops);
        Check(drops.empty(), "Roll: chance 0 never drops");

        // no rolls, no loot
        drops.clear();
        Roll(rng, Params{-3, 0, 0}, pool, drops);
        Check(drops.empty(), "Roll: rolls -3 .. 0 is an empty wreck");

        // entityEquipmentGroupMax:  a rogue drone frigate's alloys stop at its group max
        drops.clear();
        Roll(rng, Params{24, 24, 20}, std::vector<Group>{Group{129, 0.3, &ammo}}, drops);
        Check(QuantityOf(drops, 178) == 2000, "Roll: groupMax 20 stops a group at 20 drops (24 rolls)");
        drops.clear();
        Roll(rng, Params{24, 24, 0}, std::vector<Group>{Group{129, 0.3, &ammo}}, drops);
        Check(QuantityOf(drops, 178) == 2400, "Roll: groupMax 0 is no cap");
    }

    // meta scaling:  a T1 item at 0.8 against a meta 4 item at 0.1 drops 8 times as often
    {
        std::vector<Item> weapons{MakeItem(484, 0.8, 1, 1, 0), MakeItem(8759, 0.1, 1, 1, 4)};
        std::vector<Group> pool{Group{7, 0.3, &weapons}};
        Rng rng(99);
        std::vector<Drop> drops;
        Roll(rng, Params{100000, 100000, 0}, pool, drops);
        uint32_t t1(QuantityOf(drops, 484)), meta4(QuantityOf(drops, 8759));
        // MaxRolls caps the rolls:  1024 rolls, half on each item
        Check(Near(static_cast<double>(t1) / meta4, 8.0, 2.5), "Roll: T1 (0.8) drops ~8x as often as meta 4 (0.1)");
        Check(t1 + meta4 <= static_cast<uint32_t>(MaxRolls), "Roll: never more drops than rolls");
    }

    // a seed gives the same loot
    {
        std::vector<Item> weapons{MakeItem(484, 0.8), MakeItem(8785, 0.6, 1, 1, 1), MakeItem(8759, 0.1, 1, 1, 4)};
        std::vector<Item> ammo{MakeItem(178, 0.25, 100, 100), MakeItem(179, 0.25, 100, 100)};
        std::vector<Item> faction{MakeItem(13773, 0.15)};
        std::vector<Group> pool{Group{7, 0.3, &weapons}, Group{87, 0.3, &ammo}, Group{46, 0.3, &faction}};
        Params params{2, 6, 0};
        bool same(true);
        for (uint32_t seed = 1; seed < 50; ++seed) {
            Rng a(seed), b(seed);
            std::vector<Drop> da, db;
            Roll(a, params, pool, da);
            Roll(b, params, pool, db);
            same = same and (da.size() == db.size());
            for (size_t i = 0; same and (i < da.size()); ++i)
                same = (da[i].typeID == db[i].typeID) and (da[i].quantity == db[i].quantity);
        }
        Check(same, "Roll: a seed gives the same drops");
        // the same on every compiler:  seed 42's first four kills (worked out under msvc;  gcc must agree)
        {
            const std::vector<std::vector<Drop>> golden{
                {{179, 100}},
                {{484, 1}, {178, 100}},
                {{8785, 1}, {179, 100}},
                {{178, 100}} };
            Rng g(42);
            bool match(true);
            for (auto& want : golden) {
                std::vector<Drop> got;
                Roll(g, params, pool, got);
                match = match and (got.size() == want.size());
                for (size_t i = 0; match and (i < got.size()); ++i)
                    match = (got[i].typeID == want[i].typeID) and (got[i].quantity == want[i].quantity);
            }
            Check(match, "Roll: seed 42 gives its known drops");
        }

        // the share of kills with nothing:  2 .. 6 rolls over this pool
        Rng rng(2024);
        int emptyWrecks(0);
        const int kills(20000);
        for (int i = 0; i < kills; ++i) {
            std::vector<Drop> drops;
            Roll(rng, params, pool, drops);
            if (drops.empty())
                ++emptyWrecks;
        }
        // a roll drops with (0.5 + 0.25 + 0.15) / 3 = 0.3.  P(empty) = mean over 2..6 rolls of 0.7^n
        double expect((std::pow(0.7, 2) + std::pow(0.7, 3) + std::pow(0.7, 4) + std::pow(0.7, 5) + std::pow(0.7, 6)) / 5.0);
        Check(Near(static_cast<double>(emptyWrecks) / kills, expect, 0.015), "Roll: empty wrecks come from the rolls and the chances");
    }

    ::printf("%s\n", (failures ? "LootRollTest FAILED" : "LootRollTest passed"));
    return (failures ? 1 : 0);
}
