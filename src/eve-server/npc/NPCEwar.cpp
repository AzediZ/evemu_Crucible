 /**
  * @name NPCEwar.cpp
  *   how Sansha incursion ships fly and fight.  numbers come from each ship's attributes, which follow the
  *   EVE University wiki's Sansha's Manual (sql/migrations, incursions)
  *
  *   - they orbit their target at their own orbit range (entityFlyRange) and orbit speed (entityCruiseSpeed)
  *   - far from that orbit, or chasing, they run a propulsion module: top speed (maxVelocity) and the same
  *       signature bloom as a player's microwarpdrive.  they switch it off near their orbit
  *   - they see the whole grid and spread their targets over the pilots on it, switching now and then
  *   - EWAR: warp disruptors, stasis webs, target painters, energy neutralizers and ECM, each with its own range
  *   - remote shield repairs on damaged friends, and the motherships' remote ECM burst
  *
  * @date:   12 October 2026
  */

#include <algorithm>

#include "eve-server.h"

#include "Client.h"
#include "EVEServerConfig.h"
#include "inventory/AttributeEnum.h"
#include "npc/NPC.h"
#include "npc/NPCAI.h"
#include "ship/Ship.h"
#include "system/DestinyManager.h"
#include "system/SystemBubble.h"
#include "system/SystemManager.h"
#include "system/TargetManager.h"

namespace {
    // a microwarpdrive's signature radius bonus (+500%)
    const double PROP_SIG_FACTOR = 6.0;
    // incursion pockets are one grid; their ships see all of it
    const uint32 INCURSION_SIGHT_RANGE = 250000;
    // jams last one cycle of the jammer
    const double JAM_CYCLE = 20000.0;

    bool IsIncursionGroup(uint32 groupID) {
        switch (groupID) {
            case EVEDB::invGroups::Incursion_Sanshas_Nation_Industrial:
            case EVEDB::invGroups::Incursion_Sanshas_Nation_Capital:
            case EVEDB::invGroups::Incursion_Sanshas_Nation_Frigate:
            case EVEDB::invGroups::Incursion_Sanshas_Nation_Cruiser:
            case EVEDB::invGroups::Incursion_Sanshas_Nation_Battleship:
                return true;
        }
        return false;
    }

    // gap between two balls' surfaces
    double Gap(SystemEntity* pA, SystemEntity* pB) {
        double gap(pA->GetPosition().distance(pB->GetPosition()) - pA->GetRadius() - pB->GetRadius());
        return (gap < 0.0 ? 0.0 : gap);
    }

    double Attr(InventoryItemRef iRef, uint16 attrID) {
        return (iRef->HasAttribute(attrID) ? iRef->GetAttribute(attrID).get_double() : 0.0);
    }

    // a player's ship we may use EWAR on
    bool IsPlayerShip(SystemEntity* pSE) {
        return ((pSE != nullptr) and pSE->IsShipSE() and pSE->HasPilot());
    }
}

void NPCAIMgr::InitIncursion()
{
    m_incursion = IsIncursionGroup(m_self->groupID());
    if (!m_incursion)
        return;

    // orbit range comes straight from the ship.  the base AI's boost range doesn't apply
    m_flyRange = m_self->GetAttribute(AttrEntityFlyRange).get_uint32();
    if (m_sightRange < INCURSION_SIGHT_RANGE)
        m_sightRange = INCURSION_SIGHT_RANGE;
    m_nextBurst = GetTimeMSeconds() + MakeRandomInt(60, 300) * 1000;
}

