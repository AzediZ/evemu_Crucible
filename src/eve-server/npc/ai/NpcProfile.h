 /**
  * @name NpcProfile.h
  *   immutable per-type npc data, built once from the type's Crucible dogma (PLAN.md 6.2)
  *
  *   every value comes from the type's attributes and effects.  a missing attribute takes dogma's default
  *   (dgmAttributeTypes.defaultValue, StaticDataMgr::GetAttributeDefault), never 0.  what an npc can fire comes from its
  *   effects:  turrets need effect 10 (targetAttack), missiles effect 569 (missileLaunchingForEntity).
  *   problems in the data are found when the profile is built, logged once per type (NPC__AI_WARN) and listed by
  *   '/npcai audit' (and npc.AuditOnStartup).  unusable weapons are disabled instead of firing NaN or zero-damage shots.
  *
  *   NpcProfile::Get() caches profiles by typeID, so every npc of a type shares one.
  */

#ifndef _EVE_NPC_AI_NPCPROFILE_H_
#define _EVE_NPC_AI_NPCPROFILE_H_

#include "eve-server.h"

class ItemType;

namespace NpcAI {
    // the ai a type gets (which brain runs it, npc/ai/NpcBrain.h).  Law is set at run time by LawEnforcement
    namespace Tier {
        enum : uint8 {
            Standard    = 0,    // belt, gate, mission and deadspace rats
            LevelOne    = 1,    // AI_ShouldUseTargetSwitching (1648) > 0:  sleepers, incursion sansha, officers, 5 uniques
            Passive     = 2,    // no weapons, but it moves or has ewar (haulers, customs officials, convoys)
            Inert       = 3,    // no weapons, ewar or support, and it doesn't move (overseer structures, belongings, props)
            Sentry      = 4,    // sentry gun groups (npc/SentryAI)
            Law         = 5     // CONCORD and faction police (set by LawEnforcement, never by the profile)
        };
    }
    const char* GetTierName(uint8 tier);

    // which level-one npcs a type belongs to (by group):  the incursion newcomer bonus and the sleepers' capital escalation
    namespace L1Family {
        enum : uint8 {
            Other       = 0,    // asteroid officers, the 5 unique npcs
            Sleeper     = 1,    // groups 959-961, 982-987
            Incursion   = 2     // groups 1052-1054, 1056
        };
    }
    // what a group assist (effects 4686-4689) gives, and what an npc has on it
    namespace Buff {
        enum : uint8 {
            ShieldResist    = 0,    // NPCGroupShieldAssist (4686):  shield resonances x
            Speed           = 1,    // NPCGroupSpeedAssist (4687):  speed x
            PropJam         = 2,    // NPCGroupPropJamAssist (4688):  scram and web range x
            ArmorResist     = 3,    // NPCGroupArmorAssist (4689):  armor resonances x
            Count           = 4
        };
    }

    // data problems found while building a profile (bit flags)
    namespace Problem {
        enum : uint32 {
            TurretNoDamage          = 1 << 0,   // effect 10 but no damage:  turret disabled
            DamageNoTurretEffect    = 1 << 1,   // damage attributes but no effect 10:  no turret
            MissileAttrsNoEffect    = 1 << 2,   // missileLaunchDuration 1 and entityMissileTypeID but no effect 569:  no missiles (correct)
            MissileNoDuration       = 1 << 3,   // effect 569 without missileLaunchDuration:  dgm default 20s
            NoScanResolution        = 1 << 4,   // no scanResolution:  lock time from scanSpeed
            NoLockData              = 1 << 5,   // neither scanResolution nor scanSpeed:  lock time from the radius table
            FlyRangeZero            = 1 << 6,   // entityFlyRange 0:  orbit at min(maxRange, entityAttackRange), else 1000m
            TurretTooFast           = 1 << 7,   // effect 10 with a turret cycle under 500ms (or none):  turret disabled
            AttackRangeZero         = 1 << 8,   // weapons but entityAttackRange 0:  dgm default
            MissileTypeMissing      = 1 << 9,   // effect 569 but no usable missile type:  no missiles
            L1LevelTwo              = 1 << 10,  // AI_ShouldUseTargetSwitching 2 (type 2943):  treated as 1
            NoTracking              = 1 << 11,  // turret without trackingSpeed:  hits only targets without angular velocity
            MissileNoEffect         = 1 << 12,  // a real missileLaunchDuration and entityMissileTypeID but no effect 569:  no missiles
                                                //  (rogue drones, some commanders;  the attribute-only gating fired them)
            MissileCycleTooShort    = 1 << 13,  // effect 569 with missileLaunchDuration under 1s (the placeholder 1):  dgm default 20s
            RangeBeyondAttack       = 1 << 14,  // entityFlyRange beyond 0.9 x entityAttackRange:  the orbit is kept inside it
            SentryNoRange           = 1 << 15,  // sentry turret without maxRange:  entityAttackRange is its optimal
            Count                   = 16
        };
    }
    const char* GetProblemText(uint32 problemBit);
    // the problems that are worth a warning (MissileAttrsNoEffect is the correct reading of the data)
    const uint32 WarnProblems = ~static_cast<uint32>(Problem::MissileAttrsNoEffect);

