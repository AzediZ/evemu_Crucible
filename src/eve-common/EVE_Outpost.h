 /**
  * @name EVE_Outpost.h
  *     outpost upgrades and improvements (Dominion rules, unchanged in Crucible)
  *
  * these are pure functions, so the unit tests (eve-test) can check them
  *
  *  upgrades:  the Foundation, Pedestal and Monument Upgrade Platforms (group 835) build the 'Rank 1/2/3 Upgrade'
  *   (group 876, the platform's stationTypeID) into an outpost.  each one raises the outpost's upgradeLevel by one,
  *   in order (the platforms' descriptions:  "A Foundation outpost upgrade is a prerequisite for the installation of the
  *   Pedestal upgrade").  the level opens improvement slots:
  *    Foundation  "allows for the installation of 1 Tier 1 outpost improvement"
  *    Pedestal    "1 Tier 1 improvement and 1 Tier 2 improvement"
  *    Monument    "1 Tier 1 improvement, 1 Tier 2 improvement and 1 Tier 3 improvement"
  *   so level 1/2/3 holds 1/2/3 tier 1, 0/1/2 tier 2 and 0/0/1 tier 3 improvements:  the six slots the client's station
  *   management window reads (corpStationMgr.GetStationImprovements:  improvementTier1a/2a/3a/1b/2b/1c TypeID,
  *   base_stationmanagement.py GetRemoteInstalledImprovements).
  *
  *  improvements:  the race's Basic (tier 1), plain (tier 2) and Advanced (tier 3) Factory, Plant, Laboratory, Research
  *   Facility (Caldari), Refinery and Office platforms (group 836) build the 'Outpost Improvements' (group 872).
  *   the outpost's race must match (staImprovements.raceID, GetImprovementStaticData) and a tier needs the tier
  *   below it of the same kind (requiredImprovementTypeID).  what they give is in their descriptions (invTypes):
  *   extra factory/lab/office slots, assembly lines with better time multipliers, refinery efficiency.  the line types
  *   are CCP's (ramAssemblyLineTypes 31-138:  'Amarr Outpost Factory1(2) Tier1(2)' is a factory at tier 1 together
  *   with a plant at tier 2, their per-group multipliers are in ramAssemblyLineTypeDetailPerGroup/Category).
  *
  *  the outpost's own services, lines and refinery come from the static data (staOperationServices of its operation,
  *   ramInstallationTypeContents and staStationTypes.reprocessingEfficiency of its type).
  */


#ifndef EVE_OUTPOST_H
#define EVE_OUTPOST_H

#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <vector>


namespace OutpostRules {

    // the player outpost types.  the npc conquerable stations (12242, 12294, 12295) take no improvements
    //  (base_stationmanagement.ShouldDisplayImprovements)
    static constexpr uint16_t CaldariResearchOutpost        = 21642;
    static constexpr uint16_t AmarrFactoryOutpost           = 21644;
    static constexpr uint16_t GallenteAdministrativeOutpost = 21645;
    static constexpr uint16_t MinmatarServiceOutpost        = 21646;

    // chrRaces.raceID
    enum class Race : uint8_t {
        None     = 0,
        Caldari  = 1,
        Minmatar = 2,
        Amarr    = 4,
        Gallente = 8
    };

    inline Race OutpostRace(uint16_t stationTypeID)
    {
        switch (stationTypeID) {
            case CaldariResearchOutpost:        return Race::Caldari;
            case AmarrFactoryOutpost:           return Race::Amarr;
            case GallenteAdministrativeOutpost: return Race::Gallente;
            case MinmatarServiceOutpost:        return Race::Minmatar;
        }
        return Race::None;
    }

    /* the office slots a new outpost has.  staStationTypes lost its officeSlots column in this dump:  these are the era
     *  numbers (CCP TomB 'More details on Outposts' 2005-07-07, Revelations II raised the gallente one to 24;  the
     *  2010 outpost tables of the EVE wikis and Ten Ton Hammer agree:  amarr 4, caldari 8, gallente 24, minmatar 6)
     */
    inline uint8_t BaseOfficeSlots(Race race)
    {
        switch (race) {
            case Race::Amarr:       return 4;
            case Race::Caldari:     return 8;
            case Race::Gallente:    return 24;
            case Race::Minmatar:    return 6;
            default:                return 0;
        }
    }

