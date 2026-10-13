 /**
  * @name AmbientPilot.cpp
  *   the pilot of one ambient ship (doc/code_and_design_notes/ambient-npc-plan.md section 8, 9).  see AmbientPilot.h
  */

#include "eve-server.h"

#include <sstream>

#include "EVEServerConfig.h"
#include "inventory/AttributeEnum.h"
#include "npc/NPCAI.h"
#include "npc/ai/AmbientBrain.h"
#include "npc/ai/AmbientRules.h"
#include "npc/ambient/AmbientMgr.h"
#include "npc/ambient/AmbientNPC.h"
#include "npc/ambient/AmbientPilot.h"
#include "npc/ambient/AmbientSystem.h"
#include "system/Asteroid.h"
#include "system/CrimeWatch.h"
#include "system/DestinyManager.h"
#include "system/SystemBubble.h"
#include "system/SystemManager.h"

using namespace Ambient;

namespace {
    // the player jump range:  2500m from the gate's surface
    const double c_jumpRange = 2500.0;
    // under this a WarpTo is flown, not warped (the client's own WarpTo turns into a goto under 100km)
    const double c_minWarp = 150000.0;
    // a warp that hasn't started after this long is ordered again (a cut-short align, a bubble)
    const int64 c_retryMs = 10000;
    // a criminal next to CONCORD rolls to get away every 5s (8.5)
    const int64 c_fleeRollMs = 5000;
}

AmbientPilot::AmbientPilot(AmbientNPC* pNPC, AmbientSystem* pSystem, uint64_t seed)
: m_npc(pNPC),
  m_sys(pSystem),
  m_rng(seed, pNPC->GetID()),
  m_leg(0),
  m_legStarted(false),
  m_warpSeen(false),
  m_shortHop(false),
  m_removing(false),
  m_fleeing(false),
  m_paused(false),
  m_resume(false),
  m_flagged(false),
  m_waitUncloak(false),
  m_until(0),
  m_legStart(0),
  m_uncloakAt(0),
  m_lastDamage(0),
  m_noPreySince(0),
  m_nextFleeRoll(0),
  m_nodeID(0),
  m_nodeKind(0),
  m_goal(NULL_ORIGIN),
  m_mineState(MinePick),
  m_rockID(0),
  m_cycles(0),
  m_fullAt(0),
  m_cycleEnd(0),
  m_beams(false)
{
}

void AmbientPilot::SetLegs(const std::vector<LegData>& legs, size_t first/*0*/)
{
    m_legs = legs;
    m_leg = first;
    m_legStarted = false;
}

SystemEntity* AmbientPilot::GetSE(uint32 id) const
{
    return ((id == 0) ? nullptr : m_npc->SystemMgr()->GetSE(id));
}

void AmbientPilot::SetSpeed()
{
    const Archetype* pArch(m_npc->GetSpec().arch);
    if (pArch != nullptr)
        m_npc->DestinyMgr()->SetMaxVelocity(pArch->maxVelocity);
}

void AmbientPilot::Begin(uint8 start, uint32 startNode, uint8 startNodeKind, int64 now)
{
    m_nodeID = startNode;
    m_nodeKind = startNodeKind;
    // the damage the ai has seen before we started doesn't count
    m_lastDamage = m_npc->GetAIMgr()->GetLastDamagedAt();
    SetSpeed();
    switch (start) {
        case Start::JumpIn: {
            /* 9.2:  session cloaked since before it was added, so no one got the ball.  the uncloak shows the shimmer and adds
             *  the ball, as for a pilot who jumped in and started moving */
            m_uncloakAt = now + AmbientRules::UniformMs(m_rng, sConfig.ambient.ArrivalCloakMin * 1000.0,
                                                         sConfig.ambient.ArrivalCloakMax * 1000.0);
            m_waitUncloak = true;
        } break;
        case Start::Undock: {
            // 9.3:  AddBalls at the undock point, then the undock velocity and GotoDirection.  no fx exists for it
            GVector dir(m_sys->UndockDirection(startNode));
            m_npc->DestinyMgr()->Undock(dir);
            StartLeg(now);
        } break;
        default:
            StartLeg(now);
            break;
    }
}

