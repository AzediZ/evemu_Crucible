 /**
  * @name LevelOneBrain.cpp
  *   the level-one ai (PLAN.md 4.6, 4.7, 5.7).  see LevelOneBrain.h
  */

#include "eve-server.h"

#include <iomanip>

#include "EVEServerConfig.h"
#include "npc/Drone.h"
#include "npc/NPC.h"
#include "npc/NPCAI.h"
#include "npc/ai/AggroGroup.h"
#include "npc/ai/LevelOneBrain.h"
#include "npc/ai/NpcMath.h"
#include "npc/ai/NpcProfile.h"
#include "npc/ai/NpcRng.h"
#include "system/DestinyManager.h"
#include "system/SystemBubble.h"
#include "system/SystemManager.h"
#include "system/TargetManager.h"
#include "system/cosmicMgrs/DungeonMgr.h"

namespace {
    // the jam response's goto point is sent again only when it moved this much (m)
    const double c_jamGotoSlack = 5000.0;

    bool IsCapitalShip(SystemEntity* pSE)
    {
        if ((pSE == nullptr) or !pSE->IsShipSE())
            return false;
        switch (pSE->GetGroupID()) {
            case EVEDB::invGroups::Carrier:
            case EVEDB::invGroups::Dreadnought:
            case EVEDB::invGroups::Supercarrier:
            case EVEDB::invGroups::Titan:
            case EVEDB::invGroups::CapitalIndustrialShip:
                return true;
        }
        return false;
    }

    // a ship with a pilot that isn't in a pod
    bool IsPilotedShip(SystemEntity* pSE)
    {
        return ((pSE != nullptr) and pSE->IsShipSE() and pSE->HasPilot() and !pSE->GetPilot()->InPod());
    }
}

LevelOneBrain::LevelOneBrain(const NpcProfile& profile)
: m_preferredSig(0.0),
  m_nextEval(0),
  m_nextDecay(0),
  m_jamGotoTarget(0),
  m_jamCuddle(false),
  m_jamGoto(NULL_ORIGIN),
  m_newcomerCount(0)
{
    // "if it is 0 or below it uses the ship's signature radius instead" (1655)
    if (profile.l1SignatureRadius)
        m_preferredSig = ((profile.l1PreferredSig > 0.0f) ? profile.l1PreferredSig : profile.sigRadius);
    for (size_t i = 0; i < c_maxRanked; ++i) {
        m_ranked[i] = nullptr;
        m_rankScore[i] = 0.0;
        m_newcomers[i] = nullptr;
    }
}

int64 LevelOneBrain::SwitchTimerMs(const NpcProfile& profile)
{
    // AI_TargetSwitchTimer (1416, dgm default 60s);  npc.TargetSwitchTime > 0 replaces it
    if (sConfig.npc.TargetSwitchTime > 0)
        return static_cast<int64>(sConfig.npc.TargetSwitchTime) * 1000;
    return static_cast<int64>(profile.l1SwitchTimerMs);
}

double LevelOneBrain::Score(NpcContext& ctx, SystemEntity* pSE)
{
    if (pSE == nullptr)
        return 0.0;
    NpcMath::L1ScoreInputs in = NpcMath::L1ScoreInputs();
        in.threat = ctx.ai->GetThreat(pSE->GetID());
        in.targetSig = pSE->GetSignatureRadius();
        in.preferredSig = m_preferredSig;
        in.useSigRadius = ctx.profile.l1SignatureRadius;
        in.droneRecentDamager = (pSE->IsDroneSE() and (ctx.ai->RecentDamageBy(pSE->GetID()) > 0.0f));
        in.inAttackRange = (ctx.ai->SurfaceDistance(pSE) <= ctx.profile.attackRange);
        in.inAcquireRange = (ctx.npc->GetPosition().distance(pSE->GetPosition()) <= ctx.ai->AcquireRange());
        in.locked = (ctx.npc->TargetMgr()->GetTarget(pSE->GetID(), false) != nullptr);
    return NpcMath::L1Score(in);
}