    /* upgrades.  the Rank N Upgrade types are the platforms' stationTypeID */
    static constexpr uint16_t Rank1Upgrade = 28183;     // Foundation Upgrade Platform (27656)
    static constexpr uint16_t Rank2Upgrade = 28184;     // Pedestal Upgrade Platform (27658)
    static constexpr uint16_t Rank3Upgrade = 28185;     // Monument Upgrade Platform (27660)
    static constexpr uint8_t  MaxUpgradeLevel = 3;

    inline uint8_t UpgradeRank(uint16_t typeID)
    {
        if ((typeID < Rank1Upgrade) or (typeID > Rank3Upgrade))
            return 0;
        return (uint8_t)(typeID - Rank1Upgrade + 1);
    }

    enum class UpgradeResult : uint8_t {
        Ok,
        NotAnUpgrade,
        AlreadyInstalled,       // the outpost is at this rank or above
        MissingPrevious         // the rank below is not installed
    };

    inline UpgradeResult CheckUpgrade(uint8_t upgradeLevel, uint16_t upgradeTypeID)
    {
        uint8_t rank(UpgradeRank(upgradeTypeID));
        if (rank == 0)
            return UpgradeResult::NotAnUpgrade;
        if (upgradeLevel >= rank)
            return UpgradeResult::AlreadyInstalled;
        if (upgradeLevel + 1 < rank)
            return UpgradeResult::MissingPrevious;
        return UpgradeResult::Ok;
    }

    /* improvements */
    enum class Kind : uint8_t {
        Factory,
        Plant,
        Laboratory,
        ResearchFacility,       // Caldari only:  invention
        Refinery,
        Office
    };

    struct Improvement {
        uint16_t typeID;
        Race     race;
        Kind     kind;
        uint8_t  tier;          // 1 Basic, 2 (plain), 3 Advanced
        uint16_t requiredTypeID;    // the tier below, 0 for tier 1
    };

    /* the types run per race:  Advanced x5, Basic x5, plain x5 (28076-28135), in the race's kind order */
    inline bool FindImprovement(uint16_t typeID, Improvement& into)
    {
        static const Kind amarrOrder[5]   = {Kind::Factory, Kind::Plant, Kind::Laboratory, Kind::Office, Kind::Refinery};
        static const Kind caldariOrder[5] = {Kind::Factory, Kind::Laboratory, Kind::ResearchFacility, Kind::Office, Kind::Refinery};
        struct Block { uint16_t first; Race race; const Kind* order; };
        static const Block blocks[4] = {
            {28076, Race::Amarr,    amarrOrder},
            {28091, Race::Caldari,  caldariOrder},
            {28106, Race::Gallente, amarrOrder},
            {28121, Race::Minmatar, amarrOrder}
        };
        for (const Block& cur : blocks) {
            if ((typeID < cur.first) or (typeID >= cur.first + 15))
                continue;
            uint16_t offset(typeID - cur.first);
            uint8_t group(offset / 5), index(offset % 5);
            static const uint8_t tierOfGroup[3] = {3, 1, 2};
            into.typeID = typeID;
            into.race   = cur.race;
            into.kind   = cur.order[index];
            into.tier   = tierOfGroup[group];
            // Advanced needs the plain one (+10), the plain one needs the Basic one (-5)
            into.requiredTypeID = (into.tier == 3 ? typeID + 10 : (into.tier == 2 ? typeID - 5 : 0));
            return true;
        }
        return false;
    }

    // the client's slot order:  improvementTier1a, 2a, 3a, 1b, 2b, 1c
    enum Slot : uint8_t {
        Tier1a, Tier2a, Tier3a, Tier1b, Tier2b, Tier1c, SlotCount
    };
    typedef std::array<uint16_t, SlotCount> Slots;