void AmbientPilot::StartLeg(int64 now)
{
    m_legStarted = true;
    m_legStart = now;
    m_warpSeen = false;
    m_shortHop = false;
    m_until = 0;
    if (m_leg >= m_legs.size()) {
        // nothing left:  leave the system the way traffic does
        BuildLeave(now);
        if (m_leg >= m_legs.size()) {
            Finish(Removal::Done);
            return;
        }
    }
    const LegData& leg(m_legs[m_leg]);
    _log(AMBIENT__TRACE, "%s(%u) [%u]: leg %u/%u %s %u", m_npc->GetName(), m_npc->GetID(), m_npc->GetTravellerID(),
         (uint32)m_leg + 1, (uint32)m_legs.size(), LegName(leg.kind), leg.targetID);
    DestinyManager* pDestiny(m_npc->DestinyMgr());
    switch (leg.kind) {
        case Leg::Hold: {
            m_until = now + leg.holdMs;
        } break;
        case Leg::WarpTo: {
            DoWarp(leg, now);
        } break;
        case Leg::ApproachGate: {
            SystemEntity* pGate(GetSE(leg.targetID));
            if (pGate == nullptr) {
                NextLeg(now);
                return;
            }
            SetSpeed();
            double surface(m_npc->GetPosition().distance(pGate->GetPosition()) - m_npc->GetRadius() - pGate->GetRadius());
            if (surface <= c_jumpRange) {
                // landed in jump range:  a moment, then the jump (pilots don't jump the same tick they land)
                m_until = now + AmbientRules::UniformMs(m_rng, 500.0, 3500.0);
            } else {
                pDestiny->Follow(pGate, 500);
            }
        } break;
        case Leg::JumpOut: {
            DoJumpOut(leg.targetID, now);
        } break;
        case Leg::ApproachDock: {
            SetSpeed();
            m_goal = m_sys->DockPoint(leg.targetID);
            pDestiny->GotoPoint(m_goal);
        } break;
        case Leg::Dock: {
            Finish(Removal::Docked);
        } break;
        case Leg::Loiter: {
            SystemEntity* pObj(GetSE(leg.targetID));
            if (pObj != nullptr) {
                SetSpeed();
                pDestiny->Orbit(pObj, static_cast<uint32>(m_rng.Range(15000.0, 30000.0)));
            }
            m_until = now + leg.holdMs;
        } break;
        case Leg::Mine: {
            m_mineState = MinePick;
            m_rockID = 0;
            if (m_fullAt == 0)
                m_fullAt = AmbientRules::MinerCycles(m_rng, sConfig.ambient.MinerCyclesMin, sConfig.ambient.MinerCyclesMax);
        } break;
        case Leg::Visit: {
            SystemEntity* pMiner(GetSE(leg.targetID));
            if (pMiner == nullptr) {
                NextLeg(now);
                return;
            }
            SetSpeed();
            pDestiny->Follow(pMiner, 1500);
            m_until = now + leg.holdMs;
        } break;
        case Leg::Hunt: {
            m_noPreySince = now;
        } break;
        case Leg::Leave: {
            BuildLeave(now);
            StartLeg(now);
        } break;
        case Leg::Squad:
        default:
            break;
    }
}

void AmbientPilot::NextLeg(int64 now)
{
    ++m_leg;
    StartLeg(now);
}