size_t LevelOneBrain::Rank(NpcContext& ctx, SystemEntity** out, double* scores, size_t max)
{
    if ((ctx.npc->SysBubble() == nullptr) or (max == 0))
        return 0;
    // a passive group that was never aggroed doesn't pick anyone;  an aggroed one only the pilots it remembers
    AggroGroup* pGroup(ctx.ai->GetGroup());
    AggroGroup* pOnly(nullptr);
    if ((pGroup != nullptr) and pGroup->IsPassive()) {
        if (!pGroup->WasAlerted())
            return 0;
        pOnly = pGroup;
    }
    double distances[c_maxRanked];
    if (max > c_maxRanked)
        max = c_maxRanked;
    size_t count(0);
    ctx.npc->SysBubble()->ForEachDynamic([&](SystemEntity* cur) {
        // ships and player drones out to the undamped R_acq (damped:  we close in until we can lock, 4.6)
        if (!ctx.ai->IsCandidate(cur, true, true))
            return;
        if ((pOnly != nullptr) and !pOnly->IsAggressor(cur, ctx.now))
            return;
        double score(Score(ctx, cur));
        if (!(score > 0.0))
            return;
        double distance(ctx.npc->GetPosition().distance(cur->GetPosition()));
        // insertion into the best-first list:  higher score first, ties to the nearer one
        size_t pos(count);
        while ((pos > 0) and ((scores[pos - 1] < score) or ((scores[pos - 1] == score) and (distances[pos - 1] > distance))))
            --pos;
        if (pos >= max)
            return;
        size_t last((count < max) ? count : (max - 1));
        for (size_t i = last; i > pos; --i) {
            out[i] = out[i - 1];
            scores[i] = scores[i - 1];
            distances[i] = distances[i - 1];
        }
        out[pos] = cur;
        scores[pos] = score;
        distances[pos] = distance;
        if (count < max)
            ++count;
    });
    return count;
}

void LevelOneBrain::Decay(NpcContext& ctx)
{
    int64 interval(static_cast<int64>(std::max<uint32>(sConfig.npc.L1ThreatDecayInterval, 1)) * 1000);
    if (m_nextDecay == 0) {
        m_nextDecay = ctx.now + interval;
        return;
    }
    if (ctx.now < m_nextDecay)
        return;
    // every interval that passed (a dormant npc wakes up with what is left after the whole gap), drift free
    int64 steps(1 + ((ctx.now - m_nextDecay) / interval));
    m_nextDecay += steps * interval;
    double factor(std::pow(static_cast<double>(sConfig.npc.L1ThreatDecay), static_cast<double>(std::min<int64>(steps, 10000))));
    ctx.ai->GetThreatTable().Decay(static_cast<float>(factor), 1.0f);
}

void LevelOneBrain::AddThreat(NpcContext& ctx, SystemEntity* pSource, float amount, bool share)
{
    if ((pSource == nullptr) or !(amount > 0.0f))
        return;
    ctx.ai->AddThreat(pSource, amount);
    // the group knows what one of us knows (5.7, N1:  it was shared by corporation, bubble wide)
    if (!share or !(sConfig.npc.L1ThreatShare > 0.0f))
        return;
    AggroGroup* pGroup(ctx.ai->GetGroup());
    if (pGroup == nullptr)
        return;
    float shared(amount * sConfig.npc.L1ThreatShare);
    pGroup->ForEachMember([&](NPC* pNPC) {
        if ((pNPC == ctx.npc) or pNPC->IsDead() or (pNPC->GetAIMgr() == nullptr))
            return;
        pNPC->GetAIMgr()->ShareThreat(pSource, shared);
    });
}