    // how many improvements of a tier an upgrade level holds
    inline uint8_t MaxOfTier(uint8_t upgradeLevel, uint8_t tier)
    {
        if (upgradeLevel > MaxUpgradeLevel)
            upgradeLevel = MaxUpgradeLevel;
        if ((tier == 0) or (upgradeLevel < tier))
            return 0;
        return upgradeLevel - tier + 1;
    }

    enum class InstallResult : uint8_t {
        Ok,
        NotAnImprovement,
        WrongRace,
        AlreadyInstalled,
        MissingPrerequisite,    // the tier below of the same kind
        NoSlot                  // the upgrade level holds no more of this tier
    };

    inline bool IsInstalled(const Slots& slots, uint16_t typeID)
    {
        for (uint16_t cur : slots)
            if ((cur != 0) and (cur == typeID))
                return true;
        return false;
    }

    inline InstallResult CheckInstall(Race outpostRace, uint8_t upgradeLevel, const Slots& slots, uint16_t typeID)
    {
        Improvement imp = Improvement();
        if (!FindImprovement(typeID, imp))
            return InstallResult::NotAnImprovement;
        if ((outpostRace == Race::None) or (imp.race != outpostRace))
            return InstallResult::WrongRace;
        if (IsInstalled(slots, typeID))
            return InstallResult::AlreadyInstalled;
        if ((imp.requiredTypeID != 0) and !IsInstalled(slots, imp.requiredTypeID))
            return InstallResult::MissingPrerequisite;
        uint8_t count(0);
        Improvement cur = Improvement();
        for (uint16_t id : slots)
            if ((id != 0) and FindImprovement(id, cur) and (cur.tier == imp.tier))
                ++count;
        if (count >= MaxOfTier(upgradeLevel, imp.tier))
            return InstallResult::NoSlot;
        return InstallResult::Ok;
    }

    /* the slots the installed improvements take:  a branch is one kind, tier 1 up.  the kind with a tier 3 (else the
     *  first with a tier 2) takes branch a, another kind with a tier 2 branch b, the rest take the free tier 1 slots.
     *  any set CheckInstall let in fits.  ids that don't fit are left out
     */
    inline Slots ArrangeSlots(const std::vector<uint16_t>& typeIDs)
    {
        struct Chain { Kind kind; uint8_t top; uint16_t ids[3]; };
        std::vector<Chain> chains;
        Improvement imp = Improvement();
        for (uint16_t id : typeIDs) {
            if ((id == 0) or !FindImprovement(id, imp))
                continue;
            Chain* pChain(nullptr);
            for (Chain& cur : chains)
                if (cur.kind == imp.kind)
                    pChain = &cur;
            if (pChain == nullptr) {
                chains.push_back(Chain{imp.kind, 0, {0, 0, 0}});
                pChain = &chains.back();
            }
            pChain->ids[imp.tier - 1] = id;
            if (imp.tier > pChain->top)
                pChain->top = imp.tier;
        }

        // highest chain first (stable, so equal chains keep their order)
        std::vector<Chain> sorted;
        for (uint8_t top = 3; top > 0; --top)
            for (const Chain& cur : chains)
                if (cur.top == top)
                    sorted.push_back(cur);

        Slots slots = {{0, 0, 0, 0, 0, 0}};
        static const uint8_t branchSlots[3][3] = {
            {Tier1a, Tier2a, Tier3a},
            {Tier1b, Tier2b, SlotCount},
            {Tier1c, SlotCount, SlotCount}
        };
        for (uint8_t branch = 0; (branch < 3) and (branch < sorted.size()); ++branch)
            for (uint8_t tier = 0; tier < 3; ++tier)
                if ((sorted[branch].ids[tier] != 0) and (branchSlots[branch][tier] < SlotCount))
                    slots[branchSlots[branch][tier]] = sorted[branch].ids[tier];
        return slots;
    }

    // the highest tier of a kind among the installed improvements, 0 for none
    inline uint8_t TierOf(const Slots& slots, Kind kind)
    {
        uint8_t tier(0);
        Improvement imp = Improvement();
        for (uint16_t id : slots)
            if ((id != 0) and FindImprovement(id, imp) and (imp.kind == kind) and (imp.tier > tier))
                tier = imp.tier;
        return tier;
    }

