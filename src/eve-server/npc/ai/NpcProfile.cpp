 /**
  * @name NpcProfile.cpp
  *   immutable per-type npc data, built once from the type's Crucible dogma (PLAN.md 6.2).  see NpcProfile.h
  */

#include "eve-server.h"

#include "EVEServerConfig.h"
#include "StaticDataMgr.h"
#include "inventory/AttributeEnum.h"
#include "inventory/InventoryItem.h"
#include "inventory/ItemFactory.h"
#include "inventory/ItemType.h"
#include "npc/ai/NpcMath.h"
#include "npc/ai/NpcProfile.h"

namespace {
    // ewar, support and other npc effects that decide what a type is (EVE_Effects.h names where there is one)
    const uint16 c_ewarEffects[] = {
        EVEEffectID::warpScrambleForEntity, EVEEffectID::modifyTargetSpeed2, EVEEffectID::entityTargetJam,
        EVEEffectID::entityCapacitorDrain, EVEEffectID::entityTrackingDisruptOld, EVEEffectID::entitySensorDampen,
        EVEEffectID::entityTargetPaint,
        3710,   // concordTargetJam
        EVEEffectID::concordWarpScramble, EVEEffectID::concordModifyTargetSpeed,
        4656    // NPCRemoteECM
    };
    const uint16 c_selfRepairEffects[] = {
        EVEEffectID::shieldBoostingForEntities, EVEEffectID::armorRepairForEntities,
        EVEEffectID::entityShieldBoostingSmall, EVEEffectID::entityShieldBoostingMedium, EVEEffectID::entityShieldBoostingLarge,
        EVEEffectID::entityArmorRepairingSmall, EVEEffectID::entityArmorRepairingMedium, EVEEffectID::entityArmorRepairingLarge
    };
    const uint16 c_remoteRepairEffects[] = { EVEEffectID::NPCRemoteArmorRepair, EVEEffectID::NPCRemoteShieldBoost };
    const uint16 c_groupAssistEffects[] = {
        EVEEffectID::NPCGroupShieldAssist, EVEEffectID::NPCGroupSpeedAssist, EVEEffectID::NPCGroupPropJamAssist,
        EVEEffectID::NPCGroupArmorAssist
    };

    /* a turret cycle this long is the data's placeholder for "no guns" (speed 20,000,000 = 5.5 hours on ~95 sentries and
     *  overseer structures, 15,000,000 on one;  real npc cycles are under 20s):  missile-only (PLAN.md 4.11) */
    const uint32 c_placeholderTurretCycleMs = 10000000;

    std::mutex s_cacheMutex;
    std::unordered_map<uint16, std::shared_ptr<const NpcProfile>> s_cache;

    // the type's value, or dogma's default when the type doesn't have the attribute
    double Attr(const ItemType& type, uint16 attrID)
    {
        if (type.HasAttribute(attrID))
            return type.GetAttribute(attrID).get_double();
        return sDataMgr.GetAttributeDefault(attrID);
    }

    bool IsSentryGroup(uint16 groupID)
    {
        switch (groupID) {
            case EVEDB::invGroups::Sentry_Gun:
            case EVEDB::invGroups::Protective_Sentry_Gun:
            case EVEDB::invGroups::Destructible_Sentry_Gun:
            case EVEDB::invGroups::Mobile_Sentry_Gun:
            case EVEDB::invGroups::Deadspace_Overseer_s_Sentry:
                return true;
        }
        return false;
    }

    /* self-repair (5.9):  the first of these effects the type has, and the attribute holding its per-cycle delay
     *  ("skip") chance and that chance's multiplier (0 = none).  876 and 878 have no activation attribute in dgmEffects;
     *  by convention they use the generic 639/638 */
    struct RepairEffect { uint16 effectID; uint16 chanceAttr; uint16 multAttr; };
    const RepairEffect c_shieldRepair[] = {
        { EVEEffectID::entityShieldBoostingSmall,  AttrEntityShieldBoostDelayChanceSmall,  AttrEntityShieldBoostDelayChanceSmallMultiplier },
        { EVEEffectID::entityShieldBoostingMedium, AttrEntityShieldBoostDelayChanceMedium, AttrEntityShieldBoostDelayChanceMediumMultiplier },
        { EVEEffectID::entityShieldBoostingLarge,  AttrEntityShieldBoostDelayChanceLarge,  AttrEntityShieldBoostDelayChanceLargeMultiplier },
        { EVEEffectID::shieldBoostingForEntities,  AttrEntityShieldBoostDelayChance,       0 }
    };
    const RepairEffect c_armorRepair[] = {
        { EVEEffectID::entityArmorRepairingSmall,  AttrEntityArmorRepairDelayChanceSmall,  AttrEntityArmorRepairDelayChanceSmallMultiplier },
        { EVEEffectID::entityArmorRepairingMedium, AttrEntityArmorRepairDelayChanceMedium, AttrEntityArmorRepairDelayChanceMediumMultiplier },
        { EVEEffectID::entityArmorRepairingLarge,  AttrEntityArmorRepairDelayChanceLarge,  AttrEntityArmorRepairDelayChanceLargeMultiplier },
        { EVEEffectID::armorRepairForEntities,     AttrEntityArmorRepairDelayChance,       0 }
    };