void NPCAIMgr::FlyIncursion(SystemEntity* pSE, double dist)
{
    m_warpOutTimer.Disable();
    // objectives and other structures don't move
    if ((m_maxSpeed == 0) and (m_orbitSpeed == 0)) {
        m_state = NPCAI::State::Engaged;
        return;
    }

    // propulsion on well outside the orbit, off again when nearly there.  the gap between the two keeps it from flickering
    double gap(dist - m_flyRange);
    if (m_propOn) {
        if (gap < std::max(2000.0, m_flyRange * 0.1))
            SetPropulsion(false);
    } else if (gap > std::max(5000.0, m_flyRange * 0.25)) {
        SetPropulsion(true);
    }

    // far away this flies straight at the target, then settles into the orbit.  it does nothing when unchanged
    m_destiny->Orbit(pSE, m_flyRange);
    int8 state(m_propOn ? NPCAI::State::Chasing : NPCAI::State::Engaged);
    if (state != m_state)
        _log(NPC__AI_TRACE, "%s(%u): %s %s(%u) at %.0fm, orbit %um, propulsion %s.", m_npc->GetName(), m_npc->GetID(),
             (m_propOn ? "Chasing" : "Orbiting"), pSE->GetName(), pSE->GetID(), dist, m_flyRange, (m_propOn ? "on" : "off"));
    m_state = state;
}

void NPCAIMgr::SetPropulsion(bool on)
{
    if (on == m_propOn)
        return;
    m_propOn = on;
    m_destiny->SetMaxVelocity(on ? m_maxSpeed : m_orbitSpeed);
    // multiply rather than set, so target painters on us stay correct
    double sig(m_self->GetAttribute(AttrSignatureRadius).get_double());
    m_self->SetAttribute(AttrSignatureRadius, (on ? sig * PROP_SIG_FACTOR : sig / PROP_SIG_FACTOR), false);
    m_sigRadius = m_self->GetAttribute(AttrSignatureRadius).get_uint32();
}

SystemEntity* NPCAIMgr::PickIncursionTarget(SystemEntity* pCurrent/*nullptr*/)
{
    SystemBubble* pBubble(m_npc->SysBubble());
    if (pBubble == nullptr)
        return nullptr;
    std::vector<Client*> clients;
    pBubble->GetPlayers(clients);
    std::vector<SystemEntity*> picks;
    for (auto cur : clients) {
        if (cur->IsInvul() or (cur->GetShipSE() == nullptr))
            continue;
        if (cur->InPod() and !sConfig.npc.TargetPod)
            continue;
        DestinyManager* pDestiny(cur->GetShipSE()->DestinyMgr());
        if ((pDestiny == nullptr) or pDestiny->IsCloaked() or pDestiny->IsWarping())
            continue;
        if (m_npc->GetPosition().distance(cur->GetShipSE()->GetPosition()) > m_sightRange)
            continue;
        if (cur->GetShipSE() == pCurrent)
            continue;
        picks.push_back(cur->GetShipSE());
    }
    if (picks.empty())
        return nullptr;
    return picks[MakeRandomInt(0, picks.size() - 1)];
}

void NPCAIMgr::ProcessIncursion(SystemEntity* pSE)
{
    double now(GetTimeMSeconds());
    // now and then move to another pilot, so the fleet's aggro spreads.  only when locked, and never to a pilot
    //  we're already locking
    if (now >= m_nextSwitch) {
        bool first(m_nextSwitch == 0);
        m_nextSwitch = now + MakeRandomInt(30, 90) * 1000;
        if (!first and (MakeRandomFloat() < 0.35) and (m_npc->TargetMgr()->GetTarget(pSE->GetID(), true) != nullptr)) {
            SystemEntity* pNew(PickIncursionTarget(pSE));
            if ((pNew != nullptr) and (m_npc->TargetMgr()->GetTarget(pNew->GetID(), false) == nullptr)) {
                _log(NPC__AI_TRACE, "%s(%u): switching target from %s(%u) to %s(%u).", m_npc->GetName(), m_npc->GetID(),
                     pSE->GetName(), pSE->GetID(), pNew->GetName(), pNew->GetID());
                ClearEwar();
                // lock the new one first, so dropping the old one doesn't send us idle
                Target(pNew);
                if (m_npc->TargetMgr()->GetTarget(pNew->GetID(), false) != nullptr)
                    m_npc->TargetMgr()->ClearTarget(pSE);
                return;
            }
        }
    }

    ProcessEwar(pSE);
    RemoteEcmBurst();
}