    /* the extra slots one improvement gives (its description:  "Gives an additional five factory slots").  each
     *  installed tier adds its own, so a Basic and a plain Amarr factory give 5 + 7
     */
    inline uint8_t ExtraSlots(Race race, Kind kind, uint8_t tier)
    {
        if ((tier == 0) or (tier > 3))
            return 0;
        static const uint8_t three5_7[3]  = {3, 5, 7};
        static const uint8_t five7_9[3]   = {5, 7, 9};
        static const uint8_t two3_4[3]    = {2, 3, 4};
        static const uint8_t five5_9[3]   = {5, 5, 9};
        switch (kind) {
            case Kind::Factory:
                if (race == Race::Amarr)    return five7_9[tier - 1];
                if (race == Race::Caldari)  return three5_7[tier - 1];
                return 0;           // gallente and minmatar factories only shorten times
            case Kind::Plant:
                return three5_7[tier - 1];
            case Kind::Laboratory:  // each of ME, PE and copy
                return (race == Race::Caldari ? five5_9[tier - 1] : two3_4[tier - 1]);
            case Kind::ResearchFacility:
                return five7_9[tier - 1];
            case Kind::Office:
                return (race == Race::Gallente ? five7_9[tier - 1] : three5_7[tier - 1]);
            case Kind::Refinery:
                return 0;
        }
        return 0;
    }

    // the sum of the extra slots of every installed tier of a kind
    inline uint16_t ExtraSlotsInstalled(Race race, const Slots& slots, Kind kind)
    {
        uint16_t total(0);
        Improvement imp = Improvement();
        for (uint16_t id : slots)
            if ((id != 0) and FindImprovement(id, imp) and (imp.kind == kind))
                total += ExtraSlots(race, kind, imp.tier);
        return total;
    }

    /* the refinery efficiency (staStations.reprocessingEfficiency) with a refinery of this tier.  the minmatar outpost
     *  has its own (0.35, staStationTypes) that the improvements raise to 40/45/50%;  the others have none and a basic
     *  refinery is "installed ... with an efficiency of 10%", then 20% and 30%
     */
    inline double RefineryEfficiency(Race race, uint8_t tier, double baseEfficiency)
    {
        if ((tier == 0) or (tier > 3))
            return baseEfficiency;
        static const double minmatar[3] = {0.40, 0.45, 0.50};
        static const double others[3]   = {0.10, 0.20, 0.30};
        return (race == Race::Minmatar ? minmatar[tier - 1] : others[tier - 1]);
    }

    // staServices bits (Station:: in EVE_Station.h)
    static constexpr uint32_t ServiceReprocessingPlant = 16;
    static constexpr uint32_t ServiceRefinery          = 32;
    static constexpr uint32_t ServiceLaboratory        = 16384;

    // the services the improvements add to the outpost's operation's:  a refinery, and labs on the minmatar outpost
    inline uint32_t AddedServices(const Slots& slots)
    {
        uint32_t mask(0);
        if (TierOf(slots, Kind::Refinery) > 0)
            mask |= ServiceReprocessingPlant | ServiceRefinery;
        if ((TierOf(slots, Kind::Laboratory) > 0) or (TierOf(slots, Kind::ResearchFacility) > 0))
            mask |= ServiceLaboratory;
        return mask;
    }

    /* assembly line types (ramAssemblyLineTypes) */
    namespace Line {
        static constexpr uint8_t StationCopying     = 5;
        static constexpr uint8_t StationManufacture = 6;
        static constexpr uint8_t StationME          = 7;
        static constexpr uint8_t StationPE          = 8;
        static constexpr uint8_t AmarrManufacture   = 31;
        static constexpr uint8_t CaldariME          = 32;
        static constexpr uint8_t CaldariPE          = 33;
        static constexpr uint8_t CaldariCopying     = 34;
        static constexpr uint8_t CaldariInvention   = 40;
    }

