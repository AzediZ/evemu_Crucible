
/**
 * @name POS_AI.h
 *   Class for POS Weapon Artificial Intelligence.
 *
 * @Author:         Allan
 * @date:   28 December 17
 */

#ifndef EVEMU_POS_POS_AI_H_
#define EVEMU_POS_POS_AI_H_

#include "inventory/InventoryItem.h"

class StructureSE;
class SystemEntity;
class TowerSE;

/* starbase defenses:  weapon batteries (turrets, missile launchers) and electronic warfare batteries.
 *  an online battery of a powered tower picks the closest target its tower considers hostile (TowerSE::IsHostile),
 *  locks it and fires at its rate of fire with its loaded charges (flagHiSlot0).  ewar batteries keep their effect on
 *  the target while they cycle.  automatic fire is not an aggressive act by anyone.
 *  a pilot controlling the battery (manual control) chooses the target instead
 */
class POS_AI {
public:
    POS_AI(StructureSE* pSE);
    ~POS_AI();

    // every tic (from WeaponSE/BatterySE::Process)
    void Process();
    // drops the target and ends any ewar (offline, destroyed, control changes)
    void Stop();

    /* manual control */
    // the controller's target (nullptr = none).  the battery fires at it while it is locked and the controller wants it to
    void SetManualTarget(uint32 targetID)               { m_manualTargetID = targetID; }
    void SetManualFire(bool fire)                       { m_manualFire = fire; }
    bool IsManualFire()                                 { return m_manualFire; }
    uint32 GetManualTargetID()                          { return m_manualTargetID; }

    // range of the battery against a target (turret optimal + falloff, missile flight, ewar range)
    double GetRange();
    // lock time of the battery against a target (ms)
    float GetLockTime(SystemEntity* pTarget);
    // the battery can do something to this target (alive, in space, visible, in range)
    bool IsEngageable(SystemEntity* pTarget);

private:
    enum Kind : uint8 {
        Turret      = 1,
        Launcher    = 2,
        Ewar        = 3
    };
    // kinds of ewar beyond Ewar:: (EwarState) ones
    enum : uint8 { Neut = 100 };

    SystemEntity* FindTarget(TowerSE* pTower);
    void Fire(SystemEntity* pTarget);
    void FireTurret(SystemEntity* pTarget);
    void FireMissile(SystemEntity* pTarget);
    void CycleEwar(SystemEntity* pTarget);
    void EndEwar();
    // the loaded charge (a stack in flagHiSlot0), or nullptr
    InventoryItemRef GetCharge();
    // a tower bonus attribute (percent) as a multiplier
    float TowerBonus(uint16 attrID);
    // rate of fire (ms) with the tower's bonus
    uint32 GetCycleTime();

    StructureSE* m_self;            // we do not own this
    InventoryItemRef m_selfRef;

    Kind m_kind;
    uint8 m_ewarKind;
    const char* m_guid;

    Timer m_scanTimer;              // looking for targets
    Timer m_fireTimer;              // rate of fire / ewar cycle

    uint32 m_targetID;              // current target
    uint32 m_ewarTargetID;          // target of the running ewar session
    uint32 m_manualTargetID;        // manual control
    bool m_manualFire;
};

#endif  // EVEMU_POS_POS_AI_H_
