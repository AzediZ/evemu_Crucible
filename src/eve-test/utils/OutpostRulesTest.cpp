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
#include <cstdio>

#include "EVE_Outpost.h"

namespace {
    int failures = 0;

    void Check(bool ok, const char* what)
    {
        ::printf("%s %s\n", (ok ? "  ok  " : "  FAIL"), what);
        if (!ok)
            ++failures;
    }

    bool Near(double a, double b)
    {
        return std::fabs(a - b) < 1e-9;
    }
}

int utils_OutpostRulesTest( int /*argc*/, char* /*argv*/[] )
{
    using namespace OutpostRules;

    ::printf("outpost types:\n");
    Check(OutpostRace(AmarrFactoryOutpost) == Race::Amarr, "the amarr factory outpost is amarr");
    Check(OutpostRace(MinmatarServiceOutpost) == Race::Minmatar, "the minmatar service outpost is minmatar");
    Check(OutpostRace(12242) == Race::None, "the npc conquerable station takes no improvements");
    Check((BaseOfficeSlots(Race::Amarr) == 4) and (BaseOfficeSlots(Race::Caldari) == 8)
          and (BaseOfficeSlots(Race::Gallente) == 24) and (BaseOfficeSlots(Race::Minmatar) == 6), "base office slots 4/8/24/6");

    ::printf("upgrades:\n");
    Check(UpgradeRank(Rank1Upgrade) == 1 and UpgradeRank(Rank3Upgrade) == 3 and UpgradeRank(28081) == 0, "rank of the upgrade types");
    Check(CheckUpgrade(0, Rank1Upgrade) == UpgradeResult::Ok, "foundation on a new outpost");
    Check(CheckUpgrade(0, Rank2Upgrade) == UpgradeResult::MissingPrevious, "no pedestal without a foundation");
    Check(CheckUpgrade(1, Rank2Upgrade) == UpgradeResult::Ok, "pedestal after the foundation");
    Check(CheckUpgrade(1, Rank3Upgrade) == UpgradeResult::MissingPrevious, "no monument without a pedestal");
    Check(CheckUpgrade(2, Rank1Upgrade) == UpgradeResult::AlreadyInstalled, "a foundation only once");
    Check(CheckUpgrade(3, 28081) == UpgradeResult::NotAnUpgrade, "an improvement is not an upgrade");

    ::printf("improvement types:\n");
    Improvement imp = Improvement();
    Check(FindImprovement(28081, imp) and (imp.race == Race::Amarr) and (imp.kind == Kind::Factory) and (imp.tier == 1)
          and (imp.requiredTypeID == 0), "28081 Amarr Basic Outpost Factory");
    Check(FindImprovement(28086, imp) and (imp.kind == Kind::Factory) and (imp.tier == 2) and (imp.requiredTypeID == 28081),
          "28086 Amarr Outpost Factory needs the basic one");
    Check(FindImprovement(28076, imp) and (imp.kind == Kind::Factory) and (imp.tier == 3) and (imp.requiredTypeID == 28086),
          "28076 Amarr Advanced Outpost Factory needs the plain one");
    Check(FindImprovement(28098, imp) and (imp.race == Race::Caldari) and (imp.kind == Kind::ResearchFacility) and (imp.tier == 1),
          "28098 Caldari Basic Outpost Research Facility");
    Check(FindImprovement(28135, imp) and (imp.race == Race::Minmatar) and (imp.kind == Kind::Refinery) and (imp.tier == 2)
          and (imp.requiredTypeID == 28130), "28135 Minmatar Outpost Refinery");
    Check(FindImprovement(28109, imp) and (imp.race == Race::Gallente) and (imp.kind == Kind::Office) and (imp.tier == 3),
          "28109 Gallente Advanced Outpost Office");
    Check(!FindImprovement(28183, imp) and !FindImprovement(28136, imp), "upgrades and other types are no improvements");

    ::printf("installing:\n");
    Slots none = {{0, 0, 0, 0, 0, 0}};
    Check(CheckInstall(Race::Amarr, 0, none, 28081) == InstallResult::NoSlot, "no improvement without a foundation");
    Check(CheckInstall(Race::Amarr, 1, none, 28081) == InstallResult::Ok, "a basic factory with the foundation");
    Check(CheckInstall(Race::Caldari, 1, none, 28081) == InstallResult::WrongRace, "an amarr improvement in a caldari outpost");
    Check(CheckInstall(Race::Amarr, 1, none, 28183) == InstallResult::NotAnImprovement, "an upgrade is not an improvement");
    Slots oneBasic = ArrangeSlots({28081});
    Check(CheckInstall(Race::Amarr, 1, oneBasic, 28081) == InstallResult::AlreadyInstalled, "one of each");
    Check(CheckInstall(Race::Amarr, 1, oneBasic, 28084) == InstallResult::NoSlot, "the foundation holds one tier 1");
    Check(CheckInstall(Race::Amarr, 2, oneBasic, 28086) == InstallResult::Ok, "the pedestal takes a tier 2");
    Check(CheckInstall(Race::Amarr, 2, oneBasic, 28087) == InstallResult::MissingPrerequisite, "a plant needs the basic plant");
    Check(CheckInstall(Race::Amarr, 1, oneBasic, 28086) == InstallResult::NoSlot, "the foundation holds no tier 2");
    Slots level2 = ArrangeSlots({28081, 28086, 28084});
    Check(CheckInstall(Race::Amarr, 2, level2, 28076) == InstallResult::NoSlot, "the pedestal holds no tier 3");
    Check(CheckInstall(Race::Amarr, 3, level2, 28076) == InstallResult::Ok, "the monument takes the tier 3");
    Check(CheckInstall(Race::Amarr, 3, level2, 28089) == InstallResult::Ok, "and a second tier 2");
    Slots full = ArrangeSlots({28081, 28086, 28076, 28084, 28089, 28085});
    Check(CheckInstall(Race::Amarr, 3, full, 28082) == InstallResult::NoSlot, "three tier 1 at most");
    Check(CheckInstall(Race::Amarr, 3, full, 28090) == InstallResult::NoSlot, "two tier 2 at most");
    Check(CheckInstall(Race::Amarr, 3, full, 28079) == InstallResult::NoSlot, "one tier 3 at most");

    ::printf("slots:\n");
    Check((full[Tier1a] == 28081) and (full[Tier2a] == 28086) and (full[Tier3a] == 28076), "the tier 3 kind takes branch a");
    Check((full[Tier1b] == 28084) and (full[Tier2b] == 28089), "the other tier 2 kind takes branch b");
    Check(full[Tier1c] == 28085, "the last basic takes 1c");
    Slots order = ArrangeSlots({28084, 28081, 28086});
    Check((order[Tier1a] == 28081) and (order[Tier2a] == 28086) and (order[Tier1b] == 28084), "the tier 2 kind moves to branch a");

    ::printf("benefits:\n");
    Check(OfficeSlots(Race::Amarr, BaseOfficeSlots(Race::Amarr), ArrangeSlots({28084, 28089})) == 4 + 3 + 5, "amarr offices 4 + 3 + 5");
    Check(OfficeSlots(Race::Gallente, 24, ArrangeSlots({28114})) == 29, "gallente basic office +5");
    Check(Near(RefineryEfficiency(Race::Amarr, 0, 0.0), 0.0), "no refinery without the improvement");
    Check(Near(RefineryEfficiency(Race::Amarr, 1, 0.0), 0.10), "amarr basic refinery 10%");
    Check(Near(RefineryEfficiency(Race::Minmatar, 0, 0.35), 0.35), "minmatar base 35%");
    Check(Near(RefineryEfficiency(Race::Minmatar, 3, 0.35), 0.50), "minmatar advanced refinery 50%");
    Check(AddedServices(none) == 0, "no services without improvements");
    Check(AddedServices(ArrangeSlots({28085})) == (ServiceReprocessingPlant | ServiceRefinery), "a refinery adds reprocessing");
    Check(AddedServices(ArrangeSlots({28128})) == ServiceLaboratory, "a minmatar lab adds the laboratory");

    ::printf("assembly lines:\n");
    // ramInstallationTypeContents of the amarr factory outpost
    std::map<uint8_t, uint16_t> amarr = {{5, 1}, {7, 1}, {8, 1}, {31, 20}, {37, 10}};
    Check(AssemblyLines(Race::Amarr, amarr, none) == amarr, "no improvements, the type's own lines");
    std::map<uint8_t, uint16_t> f1 = AssemblyLines(Race::Amarr, amarr, ArrangeSlots({28081}));
    Check((f1.count(31) == 0) and (f1[43] == 25) and (f1[37] == 10), "basic factory:  25 'Factory1 Tier1' lines, boosters stay");
    std::map<uint8_t, uint16_t> f2p1 = AssemblyLines(Race::Amarr, amarr, ArrangeSlots({28081, 28086, 28082}));
    Check((f2p1.count(43) == 0) and (f2p1[56] == 20 + 5 + 7 + 3), "factory 2 and plant 1:  'Factory1(2) Tier2(1)', 35 lines");
    std::map<uint8_t, uint16_t> lab = AssemblyLines(Race::Amarr, amarr, ArrangeSlots({28083}));
    Check((lab[69] == 3) and (lab[71] == 3) and (lab[72] == 3) and (lab.count(5) == 0), "amarr basic lab:  3 each of ME/PE/copy");
    std::map<uint8_t, uint16_t> minmatar = {{6, 2}};
    std::map<uint8_t, uint16_t> mlab = AssemblyLines(Race::Minmatar, minmatar, ArrangeSlots({28128}));
    Check((mlab[138] == 2) and (mlab[127] == 2) and (mlab[128] == 2) and (mlab[6] == 2), "minmatar basic lab adds 2 each");
    std::map<uint8_t, uint16_t> mplant = AssemblyLines(Race::Minmatar, minmatar, ArrangeSlots({28127}));
    Check((mplant.count(6) == 0) and (mplant[124] == 5), "minmatar basic plant:  5 'Factory2 Tier1' lines");
    std::map<uint8_t, uint16_t> caldari = {{3, 15}, {6, 2}, {32, 10}, {33, 10}, {34, 10}, {37, 5}, {40, 10}};
    std::map<uint8_t, uint16_t> rf = AssemblyLines(Race::Caldari, caldari, ArrangeSlots({28098}));
    Check((rf.count(40) == 0) and (rf[90] == 15) and (rf[3] == 15), "caldari basic research facility:  15 invention lines");
    std::map<uint8_t, uint16_t> cf = AssemblyLines(Race::Caldari, caldari, ArrangeSlots({28096}));
    Check((cf.count(6) == 0) and (cf[70] == 5), "caldari basic factory:  5 'Factory Tier1' lines");
    Check(ManufacturingLine(Race::Gallente, 3, 2) == 103, "gallente 'Factory1(2) Tier3(2)'");

    ::printf("%s\n", (failures ? "OutpostRulesTest FAILED" : "OutpostRulesTest passed"));
    return failures;
}