    // today's lock time by size, for types without scanResolution and scanSpeed
    double RadiusLockTimeMs(float radius)
    {
        if (radius < 30)
            return 1500;
        if (radius < 60)
            return 2500;
        if (radius < 150)
            return 4000;
        if (radius < 280)
            return 6000;
        if (radius < 550)
            return 8000;
        return 13000;
    }
}

const char* NpcAI::GetTierName(uint8 tier)
{
    switch (tier) {
        case Tier::Standard:    return "Standard";
        case Tier::LevelOne:    return "LevelOne";
        case Tier::Passive:     return "Passive";
        case Tier::Inert:       return "Inert";
        case Tier::Sentry:      return "Sentry";
        case Tier::Law:         return "Law";
    }
    return "Unknown";
}

const char* NpcAI::GetProblemText(uint32 problemBit)
{
    switch (problemBit) {
        case Problem::TurretNoDamage:       return "effect 10 but zero damage -> turret disabled";
        case Problem::DamageNoTurretEffect: return "damage attributes but no effect 10 -> no turret";
        case Problem::MissileAttrsNoEffect: return "missileLaunchDuration 1 and entityMissileTypeID but no effect 569 -> no missiles (correct)";
        case Problem::MissileNoDuration:    return "effect 569 without missileLaunchDuration -> 20s default";
        case Problem::NoScanResolution:     return "missing scanResolution -> scanSpeed fallback";
        case Problem::NoLockData:           return "missing scanResolution and scanSpeed -> radius table";
        case Problem::FlyRangeZero:         return "entityFlyRange 0 -> orbit at min(maxRange, entityAttackRange)";
        case Problem::TurretTooFast:        return "effect 10 with a turret cycle under 500ms -> turret disabled";
        case Problem::AttackRangeZero:      return "weapons but entityAttackRange 0 -> dgm default";
        case Problem::MissileTypeMissing:   return "effect 569 but no usable missile type -> no missiles";
        case Problem::L1LevelTwo:           return "AI_ShouldUseTargetSwitching 2 -> treated as 1";
        case Problem::NoTracking:           return "turret without trackingSpeed -> hits only targets without angular velocity";
        case Problem::MissileCycleTooShort: return "effect 569 with missileLaunchDuration under 1s -> 20s default";
        case Problem::RangeBeyondAttack:    return "entityFlyRange beyond 0.9 x entityAttackRange -> orbit kept inside it";
        case Problem::SentryNoRange:        return "sentry turret without maxRange -> entityAttackRange as optimal";
        case Problem::MissileNoEffect:      return "missileLaunchDuration and entityMissileTypeID but no effect 569 -> no missiles (they fired without RequireWeaponEffects)";
        case Problem::TurretPlaceholder:    return "effect 10 with the placeholder speed 20,000,000 -> no turret (missile-only, correct)";
    }
    return "unknown";
}

std::string NpcProfile::ProblemList(uint32 problems)
{
    std::string list;
    for (uint32 i = 0; i < NpcAI::Problem::Count; ++i) {
        uint32 bit(1u << i);
        if ((problems & bit) == 0)
            continue;
        if (!list.empty())
            list += ";  ";
        list += NpcAI::GetProblemText(bit);
    }
    return list;
}

double NpcProfile::MissileFireRange() const
{
    if (!hasMissile)
        return 0.0;
    double range(attackRange);
    if (missileRange > 0.0)
        range = std::min(range, 0.95 * missileRange);
    return range;
}

double NpcProfile::LockTimeMs(double sig, double scanResMult/*1.0*/) const
{
    if (scanResMult <= 0.0)
        scanResMult = 1.0;
    switch (lockFrom) {
        case NpcAI::LockFrom::ScanResolution:
            return NpcMath::LockTimeMs(scanResolution * scanResMult, sig);
        case NpcAI::LockFrom::ScanSpeed:
            // a dampened scan speed takes longer
            return std::min(180000.0, std::max(1000.0, scanSpeed / scanResMult));
    }
    return RadiusLockTimeMs(radius);
}

