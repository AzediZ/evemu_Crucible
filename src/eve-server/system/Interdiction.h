 /**
  * @name Interdiction.h
  *   warp disruption fields ("bubbles") and the warp rules that go with them
  *
  *   three sources in Crucible:
  *     interdiction sphere launcher -> warp disrupt probe (20km, 2 minutes)
  *     mobile warp disruptor (anchored deployable, 5-40km)
  *     warp disruption field generator (HIC, centred on the ship while active)
  *   a ship inside a field can't start a warp, and a warp whose line crosses a field on the destination grid ends at its edge.
  *   fields only work in 0.0 and wormholes.  ships with an interdiction nullifier (T3 subsystem) are immune.
  */

#ifndef _EVE_SYSTEM_INTERDICTION_H_
#define _EVE_SYSTEM_INTERDICTION_H_

#include "python/classes/PyExceptions.h"
#include "system/SystemEntity.h"

class SystemManager;

namespace Interdiction {
    // what made the field.  decides the error text a caught pilot sees
    namespace Source {
        enum : uint8 {
            Probe       = 1,    // launched warp disrupt probe
            Deployable  = 2,    // anchored mobile warp disruptor
            Generator   = 3     // HIC's warp disruption field generator
        };
    }
}

struct WarpDisruptField {
    SystemEntity* pSE;      // the probe sphere, the anchored deployable or the HIC.  its position is the field centre
    uint32 typeID;          // type shown in messages: probe/deployable type, or the HIC's *ship* typeID for Generator
    double radius;          // metres
    uint8  source;          // Interdiction::Source
};

class InterdictionMgr {
public:
    InterdictionMgr(SystemManager* pSystem);
    ~InterdictionMgr()                                  { /* fields are not owned */ }

    /* field list.  keyed by the source entity's itemID.  adding again replaces (new radius) */
    void AddField(SystemEntity* pSE, double radius, uint8 source, uint32 typeID);
    void RemoveField(uint32 entityID);                  // no-op if absent
    void Clear()                                        { m_fields.clear(); }
    bool HasFields() const                              { return !m_fields.empty(); }
    bool HasField(uint32 entityID) const                { return (m_fields.find(entityID) != m_fields.end()); }
    void GetFields(std::vector<const WarpDisruptField*>& into) const;

    // nullified (T3 interdiction nullifier) ships are never caught
    static bool IsImmune(SystemEntity* pSE);
    // empire (high or low sec) refuses everything flagged disallowInEmpireSpace, unless world.empireInterdiction
    static bool IsRestricted(SystemManager* pSystem);

    // the field containing pos, or nullptr.  *escape = metres still to go to leave it (the deepest field wins)
    const WarpDisruptField* GetFieldAt(const GPoint& pos, double* escape = nullptr) const;
    // pSE is a player ship (or pod), not immune, not login-warping and inside a field
    bool IsWarpDisrupted(SystemEntity* pSE) const;
    // throws WarpDisrupted / WarpScrambledByWarpDisruptionFieldGenerator if pSE is caught.
    //  the autopilot only stops itself on 'WarpDisrupted' (autopilot.py), so autoPilot always uses that key
    void CheckCanWarp(SystemEntity* pSE, bool autoPilot = false) const;
    // the error for a ship caught inside pField
    static UserError MakeDisruptedError(const WarpDisruptField* pField, double escape, bool autoPilot = false);

    /* warp line test.
     *  from = warp start, target = the point warped to (before the warp-to range), exitPoint = where the ship would come out.
     *  only fields on the destination grid count.  the test uses the infinite line through the target, so a field
     *  behind the destination still catches the warp (drag bubble).
     *  returns true when the warp must end at dropPoint (inside the first field crossed), with pField the field.
     */
    bool GetWarpPathDrop(const GPoint& from, const GPoint& target, const GPoint& exitPoint, double inset,
                         GPoint& dropPoint, const WarpDisruptField*& pField) const;

private:
    // a field generator only works with its pilot aboard (a hull left behind after eject/logoff keeps its entry until it leaves space)
    static bool IsLive(const WarpDisruptField& field);

    SystemManager* m_system;    // we do not own this

    std::map<uint32, WarpDisruptField> m_fields;
};

/* launched warp disrupt probe (interdiction sphere).  temp item, never saved, fixed life (explosionDelay, 2 minutes) */
class InterdictionSphereSE
: public ItemSystemEntity
{
public:
    InterdictionSphereSE(InventoryItemRef self, EVEServiceManager& services, SystemManager* system, const FactionData& data, uint32 lifeMS);
    virtual ~InterdictionSphereSE()                     { /* Do nothing here */ }

    /* class type pointer querys. */
    virtual InterdictionSphereSE* GetInterdictionSphereSE() { return this; }
    /* class type tests. */
    virtual bool                IsSystemEntity()        { return false; }
    virtual bool                IsInterdictionSphereSE() { return true; }

    /* SystemEntity interface */
    virtual void                Process();              // removes itself (and its field) when its life ends
    virtual void                MakeDamageState(DoDestinyDamageState& into);    // the type has no shield/armor attributes

private:
    Timer m_lifeTimer;
};

#endif  // _EVE_SYSTEM_INTERDICTION_H_