void LevelOneBrain::OnDamage(NpcContext& ctx, SystemEntity* pSource, float hp)
{
    if ((pSource == nullptr) or !(hp > 0.0f))
        return;
    // "tanking modifier applied to drones/ships.  1.0 is no modifier" (owner decision 5:  a multiplier on damage threat)
    float tanking(pSource->IsDroneSE() ? ctx.profile.l1TankingDrone : ctx.profile.l1TankingShip);
    float threat(hp * std::max(0.0f, tanking));
    AddThreat(ctx, pSource, threat, true);
    // the drone's pilot is behind it
    if (pSource->IsDroneSE() and (sConfig.npc.L1DroneControllerShare > 0.0f)) {
        SystemEntity* pController(pSource->GetDroneSE()->GetController());
        if (pController != nullptr)
            AddThreat(ctx, pController, threat * sConfig.npc.L1DroneControllerShare, true);
    }
}

void LevelOneBrain::OnEwar(NpcContext& ctx, SystemEntity* pSource, uint8 effect, bool jammed)
{
    // "should the entity watch for effects when choosing targets" (1652)
    // a new jam:  the jam response starts over (SetIdle stopped us)
    if (jammed)
        m_jamGotoTarget = 0;
    if ((pSource == nullptr) or !ctx.profile.l1EffectMultiplier)
        return;
    AddThreat(ctx, pSource, static_cast<float>(NpcMath::L1EffectThreat(effect, jammed, sConfig.npc.L1EffectThreat, ctx.profile.maxHP)), true);
}

void LevelOneBrain::OnAssist(NpcContext& ctx, SystemEntity* pRepairer, SystemEntity* pPatient, float hp)
{
    // remote repair of someone we fight (1652, owner decision 15)
    if ((pRepairer == nullptr) or (pPatient == nullptr) or !ctx.profile.l1EffectMultiplier or !(sConfig.npc.L1RepairThreat > 0.0f))
        return;
    if ((ctx.npc->TargetMgr()->GetTarget(pPatient->GetID(), false) == nullptr) and !(ctx.ai->GetThreat(pPatient->GetID()) > 0.0f))
        return;
    // every level-one npc that sees it counts it itself (NotifyRemoteAssist):  no group share on top
    AddThreat(ctx, pRepairer, hp * sConfig.npc.L1RepairThreat, false);
}

void LevelOneBrain::OnGroupThreat(NpcContext& ctx, SystemEntity* pSource, float amount)
{
    // not passed on again
    ctx.ai->AddThreat(pSource, amount);
}

SystemEntity* LevelOneBrain::Acquire(NpcContext& ctx)
{
    Decay(ctx);
    m_jamGotoTarget = 0;
    // who's around (newcomers, capitals), on the evaluation cadence
    if (ctx.now >= m_nextEval) {
        m_nextEval = ctx.now + static_cast<int64>(std::max<uint32>(sConfig.npc.L1EvalInterval, 1)) * 1000;
        LookAround(ctx, nullptr);
    }
    size_t count(Rank(ctx, m_ranked, m_rankScore, 1));
    return (count > 0 ? m_ranked[0] : nullptr);
}

SystemEntity* LevelOneBrain::OnAggressed(NpcContext& ctx, SystemEntity* pBy)
{
    // our best pick (the aggressor's threat is in already), else the aggressor
    size_t count(Rank(ctx, m_ranked, m_rankScore, 1));
    if (count > 0)
        return m_ranked[0];
    return pBy;
}

void LevelOneBrain::OnGroupAlert(NpcContext& ctx, SystemEntity* pAggressor)
{
    if (pAggressor == nullptr)
        return;
    // the reaction delay (5.6), then our own best pick:  the group's shared threat points most of us the same way
    double delay(ctx.ai->DrawReactionDelay());
    SystemEntity* pTarget(pAggressor);
    size_t count(Rank(ctx, m_ranked, m_rankScore, 1));
    if (count > 0)
        pTarget = m_ranked[0];
    ctx.ai->QueueAggro(pTarget, ctx.now + static_cast<int64>(delay), true);
}