void AmbientPilot::BuildLeave(int64 now)
{
    // the rest of the trip:  dock at one of the system's stations, else jump out through one of its gates
    std::vector<LegData> legs;
    uint32 stationID(m_sys->PickStation(m_rng, m_npc->SysBubble()));
    if (stationID != 0) {
        LegData warp;
        warp.kind = Leg::WarpTo;
        warp.node = AmbientRules::Node::Station;
        warp.targetID = stationID;
        legs.push_back(warp);
        LegData approach;
        approach.kind = Leg::ApproachDock;
        approach.targetID = stationID;
        legs.push_back(approach);
        LegData dock;
        dock.kind = Leg::Dock;
        dock.targetID = stationID;
        legs.push_back(dock);
    } else {
        uint32 gateID(m_sys->PickGate(m_rng, m_npc->SysBubble()));
        if (gateID == 0) {
            m_legs.erase(m_legs.begin() + std::min(m_leg, m_legs.size()), m_legs.end());
            return;
        }
        LegData warp;
        warp.kind = Leg::WarpTo;
        warp.node = AmbientRules::Node::Gate;
        warp.targetID = gateID;
        legs.push_back(warp);
        LegData approach;
        approach.kind = Leg::ApproachGate;
        approach.targetID = gateID;
        legs.push_back(approach);
        LegData jump;
        jump.kind = Leg::JumpOut;
        jump.targetID = gateID;
        legs.push_back(jump);
    }
    m_legs.erase(m_legs.begin() + std::min(m_leg, m_legs.size()), m_legs.end());
    m_legs.insert(m_legs.end(), legs.begin(), legs.end());
}

void AmbientPilot::DoWarp(const LegData& leg, int64 now)
{
    GPoint target;
    double radius(0.0);
    if (!m_sys->NodePosition(leg.targetID, leg.node, target, radius)) {
        NextLeg(now);
        return;
    }
    double landing(AmbientRules::LandingDistance(m_rng, leg.node, radius));
    // patrols land spread out (8.3)
    if (m_npc->GetSpec().squadID != 0)
        landing = std::max(500.0, landing + m_rng.Range(-3000.0, 3000.0));
    SetSpeed();
    DestinyManager* pDestiny(m_npc->DestinyMgr());
    double distance(m_npc->GetPosition().distance(target));
    if ((distance - landing) < c_minWarp) {
        // close by:  fly there (a short hop on the same or the next grid)
        GVector back(target, m_npc->GetPosition());
        back.normalize();
        m_goal = target + (back * landing);
        m_shortHop = true;
        pDestiny->GotoPoint(m_goal);
        return;
    }
    const Archetype* pArch(m_npc->GetSpec().arch);
    pDestiny->WarpTo(target, static_cast<int32>(landing), false, nullptr, (pArch != nullptr ? pArch->warpFactor : 0), false, true);
}

bool AmbientPilot::UpdateWarp(int64 now)
{
    DestinyManager* pDestiny(m_npc->DestinyMgr());
    if (m_shortHop) {
        if ((m_npc->GetPosition().distance(m_goal) < 2000.0) or ((now - m_legStart) > 240000)) {
            pDestiny->Stop();
            return true;
        }
        return false;
    }
    if (pDestiny->IsWarping()) {
        m_warpSeen = true;
        return false;
    }
    if (pDestiny->IsAligningForWarp()) {
        // a freighter takes 1.5 minutes to align.  far longer means something stopped us
        if ((now - m_legStart) > std::max<int64>(120000, static_cast<int64>(pDestiny->GetAlignTime() * 6000.0f)))
            StartLeg(now);
        return false;
    }
    if (m_warpSeen)
        return true;
    // the warp didn't start (scrambled, a warp disruption field, a stop):  again a little later
    if (m_until == 0) {
        m_until = now + c_retryMs;
    } else if (now >= m_until) {
        StartLeg(now);
    }
    return false;
}

void AmbientPilot::Arrived(int64 now)
{
    const LegData& leg(m_legs[m_leg]);
    m_nodeID = leg.targetID;
    m_nodeKind = leg.node;
    if (m_fleeing) {
        // off grid and gone (no kill, no wreck)
        _log(AMBIENT__TRACE, "%s(%u) [%u]: got away.", m_npc->GetName(), m_npc->GetID(), m_npc->GetTravellerID());
        Finish(Removal::Fled);
        return;
    }
    // nobody watches the grid we landed on:  the rest of the trip goes on off grid (6.8 rule 4, 8.1)
    if (!m_sys->IsObservedBubble(m_npc->SysBubble())) {
        // traffic and miners on their way out go on as travellers.  squads and criminals are the system's to bring back
        const Spec& spec(m_npc->GetSpec());
        bool travels((spec.squadID == 0) and (spec.role != Role::Criminal));
        // (the next warp goes to a watched grid:  it goes there for real)
        if (travels and ((m_leg + 1) < m_legs.size()) and (m_legs[m_leg + 1].kind == Leg::WarpTo)
        and m_sys->IsNodeObserved(m_legs[m_leg + 1].targetID)) {
            NextLeg(now);
            return;
        }
        if (travels and ((m_leg + 1) < m_legs.size()))
            m_sys->Dematerialise(m_npc, MakeTraveller(m_leg + 1, now));
        Finish(Removal::OffGrid);
        return;
    }
    NextLeg(now);
}