double NpcProfile::ReactionDelayMs(double u, double scale/*1.0*/) const
{
    return NpcMath::ReactionDelayMs(attackDelayMin, attackDelayMax, reactionFactor, scale, u);
}

std::shared_ptr<const NpcProfile> NpcProfile::Get(uint16 typeID)
{
    {
        std::lock_guard<std::mutex> lock(s_cacheMutex);
        auto itr = s_cache.find(typeID);
        if (itr != s_cache.end())
            return itr->second;
    }
    const ItemType* pType(sItemFactory.GetType(typeID));
    if (pType == nullptr)
        return nullptr;
    return Get(*pType);
}

std::shared_ptr<const NpcProfile> NpcProfile::Get(const ItemType& type)
{
    std::lock_guard<std::mutex> lock(s_cacheMutex);
    auto itr = s_cache.find(type.id());
    if (itr != s_cache.end())
        return itr->second;
    std::shared_ptr<const NpcProfile> profile(Build(type, true));
    s_cache.emplace(type.id(), profile);
    return profile;
}

std::shared_ptr<const NpcProfile> NpcProfile::Build(const ItemType& type, bool log)
{
    std::shared_ptr<NpcProfile> p(std::make_shared<NpcProfile>());
    NpcProfile& r(*p);
    r.typeID = type.id();
    r.groupID = type.groupID();
    r.categoryID = type.categoryID();
    r.name = type.name();
    r.problems = 0;
    bool requireEffects(sConfig.npc.RequireWeaponEffects);

    /* turret */
    r.turretCycleMs = static_cast<uint32>(EvE::max(Attr(type, AttrSpeed)));
    r.damageMultiplier = static_cast<float>(Attr(type, AttrDamageMultiplier));
    r.turretDamage = static_cast<float>(Attr(type, AttrEmDamage) + Attr(type, AttrExplosiveDamage)
                                      + Attr(type, AttrKineticDamage) + Attr(type, AttrThermalDamage));
    r.optimal = static_cast<float>(Attr(type, AttrMaxRange));
    r.falloff = static_cast<float>(Attr(type, AttrFalloff));
    r.tracking = static_cast<float>(Attr(type, AttrTrackingSpeed));
    r.sigResolution = static_cast<float>(Attr(type, AttrOptimalSigRadius));
    bool turretEffect(type.HasEffect(EVEEffectID::targetAttack));
    if (r.turretCycleMs >= c_placeholderTurretCycleMs) {
        // no guns (whatever the effects and damage say).  the missiles below are its weapon
        r.problems |= NpcAI::Problem::TurretPlaceholder;
        r.hasTurret = false;
    } else if (requireEffects) {
        r.hasTurret = turretEffect;
        if (turretEffect and (r.turretDamage <= 0.0f)) {
            r.problems |= NpcAI::Problem::TurretNoDamage;
            r.hasTurret = false;
        }
        if (turretEffect and (r.turretCycleMs < 500)) {
            r.problems |= NpcAI::Problem::TurretTooFast;
            r.hasTurret = false;
        }
        if (!turretEffect and (r.turretDamage > 0.0f))
            r.problems |= NpcAI::Problem::DamageNoTurretEffect;
    } else {
        // the old gating:  anything with a cycle time shoots
        r.hasTurret = (r.turretCycleMs > 0);
    }
    if (r.hasTurret and (r.tracking <= 0.0f))
        r.problems |= NpcAI::Problem::NoTracking;

    /* missiles */
    r.missileTypeID = static_cast<uint16>(EvE::max(Attr(type, AttrEntityMissileTypeID)));
    r.missileDamageMultiplier = static_cast<float>(Attr(type, AttrMissileDamageMultiplier));
    bool missileEffect(type.HasEffect(EVEEffectID::missileLaunchingForEntity));
    bool hasDuration(type.HasAttribute(AttrMissileLaunchDuration));
    r.missileCycleMs = static_cast<uint32>(EvE::max(Attr(type, AttrMissileLaunchDuration)));
    if (requireEffects) {
        r.hasMissile = missileEffect;
        if (missileEffect and !hasDuration)
            r.problems |= NpcAI::Problem::MissileNoDuration;    // the dgm default (20s) is already in missileCycleMs
        if (!missileEffect and hasDuration and (r.missileTypeID > 0))
            r.problems |= ((r.missileCycleMs == 1) ? NpcAI::Problem::MissileAttrsNoEffect : NpcAI::Problem::MissileNoEffect);
    } else {
        r.hasMissile = (r.missileCycleMs > 100) and (r.missileTypeID > 0);
    }
    if (r.missileCycleMs < 1000) {
        // the placeholder 1 (or nonsense):  dogma's default
        if (r.hasMissile)
            r.problems |= NpcAI::Problem::MissileCycleTooShort;
        r.missileCycleMs = static_cast<uint32>(sDataMgr.GetAttributeDefault(AttrMissileLaunchDuration));
        if (r.missileCycleMs < 1000)
            r.missileCycleMs = 20000;
    }
    r.missileRange = 0.0;
    if (r.hasMissile) {
        const ItemType* pMissile((r.missileTypeID > 0) ? sItemFactory.GetType(r.missileTypeID) : nullptr);
        if (pMissile == nullptr) {
            r.problems |= NpcAI::Problem::MissileTypeMissing;
            r.hasMissile = false;
        } else {
            // LaunchEntityMissile() multiplies velocity by 645 and flight time by 646, Missile() flight time by rates.missileTime
            double velocity(Attr(*pMissile, AttrMaxVelocity) * Attr(type, AttrMissileEntityVelocityMultiplier));
            double flight(Attr(*pMissile, AttrExplosionDelay) * Attr(type, AttrMissileEntityFlightTimeMultiplier) * sConfig.rates.missileTime);
            r.missileRange = NpcMath::MissileRange(velocity, flight);
        }
    }

    /* ranges and movement */
    r.attackRange = static_cast<float>(Attr(type, AttrEntityAttackRange));
    if (r.HasWeapons() and (r.attackRange <= 0.0f)) {
        r.problems |= NpcAI::Problem::AttackRangeZero;
        r.attackRange = static_cast<float>(sDataMgr.GetAttributeDefault(AttrEntityAttackRange));
    }
    r.flyRange = static_cast<float>(Attr(type, AttrEntityFlyRange));
    r.orbitRange = r.flyRange;
    if (r.orbitRange <= 0.0f) {
        if (r.HasWeapons())
            r.problems |= NpcAI::Problem::FlyRangeZero;
        r.orbitRange = std::min(r.optimal, r.attackRange);
        if (r.orbitRange <= 0.0f)
            r.orbitRange = 1000.0f;
    }
    /* weapons fire only inside entityAttackRange (B7).  a few armed types (dire guristas despoilers, some mission and
     *  incursion frigates) orbit beyond it and would never shoot:  their orbit is kept inside 90% of it */
    double fireRange(r.hasTurret ? r.attackRange : r.MissileFireRange());
    if (r.HasWeapons() and (fireRange > 0.0) and (r.orbitRange > 0.9 * fireRange)) {
        r.problems |= NpcAI::Problem::RangeBeyondAttack;
        r.orbitRange = static_cast<float>(0.9 * fireRange);
    }
    r.chaseMaxDistance = static_cast<float>(Attr(type, AttrEntityChaseMaxDistance));
    r.boostRange = std::max(r.chaseMaxDistance, r.orbitRange);
    r.maxVelocity = static_cast<float>(Attr(type, AttrMaxVelocity));
    r.cruiseSpeed = static_cast<float>(Attr(type, AttrEntityCruiseSpeed));
    if (r.cruiseSpeed <= 0.0f)
        r.cruiseSpeed = r.maxVelocity;
    r.wanderRange = static_cast<float>(Attr(type, AttrEntityMaxWanderRange));
    r.assistRange = static_cast<float>(Attr(type, AttrNpcAssistanceRange));
    r.sigRadius = static_cast<float>(Attr(type, AttrSignatureRadius));
    r.radius = type.radius();
    r.sigBloom = static_cast<float>(Attr(type, AttrEntityMaxVelocitySignatureRadiusMultiplier));
    // R_acq (5.4):  no era number;  frigates' entityAttackRange (~3km) alone can't be how far they see
    r.acquireRange = static_cast<uint32>(std::max(static_cast<double>(r.attackRange), static_cast<double>(sConfig.npc.AcquireRange)));

    /* targeting */
    double maxAttack(Attr(type, AttrMaxAttackTargets));
    r.maxAttackTargets = static_cast<uint8>(std::min(255.0, std::max(1.0, maxAttack)));
    double maxLocked(Attr(type, AttrMaxLockedTargets));
    r.maxLockedTargets = static_cast<uint8>(std::min(255.0, (maxLocked >= 1.0 ? maxLocked : static_cast<double>(r.maxAttackTargets))));
    r.scanResolution = static_cast<float>(Attr(type, AttrScanResolution));
    r.scanSpeed = static_cast<float>(Attr(type, AttrScanSpeed));
    if (r.scanResolution > 0.0f) {
        r.lockFrom = NpcAI::LockFrom::ScanResolution;
    } else if (r.scanSpeed > 0.0f) {
        r.lockFrom = NpcAI::LockFrom::ScanSpeed;
        if (r.HasWeapons())
            r.problems |= NpcAI::Problem::NoScanResolution;
    } else {
        r.lockFrom = NpcAI::LockFrom::RadiusTable;
        if (r.HasWeapons())
            r.problems |= NpcAI::Problem::NoLockData;
    }

    /* timings */
    r.attackDelayMin = static_cast<float>(Attr(type, AttrEntityAttackDelayMin));
    r.attackDelayMax = static_cast<float>(Attr(type, AttrEntityAttackDelayMax));
    r.reactionFactor = static_cast<float>(Attr(type, AttrEntityReactionFactor));
    r.chaseMaxDelay = static_cast<float>(Attr(type, AttrEntityChaseMaxDelay));
    r.chaseMaxDelayChance = static_cast<float>(Attr(type, AttrEntityChaseMaxDelayChance));
    r.chaseMaxDuration = static_cast<float>(Attr(type, AttrEntityChaseMaxDuration));
    r.chaseMaxDurationChance = static_cast<float>(Attr(type, AttrEntityChaseMaxDurationChance));

    /* level one.  "when disabled, 1651, 1652 and 1650 are disabled also" (1648's description) */
    double l1(Attr(type, AttrAI_ShouldUseTargetSwitching));
    r.l1TargetSwitching = (l1 > 0.0 ? 1 : 0);
    if (l1 > 1.0)
        r.problems |= NpcAI::Problem::L1LevelTwo;
    r.l1SecondaryTarget = (Attr(type, AttrAI_ShouldUseSecondaryTarget) > 0.0);
    r.l1SignatureRadius = r.IsLevelOne() and (Attr(type, AttrAI_ShouldUseSignatureRadius) > 0.0);
    r.l1EffectMultiplier = r.IsLevelOne() and (Attr(type, AttrAI_ShouldUseEffectMultiplier) > 0.0);
    r.l1ChanceToNotSwitch = (r.IsLevelOne() ? static_cast<float>(Attr(type, AttrAI_ChanceToNotTargetSwitch)) : 0.0f);
    r.l1PreferredSig = static_cast<float>(Attr(type, AttrAI_PreferredSignatureRadius));
    r.l1TankingDrone = static_cast<float>(Attr(type, AttrAI_TankingModifierDrone));
    r.l1TankingShip = static_cast<float>(Attr(type, AttrAI_TankingModifier));
    r.l1SwitchTimerMs = static_cast<uint32>(EvE::max(Attr(type, AttrTargetSwitchTimer)));
    r.evasive = static_cast<float>(Attr(type, AttrEvasiveManeuverLevel));
    switch (r.groupID) {
        case EVEDB::invGroups::Deadspace_Sleeper_Sleepless_Sentinel:
        case EVEDB::invGroups::Deadspace_Sleeper_Awakened_Sentinel:
        case EVEDB::invGroups::Deadspace_Sleeper_Emergent_Sentinel:
        case EVEDB::invGroups::Deadspace_Sleeper_Sleepless_Defender:
        case EVEDB::invGroups::Deadspace_Sleeper_Sleepless_Patroller:
        case EVEDB::invGroups::Deadspace_Sleeper_Awakened_Defender:
        case EVEDB::invGroups::Deadspace_Sleeper_Awakened_Patroller:
        case EVEDB::invGroups::Deadspace_Sleeper_Emergent_Defender:
        case EVEDB::invGroups::Deadspace_Sleeper_Emergent_Patroller:
            r.l1Family = NpcAI::L1Family::Sleeper;
            break;
        case EVEDB::invGroups::Incursion_Sanshas_Nation_Capital:
        case EVEDB::invGroups::Incursion_Sanshas_Nation_Frigate:
        case EVEDB::invGroups::Incursion_Sanshas_Nation_Cruiser:
        case EVEDB::invGroups::Incursion_Sanshas_Nation_Battleship:
            r.l1Family = NpcAI::L1Family::Incursion;
            break;
        default:
            r.l1Family = NpcAI::L1Family::Other;
            break;
    }
    r.maxHP = static_cast<float>(Attr(type, AttrShieldCapacity) + Attr(type, AttrArmorHP) + Attr(type, AttrHP));

    /* ewar and support */
    for (auto effectID : c_ewarEffects)
        if (type.HasEffect(effectID))
            r.ewarEffects.push_back(effectID);
    r.hasSelfRepair = false;
    for (auto effectID : c_selfRepairEffects)
        if (type.HasEffect(effectID))
            r.hasSelfRepair = true;
    /* self-repair by effect (5.9, N3):  the old fallback order could pick the generic 639 (dgm default 1 = never repair) */
    auto repair = [&type](const RepairEffect* list, size_t count, uint16 cycleAttr, uint16 amountAttr, NpcProfile::SelfRepair& into) {
        into = NpcProfile::SelfRepair();
        for (size_t i = 0; i < count; ++i) {
            if (!type.HasEffect(list[i].effectID))
                continue;
            into.amount = static_cast<float>(Attr(type, amountAttr));
            if (into.amount <= 0.0f)
                return;
            into.effectID = list[i].effectID;
            into.cycleMs = static_cast<uint32>(std::max(1000.0, Attr(type, cycleAttr)));
            into.skipChance = static_cast<float>(NpcMath::RepairSkipChance(Attr(type, list[i].chanceAttr),
                                                 (list[i].multAttr != 0 ? Attr(type, list[i].multAttr) : 1.0)));
            /* every cycle skipped is no repair:  its timer would never stop itself (NPC::UseShieldRecharge), and the npc would
             *  count as busy for good (level of detail, dormancy).  no Crucible type has this;  a data edit could */
            if (into.skipChance >= 1.0f)
                into = NpcProfile::SelfRepair();
            return;
        }
    };
    repair(c_shieldRepair, sizeof(c_shieldRepair) / sizeof(c_shieldRepair[0]), AttrEntityShieldBoostDuration, AttrEntityShieldBoostAmount, r.selfShield);
    repair(c_armorRepair, sizeof(c_armorRepair) / sizeof(c_armorRepair[0]), AttrEntityArmorRepairDuration, AttrEntityArmorRepairAmount, r.selfArmor);
    r.hasRemoteRepair = false;
    for (auto effectID : c_remoteRepairEffects)
        if (type.HasEffect(effectID))
            r.hasRemoteRepair = true;
    r.hasGroupAssist = false;
    for (auto effectID : c_groupAssistEffects)
        if (type.HasEffect(effectID))
            r.hasGroupAssist = true;
    // a chance above 1 is a percentage (a few types, 5.8)
    auto chance = [&type](uint16 attrID) -> float {
        double value(Attr(type, attrID));
        if (value > 1.0)
            value /= 100.0;
        return static_cast<float>(std::min(1.0, std::max(0.0, value)));
    };
    /* remote repair of friends (5.9, N4:  the reach is npcAssistanceRange, dgm default 5km, never the sight range) */
    auto remote = [&type, &chance](uint16 effectID, uint16 amountAttr, uint16 cycleAttr, uint16 chanceAttr, uint16 thresholdAttr,
                                   uint16 maxAttr, NpcProfile::RemoteRepair& into) {
        into = NpcProfile::RemoteRepair();
        if (!type.HasEffect(effectID))
            return;
        into.amount = static_cast<float>(Attr(type, amountAttr));
        if (into.amount <= 0.0f)
            return;
        into.effectID = effectID;
        into.cycleMs = static_cast<uint32>(std::max(1000.0, Attr(type, cycleAttr)));
        into.chance = chance(chanceAttr);
        into.threshold = static_cast<float>(Attr(type, thresholdAttr));
        into.maxTargets = static_cast<uint8>(std::min(255.0, std::max(1.0, Attr(type, maxAttr))));
    };
    remote(EVEEffectID::NPCRemoteArmorRepair, AttrNpcRemoteArmorRepairAmount, AttrNpcRemoteArmorRepairDuration,
           AttrNpcRemoteArmorRepairChance, AttrNpcRemoteArmorRepairThreshold, AttrNpcRemoteArmorRepairMaxTargets, r.remoteArmor);
    remote(EVEEffectID::NPCRemoteShieldBoost, AttrNpcRemoteShieldBoostAmount, AttrNpcRemoteShieldBoostDuration,
           AttrNpcRemoteShieldBoostChance, AttrNpcRemoteShieldBoostThreshold, AttrNpcRemoteShieldBoostMaxTargets, r.remoteShield);
    /* group assists (4686-4689) */
    struct AssistEffect { uint16 effectID; uint8 kind; uint16 bonusAttr; uint16 durationAttr; uint16 chanceAttr; };
    const AssistEffect assists[] = {
        { EVEEffectID::NPCGroupShieldAssist,  NpcAI::Buff::ShieldResist, AttrentityGroupShieldResistanceBonus,
          AttrentityGroupShieldResistanceDuration, AttrentityGroupShieldResistanceActivationChance },
        { EVEEffectID::NPCGroupSpeedAssist,   NpcAI::Buff::Speed,        AttrentityGroupSpeedBonus,
          AttrentityGroupSpeedDuration, AttrentityGroupSpeedActivationChance },
        { EVEEffectID::NPCGroupPropJamAssist, NpcAI::Buff::PropJam,      AttrentityGroupPropJamBonus,
          AttrentityGroupPropJamDuration, AttrentityGroupPropJamActivationChance },
        { EVEEffectID::NPCGroupArmorAssist,   NpcAI::Buff::ArmorResist,  AttrentityGroupArmorResistanceBonus,
          AttrentityGroupArmorResistanceDuration, AttrentityGroupArmorResistanceActivationChance }
    };
    for (auto& cur : assists) {
        if (!type.HasEffect(cur.effectID))
            continue;
        NpcProfile::GroupAssist ga = NpcProfile::GroupAssist();
            ga.effectID = cur.effectID;
            ga.kind = cur.kind;
            ga.bonus = static_cast<float>(Attr(type, cur.bonusAttr));
            ga.durationMs = static_cast<uint32>(std::max(1000.0, Attr(type, cur.durationAttr)));
            ga.chance = chance(cur.chanceAttr);
        if (ga.bonus != 0.0f)
            r.groupAssists.push_back(ga);
    }
    /* remote ecm (4656) */
    r.remoteEcm = NpcProfile::RemoteEcm();
    r.remoteEcm.has = type.HasEffect(4656);
    if (r.remoteEcm.has) {
        r.remoteEcm.baseMs = static_cast<float>(Attr(type, AttrentityRemoteECMBaseDuration));
        r.remoteEcm.minMs = static_cast<float>(Attr(type, AttrentityRemoteECMMinDuration));
        r.remoteEcm.scale = static_cast<float>(Attr(type, AttrentityRemoteECMDurationScale));
        r.remoteEcm.playerStep = static_cast<float>(Attr(type, AttrentityRemoteECMExtraPlayerScale));
        r.remoteEcm.intendedPlayers = static_cast<float>(Attr(type, AttrentityRemoteECMIntendedNumPlayers));
        r.remoteEcm.chance = chance(AttrentityRemoteECMChanceOfActivation);
        if (r.remoteEcm.baseMs < 1000.0f)
            r.remoteEcm.has = false;
    }

    /* respawn, bounty, security */
    r.groupRespawnChance = static_cast<float>(Attr(type, AttrEntityGroupRespawnChance));
    r.bounty = Attr(type, AttrEntityKillBounty);
    r.securityBonus = static_cast<float>(Attr(type, AttrEntitySecurityStatusKillBonus));

    /* tier */
    bool support(r.hasSelfRepair or r.hasRemoteRepair or r.hasGroupAssist);
    if (IsSentryGroup(r.groupID)) {
        r.tier = NpcAI::Tier::Sentry;
        // sentries don't move:  the range rule above doesn't apply.  one without maxRange shoots out to entityAttackRange
        r.problems &= ~static_cast<uint32>(NpcAI::Problem::RangeBeyondAttack);
        if (r.hasTurret and (r.optimal <= 0.0f)) {
            r.problems |= NpcAI::Problem::SentryNoRange;
            r.optimal = r.attackRange;
        }
    } else if (!r.HasWeapons() and r.ewarEffects.empty() and !support and (r.maxVelocity <= 0.0f)) {
        r.tier = NpcAI::Tier::Inert;
    } else if (!r.HasWeapons()) {
        r.tier = NpcAI::Tier::Passive;
    } else if (r.IsLevelOne()) {
        r.tier = NpcAI::Tier::LevelOne;
    } else {
        r.tier = NpcAI::Tier::Standard;
    }

    if (log) {
        uint32 warn(r.problems & NpcAI::WarnProblems);
        if (warn != 0)
            _log(NPC__AI_WARN, "NpcProfile %s(%u) group %u:  %s", r.name.c_str(), r.typeID, r.groupID, ProblemList(warn).c_str());
        _log(NPC__AI_TRACE, "NpcProfile %s(%u):  tier %s, turret %s (%ums, x%.2f, %.0f/%.0f, %.4f, %.0f), missile %s (%u, %ums, x%.2f, %.0fm), "
                "atk %.0f, orbit %.0f, boost %.0f, scanRes %.0f, locks %u/%u, delay %.0f-%.0f x%.2f, l1 %u",
                r.name.c_str(), r.typeID, NpcAI::GetTierName(r.tier), (r.hasTurret ? "yes" : "no"), r.turretCycleMs, r.damageMultiplier,
                r.optimal, r.falloff, r.tracking, r.sigResolution, (r.hasMissile ? "yes" : "no"), r.missileTypeID, r.missileCycleMs,
                r.missileDamageMultiplier, r.missileRange, r.attackRange, r.orbitRange, r.boostRange, r.scanResolution,
                r.maxLockedTargets, r.maxAttackTargets, r.attackDelayMin, r.attackDelayMax, r.reactionFactor, r.l1TargetSwitching);
    }
    return p;
}