void NPCAIMgr::ProcessEwar(SystemEntity* pSE)
{
    // EWAR needs a lock and a player's ship
    if (!IsPlayerShip(pSE) or (m_npc->TargetMgr()->GetTarget(pSE->GetID(), true) == nullptr) or m_npc->TargetMgr()->IsJammed()) {
        ClearEwar();
        return;
    }

    // anything still held on a previous target goes first
    if ((m_pointID != 0) and (m_pointID != pSE->GetID()))
        StopPoint();
    if ((m_webID != 0) and (m_webID != pSE->GetID()))
        StopWeb();
    if ((m_paintID != 0) and (m_paintID != pSE->GetID()))
        StopPaint();

    double now(GetTimeMSeconds());
    double gap(Gap(m_npc, pSE));
    InventoryItemRef tRef(pSE->GetSelf());
    Client* pClient(pSE->GetPilot());

    // warp disruptor: holds while in range
    if ((m_warpScramRange > 0) and (Attr(m_self, AttrWarpScrambleStrength) > 0)) {
        if (gap <= m_warpScramRange) {
            if (m_pointID != pSE->GetID()) {
                StopPoint();
                m_pointID = pSE->GetID();
                m_pointStrength = static_cast<int32>(Attr(m_self, AttrWarpScrambleStrength));
                tRef->SetAttribute(AttrWarpScrambleStatus, tRef->GetAttribute(AttrWarpScrambleStatus).get_int() + m_pointStrength);
                m_destiny->SendSpecialEffect(m_self->itemID(), m_self->itemID(), m_self->typeID(), pSE->GetID(), 0,
                                             "effects.WarpScramble", true, true, true, 5000, 10000);
                if (pClient != nullptr)
                    pClient->SendNotifyMsg("%s is warp disrupting you.", m_npc->GetName());
            }
        } else if (m_pointID == pSE->GetID()) {
            StopPoint();
        }
    }

    // stasis web: holds while in range
    double webRange(Attr(m_self, AttrModifyTargetSpeedRange));
    if ((webRange > 0) and (Attr(m_self, AttrSpeedFactor) < 0)) {
        if (gap <= webRange) {
            if (m_webID != pSE->GetID()) {
                StopWeb();
                if (pSE->DestinyMgr() != nullptr) {
                    pSE->DestinyMgr()->WebbedMe(m_self, true);
                    m_webID = pSE->GetID();
                    m_destiny->SendSpecialEffect(m_self->itemID(), m_self->itemID(), m_self->typeID(), pSE->GetID(), 0,
                                                 "effects.ModifyTargetSpeed", true, true, true, 5000, 10000);
                }
            }
        } else if (m_webID == pSE->GetID()) {
            StopWeb();
        }
    }

    // target painter: holds while within optimal + falloff
    double paintRange(Attr(m_self, AttrEntityTargetPaintMaxRange) + Attr(m_self, AttrEntityTargetPaintFallOff));
    double paint(Attr(m_self, AttrEntityTargetPaintMultiplier));
    if ((paintRange > 0) and (paint > 1.0)) {
        if (gap <= paintRange) {
            if (m_paintID != pSE->GetID()) {
                StopPaint();
                tRef->SetAttribute(AttrSignatureRadius, tRef->GetAttribute(AttrSignatureRadius).get_double() * paint);
                m_paintID = pSE->GetID();
                m_paintFactor = paint;
                m_destiny->SendSpecialEffect(m_self->itemID(), m_self->itemID(), m_self->typeID(), pSE->GetID(), 0,
                                             "effects.TargetPaint", true, true, true, 5000, 10000);
            }
        } else if (m_paintID == pSE->GetID()) {
            StopPaint();
        }
    }

    // energy neutralizer: drains every cycle while in range
    double neut(Attr(m_self, AttrEntityCapacitorDrainAmount));
    if ((neut > 0) and (now >= m_nextNeut)) {
        double range(Attr(m_self, AttrEntityCapacitorDrainMaxRange) + Attr(m_self, AttrEntityCapacitorFallOff));
        double cycle(Attr(m_self, AttrEntityCapacitorDrainDuration));
        if (cycle < 1000.0)
            cycle = 10000.0;
        if (gap <= range) {
            double charge(Attr(tRef, AttrCapacitorCharge) - neut);
            tRef->SetAttribute(AttrCapacitorCharge, (charge < 0.0 ? 0.0 : charge));
            m_destiny->SendSpecialEffect(m_self->itemID(), m_self->itemID(), m_self->typeID(), pSE->GetID(), 0,
                                         "effects.EnergyDestabilization", true, true, true, static_cast<int32>(cycle), 1);
            m_nextNeut = now + cycle;
        } else {
            m_nextNeut = now + 1000.0;
        }
    }

    // ECM: one try per cycle.  the chance is jam strength over the ship's sensor strength, as for ECM drones
    double jam(Attr(m_self, AttrScanGravimetricStrengthBonus));
    if ((jam > 0) and (now >= m_nextJam)) {
        double range(Attr(m_self, AttrEntityTargetJamMaxRange) + Attr(m_self, AttrEntityTargetJamFallOff));
        double cycle(Attr(m_self, AttrEntityTargetJamDuration));
        if (cycle < 1000.0)
            cycle = JAM_CYCLE;
        m_nextJam = now + cycle;
        if ((gap <= range) and (pSE->TargetMgr() != nullptr)) {
            static const uint16 sensor[4] = {AttrScanGravimetricStrength, AttrScanLadarStrength, AttrScanMagnetometricStrength, AttrScanRadarStrength};
            double strength(0.0);
            for (uint8 i = 0; i < 4; ++i)
                strength = std::max(strength, Attr(tRef, sensor[i]));
            if (strength <= 0.0)
                strength = 1.0;
            m_destiny->SendSpecialEffect(m_self->itemID(), m_self->itemID(), m_self->typeID(), pSE->GetID(), 0,
                                         "effects.ElectronicAttributeModifyTarget", true, true, true, static_cast<int32>(cycle), 1);
            if (MakeRandomFloat(0.0, 1.0) < jam / strength) {
                pSE->TargetMgr()->Jam(static_cast<uint32>(cycle));
                if (pClient != nullptr)
                    pClient->SendNotifyMsg("You have been jammed by %s.", m_npc->GetName());
            }
        }
    }
}