SystemEntity* LevelOneBrain::RepickPrimary(NpcContext& ctx, SystemEntity* pLost)
{
    // the best-scoring lock.  losing the primary isn't a switch:  the switch timer goes on
    SystemEntity* pBest(nullptr);
    double best(0.0), bestDistance(0.0);
    ctx.npc->TargetMgr()->ForEachTarget([&](SystemEntity* cur, bool locked) {
        if ((cur == pLost) or !ctx.ai->CanAggro(cur))
            return;
        double score(Score(ctx, cur));
        double distance(ctx.npc->GetPosition().distance(cur->GetPosition()));
        if ((pBest == nullptr) or (score > best) or ((score == best) and (distance < bestDistance))) {
            pBest = cur;
            best = score;
            bestDistance = distance;
        }
    });
    return pBest;
}

void LevelOneBrain::Think(NpcContext& ctx)
{
    Decay(ctx);
    m_jamGotoTarget = 0;
    // a switch whose lock failed (jam, range) is off
    uint32 switchTo(ctx.ai->GetSwitchToID());
    if ((switchTo != 0) and (ctx.npc->TargetMgr()->GetTarget(switchTo, false) == nullptr))
        ctx.ai->SetSwitchTo(0);
    if (ctx.now < m_nextEval)
        return;
    int64 interval(static_cast<int64>(std::max<uint32>(sConfig.npc.L1EvalInterval, 1)) * 1000);
    m_nextEval = (((m_nextEval == 0) or ((ctx.now - m_nextEval) > (2 * interval))) ? ctx.now : m_nextEval) + interval;
    Evaluate(ctx);
}

void LevelOneBrain::Evaluate(NpcContext& ctx)
{
    SystemEntity* pPrimary(ctx.npc->TargetMgr()->GetTarget(ctx.ai->GetPrimaryID(), false));
    LookAround(ctx, pPrimary);
    Switch(ctx, pPrimary);
    // the switch may have dropped the primary (one-lock types)
    pPrimary = ctx.npc->TargetMgr()->GetTarget(ctx.ai->GetPrimaryID(), false);
    RefreshLocks(ctx, pPrimary);
}

void LevelOneBrain::LookAround(NpcContext& ctx, SystemEntity* pPrimary)
{
    bool incursion(ctx.profile.l1Family == NpcAI::L1Family::Incursion);
    bool sleeper(ctx.profile.l1Family == NpcAI::L1Family::Sleeper);
    if ((!incursion and !sleeper) or (ctx.npc->SysBubble() == nullptr))
        return;
    int64 memory(static_cast<int64>(sConfig.npc.L1NewcomerMemory) * 1000);
    m_newcomerCount = 0;
    float primaryThreat((pPrimary != nullptr) ? ctx.ai->GetThreat(pPrimary->GetID()) : 0.0f);
    ctx.npc->SysBubble()->ForEachDynamic([&](SystemEntity* cur) {
        if (!IsPilotedShip(cur) or !ctx.ai->IsCandidate(cur, false, true))
            return;
        if (!m_seen.Touch(cur->GetID(), ctx.now, memory))
            return;
        // incursion sansha "most likely will change targets to any newcomers" (4.7)
        if (incursion and (pPrimary != nullptr) and (cur != pPrimary)) {
            double add(NpcMath::L1NewcomerThreat(ctx.ai->GetThreat(cur->GetID()), primaryThreat, sConfig.npc.L1NewcomerThreat));
            if (add > 0.0) {
                ctx.ai->AddThreat(cur, static_cast<float>(add));
                _log(NPC__AI_TRACE, "%s(%u): Newcomer %s(%u):  threat +%.0f.", ctx.npc->GetName(), ctx.npc->GetID(), cur->GetName(),
                        cur->GetID(), add);
                m_newcomers[m_newcomerCount < c_maxRanked ? m_newcomerCount++ : (c_maxRanked - 1)] = cur;
            }
        }
        // sleepers call for reinforcements when a capital ship comes into sight (4.6;  the spawn is phase 5's)
        if (sleeper and IsCapitalShip(cur) and (ctx.npc->SystemMgr()->GetDungMgr() != nullptr))
            ctx.npc->SystemMgr()->GetDungMgr()->OnCapitalSighted(ctx.npc, cur);
    });
    /* newcomers are locked at once (making room if need be):  with both locked, 1.2 x the primary's threat beats it by the
     *  margin.  one-lock types keep their primary until threat alone wins */
    TargetManager* pTargMgr(ctx.npc->TargetMgr());
    for (size_t i = 0; i < m_newcomerCount; ++i) {
        SystemEntity* pSE(m_newcomers[i]);
        if ((pTargMgr->GetTarget(pSE->GetID(), false) != nullptr) or (ctx.profile.maxLockedTargets < 2))
            continue;
        if (ctx.npc->GetPosition().distance(pSE->GetPosition()) > ctx.ai->LockRange())
            continue;
        if ((pTargMgr->TargetCount() >= ctx.profile.maxLockedTargets) and !MakeRoom(ctx, pPrimary, &m_newcomers[i], 1))
            break;
        ctx.ai->StartLock(pSE);
    }
    m_newcomerCount = 0;
}