    // the manufacturing line of a race's outpost with its factory (Factory1) and plant (Factory2) at these tiers
    inline uint8_t ManufacturingLine(Race race, uint8_t factoryTier, uint8_t plantTier)
    {
        if (factoryTier > 3)
            factoryTier = 3;
        if (plantTier > 3)
            plantTier = 3;
        static const uint8_t amarr[4][4] = {
            {31, 66, 67, 68},   // Amarr Outpost Manufacturing, Factory2 Tier1/2/3
            {43, 45, 47, 48},   // Factory1 Tier1, Factory1(2) Tier1(1/2/3)
            {54, 56, 57, 58},
            {59, 60, 61, 61}    // tier 3 and tier 3 can't be (one tier 3 slot)
        };
        static const uint8_t gallente[4][4] = {
            {6,   104, 105, 106},
            {93,  94,  95,  96},
            {137, 98,  99,  100},
            {136, 102, 103, 103}
        };
        static const uint8_t minmatar[4][4] = {
            {6,  124, 125, 126},
            {46, 116, 117, 118},
            {52, 119, 120, 121},
            {62, 122, 123, 123}
        };
        static const uint8_t caldari[4] = {6, 70, 79, 80};   // no plant
        switch (race) {
            case Race::Amarr:       return amarr[factoryTier][plantTier];
            case Race::Gallente:    return gallente[factoryTier][plantTier];
            case Race::Minmatar:    return minmatar[factoryTier][plantTier];
            case Race::Caldari:     return caldari[factoryTier];
            default:                return Line::StationManufacture;
        }
    }

    // the ME, PE and copying lines of a race's outpost with its laboratory at this tier (0:  the outpost's own)
    struct LabLines { uint8_t me, pe, copy; };
    inline LabLines LaboratoryLines(Race race, uint8_t tier)
    {
        if (tier > 3)
            tier = 3;
        static const LabLines amarr[4]    = {{7, 8, 5},   {69, 71, 72},   {73, 74, 75},   {76, 77, 78}};
        static const LabLines caldari[4]  = {{32, 33, 34}, {81, 82, 83},  {84, 85, 86},   {87, 88, 89}};
        static const LabLines gallente[4] = {{7, 8, 5},   {107, 108, 109}, {110, 111, 112}, {113, 114, 115}};
        static const LabLines minmatar[4] = {{7, 8, 5},   {138, 127, 128}, {129, 130, 131}, {132, 133, 134}};
        switch (race) {
            case Race::Amarr:       return amarr[tier];
            case Race::Caldari:     return caldari[tier];
            case Race::Gallente:    return gallente[tier];
            case Race::Minmatar:    return minmatar[tier];
            default:                return amarr[0];
        }
    }

    // the caldari invention line with its research facility at this tier
    inline uint8_t InventionLine(uint8_t tier)
    {
        static const uint8_t lines[4] = {Line::CaldariInvention, 90, 91, 92};
        return lines[(tier > 3 ? 3 : tier)];
    }