void AmbientPilot::DoJumpOut(uint32 gateID, int64 now)
{
    // 9.3:  the jump effect on the ship (target the gate:  it hides the ship after 880ms) and the gate's activity flash, then a
    //  silent removal.  the ship goes on in the next system (coherence, 6.8 rule 1)
    DestinyManager* pDestiny(m_npc->DestinyMgr());
    pDestiny->Stop();
    pDestiny->SendJumpOut(gateID);
    pDestiny->SendGateActivity(gateID);
    m_sys->OnJumpOut(m_npc, gateID, MakeTraveller(m_leg + 1, now));
    Finish(Removal::Jumped, sConfig.ambient.JumpOutRemoveDelay);
}

void AmbientPilot::Finish(uint8 reason, int64 delayMs/*0*/)
{
    if (m_removing)
        return;
    m_removing = true;
    StopBeams();
    m_sys->QueueRemoval(m_npc->GetID(), reason, delayMs);
}

void AmbientPilot::OnKilled()
{
    StopBeams();
    m_removing = true;
}

void AmbientPilot::OnRemoving()
{
    StopBeams();
    m_removing = true;
}

Traveller AmbientPilot::MakeTraveller(size_t from, int64 now) const
{
    Traveller t;
    t.spec = m_npc->GetSpec();
    t.spec.fakeModules.clear();     // new ids if it comes back
    if (from < m_legs.size())
        t.legs.assign(m_legs.begin() + from, m_legs.end());
    t.leg = 0;
    t.atNode = m_nodeID;
    t.atNodeKind = m_nodeKind;
    t.nextAt = now;
    t.created = now;
    return t;
}

bool AmbientPilot::IsStationary() const
{
    if (m_leg >= m_legs.size())
        return false;
    switch (m_legs[m_leg].kind) {
        case Leg::Mine:
        case Leg::Loiter:
        case Leg::Hunt:
        case Leg::Squad:
        case Leg::Visit:
            return true;
        default:
            return false;
    }
}

bool AmbientPilot::IsBusy() const
{
    if (m_removing or (m_leg >= m_legs.size()))
        return true;
    uint8 kind(m_legs[m_leg].kind);
    return ((kind == Leg::WarpTo) or (kind == Leg::Hold));
}

bool AmbientPilot::AwaitingOrders() const
{
    return (!m_removing and !m_fleeing and (m_leg < m_legs.size()) and (m_legs[m_leg].kind == Leg::Squad));
}

bool AmbientPilot::MayTarget(SystemEntity* pSE)
{
    uint8 role(m_npc->GetRole());
    uint8 mode(IsMilitary(role) ? AmbientBrain::Military : (role == Role::Criminal ? AmbientBrain::Criminal : AmbientBrain::Civilian));
    return AmbientBrain::MayTarget(mode, m_npc, pSE);
}