void LevelOneBrain::Switch(NpcContext& ctx, SystemEntity* pPrimary)
{
    // one switch at a time;  a lost primary is the state machine's (RepickPrimary)
    if ((pPrimary == nullptr) or (ctx.ai->GetSwitchToID() != 0))
        return;
    size_t count(Rank(ctx, m_ranked, m_rankScore, 1));
    if ((count == 0) or (m_ranked[0] == pPrimary))
        return;
    SystemEntity* pBest(m_ranked[0]);
    // damped:  it has to be within what we can lock (the state machine closes in on a target we can't lock yet)
    if (ctx.npc->GetPosition().distance(pBest->GetPosition()) > ctx.ai->LockRange())
        return;
    double best(m_rankScore[0]), current(Score(ctx, pPrimary));
    int64 timer(SwitchTimerMs(ctx.profile));
    if (!NpcMath::L1ShouldSwitch(best, current, sConfig.npc.L1SwitchMargin, ctx.now, ctx.ai->GetLastSwitch(), timer,
                                 ctx.rng.Uniform(), ctx.profile.l1ChanceToNotSwitch))
        return;
    _log(NPC__AI_TRACE, "%s(%u): %s(%u) scores %.0f against the primary's %.0f:  switching.", ctx.npc->GetName(), ctx.npc->GetID(),
            pBest->GetName(), pBest->GetID(), best, current);
    TargetManager* pTargMgr(ctx.npc->TargetMgr());
    if (pTargMgr->GetTarget(pBest->GetID(), true) != nullptr) {
        ctx.ai->SwitchPrimary(pBest);
        return;
    }
    if (pTargMgr->GetTarget(pBest->GetID(), false) != nullptr) {
        // locking it already:  it's the primary once the lock completes
        ctx.ai->SetSwitchTo(pBest->GetID());
        return;
    }
    if (pTargMgr->TargetCount() >= ctx.profile.maxLockedTargets) {
        SystemEntity* keep[1] = { pBest };
        // one-lock types let the primary go
        if (!MakeRoom(ctx, pPrimary, keep, 1))
            ctx.ai->DropLock(pPrimary);
    }
    if (!ctx.ai->StartLock(pBest))
        return;
    if (ctx.ai->GetPrimaryID() == 0) {
        ctx.ai->SwitchPrimary(pBest);
    } else {
        ctx.ai->SetSwitchTo(pBest->GetID());
    }
}