/* remote shield repairs.  every cycle, up to maxTargets damaged Sansha ships in range get the repair amount, most damaged
 *  (by shield fraction) first.  a ship below its threshold counts as damaged.  logistics never rep themselves
 */
void NPCAIMgr::ProcessRepairs()
{
    double amount(Attr(m_self, AttrNpcRemoteShieldBoostAmount));
    if ((amount <= 0) or (m_repairsDisabled and !m_repairPlayers))
        return;
    double now(GetTimeMSeconds());
    if (now < m_nextRep)
        return;
    double cycle(Attr(m_self, AttrNpcRemoteShieldBoostDuration));
    if (cycle < 1000.0)
        cycle = 5000.0;
    m_nextRep = now + cycle;

    SystemBubble* pBubble(m_npc->SysBubble());
    if (pBubble == nullptr)
        return;
    double range(Attr(m_self, AttrNpcAssistanceRange));
    if (range <= 0)
        range = m_maxAttackRange;
    double threshold(Attr(m_self, AttrNpcRemoteShieldBoostThreshold));
    if ((threshold <= 0) or (threshold > 1.0))
        threshold = 0.95;
    uint8 maxTargets(static_cast<uint8>(Attr(m_self, AttrNpcRemoteShieldBoostMaxTargets)));
    if (maxTargets < 1)
        maxTargets = 1;

    // the friends that need it most.  after a mothership logistics hack, the pilots instead
    std::vector<SystemEntity*> entities;
    pBubble->GetEntityVec(entities);
    std::vector<std::pair<double, SystemEntity*>> hurt;
    for (auto cur : entities) {
        if (cur == m_npc)
            continue;
        if (m_repairPlayers) {
            if (!IsPlayerShip(cur))
                continue;
        } else {
            if (!cur->IsNPCSE())
                continue;
            NPC* pNPC(cur->GetNPCSE());
            if ((pNPC->GetAIMgr() == nullptr) or !pNPC->GetAIMgr()->IsIncursion())
                continue;
        }
        InventoryItemRef iRef(cur->GetSelf());
        double capacity(Attr(iRef, AttrShieldCapacity));
        if (capacity <= 0)
            continue;
        double frac(Attr(iRef, AttrShieldCharge) / capacity);
        if (frac >= threshold)
            continue;
        if (Gap(m_npc, cur) > range)
            continue;
        hurt.push_back(std::make_pair(frac, cur));
    }
    if (hurt.empty())
        return;
    std::sort(hurt.begin(), hurt.end(), [](const std::pair<double, SystemEntity*>& a, const std::pair<double, SystemEntity*>& b) { return a.first < b.first; });
    for (size_t i = 0; (i < hurt.size()) and (i < maxTargets); ++i) {
        SystemEntity* pSE(hurt[i].second);
        if (pSE->IsNPCSE()) {
            pSE->GetNPCSE()->GetAIMgr()->RepairShield(static_cast<float>(amount), m_npc);
            continue;
        }
        InventoryItemRef iRef(pSE->GetSelf());
        double capacity(Attr(iRef, AttrShieldCapacity));
        double charge(Attr(iRef, AttrShieldCharge) + amount);
        iRef->SetAttribute(AttrShieldCharge, (charge > capacity ? capacity : charge));
        pSE->SendDamageStateChanged();
        m_destiny->SendSpecialEffect(m_npc->GetID(), m_npc->GetID(), m_self->typeID(), pSE->GetID(), 0,
                                     "effects.ShieldTransfer", false, true, true, 5000, 1);
    }
}