void NpcProfile::Audit(std::string& report, bool logEach/*false*/)
{
    double start(GetTimeMSeconds());
    std::map<uint16, Inv::TypeData> types;
    sDataMgr.GetTypes(types);

    uint32 counts[NpcAI::Problem::Count] = {0}, movingCounts[NpcAI::Problem::Count] = {0};
    uint32 tiers[NpcAI::Tier::Law + 1] = {0};
    uint32 total(0), moving(0), withProblems(0);
    for (auto& cur : types) {
        Inv::GrpData group;
        sDataMgr.GetGroup(cur.second.groupID, group);
        if (group.catID != EVEDB::invCategories::Entity)
            continue;
        const ItemType* pType(sItemFactory.GetType(cur.first));
        if (pType == nullptr)
            continue;
        // a fresh build (not the cache), so the counts follow the current config
        std::shared_ptr<const NpcProfile> p(Build(*pType, false));
        ++total;
        bool isMoving(p->maxVelocity > 0.0f);
        if (isMoving)
            ++moving;
        if (p->tier <= NpcAI::Tier::Law)
            ++tiers[p->tier];
        if ((p->problems & NpcAI::WarnProblems) != 0) {
            ++withProblems;
            if (logEach)
                _log(NPC__AI_WARN, "NpcProfile audit %s(%u) group %u:  %s", p->name.c_str(), p->typeID, p->groupID,
                        ProblemList(p->problems & NpcAI::WarnProblems).c_str());
        }
        for (uint32 i = 0; i < NpcAI::Problem::Count; ++i)
            if ((p->problems & (1u << i)) != 0) {
                ++counts[i];
                if (isMoving)
                    ++movingCounts[i];
            }
    }

    std::ostringstream str;
    str << "npc profile audit:  " << total << " entity types (" << moving << " moving), " << withProblems
        << " with warnings, " << std::fixed << std::setprecision(0) << (GetTimeMSeconds() - start) << "ms<br>";
    str << "tiers:";
    for (uint8 t = 0; t <= NpcAI::Tier::Law; ++t)
        str << "  " << NpcAI::GetTierName(t) << " " << tiers[t];
    str << "<br>problems (moving types / all):<br>";
    for (uint32 i = 0; i < NpcAI::Problem::Count; ++i)
        str << "  " << movingCounts[i] << " / " << counts[i] << "  " << NpcAI::GetProblemText(1u << i) << "<br>";
    report = str.str();

    std::string plain(report);
    for (size_t pos = plain.find("<br>"); pos != std::string::npos; pos = plain.find("<br>"))
        plain.replace(pos, 4, "\n");
    _log(NPC__AI_WARN, "%s", plain.c_str());
}