bool LevelOneBrain::MakeRoom(NpcContext& ctx, SystemEntity* pPrimary, SystemEntity* const* keep, size_t keepCount)
{
    uint32 switchTo(ctx.ai->GetSwitchToID());
    SystemEntity* pWorst(nullptr);
    double worst(0.0);
    ctx.npc->TargetMgr()->ForEachTarget([&](SystemEntity* cur, bool locked) {
        if ((cur == pPrimary) or (cur->GetID() == switchTo))
            return;
        for (size_t i = 0; i < keepCount; ++i)
            if (keep[i] == cur)
                return;
        double score(Score(ctx, cur));
        if ((pWorst == nullptr) or (score < worst)) {
            pWorst = cur;
            worst = score;
        }
    });
    if (pWorst == nullptr)
        return false;
    ctx.ai->DropLock(pWorst);
    return true;
}

void LevelOneBrain::RefreshLocks(NpcContext& ctx, SystemEntity* pPrimary)
{
    // the locks follow the best candidates (5.7);  locks on others stay until a better one needs the slot
    TargetManager* pTargMgr(ctx.npc->TargetMgr());
    size_t slots(ctx.profile.maxLockedTargets);
    if (slots < 2)
        return;
    size_t count(Rank(ctx, m_ranked, m_rankScore, std::min(slots, c_maxRanked)));
    for (size_t i = 0; i < count; ++i) {
        SystemEntity* pSE(m_ranked[i]);
        if (pTargMgr->GetTarget(pSE->GetID(), false) != nullptr)
            continue;
        // never a lock dropped for one we can't get (damps)
        if (ctx.npc->GetPosition().distance(pSE->GetPosition()) > ctx.ai->LockRange())
            continue;
        if ((pTargMgr->TargetCount() >= slots) and !MakeRoom(ctx, pPrimary, m_ranked, count))
            break;
        ctx.ai->StartLock(pSE);
    }
}

void LevelOneBrain::WhileJammed(NpcContext& ctx, SystemEntity* pJammer, double jammerRange)
{
    /* "when jammed by X, non-battleship L1 NPCs move out to X's ECM optimal + falloff + 5 km;  battleships orbit X at 5 km"
     *  (4.6, EVE Uni 2011) */
    Decay(ctx);
    DestinyManager* pDestiny(ctx.npc->DestinyMgr());
    if ((pJammer == nullptr) or pJammer->IsDead() or (pJammer->SysBubble() != ctx.npc->SysBubble()) or (pDestiny == nullptr)
    or pDestiny->IsWarping())
        return;
    // the jammer is the threat;  its pilot (a drone's) is whom we keep away from
    SystemEntity* pFrom(pJammer);
    if (pJammer->IsDroneSE() and (pJammer->GetDroneSE()->GetController() != nullptr)
    and (pJammer->GetDroneSE()->GetController()->SysBubble() == ctx.npc->SysBubble()))
        pFrom = pJammer->GetDroneSE()->GetController();
    bool cuddle(false);
    double keep(NpcMath::L1JamDistance(ctx.profile.sigRadius, jammerRange, cuddle));
    if (cuddle) {
        if ((m_jamGotoTarget != pFrom->GetID()) or !m_jamCuddle) {
            m_jamGotoTarget = pFrom->GetID();
            m_jamCuddle = true;
            pDestiny->SetMaxVelocity(ctx.profile.maxVelocity);
            pDestiny->Orbit(pFrom, static_cast<uint32>(keep));
            _log(NPC__AI_TRACE, "%s(%u): Jammed:  cuddling %s(%u) at %.0fm.", ctx.npc->GetName(), ctx.npc->GetID(), pFrom->GetName(),
                    pFrom->GetID(), keep);
        }
        return;
    }
    if (ctx.ai->SurfaceDistance(pFrom) >= keep)
        return;
    // straight away from it, out to its reach + 5km
    GVector away(pFrom->GetPosition(), ctx.npc->GetPosition());
    if (away.isZero())
        away = GVector(1, 0, 0);
    away.normalize();
    GPoint point(pFrom->GetPosition() + (away * (keep + pFrom->GetRadius() + ctx.npc->GetRadius() + 1000.0)));
    if ((m_jamGotoTarget == pFrom->GetID()) and !m_jamCuddle and (m_jamGoto.distance(point) < c_jamGotoSlack))
        return;
    m_jamGotoTarget = pFrom->GetID();
    m_jamCuddle = false;
    m_jamGoto = point;
    pDestiny->SetMaxVelocity(ctx.profile.maxVelocity);
    pDestiny->GotoPoint(point);
    _log(NPC__AI_TRACE, "%s(%u): Jammed:  moving out of %s(%u)'s reach (%.0fm).", ctx.npc->GetName(), ctx.npc->GetID(), pFrom->GetName(),
            pFrom->GetID(), keep);
}