void AmbientPilot::Tick(int64 now)
{
    if (m_removing or m_npc->IsDead())
        return;
    NPCAIMgr* pAI(m_npc->GetAIMgr());

    // gate arrival:  the uncloak, then the first leg (9.2)
    if (m_uncloakAt != 0) {
        if (now < m_uncloakAt)
            return;
        m_uncloakAt = 0;
        // UnCloak() sends the shimmer before the ball:  observers' clients drop an effect on a ball they don't have yet, so
        //  the shimmer goes again once the ball is there
        m_npc->DestinyMgr()->UnCloak(Destiny::Cloak::Session);
        m_npc->DestinyMgr()->SendCloakFx(false);
        if (m_waitUncloak) {
            m_waitUncloak = false;
            StartLeg(now);
        }
        return;
    }

    // civilians never fight:  hurt, they leave (8.1)
    if (IsCivilian(m_npc->GetRole()) and (pAI->GetLastDamagedAt() > m_lastDamage)) {
        m_lastDamage = pAI->GetLastDamagedAt();
        if (!m_fleeing) {
            Flee(now);
            return;
        }
    }
    if (m_npc->GetRole() == Role::Criminal) {
        CriminalTick(now);
        if (m_removing)
            return;
    }

    // a fight:  the ai has the ship (fleeing pilots keep it)
    if (!pAI->IsIdle() and !m_fleeing) {
        m_paused = true;
        return;
    }
    if (m_paused or m_resume) {
        m_paused = false;
        m_resume = false;
        StopBeams();
        StartLeg(now);
        return;
    }
    if (!m_legStarted) {
        StartLeg(now);
        return;
    }
    if (m_leg >= m_legs.size())
        return;

    const LegData& leg(m_legs[m_leg]);
    switch (leg.kind) {
        case Leg::Hold: {
            if (now >= m_until)
                NextLeg(now);
        } break;
        case Leg::WarpTo: {
            if (UpdateWarp(now))
                Arrived(now);
        } break;
        case Leg::ApproachGate: {
            if ((m_until != 0) and (now < m_until))
                break;
            SystemEntity* pGate(GetSE(leg.targetID));
            if (pGate == nullptr) {
                NextLeg(now);
                break;
            }
            double surface(m_npc->GetPosition().distance(pGate->GetPosition()) - m_npc->GetRadius() - pGate->GetRadius());
            if ((surface <= c_jumpRange) or ((m_until != 0) and (now >= m_until))) {
                NextLeg(now);
            } else if ((now - m_legStart) > 180000) {
                // stuck (bumped, blocked):  jump from where we are if close enough, else again
                if (surface < 10000.0) {
                    NextLeg(now);
                } else {
                    StartLeg(now);
                }
            }
        } break;
        case Leg::ApproachDock: {
            SystemEntity* pStation(GetSE(leg.targetID));
            double toCentre(pStation != nullptr ? m_npc->GetPosition().distance(pStation->GetPosition()) - pStation->GetRadius() : 0.0);
            if ((m_npc->GetPosition().distance(m_goal) < 500.0) or (toCentre < 1500.0) or ((now - m_legStart) > 240000))
                NextLeg(now);
        } break;
        case Leg::Loiter:
        case Leg::Visit: {
            if ((m_until != 0) and (now >= m_until))
                NextLeg(now);
            else if ((leg.kind == Leg::Visit) and (GetSE(leg.targetID) == nullptr))
                NextLeg(now);
        } break;
        case Leg::Mine: {
            MineTick(now);
        } break;
        case Leg::Hunt:
        case Leg::Squad:
        default:
            break;
    }
}

void AmbientPilot::Flee(int64 now)
{
    StopBeams();
    m_fleeing = true;
    _log(AMBIENT__TRACE, "%s(%u) [%u]: hurt.  fleeing.", m_npc->GetName(), m_npc->GetID(), m_npc->GetTravellerID());
    // a station close by:  dock (8, Flee).  else warp to a celestial off this grid
    std::vector<LegData> legs;
    uint32 stationID(m_sys->StationNear(m_npc->GetPosition(), 15000.0));
    if (stationID != 0) {
        m_fleeing = false;  // docking is the way out:  it isn't a warp off grid
        LegData approach;
        approach.kind = Leg::ApproachDock;
        approach.targetID = stationID;
        legs.push_back(approach);
        LegData dock;
        dock.kind = Leg::Dock;
        dock.targetID = stationID;
        legs.push_back(dock);
    } else {
        LegData warp;
        warp.kind = Leg::WarpTo;
        if (!m_sys->FleeNode(m_npc->GetPosition(), m_npc->SysBubble(), m_rng, warp.targetID, warp.node)) {
            Finish(Removal::Fled);
            return;
        }
        legs.push_back(warp);
    }
    SetLegs(legs, 0);
    StartLeg(now);
}