void NpcProfile::StartupChecks()
{
    // the per-shooter crit chances are gone:  one wrecking chance inside a hit for every turret (npc ai plan 5.1)
    struct { const char* name; float value; } crits[] = {
        { "NpcCritChance", sConfig.rates.NpcCritChance }, { "PlayerCritChance", sConfig.rates.PlayerCritChance },
        { "SentryCritChance", sConfig.rates.SentryCritChance }, { "DroneCritChance", sConfig.rates.DroneCritChance },
        { "ConcordCritChance", sConfig.rates.ConcordCritChance }
    };
    for (auto& cur : crits)
        if (cur.value >= 0.0f)
            sLog.Warning("     ServerConfig", "<rates><%s> is deprecated and ignored.  Every turret uses <WreckingChance> (%.3f), rolled inside a hit.",
                    cur.name, sConfig.rates.WreckingChance);

    // the data leash replaced the flat one (npc ai plan 5.4)
    if (sConfig.npc.LeashRange > 0)
        sLog.Warning("     ServerConfig", "<npc><LeashRange> is deprecated.  It replaces the data leash's hard range (%u m instead of %.1f x entityMaxWanderRange).",
                sConfig.npc.LeashRange, sConfig.npc.LeashHardScale);

    // level-one npcs score everything within R_acq (npc ai plan 5.7, B18)
    if (sConfig.npc.ThreatRadius > 0.0f)
        sLog.Warning("     ServerConfig", "<npc><ThreatRadius> is deprecated and ignored.  Level-one npcs consider every ship and drone within their acquisition range.");
    if (sConfig.npc.TargetSwitchTime > 0)
        sLog.Warning("     ServerConfig", "<npc><TargetSwitchTime> %us replaces every level-one npc's AI_TargetSwitchTimer (1416).  0 uses the data (60s).",
                sConfig.npc.TargetSwitchTime);

    if (sConfig.npc.AuditOnStartup) {
        std::string report;
        Audit(report);
    }
}