    // where the lock time comes from
    namespace LockFrom {
        enum : uint8 { ScanResolution = 0, ScanSpeed = 1, RadiusTable = 2 };
    }
}

struct NpcProfile {
    /* identity */
    uint16 typeID;
    uint16 groupID;
    uint8 categoryID;
    std::string name;
    uint8 tier;                     // NpcAI::Tier
    uint32 problems;                // NpcAI::Problem bits

    /* turret (effect 10) */
    bool hasTurret;
    uint32 turretCycleMs;           // speed (51)
    float damageMultiplier;         // damageMultiplier (64), turrets only
    float turretDamage;             // em + explosive + kinetic + thermal (114/116/117/118)
    float optimal;                  // maxRange (54)
    float falloff;                  // falloff (158)
    float tracking;                 // trackingSpeed (160)
    float sigResolution;            // optimalSigRadius (620)

    /* missiles (effect 569) */
    bool hasMissile;
    uint16 missileTypeID;           // entityMissileTypeID (507)
    uint32 missileCycleMs;          // missileLaunchDuration (506, dgm default 20000)
    float missileDamageMultiplier;  // missileDamageMultiplier (212), npc missiles only
    double missileRange;            // m.  the missile's velocity x 645 times flight time x 646 x rates.missileTime

    /* ranges and movement (5.4, 5.5) */
    float attackRange;              // entityAttackRange (247):  weapons fire only within this surface distance
    float flyRange;                 // entityFlyRange (416) as in the data
    float orbitRange;               // r_o:  416 (kept inside 0.9 x R_atk for armed types), or min(maxRange, R_atk) when 0, else 1000
    float chaseMaxDistance;         // entityChaseMaxDistance (665)
    float boostRange;               // r_b = max(665, r_o)
    float cruiseSpeed;              // entityCruiseSpeed (508), 0 = maxVelocity
    float maxVelocity;              // maxVelocity (37)
    float wanderRange;              // entityMaxWanderRange (584):  leash
    float assistRange;              // npcAssistanceRange (1464)
    float sigRadius;                // signatureRadius (552)
    float radius;
    float sigBloom;                 // entityMaxVelocitySignatureRadiusMultiplier (1133):  mwd signature bloom
    uint32 acquireRange;            // R_acq = max(entityAttackRange, npc.AcquireRange):  proactive acquisition and lock range

    /* targeting (5.3) */
    uint8 maxLockedTargets;         // maxLockedTargets (192)
    uint8 maxAttackTargets;         // maxAttackTargets (193)
    float scanResolution;           // scanResolution (564)
    float scanSpeed;                // scanSpeed (79), ms
    uint8 lockFrom;                 // NpcAI::LockFrom

    /* reaction (5.6) and chase (5.5) timings, ms */
    float attackDelayMin;           // entityAttackDelayMin (475)
    float attackDelayMax;           // entityAttackDelayMax (476)
    float reactionFactor;           // entityReactionFactor (466)
    float chaseMaxDelay;            // 580
    float chaseMaxDelayChance;      // 581
    float chaseMaxDuration;         // 582
    float chaseMaxDurationChance;   // 583

    /* level-one ai (5.7).  1650-1652 count only when 1648 > 0 */
    uint8 l1TargetSwitching;        // AI_ShouldUseTargetSwitching (1648), 2 read as 1
    bool l1SecondaryTarget;         // 1649
    bool l1SignatureRadius;         // 1650
    bool l1EffectMultiplier;        // 1652
    float l1ChanceToNotSwitch;      // 1651
    float l1PreferredSig;           // 1655 (-1 = our own signature)
    float l1TankingDrone;           // 1656
    float l1TankingShip;            // 1657
    uint32 l1SwitchTimerMs;         // AI_TargetSwitchTimer (1416)
    float evasive;                  // AI_ShouldUseEvasiveManeuver (1414)
    uint8 l1Family;                 // NpcAI::L1Family (from the group):  sleeper, incursion or other
    float maxHP;                    // shield capacity + armor hp + hull hp (E of the level-one effect threat, 5.7)