    /* the outpost's assembly lines (lineTypeID / count):  its type's own (ramInstallationTypeContents), with the lines
     *  the improvements upgrade retyped and the extra slots added.  lines nothing touches (boosters, reverse
     *  engineering, the gallente station invention line) stay as they are
     */
    inline std::map<uint8_t, uint16_t> AssemblyLines(Race race, const std::map<uint8_t, uint16_t>& base, const Slots& slots)
    {
        std::map<uint8_t, uint16_t> lines(base);
        // takes the count of the role's own line type (0 if the outpost has none) into the line type for the tier
        auto retype = [&lines](uint8_t from, uint8_t to, uint16_t extra) {
            uint16_t count(0);
            std::map<uint8_t, uint16_t>::iterator itr = lines.find(from);
            if (itr != lines.end()) {
                count = itr->second;
                lines.erase(itr);
            }
            count += extra;
            if (count > 0)
                lines[to] += count;
        };

        uint8_t factory(TierOf(slots, Kind::Factory)), plant(TierOf(slots, Kind::Plant));
        uint8_t mfgBase(race == Race::Amarr ? (uint8_t)Line::AmarrManufacture : (uint8_t)Line::StationManufacture);
        if ((factory > 0) or (plant > 0))
            retype(mfgBase, ManufacturingLine(race, factory, plant),
                   ExtraSlotsInstalled(race, slots, Kind::Factory) + ExtraSlotsInstalled(race, slots, Kind::Plant));

        uint8_t lab(TierOf(slots, Kind::Laboratory));
        if (lab > 0) {
            LabLines from(LaboratoryLines(race, 0)), to(LaboratoryLines(race, lab));
            uint16_t extra(ExtraSlotsInstalled(race, slots, Kind::Laboratory));
            retype(from.me, to.me, extra);
            retype(from.pe, to.pe, extra);
            retype(from.copy, to.copy, extra);
        }

        uint8_t research(TierOf(slots, Kind::ResearchFacility));
        if (research > 0)
            retype(Line::CaldariInvention, InventionLine(research), ExtraSlotsInstalled(race, slots, Kind::ResearchFacility));

        return lines;
    }

    // the outpost's office slots with these improvements
    inline uint16_t OfficeSlots(Race race, uint16_t baseSlots, const Slots& slots)
    {
        return baseSlots + ExtraSlotsInstalled(race, slots, Kind::Office);
    }
}


/* outpost sieges (Dominion rules, unchanged in Crucible).  the npc conquerable stations (12242, 12294, 12295) follow them too:
 *  their types have the same hit points and reinforcement attributes as the outposts.
 *   - an outpost whose owner's alliance holds sovereignty in its system can only be attacked while that claim is under
 *     blockade (SBUs online at more than half the stargates).  any other outpost can be attacked at any time, without SBUs
 *     ("An owner without sovereignty has a vulnerable Outpost.  The attacker does not need SBU(s)", CCP Abathur, 'Dominion -
 *     Storming the Gates', 2009).  if the blockade breaks "the siege is considered over" and it is invulnerable again
 *   - two reinforcement timers, only while the owner holds sovereignty there ("Stations only have reinforcement timers,
 *     when the station holder has sovereignty"):  at 25% shield it goes into reinforcement and comes out at 0% shield, at
 *     50% armor it goes into reinforcement again and comes out at 25% armor.  reinforced, it can't be shot or repaired
 *   - it comes out at the exit time the owner set in station management (staStations.exitTime, HHMM, 12:00 if none) once
 *     that time has come round again plus 48 hours (the type's reinforcementDuration), give or take 3 hours (its
 *     reinforcementVariance).  "2x 48 hour reinforcement timers to ensure at least 4 days between it switching hands"
 *   - at 0% structure "the ownership of the station changes over to the corporation, which the last player to damage the
 *     station belongs to" ('sovereignty for the uninitiated', A Mule in EvE, April 2011).  taking it doesn't give sovereignty
 *   - hit points:  100m shield, 150m armor, 100m structure (the types' data)
 */
namespace OutpostSiege {

    enum class Phase : uint8_t {
        Shield              = 0,    // the next reinforcement is at 25% shield
        ShieldReinforced    = 1,
        Armor               = 2,    // the next reinforcement is at 50% armor
        ArmorReinforced     = 3,
        Structure           = 4     // taken at 0% structure
    };

    static constexpr double ShieldReinforceAt   = 0.25;
    static constexpr double ShieldExit          = 0.0;
    static constexpr double ArmorReinforceAt    = 0.50;
    static constexpr double ArmorExit           = 0.25;
    // staStations.exitTime is NULL:  noon EVE time
    static constexpr int32_t DefaultExitTime    = 1200;

    inline bool IsReinforced(Phase phase)
    {
        return ((phase == Phase::ShieldReinforced) or (phase == Phase::ArmorReinforced));
    }

    // the owner's alliance (0 for none), the alliance holding sovereignty in the system (0 for none) and the blockade
    inline bool Vulnerable(uint32_t ownerAllianceID, uint32_t sovAllianceID, bool blockaded)
    {
        if ((ownerAllianceID == 0) or (ownerAllianceID != sovAllianceID))
            return true;
        return blockaded;
    }