void NPCAIMgr::RepairShield(float amount, SystemEntity* pFrom/*nullptr*/)
{
    double capacity(Attr(m_self, AttrShieldCapacity));
    double charge(Attr(m_self, AttrShieldCharge) + amount);
    m_self->SetAttribute(AttrShieldCharge, (charge > capacity ? capacity : charge), false);
    m_npc->SendDamageStateChanged();
    if ((pFrom != nullptr) and (pFrom->DestinyMgr() != nullptr))
        pFrom->DestinyMgr()->SendSpecialEffect(pFrom->GetID(), pFrom->GetID(), pFrom->GetSelf()->typeID(), m_npc->GetID(), 0,
                                               "effects.ShieldTransfer", false, true, true, 5000, 1);
}

/* the motherships' remote ECM burst.  every 60 to 300 seconds it tries to jam every player ship within range, with the
 *  burst's strength (25) against each ship's sensor strength.  it breaks locks, and with them the fleet's reps
 */
void NPCAIMgr::RemoteEcmBurst()
{
    if (!m_self->HasAttribute(AttrentityRemoteECMChanceOfActivation))
        return;
    double now(GetTimeMSeconds());
    if (now < m_nextBurst)
        return;
    m_nextBurst = now + MakeRandomInt(60, 300) * 1000;

    SystemBubble* pBubble(m_npc->SysBubble());
    if (pBubble == nullptr)
        return;
    double strength(Attr(m_self, AttrScanGravimetricStrengthBonus));
    if (strength <= 0)
        strength = 25.0;
    double range(Attr(m_self, AttrEcmBurstRange));
    if (range <= 0)
        range = 60000.0;
    double duration(Attr(m_self, AttrentityRemoteECMBaseDuration));
    if ((duration < 1000.0) or (duration > 60000.0))
        duration = JAM_CYCLE;

    m_destiny->SendSpecialEffect(m_self->itemID(), m_self->itemID(), m_self->typeID(), 0, 0, "effects.ECMBurst", true, true, true, 5000, 1);
    std::vector<Client*> clients;
    pBubble->GetPlayers(clients);
    uint16 jammed(0);
    for (auto cur : clients) {
        ShipSE* pShip(cur->GetShipSE());
        if ((pShip == nullptr) or cur->IsInvul() or (pShip->TargetMgr() == nullptr))
            continue;
        if (Gap(m_npc, pShip) > range)
            continue;
        static const uint16 sensor[4] = {AttrScanGravimetricStrength, AttrScanLadarStrength, AttrScanMagnetometricStrength, AttrScanRadarStrength};
        double sensors(0.0);
        for (uint8 i = 0; i < 4; ++i)
            sensors = std::max(sensors, Attr(pShip->GetSelf(), sensor[i]));
        if (sensors <= 0.0)
            sensors = 1.0;
        if (MakeRandomFloat(0.0, 1.0) >= strength / sensors)
            continue;
        pShip->TargetMgr()->Jam(static_cast<uint32>(duration));
        cur->SendNotifyMsg("%s's ECM burst has jammed you.", m_npc->GetName());
        ++jammed;
    }
    _log(NPC__AI_TRACE, "%s(%u): remote ECM burst jammed %u ships.", m_npc->GetName(), m_npc->GetID(), jammed);
}

