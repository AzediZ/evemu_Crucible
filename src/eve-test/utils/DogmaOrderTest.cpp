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
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

#include "EVE_DogmaOrder.h"

namespace {
    int failures = 0;

    void Check(bool ok, const char* what)
    {
        ::printf("%s %s\n", (ok ? "  ok  " : "  FAIL"), what);
        if (!ok)
            ++failures;
    }

    bool Near(double a, double b, double eps = 1e-6)
    {
        return (std::fabs(a - b) <= eps * (1.0 + std::fabs(b)));
    }

    // a modifier of one item on itself, with the math the server applies (only the ops these cases use)
    enum Op { PreMul, ModAdd, ModSub, PostMul, PostDiv, PostPercent };
    struct Mod { Op op; uint16_t targ; uint16_t src; bool self; bool remove; };

    double Calc(Op op, double t, double s)
    {
        switch (op) {
            case PreMul:
            case PostMul:       return t * s;
            case PostDiv:       return t / s;
            case ModAdd:        return t + s;
            case ModSub:        return t - s;
            case PostPercent:   return t * (1.0 + s / 100.0);
        }
        return t;
    }

    // applies mods (in math order) to the item's attribs, either as listed or as DogmaOrder::Order says
    void Apply(const std::vector<Mod>& mods, std::map<uint16_t, double>& attr, bool ordered)
    {
        std::vector<DogmaOrder::Modifier> nodes;
        for (auto& m : mods) {
            DogmaOrder::Modifier n = DogmaOrder::Modifier();
            n.srcItem = 1; n.srcAttr = m.src; n.targAttr = m.targ; n.writesSrc = m.self; n.remove = m.remove;
            nodes.push_back(n);
        }
        std::vector<std::size_t> order;
        if (ordered) {
            order = DogmaOrder::Order(nodes);
        } else {
            for (std::size_t i = 0; i < mods.size(); ++i)
                order.push_back(i);
        }
        for (std::size_t i : order)
            attr[mods[i].targ] = Calc(mods[i].op, attr[mods[i].targ], attr[mods[i].src]);
    }

    bool IsPermutation(const std::vector<std::size_t>& order, std::size_t n)
    {
        if (order.size() != n)
            return false;
        std::vector<bool> seen(n, false);
        for (std::size_t i : order) {
            if ((i >= n) or seen[i])
                return false;
            seen[i] = true;
        }
        return true;
    }
}

