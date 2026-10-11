 /**
  * @name Interdiction.cpp
  *   warp disruption fields ("bubbles") and the warp rules that go with them
  */

#include "eve-server.h"

#include "Client.h"
#include "EVEServerConfig.h"
#include "inventory/AttributeEnum.h"
#include "system/BubbleManager.h"
#include "system/CrimeWatch.h"
#include "system/Interdiction.h"
#include "system/SystemEntity.h"
#include "system/SystemManager.h"


InterdictionMgr::InterdictionMgr(SystemManager* pSystem)
: m_system(pSystem)
{
    m_fields.clear();
}

void InterdictionMgr::AddField(SystemEntity* pSE, double radius, uint8 source, uint32 typeID)
{
    if ((pSE == nullptr) or (radius <= 0))
        return;

    WarpDisruptField field = WarpDisruptField();
        field.pSE = pSE;
        field.typeID = typeID;
        field.radius = radius;
        field.source = source;
    m_fields[pSE->GetID()] = field;

    _log(DESTINY__WARP_TRACE, "InterdictionMgr::AddField() - %s(%u) in %s: %.0fm field (source %u, type %u).  %lu field(s)",
            pSE->GetName(), pSE->GetID(), m_system->GetName(), radius, source, typeID, m_fields.size());
}

void InterdictionMgr::RemoveField(uint32 entityID)
{
    std::map<uint32, WarpDisruptField>::iterator itr = m_fields.find(entityID);
    if (itr == m_fields.end())
        return;
    m_fields.erase(itr);

    _log(DESTINY__WARP_TRACE, "InterdictionMgr::RemoveField() - field of %u in %s removed.  %lu field(s)",
            entityID, m_system->GetName(), m_fields.size());
}

void InterdictionMgr::GetFields(std::vector<const WarpDisruptField*>& into) const
{
    for (auto& cur : m_fields)
        into.push_back(&cur.second);
}

bool InterdictionMgr::IsImmune(SystemEntity* pSE)
{
    if ((pSE == nullptr) or !pSE->IsShipSE())
        return false;
    // effect 4369 subsystemBonusWarpBubbleImmune (interdiction nullifier subsystems) sets this to 1
    EvilNumber value(EvilZero);
    return (pSE->GetSelf()->HasAttribute(AttrWarpBubbleImmune, value) and (value > EvilZero));
}

bool InterdictionMgr::IsRestricted(SystemManager* pSystem)
{
    if (pSystem == nullptr)
        return false;
    if (sConfig.world.empireInterdiction)
        return false;
    // wormholes have negative security, so they count as 0.0
    return (CrimeWatch::SecurityClass(pSystem->GetSystemSecurityRating()) != Crime::SecClass::Null);
}

bool InterdictionMgr::IsLive(const WarpDisruptField& field)
{
    if (field.pSE == nullptr)
        return false;
    if ((field.source == Interdiction::Source::Generator) and !field.pSE->HasPilot())
        return false;
    return true;
}

const WarpDisruptField* InterdictionMgr::GetFieldAt(const GPoint& pos, double* escape/*nullptr*/) const
{
    const WarpDisruptField* pField(nullptr);
    double best(0), distance(0);
    for (auto& cur : m_fields) {
        if (!IsLive(cur.second))
            continue;
        distance = pos.distance(cur.second.pSE->GetPosition());
        if (distance >= cur.second.radius)
            continue;
        // inside.  the field that takes longest to leave is the one to report
        if ((pField == nullptr) or ((cur.second.radius - distance) > best)) {
            pField = &cur.second;
            best = cur.second.radius - distance;
        }
    }

    if ((pField != nullptr) and (escape != nullptr)) {
        *escape = ceil(best);
        if (*escape < 1)
            *escape = 1;
    }
    return pField;
}

bool InterdictionMgr::IsWarpDisrupted(SystemEntity* pSE) const
{
    if (m_fields.empty())
        return false;
    // only player ships and pods are caught.  npcs, drones and missiles are not
    if ((pSE == nullptr) or !pSE->IsShipSE())
        return false;
    if (IsImmune(pSE))
        return false;
    // a reconnecting pilot must not get stuck mid login-warp
    if (pSE->HasPilot() and pSE->GetPilot()->IsLoginWarping())
        return false;
    return (GetFieldAt(pSE->GetPosition()) != nullptr);
}