size_t LevelOneBrain::EwarTargets(NpcContext& ctx, SystemEntity* pPrimary, SystemEntity** out, size_t max)
{
    // "should use secondary effect on other targets?" (1649):  the best-scoring locked ships that aren't the primary
    // nothing before the primary is locked (bug B6a)
    if (!ctx.profile.l1SecondaryTarget or (max == 0) or (pPrimary == nullptr))
        return 0;
    double scores[c_maxRanked];
    if (max > c_maxRanked)
        max = c_maxRanked;
    size_t count(0);
    ctx.npc->TargetMgr()->ForEachTarget([&](SystemEntity* cur, bool locked) {
        if (!locked or (cur == pPrimary) or !IsPilotedShip(cur) or !ctx.ai->CanAggro(cur))
            return;
        double score(Score(ctx, cur));
        size_t pos(count);
        while ((pos > 0) and (scores[pos - 1] < score))
            --pos;
        if (pos >= max)
            return;
        size_t last((count < max) ? count : (max - 1));
        for (size_t i = last; i > pos; --i) {
            out[i] = out[i - 1];
            scores[i] = scores[i - 1];
        }
        out[pos] = cur;
        scores[pos] = score;
        if (count < max)
            ++count;
    });
    return count;
}

std::string LevelOneBrain::Describe(NpcContext& ctx)
{
    std::ostringstream str;
    str << std::fixed << std::setprecision(0);
    static const char* families[] = { "other", "sleeper", "incursion" };
    int64 timer(SwitchTimerMs(ctx.profile));
    str << "level one (" << (ctx.profile.l1Family < 3 ? families[ctx.profile.l1Family] : "?") << "):  secondary "
        << ctx.profile.l1SecondaryTarget << ", sig " << ctx.profile.l1SignatureRadius << " (pref " << m_preferredSig << "), effects "
        << ctx.profile.l1EffectMultiplier << " (E " << (sConfig.npc.L1EffectThreat * ctx.profile.maxHP) << "), tanking ship "
        << std::setprecision(2) << ctx.profile.l1TankingShip << " drone " << ctx.profile.l1TankingDrone << ", no-switch chance "
        << ctx.profile.l1ChanceToNotSwitch << std::setprecision(0) << ", switch timer " << timer << "ms (data "
        << ctx.profile.l1SwitchTimerMs << "), last switch " << (ctx.now - ctx.ai->GetLastSwitch()) << "ms ago, next look in "
        << EvE::max(m_nextEval - ctx.now) << "ms, evasive " << ctx.profile.evasive << ", seen " << m_seen.Size() << "<br>";
    size_t count(Rank(ctx, m_ranked, m_rankScore, 5));
    if (count > 0) {
        str << "scores:";
        for (size_t i = 0; i < count; ++i)
            str << "  " << m_ranked[i]->GetName() << "(" << m_ranked[i]->GetID() << ") " << m_rankScore[i]
                << (m_ranked[i]->GetID() == ctx.ai->GetPrimaryID() ? " (primary)" : "");
        str << "<br>";
    }
    return str.str();
}