int utils_DogmaOrderTest( int /*argc*/, char* /*argv*/[] )
{
    ::printf("\nDogma modifier order (calculator chains)\n");

    // attribute ids as in dgmAttributeTypes
    const uint16_t speedFactor(20), speedBoostFactor(567), calc(576), calc2(578);

    /* 10MN Afterburner II:  speedBoostFactorCalculator (710, PostMul x2) and speedBoostFactorCalculator2 (712, ModAdd), in math
     *  order the ModAdd comes first.  Crucible reads calc after its multipliers:  calc2 = 1 + 0.01 x 135 x 15,000,000 */
    std::vector<Mod> prop = {
        { ModAdd,  calc2, calc,             true, false },
        { PostMul, calc,  speedFactor,      true, false },
        { PostMul, calc,  speedBoostFactor, true, false } };
    std::map<uint16_t, double> ab = { { speedFactor, 135.0 }, { speedBoostFactor, 15000000.0 }, { calc, 0.01 }, { calc2, 1.0 } };
    std::map<uint16_t, double> abOld(ab);
    Apply(prop, abOld, false);
    Check(Near(abOld[calc2], 1.01), "math order alone:  calc2 = 1 + 0.01 (the +1% afterburner)");
    Apply(prop, ab, true);
    Check(Near(ab[calc], 20250000.0), "calc = 0.01 x speedFactor x thrust");
    Check(Near(ab[calc2], 20250001.0), "ordered:  calc2 = 1 + calc, as Crucible's dogma reads it");
    std::vector<DogmaOrder::Modifier> nodes;
    for (auto& m : prop) {
        DogmaOrder::Modifier n = DogmaOrder::Modifier();
        n.srcItem = 1; n.srcAttr = m.src; n.targAttr = m.targ; n.writesSrc = m.self; n.remove = m.remove;
        nodes.push_back(n);
    }
    std::vector<std::size_t> order(DogmaOrder::Order(nodes));
    Check((order.size() == 3) and (order[0] == 1) and (order[1] == 2) and (order[2] == 0), "the multipliers go first, in their own order");

    // undoing it (offline):  the math inverted, the reader comes off first, while calc still has the value it read
    std::vector<Mod> propOff = {
        { ModSub,  calc2, calc,             true, true },
        { PostDiv, calc,  speedFactor,      true, true },
        { PostDiv, calc,  speedBoostFactor, true, true } };
    Apply(propOff, ab, true);
    Check(Near(ab[calc], 0.01) and Near(ab[calc2], 1.0), "online then offline puts calc and calc2 back");
    // a remove whose writer comes first in math order is turned round
    std::vector<DogmaOrder::Modifier> rm(2, DogmaOrder::Modifier());
    rm[0].srcItem = 7; rm[0].srcAttr = 100; rm[0].targAttr = 200; rm[0].writesSrc = true;  rm[0].remove = true;
    rm[1].srcItem = 7; rm[1].srcAttr = 200; rm[1].targAttr = 300; rm[1].writesSrc = false; rm[1].remove = true;
    order = DogmaOrder::Order(rm);
    Check((order.size() == 2) and (order[0] == 1) and (order[1] == 0), "removing:  the reader before the writer");
    // adding the same:  writer first, which math order gives already
    rm[0].remove = rm[1].remove = false;
    order = DogmaOrder::Order(rm);
    Check((order[0] == 0) and (order[1] == 1), "adding:  the writer before the reader");
    // an add and a remove don't wait for each other
    rm[0].remove = true;
    order = DogmaOrder::Order(rm);
    Check((order[0] == 0) and (order[1] == 1), "an add and a remove keep their places");

    /* Prowler (covert transport) with Transport Ships V:  the hull's cloakingCpuNeedBonus 0.015 gets -50%
     *  (zColinTransportsCloakCpuBonus, PostPercent), the covert ops cloak's cpu 10000 is multiplied by it (covertOpsCpuBonus1,
     *  PostMul).  the ship is the source of both:  75 tf, not 150 */
    const uint16_t cloakCpuBonus(649), transportBonus(1361), cloakCpu(50);
    std::vector<Mod> cloak = {
        { PostMul,     cloakCpu,      cloakCpuBonus,  false, false },    // on the cloak, read from the ship
        { PostPercent, cloakCpuBonus, transportBonus, true,  false } };  // on the ship itself
    std::map<uint16_t, double> prowler = { { cloakCpuBonus, 0.015 }, { transportBonus, -50.0 }, { cloakCpu, 10000.0 } };
    std::map<uint16_t, double> prowlerOld(prowler);
    Apply(cloak, prowlerOld, false);
    Apply(cloak, prowler, true);
    Check(Near(prowlerOld[cloakCpu], 150.0), "math order alone:  covert ops cloak 150 tf on a Prowler");
    Check(Near(prowler[cloakCpu], 75.0), "ordered:  75 tf, as Crucible");

    // another item's attribute of the same id is no dependency
    std::vector<DogmaOrder::Modifier> other(2, DogmaOrder::Modifier());
    other[0].srcItem = 1; other[0].srcAttr = 5; other[0].targAttr = 6;
    other[1].srcItem = 2; other[1].srcAttr = 7; other[1].targAttr = 5; other[1].writesSrc = true;
    order = DogmaOrder::Order(other);
    Check((order[0] == 0) and (order[1] == 1), "a writer on another item changes nothing");
    // nor is a modifier that changes some other item's attribute
    other[1].srcItem = 1; other[1].writesSrc = false;
    order = DogmaOrder::Order(other);
    Check((order[0] == 0) and (order[1] == 1), "a modifier on another item (ship, group, owner) changes nothing");

    // a loop is broken, every modifier still goes once
    std::vector<DogmaOrder::Modifier> loop(3, DogmaOrder::Modifier());
    loop[0].srcItem = 1; loop[0].srcAttr = 10; loop[0].targAttr = 11; loop[0].writesSrc = true;
    loop[1].srcItem = 1; loop[1].srcAttr = 11; loop[1].targAttr = 10; loop[1].writesSrc = true;
    loop[2].srcItem = 1; loop[2].srcAttr = 11; loop[2].targAttr = 12; loop[2].writesSrc = true;
    order = DogmaOrder::Order(loop);
    Check(IsPermutation(order, 3), "a dependency loop:  each modifier once");
    Check(IsPermutation(DogmaOrder::Order(std::vector<DogmaOrder::Modifier>()), 0), "no modifiers");
    // a modifier reading the attribute it changes (x = x * y on itself) doesn't wait for itself
    std::vector<DogmaOrder::Modifier> selfRead(1, DogmaOrder::Modifier());
    selfRead[0].srcItem = 1; selfRead[0].srcAttr = 10; selfRead[0].targAttr = 10; selfRead[0].writesSrc = true;
    Check(IsPermutation(DogmaOrder::Order(selfRead), 1), "a modifier reading its own target");

    ::printf("%s\n", (failures ? "DogmaOrderTest FAILED" : "DogmaOrderTest passed"));
    return (failures ? 1 : 0);
}