    // reinforcement timers only for an owner holding sovereignty here
    inline bool HasTimers(uint32_t ownerAllianceID, uint32_t sovAllianceID)
    {
        return ((ownerAllianceID != 0) and (ownerAllianceID == sovAllianceID));
    }

    // the phase a hit leaves it in, from the layers' fractions left (0 to 1).  timers:  HasTimers()
    inline Phase AfterHit(Phase phase, bool timers, double shieldLeft, double armorLeft)
    {
        if (!timers)
            return phase;
        if ((phase == Phase::Shield) and (shieldLeft <= ShieldReinforceAt))
            return Phase::ShieldReinforced;
        if ((phase == Phase::Armor) and (armorLeft <= ArmorReinforceAt))
            return Phase::ArmorReinforced;
        return phase;
    }

    /* how far one hit may go, as fractions of the layer:  it stops at shieldFloor shield or armorFloor armor (the rest is
     *  lost), -1 for no stop.  it never destroys the station */
    struct HitLimits {
        double shieldFloor;
        double armorFloor;
    };
    inline HitLimits Limits(Phase phase, bool timers)
    {
        HitLimits limits = {-1.0, -1.0};
        if (!timers)
            return limits;
        if (phase == Phase::Shield)
            limits.shieldFloor = ShieldReinforceAt;
        else if (phase == Phase::Armor)
            limits.armorFloor = ArmorReinforceAt;
        return limits;
    }

    // the phase after a reinforcement ends
    inline Phase AfterReinforcement(Phase phase)
    {
        switch (phase) {
            case Phase::ShieldReinforced:   return Phase::Armor;
            case Phase::ArmorReinforced:    return Phase::Structure;
            default:                        return phase;
        }
    }

    /* the time a reinforcement ends, in seconds since midnight UTC of some day (now's day).  exitTime is HHMM (or < 0 for
     *  none), duration and variance in seconds, offset a random number in [-variance, variance]:
     *  the next exit time after now, plus the duration, plus the offset */
    inline int64_t ReinforcementEnd(int64_t nowSec, int32_t exitTime, int64_t durationSec, int64_t offsetSec)
    {
        static constexpr int64_t day(86400);
        if ((exitTime < 0) or (exitTime > 2359) or ((exitTime % 100) > 59))
            exitTime = DefaultExitTime;
        int64_t exitOfDay(((exitTime / 100) * 3600) + ((exitTime % 100) * 60));
        int64_t midnight(nowSec - (((nowSec % day) + day) % day));
        int64_t next(midnight + exitOfDay);
        if (next <= nowSec)
            next += day;
        return next + durationSec + offsetSec;
    }

    // the client's slim item structureState (spaceObject.Station.HandleStateChange shows the reinforcement effects)
    inline int8_t ClientState(Phase phase)
    {
        switch (phase) {
            case Phase::ShieldReinforced:   return 7;   // pos.STRUCTURE_SHIELD_REINFORCE
            case Phase::ArmorReinforced:    return 8;   // pos.STRUCTURE_ARMOR_REINFORCE
            default:                        return 4;   // pos.STRUCTURE_ONLINE
        }
    }

    /* passive shield regeneration over dt seconds, recharge time T seconds (shieldRechargeRate / 1000):
     *  C(t) = Cmax * (1 + e^(-5t/T) * (sqrt(C0/Cmax) - 1))^2, the curve the client draws from the damage state */
    inline double RechargedShield(double charge, double capacity, double rechargeSec, double dtSec)
    {
        if ((capacity <= 0.0) or (rechargeSec <= 0.0) or (dtSec <= 0.0))
            return charge;
        if (charge >= capacity)
            return capacity;
        double frac(charge > 0.0 ? charge / capacity : 0.0);
        double root(1.0 + std::exp(-5.0 * dtSec / rechargeSec) * (std::sqrt(frac) - 1.0));
        return capacity * root * root;
    }
}


#endif  // EVE_OUTPOST_H