void AmbientPilot::CriminalTick(int64 now)
{
    NPCAIMgr* pAI(m_npc->GetAIMgr());
    // the first offensive act puts our crime on record:  a global criminal flag (sentries shoot), CONCORD in high sec (8.5)
    if (!m_flagged and !pAI->IsIdle() and (pAI->GetPrimaryID() != 0)) {
        m_flagged = true;
        sCrimeWatch.EntityCrime(m_npc, GetSE(pAI->GetPrimaryID()));
    }
    if (m_fleeing)
        return;
    // hurt down to the structure:  get away
    double hp(m_npc->GetSelf()->GetAttribute(AttrHP).get_double());
    double damage(m_npc->GetSelf()->GetAttribute(AttrDamage).get_double());
    if ((hp > 0.0) and ((1.0 - (damage / hp)) < sConfig.ambient.CriminalFleeHull)) {
        CriminalLeave(now, "hurt");
        return;
    }
    // CONCORD on our grid in high sec:  an even chance every 5s to run
    if (m_sys->IsHighSec() and (now >= m_nextFleeRoll)) {
        m_nextFleeRoll = now + c_fleeRollMs;
        if (m_sys->ConcordOnGrid(m_npc->SysBubble()) and m_rng.Chance(0.5)) {
            CriminalLeave(now, "CONCORD");
            return;
        }
    }
    // no prey for a while:  leave
    if ((m_leg < m_legs.size()) and (m_legs[m_leg].kind == Leg::Hunt)) {
        if (!pAI->IsIdle()) {
            m_noPreySince = 0;
        } else if (m_noPreySince == 0) {
            m_noPreySince = now;
        } else if ((now - m_noPreySince) > (static_cast<int64>(sConfig.ambient.CriminalHuntTimeout) * 1000)) {
            CriminalLeave(now, "no prey");
        }
    }
}

void AmbientPilot::CriminalLeave(int64 now, const char* why)
{
    _log(AMBIENT__TRACE, "%s(%u) [%u]: leaving (%s).", m_npc->GetName(), m_npc->GetID(), m_npc->GetTravellerID(), why);
    LegData warp;
    warp.kind = Leg::WarpTo;
    if (!m_sys->FleeNode(m_npc->GetPosition(), m_npc->SysBubble(), m_rng, warp.targetID, warp.node))
        return;
    // the ai drops its fight and locks;  a scramble keeps us here (UpdateWarp retries)
    m_npc->GetAIMgr()->BeginWarp(NPCAI::State::Fleeing);
    m_fleeing = true;
    std::vector<LegData> legs(1, warp);
    SetLegs(legs, 0);
    StartLeg(now);
}

/* ------------------------------------------------------------------------------------------------------------------
 *  miners (8.2, 9.4)
 * ------------------------------------------------------------------------------------------------------------------ */
bool AmbientPilot::PickRock(int64 now)
{
    // the nearest rock without another miner on it (within 5km), within 30km of where we are when switching
    std::vector<AsteroidSE*> rocks;
    m_sys->BeltRocks(m_npc->SysBubble(), rocks);
    AsteroidSE* pBest(nullptr);
    double best(0.0);
    bool switching(m_rockID != 0);
    for (auto cur : rocks) {
        if ((cur == nullptr) or cur->IsDead() or (cur->GetID() == m_rockID))
            continue;
        double distance(m_npc->GetPosition().distance(cur->GetPosition()));
        if (switching and (distance > 30000.0))
            continue;
        if (m_sys->RockTaken(cur, m_npc))
            continue;
        if ((pBest == nullptr) or (distance < best)) {
            pBest = cur;
            best = distance;
        }
    }
    if (pBest == nullptr)
        return false;
    m_rockID = pBest->GetID();
    return true;
}