void NPCAIMgr::StopPoint()
{
    if (m_pointID == 0)
        return;
    SystemEntity* pSE(m_npc->SystemMgr()->GetSE(m_pointID));
    if ((pSE != nullptr) and pSE->GetSelf()->HasAttribute(AttrWarpScrambleStatus)) {
        InventoryItemRef tRef(pSE->GetSelf());
        tRef->SetAttribute(AttrWarpScrambleStatus, tRef->GetAttribute(AttrWarpScrambleStatus).get_int() - m_pointStrength);
        m_destiny->SendSpecialEffect(m_self->itemID(), m_self->itemID(), m_self->typeID(), m_pointID, 0,
                                     "effects.WarpScramble", true, false, false, 5000, 0);
    }
    m_pointID = 0;
    m_pointStrength = 0;
}

void NPCAIMgr::StopWeb()
{
    if (m_webID == 0)
        return;
    SystemEntity* pSE(m_npc->SystemMgr()->GetSE(m_webID));
    if ((pSE != nullptr) and (pSE->DestinyMgr() != nullptr)) {
        pSE->DestinyMgr()->WebbedMe(m_self, false);
        m_destiny->SendSpecialEffect(m_self->itemID(), m_self->itemID(), m_self->typeID(), m_webID, 0,
                                     "effects.ModifyTargetSpeed", true, false, false, 5000, 0);
    }
    m_webID = 0;
}

void NPCAIMgr::StopPaint()
{
    if (m_paintID == 0)
        return;
    SystemEntity* pSE(m_npc->SystemMgr()->GetSE(m_paintID));
    if ((pSE != nullptr) and (m_paintFactor > 0)) {
        InventoryItemRef tRef(pSE->GetSelf());
        tRef->SetAttribute(AttrSignatureRadius, tRef->GetAttribute(AttrSignatureRadius).get_double() / m_paintFactor);
        m_destiny->SendSpecialEffect(m_self->itemID(), m_self->itemID(), m_self->typeID(), m_paintID, 0,
                                     "effects.TargetPaint", true, false, false, 5000, 0);
    }
    m_paintID = 0;
    m_paintFactor = 1.0;
}

void NPCAIMgr::ClearEwar()
{
    StopPoint();
    StopWeb();
    StopPaint();
}

void NPCAIMgr::LeaveGrid(const GPoint& to)
{
    if (m_leaving)
        return;
    ClearEwar();
    if (m_propOn)
        SetPropulsion(false);
    m_leaving = true;
    m_state = NPCAI::State::WarpOut;
    if (m_npc->TargetMgr() != nullptr)
        m_npc->TargetMgr()->ClearTargets(true);
    m_destiny->WarpTo(to);
}