void InterdictionMgr::CheckCanWarp(SystemEntity* pSE, bool autoPilot/*false*/) const
{
    if (!IsWarpDisrupted(pSE))
        return;
    double escape(0);
    const WarpDisruptField* pField(GetFieldAt(pSE->GetPosition(), &escape));
    if (pField == nullptr)
        return;
    throw MakeDisruptedError(pField, escape, autoPilot);
}

UserError InterdictionMgr::MakeDisruptedError(const WarpDisruptField* pField, double escape, bool autoPilot/*false*/)
{
    if ((pField->source == Interdiction::Source::Generator) and !autoPilot) {
        // "Get {range} meters from {[character]pilot.nameWithPossessive} {[item]ship.name} to warp."
        uint32 pilotID(pField->pSE->GetOwnerID());
        if (pField->pSE->HasPilot())
            pilotID = pField->pSE->GetPilot()->GetCharacterID();
        return UserError("WarpScrambledByWarpDisruptionFieldGenerator")
                .AddFormatValue("range", new PyInt(static_cast<int32>(escape)))
                .AddFormatValue("pilot", new PyInt(pilotID))
                .AddFormatValue("ship", new PyInt(pField->typeID));
    }

    // "Get {range} meters from {object} to warp."
    return UserError("WarpDisrupted")
            .AddFormatValue("range", new PyInt(static_cast<int32>(escape)))
            .AddTypeName("object", pField->typeID);
}

InterdictionSphereSE::InterdictionSphereSE(InventoryItemRef self, EVEServiceManager& services, SystemManager* system,
                                           const FactionData& data, uint32 lifeMS)
: ItemSystemEntity(self, services, system),
m_lifeTimer(lifeMS)
{
    m_warID = data.factionID;
    m_allyID = data.allianceID;
    m_corpID = data.corporationID;
    m_ownerID = data.ownerID;
}

void InterdictionSphereSE::Process()
{
    if (m_lifeTimer.Check(false)) {
        m_lifeTimer.Disable();
        _log(DESTINY__WARP_TRACE, "InterdictionSphereSE::Process() - %s(%u) expired.", GetName(), GetID());
        // removes the ball, the field (SystemManager::RemoveEntity) and the temp item
        Delete();
        delete this;
        return;
    }

    SystemEntity::Process();
}

void InterdictionSphereSE::MakeDamageState(DoDestinyDamageState& into)
{
    SystemEntity::MakeDamageState(into);
}

bool InterdictionMgr::GetWarpPathDrop(const GPoint& from, const GPoint& target, const GPoint& exitPoint, double inset,
                                      GPoint& dropPoint, const WarpDisruptField*& pField) const
{
    pField = nullptr;
    if (m_fields.empty())
        return false;

    GVector dir(from, target);
    if (dir.isZero())
        return false;
    dir.normalize();

    double nearest(0), tcNearest(0);
    for (auto& cur : m_fields) {
        if (!IsLive(cur.second))
            continue;
        const GPoint& centre(cur.second.pSE->GetPosition());
        // only fields on the destination grid matter (bubbles at the start or mid-route don't)
        if (centre.distance(exitPoint) > BUBBLE_RADIUS_METERS)
            continue;
        GVector toCentre(from, centre);
        // distance along the warp line to the point closest to the centre
        double tc(toCentre.dotProduct(dir));
        if (tc <= 0)
            continue;
        double r2(cur.second.radius * cur.second.radius);
        double h2(toCentre.lengthSquared() - (tc * tc));
        if (h2 >= r2)
            continue;   // the line misses the field
        // where the line enters the field
        double t1(tc - sqrt(r2 - h2));
        if (t1 <= 0)
            continue;   // the start is inside this field.  CheckCanWarp() deals with that
        if ((pField == nullptr) or (t1 < nearest)) {
            pField = &cur.second;
            nearest = t1;
            tcNearest = tc;
        }
    }

    if (pField == nullptr)
        return false;

    // land just inside the field.  the client drops out of warp a little short of the exit point and coasts in
    double t(nearest + inset);
    if (t > tcNearest)
        t = tcNearest;
    dropPoint = from + (dir * t);
    return true;
}