void AmbientPilot::MineTick(int64 now)
{
    DestinyManager* pDestiny(m_npc->DestinyMgr());
    const AmbientCatalog& catalog(sAmbientMgr.Catalog());
    double range(catalog.MinerRange());
    switch (m_mineState) {
        case MinePick: {
            if (!PickRock(now)) {
                // mined out, or nothing near:  off to sell what we have
                _log(AMBIENT__TRACE, "%s(%u): no rock to mine.  leaving.", m_npc->GetName(), m_npc->GetID());
                ++m_leg;
                BuildLeave(now);
                StartLeg(now);
                return;
            }
            SystemEntity* pRock(GetSE(m_rockID));
            SetSpeed();
            if (m_npc->IsHull()) {
                // within 0.8 x the strip miner's range of the rock's surface
                pDestiny->Follow(pRock, static_cast<uint32>(range * 0.6));
            } else {
                // entity mode:  no beam can be shown, so it orbits the rock (9.4)
                pDestiny->Orbit(pRock, static_cast<uint32>(m_rng.Range(2000.0, 5000.0)));
            }
            m_mineState = MineApproach;
            m_legStart = now;
        } break;
        case MineApproach: {
            SystemEntity* pRock(GetSE(m_rockID));
            if ((pRock == nullptr) or pRock->IsDead()) {
                m_mineState = MinePick;
                break;
            }
            double surface(m_npc->GetPosition().distance(pRock->GetPosition()) - m_npc->GetRadius() - pRock->GetRadius());
            if ((surface <= (range * 0.8)) or ((now - m_legStart) > 180000)) {
                if (m_npc->IsHull())
                    pDestiny->Stop();
                StartBeams(now);
                m_mineState = MineCycling;
            }
        } break;
        case MineCycling: {
            // the strip miners start one by one (300 - 1500ms apart)
            if (m_npc->IsHull() and (catalog.MinerModuleType() != 0)) {
                uint32 left((m_fullAt > m_cycles) ? (m_fullAt - m_cycles) : 1);
                for (size_t i = 0; i < m_beamAt.size(); ++i) {
                    if ((m_beamAt[i] == 0) or (now < m_beamAt[i]))
                        continue;
                    m_beamAt[i] = 0;
                    if (i < m_npc->GetSpec().fakeModules.size())
                        pDestiny->SendSpecialEffect(m_npc->GetID(), m_npc->GetSpec().fakeModules[i], catalog.MinerModuleType(),
                                                    m_rockID, 0, "effects.Mining", false, true, true,
                                                    static_cast<int32>(catalog.MinerCycleMs()), left);
                }
            }
            if (now < m_cycleEnd)
                break;
            ++m_cycles;
            SystemEntity* pRock(GetSE(m_rockID));
            if (m_cycles >= m_fullAt) {
                // full:  off to sell it
                StopBeams();
                _log(AMBIENT__TRACE, "%s(%u): full after %u cycles.  leaving.", m_npc->GetName(), m_npc->GetID(), m_cycles);
                ++m_leg;
                BuildLeave(now);
                StartLeg(now);
                return;
            }
            if ((pRock == nullptr) or pRock->IsDead()) {
                StopBeams();
                m_mineState = MinePick;
                break;
            }
            if ((m_cycles >= 2) and m_rng.Chance(sConfig.ambient.MinerRockSwitchChance)) {
                StopBeams();
                m_mineState = MinePick;
                break;
            }
            m_cycleEnd = now + catalog.MinerCycleMs();
        } break;
    }
}

void AmbientPilot::StartBeams(int64 now)
{
    m_cycleEnd = now + sAmbientMgr.Catalog().MinerCycleMs();
    m_beams = m_npc->IsHull();
    m_beamAt.clear();
    int64 at(now);
    for (size_t i = 0; i < m_npc->GetSpec().fakeModules.size(); ++i) {
        m_beamAt.push_back(at);
        at += AmbientRules::UniformMs(m_rng, 300.0, 1500.0);
    }
}

void AmbientPilot::StopBeams()
{
    if (!m_beams)
        return;
    m_beams = false;
    m_beamAt.clear();
    if ((m_rockID == 0) or (m_npc->SysBubble() == nullptr))
        return;
    uint16 moduleType(sAmbientMgr.Catalog().MinerModuleType());
    for (auto cur : m_npc->GetSpec().fakeModules)
        m_npc->DestinyMgr()->SendSpecialEffect(m_npc->GetID(), cur, moduleType, m_rockID, 0, "effects.Mining", false, false, false, 0, 0);
}