    /* ewar and support effects the type has */
    std::vector<uint16> ewarEffects;
    bool hasSelfRepair;
    struct SelfRepair {
        uint16 effectID;            // 876/2192-2194 (shield) or 878/2195-2197 (armor), 0 = none
        uint32 cycleMs;             // entityShieldBoostDuration (636) / entityArmorRepairDuration (630)
        float amount;               // entityShieldBoostAmount (637) / entityArmorRepairAmount (631)
        float skipChance;           // the effect's delay chance (639/638, 1006-1011) x its multiplier (1015-1020)
    } selfShield, selfArmor;
    bool hasRemoteRepair;
    bool hasGroupAssist;
    /* remote repair of friends (5.9):  NPCRemoteArmorRepair (3852) / NPCRemoteShieldBoost (3855) */
    struct RemoteRepair {
        uint16 effectID;            // 0 = none
        float amount;               // npcRemoteArmorRepairAmount (1455) / npcRemoteShieldBoostAmount (1460)
        uint32 cycleMs;             // 1454 / 1458
        float chance;               // activation chance each cycle:  1453 / 1459
        float threshold;            // 1456 (damage / armor hp) / 1462 (1 - charge / capacity)
        uint8 maxTargets;           // 1501 / 1502
    } remoteArmor, remoteShield;
    /* group assists (effects 4686-4689):  a bonus for the same-group npcs within npcAssistanceRange, each duration */
    struct GroupAssist {
        uint16 effectID;
        uint8 kind;                 // NpcAI::Buff
        float bonus;                // 1671/1674/1675/1676 ("negative values is a bonus")
        uint32 durationMs;          // 1672/1677/1679/1681
        float chance;               // 1673/1678/1680/1682
    };
    std::vector<GroupAssist> groupAssists;
    /* remote ecm (effect 4656, the incursion capitals):  every burst interval (1658, recalculated from 1659-1663 and the players
     *  in the bubble) roll 1664, then jam every player ship on grid */
    struct RemoteEcm {
        bool has;
        float baseMs;               // entityRemoteECMBaseDuration (1661)
        float minMs;                // entityRemoteECMMinDuration (1659)
        float scale;                // entityRemoteECMDurationScale (1660)
        float playerStep;           // entityRemoteECMExtraPlayerScale (1662)
        float intendedPlayers;      // entityRemoteECMIntendedNumPlayers (1663)
        float chance;               // entityRemoteECMChanceOfActivation (1664)
    } remoteEcm;

    /* respawn, bounty, security */
    float groupRespawnChance;       // entityGroupRespawnChance (640)
    double bounty;                  // entityKillBounty (481)
    float securityBonus;            // entitySecurityStatusKillBonus (252)

    bool HasWeapons() const                             { return (hasTurret or hasMissile); }
    bool IsLevelOne() const                             { return (l1TargetSwitching > 0); }
    // R_atk for missiles:  min(entityAttackRange, 0.95 x missile flight range)
    double MissileFireRange() const;
    /* lock time on a target of signature sig, ms.  scanResMult is the dampener multiplier on us (1 = none).
     *  the player formula with scanResolution, else scanSpeed, else today's radius table */
    double LockTimeMs(double sig, double scanResMult = 1.0) const;
    // first activation delay for draw u (5.6), ms
    double ReactionDelayMs(double u, double scale = 1.0) const;

    // the cached profile of a type (built on first use).  nullptr for unknown types
    static std::shared_ptr<const NpcProfile> Get(uint16 typeID);
    static std::shared_ptr<const NpcProfile> Get(const ItemType& type);
    /* builds the profile of every entity (category 11) type and logs the problem counts.
     *  the summary (counts per problem, over moving types and all of them) goes into report */
    static void Audit(std::string& report, bool logEach = false);
    // at boot:  warns about deprecated settings, runs the audit when npc.AuditOnStartup is set
    static void StartupChecks();

    // a readable list of problem bits
    static std::string ProblemList(uint32 problems);

private:
    static std::shared_ptr<const NpcProfile> Build(const ItemType& type, bool log);
};

#endif  // _EVE_NPC_AI_NPCPROFILE_H_