void AmbientPilot::WarmMiner()
{
    if (m_fullAt == 0)
        m_fullAt = AmbientRules::MinerCycles(m_rng, sConfig.ambient.MinerCyclesMin, sConfig.ambient.MinerCyclesMax);
    m_cycles = static_cast<uint32>(m_rng.Uniform() * static_cast<double>(m_fullAt > 1 ? m_fullAt - 1 : 0));
}

void AmbientPilot::ResendBeams(int64 now)
{
    // a pilot who arrives mid-cycle sees the beams at once (9.4):  the start again, with what is left of the cycle
    if (!m_beams or (m_mineState != MineCycling) or (m_rockID == 0) or (now >= m_cycleEnd))
        return;
    const AmbientCatalog& catalog(sAmbientMgr.Catalog());
    uint32 left((m_fullAt > m_cycles) ? (m_fullAt - m_cycles) : 1);
    for (size_t i = 0; i < m_npc->GetSpec().fakeModules.size(); ++i) {
        if ((i < m_beamAt.size()) and (m_beamAt[i] != 0))
            continue;   // not started yet
        m_npc->DestinyMgr()->SendSpecialEffect(m_npc->GetID(), m_npc->GetSpec().fakeModules[i], catalog.MinerModuleType(), m_rockID,
                                               0, "effects.Mining", false, true, true, static_cast<int32>(m_cycleEnd - now), left);
    }
}

/* ------------------------------------------------------------------------------------------------------------------
 *  squads (8.3, 8.4)
 * ------------------------------------------------------------------------------------------------------------------ */
void AmbientPilot::SquadLoiter(uint32 objectID, int64 holdMs, int64 now)
{
    std::vector<LegData> legs;
    LegData loiter;
    loiter.kind = Leg::Loiter;
    loiter.targetID = objectID;
    loiter.holdMs = holdMs;
    legs.push_back(loiter);
    LegData squad;
    squad.kind = Leg::Squad;
    legs.push_back(squad);
    SetLegs(legs, 0);
    if (m_npc->GetAIMgr()->IsIdle())
        StartLeg(now);
}

void AmbientPilot::SquadWarp(uint32 nodeID, uint8 nodeKind, int64 delayMs, int64 now)
{
    std::vector<LegData> legs;
    if (delayMs > 0) {
        LegData hold;
        hold.kind = Leg::Hold;
        hold.holdMs = delayMs;
        legs.push_back(hold);
    }
    LegData warp;
    warp.kind = Leg::WarpTo;
    warp.node = nodeKind;
    warp.targetID = nodeID;
    legs.push_back(warp);
    // the squad picks the loiter object again once we are there
    LegData squad;
    squad.kind = Leg::Squad;
    legs.push_back(squad);
    SetLegs(legs, 0);
    if (m_npc->GetAIMgr()->IsIdle())
        StartLeg(now);
}

void AmbientPilot::SquadEngage(SystemEntity* pTarget)
{
    NPCAIMgr* pAI(m_npc->GetAIMgr());
    if ((pTarget != nullptr) and pAI->IsIdle() and pAI->CanAggro(pTarget))
        pAI->QueueAggro(pTarget);
}

std::string AmbientPilot::Describe() const
{
    std::ostringstream str;
    const Spec& spec(m_npc->GetSpec());
    str << RoleName(spec.role) << " \"" << spec.name << "\" traveller " << spec.travellerID << ", owner " << spec.ownerID
        << ", faction " << spec.factionID;
    if (spec.squadID != 0)
        str << ", squad " << spec.squadID;
    if (spec.groupID != 0)
        str << ", group " << spec.groupID;
    str << "<br>legs:";
    for (size_t i = 0; i < m_legs.size(); ++i)
        str << (i == m_leg ? " [" : " ") << LegName(m_legs[i].kind) << " " << m_legs[i].targetID << (i == m_leg ? "]" : "");
    str << "<br>" << (m_fleeing ? "fleeing " : "") << (m_paused ? "fighting " : "") << (m_removing ? "leaving space " : "");
    if (spec.role == Role::Miner)
        str << "rock " << m_rockID << ", cycles " << m_cycles << "/" << m_fullAt;
    str << "<br>";
    return str.str();
}
