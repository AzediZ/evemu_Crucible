/**
 * @name CrimeWatch.cpp
 *   EvEmu CrimeWatch code.
 *
 * @Author:         Allan
 * @date:   13 March 2016
 *
 * Crucible rules, from EVElopedia (Dec 2011 - Mar 2012), EVE University (2011) and the Crucible / Retribution patch notes.
 *  see CrimeWatch.h for the overview.
 */

#include "eve-server.h"

#include "Client.h"
#include "EntityList.h"
#include "EVEServerConfig.h"
#include "StaticDataMgr.h"
#include "character/Character.h"
#include "faction/WarManager.h"
#include "inventory/AttributeEnum.h"
#include "npc/Drone.h"
#include "npc/LawEnforcement.h"
#include "npc/NPC.h"
#include "npc/Sentry.h"
#include "npc/ai/LawRules.h"
#include "ship/Ship.h"
#include "standing/StandingDB.h"
#include "system/CrimeWatch.h"
#include "system/SystemManager.h"
#include "../../eve-common/EVE_Standings.h"

CrimeWatch::CrimeWatch()
{
}

int CrimeWatch::Initialize()
{
    // kill rights last 30 days, so they are kept in the db.  created here too in case the migration hasn't been run
    DBerror err;
    sDatabase.RunQuery(err,
        "CREATE TABLE IF NOT EXISTS crimeKillRights ("
        " holderID INT UNSIGNED NOT NULL,"
        " targetID INT UNSIGNED NOT NULL,"
        " expiresAt BIGINT NOT NULL,"
        " PRIMARY KEY (holderID, targetID))");
    sDatabase.RunQuery(err, "DELETE FROM crimeKillRights WHERE expiresAt < %lli", GetFileTimeNow());

    DBQueryResult res;
    if (sDatabase.RunQuery(res, "SELECT holderID, targetID, expiresAt FROM crimeKillRights")) {
        DBResultRow row;
        while (res.GetRow(row))
            m_killRights[row.GetUInt(0)][row.GetUInt(1)] = row.GetInt64(2);
    }

    sLog.Blue("      CrimeWatch", "CrimeWatch %s.  %lu kill rights loaded.", (sConfig.crime.Enabled ? "enabled" : "disabled"), m_killRights.size());
    return 1;
}

float CrimeWatch::SecurityLevel(float trueSec)
{
    // the client's cfg.solarsystems pseudoSecurity, then util.FmtSystemSecStatus (python 2 round: halves away from zero)
    if ((trueSec > 0.0f) and (trueSec < 0.05f))
        trueSec = 0.05f;
    float level(roundf(trueSec * 10.0f) / 10.0f);
    if (level == -0.0f)
        level = 0.0f;
    return level;
}

uint8 CrimeWatch::SecurityClass(float trueSec)
{
    float level(SecurityLevel(trueSec));
    if (level <= 0.0f)
        return Crime::SecClass::Null;
    if (level < 0.45f)
        return Crime::SecClass::Low;
    return Crime::SecClass::High;
}

int64 CrimeWatch::FlagTime()
{
    return (int64)sConfig.crime.AggFlagTime * EvE::Time::Second;
}

int64 CrimeWatch::CrimTime()
{
    return (int64)sConfig.crime.CrimFlagTime * EvE::Time::Second;
}

bool CrimeWatch::Live(int64 time, int64 duration)
{
    return ((time > 0) and ((time + duration) > GetFileTimeNow()));
}

void CrimeWatch::Flag(uint32 aggressorID, uint32 victimID, int64 now)
{
    m_records[aggressorID].victims[victimID] = now;
}

bool CrimeWatch::IsCriminal(uint32 charID)
{
    std::map<uint32, Record>::iterator itr = m_records.find(charID);
    if (itr == m_records.end())
        return false;
    std::map<uint32, int64>::iterator vItr = itr->second.victims.find(0);
    if (vItr == itr->second.victims.end())
        return false;
    return Live(vItr->second, CrimTime());
}

bool CrimeWatch::IsFlaggedTo(uint32 aggressorID, uint32 victimID)
{
    if ((aggressorID == 0) or (victimID == 0))
        return false;
    std::map<uint32, Record>::iterator itr = m_records.find(aggressorID);
    if (itr == m_records.end())
        return false;
    std::map<uint32, int64>::iterator vItr = itr->second.victims.find(victimID);
    if (vItr == itr->second.victims.end())
        return false;
    return Live(vItr->second, FlagTime());
}

/* ------------------------------------------------------------------------------------------------------------------
 *  Crimewatch 1 sentries and the npc aggression timer (npc ai plan 4.9, 4.10)
 * ------------------------------------------------------------------------------------------------------------------ */
int64 CrimeWatch::SentryAggressionTime()
{
    return (int64)sConfig.crime.SentryAggression * EvE::Time::Second;
}

void CrimeWatch::Prune(std::unordered_map<uint64_t, int64>& map)
{
    if (map.size() < 256)
        return;
    int64 now(GetFileTimeNow());
    for (std::unordered_map<uint64_t, int64>::iterator itr = map.begin(); itr != map.end(); )
        if (!LawRules::Live(itr->second, SentryAggressionTime(), now))
            itr = map.erase(itr);
        else
            ++itr;
}

void CrimeWatch::SentryAggression(uint32 charID, uint32 objectID)
{
    if (!sConfig.crime.Enabled or (sConfig.crime.SentryAggression == 0) or (charID == 0) or (objectID == 0))
        return;
    Prune(m_sentryAggression);
    m_sentryAggression[((uint64_t)objectID << 32) | charID] = GetFileTimeNow();
}

bool CrimeWatch::HasSentryAggression(uint32 charID, uint32 objectID)
{
    std::unordered_map<uint64_t, int64>::iterator itr(m_sentryAggression.find(((uint64_t)objectID << 32) | charID));
    if (itr == m_sentryAggression.end())
        return false;
    return LawRules::Live(itr->second, SentryAggressionTime(), GetFileTimeNow());
}

bool CrimeWatch::HasFactionAggression(uint32 charID, uint32 factionID)
{
    if (!sConfig.crime.NpcFactionAggression or (factionID == 0))
        return false;
    std::unordered_map<uint64_t, int64>::iterator itr(m_factionAggression.find(((uint64_t)factionID << 32) | charID));
    if (itr == m_factionAggression.end())
        return false;
    return LawRules::Live(itr->second, SentryAggressionTime(), GetFileTimeNow());
}

void CrimeWatch::NpcFactionAct(Client* pAggressor, SystemEntity* pNPC)
{
    // EVElopedia "Combat Timers" (2012):  attacking an npc makes its faction's npcs and station/gate guns hostile for 15 minutes
    if (!sConfig.crime.NpcFactionAggression or (sConfig.crime.SentryAggression == 0))
        return;
    int32 factionID(pNPC->GetWarFactionID());
    if (!IsFaction(factionID))
        return;
    uint32 me(pAggressor->GetCharacterID());
    uint64_t key(((uint64_t)factionID << 32) | me);
    bool fresh(!HasFactionAggression(me, factionID));
    Prune(m_factionAggression);
    m_factionAggression[key] = GetFileTimeNow();
    if (!fresh)
        return;
    _log(CRIME__INFO, "%s(%u) attacked %s(%u):  faction %i is hostile to them.", pAggressor->GetName(), me, pNPC->GetName(),
            pNPC->GetID(), factionID);
    // the faction's navy comes for them in its own high-sec (crime.FactionNavy)
    SystemManager* pSystem(pAggressor->SystemMgr());
    if ((pSystem == nullptr) or pAggressor->InPod() or (SecurityClass(pSystem->GetSystemSecurityRating()) != Crime::SecClass::High))
        return;
    SystemData data;
    if (sDataMgr.GetSystemData(pSystem->GetID(), data) and ((int32)data.factionID == factionID))
        pSystem->GetLaw()->ReportNavyTarget(pAggressor, data.factionID);
}

bool CrimeWatch::HasLiveFlags(uint32 charID)
{
    std::map<uint32, Record>::iterator itr = m_records.find(charID);
    if (itr == m_records.end())
        return false;
    for (auto cur : itr->second.victims)
        if (Live(cur.second, (cur.first == 0 ? CrimTime() : FlagTime())))
            return true;
    return false;
}

/* ------------------------------------------------------------------------------------------------------------------
 *  who owns the target
 * ------------------------------------------------------------------------------------------------------------------ */
bool CrimeWatch::GetOwner(SystemEntity* pSE, Owner& into)
{
    if (pSE == nullptr)
        return false;

    Client* pPilot(nullptr);
    if (pSE->IsShipSE()) {
        pPilot = pSE->GetPilot();
        if (pPilot == nullptr) {
            // an empty ship (ejected, or left in space by a pilot who logged off) still belongs to its owner
            uint32 ownerID(pSE->GetSelf()->ownerID());
            if (!IsCharacterID(ownerID))
                return false;
            into.charID = ownerID;
            into.corpID = pSE->GetCorporationID();
            into.allianceID = pSE->GetAllianceID();
            into.warFactionID = pSE->GetWarFactionID();
            CharacterRef cRef(sItemFactory.GetCharacterRef(ownerID));
            into.secStatus = (cRef.get() == nullptr ? 0.0f : cRef->GetSecurityRating());
            into.pod = (pSE->GetSelf()->groupID() == EVEDB::invGroups::Capsule);
            return true;
        }
        into.pod = pPilot->InPod();
    } else if (pSE->IsDroneSE()) {
        pPilot = pSE->GetDroneSE()->GetOwnerClient();
        if (pPilot == nullptr)
            return false;
    } else if (pSE->IsNPCSE()) {
        NPC* pNPC(pSE->GetNPCSE());
        if (pNPC->IsConcord() or (pSE->GetCorporationID() == corpCONCORD)) {
            into.concord = true;
            into.corpID = corpCONCORD;
            return true;
        }
        return false;   // rats, police and navies: shooting them is no crime
    } else if (pSE->IsSentrySE()) {
        into.sentry = true;
        into.corpID = pSE->GetSelf()->ownerID();
        // gate and station guns keep aggression per guarded object (Crimewatch 1)
        if (pSE->GetSentrySE()->IsLawSentry())
            into.protectedID = pSE->GetSentrySE()->GetProtectedID();
        return true;
    } else if (pSE->IsPOSSE() or pSE->IsCOSE()) {
        // npc owned (Interbus customs offices) can be shot without penalty.  player structures belong to their corp
        uint32 corpID(pSE->GetCorporationID());
        if (IsNPCCorp(corpID) or (corpID == 0))
            return false;
        into.structure = true;
        into.corpID = corpID;
        into.allianceID = pSE->GetAllianceID();
        return true;
    } else {
        return false;
    }

    into.charID = pPilot->GetCharacterID();
    into.corpID = pPilot->GetCorporationID();
    into.allianceID = pPilot->GetAllianceID();
    into.warFactionID = pPilot->GetWarFactionID();
    into.secStatus = pPilot->GetSecurityRating();
    return true;
}

namespace {
    // factional warfare: Caldari/Gallente and Amarr/Minmatar militias may fight anywhere
    bool EnemyMilitia(int32 a, int32 b) {
        if ((a == 0) or (b == 0))
            return false;
        return (((a == factionCaldari) and (b == factionGallente)) or ((a == factionGallente) and (b == factionCaldari))
             or ((a == factionAmarr) and (b == factionMinmatar)) or ((a == factionMinmatar) and (b == factionAmarr)));
    }
}

/* the client's consider.CanAttackFreely().  the client warns before an act this returns false for.
 *  war targets (warRelationshipAtWarCanFight) are free targets, ships, pods and structures alike.
 */
bool CrimeWatch::CanAttackFreely(Client* pAggressor, const Owner& target)
{
    // the war owner comes from the corp:  an entity's cached allianceID can be older than an alliance change
    if (sConfig.war.Enabled and (target.corpID != 0)) {
        uint32 mine(sWarMgr.WarOwnerOf(pAggressor->GetCorporationID()));
        uint32 theirs(sWarMgr.WarOwnerOf(target.corpID));
        if ((mine != 0) and (theirs != 0) and sWarMgr.IsHostile(mine, theirs))
            return true;
    }
    if (target.structure)
        return false;
    uint32 me(pAggressor->GetCharacterID());
    if (IsOutlaw(target.secStatus))
        return true;
    if (!target.pod)
        if (IsCriminal(target.charID) or IsFlaggedTo(target.charID, me) or IsFlaggedTo(target.charID, pAggressor->GetCorporationID()))
            return true;
    std::map<uint32, std::map<uint32, int64>>::iterator itr = m_killRights.find(me);
    if (itr != m_killRights.end()) {
        std::map<uint32, int64>::iterator kItr = itr->second.find(target.charID);
        if ((kItr != itr->second.end()) and (kItr->second > GetFileTimeNow()))
            return true;
    }
    if (EnemyMilitia(pAggressor->GetWarFactionID(), target.warFactionID))
        return true;
    return false;
}

/* ------------------------------------------------------------------------------------------------------------------
 *  aggressive acts
 * ------------------------------------------------------------------------------------------------------------------ */
void CrimeWatch::OffensiveAct(Client* pAggressor, SystemEntity* pTarget)
{
    if (!sConfig.crime.Enabled or (pAggressor == nullptr) or (pTarget == nullptr))
        return;
    SystemManager* pSystem(pAggressor->SystemMgr());
    if (pSystem == nullptr)
        return;
    Owner target;
    if (!GetOwner(pTarget, target)) {
        // rats, police and navies:  no crime, but the npc aggression timer
        if (pTarget->IsNPCSE())
            NpcFactionAct(pAggressor, pTarget);
        return;
    }
    Act(pAggressor, pSystem, target);
}

void CrimeWatch::Act(Client* pAggressor, SystemManager* pSystem, const Owner& target)
{
    uint32 me(pAggressor->GetCharacterID());
    if (target.charID == me)
        return;
    float trueSec(pSystem->GetSystemSecurityRating());
    uint8 secClass(SecurityClass(trueSec));
    int64 now(GetFileTimeNow());
    Record& record = m_records[me];
    record.lastAct = now;

    // CONCORD: always a crime in empire space
    if (target.concord) {
        if (secClass != Crime::SecClass::Null) {
            Flag(me, 0, now);
            if (secClass == Crime::SecClass::High)
                pSystem->GetLaw()->ReportCriminal(pAggressor);
        }
        Notify(me);
        return;
    }
    // gate/station sentries: you're flagged to their owner, and that gate's or station's guns shoot back (Crimewatch 1)
    if (target.sentry) {
        Flag(me, target.corpID, now);
        if (target.protectedID != 0)
            SentryAggression(me, target.protectedID);
        Notify(me);
        return;
    }

    if (target.charID != 0) {
        record.lastPvP = now;
        m_records[target.charID].lastPvP = now;
        // members of the same player corp may fight freely
        if ((target.corpID == pAggressor->GetCorporationID()) and !IsNPCCorp(target.corpID))
            return;
    } else if ((target.corpID == pAggressor->GetCorporationID()) or ((target.allianceID != 0) and (target.allianceID == pAggressor->GetAllianceID()))) {
        return;     // our own structures
    }

    uint32 victimID(target.structure ? target.corpID : target.charID);
    bool newFlag(!IsFlaggedTo(me, victimID));
    bool legal((secClass == Crime::SecClass::Null) or CanAttackFreely(pAggressor, target));
    Flag(me, victimID, now);

    if (!legal) {
        Flag(me, 0, now);
        if (target.charID != 0)
            m_illegal[target.charID][me] = now;
        if (newFlag)
            SecPenalty(pAggressor, target.secStatus, sConfig.crime.SecAggressionPenalty, 0.0f, SecurityLevel(trueSec), "aggression");
        _log(CRIME__INFO, "%s(%u) committed a crime against %u in %s.", pAggressor->GetName(), me, victimID, pSystem->GetName());
        if (secClass == Crime::SecClass::High)
            pSystem->GetLaw()->ReportCriminal(pAggressor);
    }

    Notify(me);
}

void CrimeWatch::SimulateCrime(Client* pAggressor)
{
    if (!sConfig.crime.Enabled or (pAggressor == nullptr) or (pAggressor->SystemMgr() == nullptr))
        return;
    // nobody in particular: no corp, no flags, no kill rights, security status 0.  attacking them is never legal
    Owner civilian;
    Act(pAggressor, pAggressor->SystemMgr(), civilian);
}

void CrimeWatch::EntityCrime(SystemEntity* pAggressor, SystemEntity* pVictim)
{
    if (!sConfig.crime.Enabled or (pAggressor == nullptr) or (pAggressor->SystemMgr() == nullptr))
        return;
    SystemManager* pSystem(pAggressor->SystemMgr());
    uint8 secClass(SecurityClass(pSystem->GetSystemSecurityRating()));
    if (secClass == Crime::SecClass::Null)
        return;
    uint32 id(pAggressor->GetID());
    int64 now(GetFileTimeNow());
    Flag(id, 0, now);
    Owner victim;
    if ((pVictim != nullptr) and GetOwner(pVictim, victim) and (victim.charID != 0))
        Flag(id, victim.charID, now);
    _log(CRIME__INFO, "%s(%u) committed a crime in %s.", pAggressor->GetName(), id, pSystem->GetName());
    // the pilots here see it flashing
    PyDict* victims(MakeVictimDict(id));
    SendToSystem(pSystem, id, victims);
    PyDecRef(victims);
    if (secClass == Crime::SecClass::High)
        pSystem->GetLaw()->ReportCriminalEntity(pAggressor);
}

void CrimeWatch::AssistanceAct(Client* pHelper, SystemEntity* pTarget)
{
    if (!sConfig.crime.Enabled or (pHelper == nullptr) or (pTarget == nullptr))
        return;
    SystemManager* pSystem(pHelper->SystemMgr());
    if (pSystem == nullptr)
        return;
    Owner target;
    if (!GetOwner(pTarget, target) or (target.charID == 0))
        return;
    uint32 me(pHelper->GetCharacterID());
    if (target.charID == me)
        return;
    uint8 secClass(SecurityClass(pSystem->GetSystemSecurityRating()));
    if (secClass == Crime::SecClass::Null)
        return;

    int64 now(GetFileTimeNow());
    bool changed(false);
    // helping a criminal or an outlaw is a crime
    if (IsCriminal(target.charID) or IsOutlaw(target.secStatus)) {
        Flag(me, 0, now);
        m_records[me].lastAct = now;
        changed = true;
        if (secClass == Crime::SecClass::High)
            pSystem->GetLaw()->ReportCriminal(pHelper);
    }
    // otherwise you take on their fights: whoever they aggressed may shoot you too
    std::map<uint32, Record>::iterator itr = m_records.find(target.charID);
    if (itr != m_records.end()) {
        for (auto cur : itr->second.victims) {
            if ((cur.first == 0) or (cur.first == me) or (cur.first == pHelper->GetCorporationID()))
                continue;
            if (!Live(cur.second, FlagTime()))
                continue;
            Flag(me, cur.first, now);
            changed = true;
        }
    }
    if (changed) {
        m_records[me].lastAct = now;
        m_records[me].lastPvP = now;
        Notify(me);
    }
}

void CrimeWatch::Theft(Client* pThief, uint32 ownerID, uint32 ownerCorpID)
{
    if (!sConfig.crime.Enabled or (pThief == nullptr) or (pThief->SystemMgr() == nullptr))
        return;
    if (SecurityClass(pThief->SystemMgr()->GetSystemSecurityRating()) == Crime::SecClass::Null)
        return;
    // flagged to the owner's corporation, or to the owner if they're in an npc corp.  not a crime: CONCORD doesn't care
    uint32 victimID((IsNPCCorp(ownerCorpID) or (ownerCorpID == 0)) ? ownerID : ownerCorpID);
    int64 now(GetFileTimeNow());
    Flag(pThief->GetCharacterID(), victimID, now);
    m_records[pThief->GetCharacterID()].lastPvP = now;
    _log(CRIME__INFO, "%s(%u) stole from %u (flagged to %u).", pThief->GetName(), pThief->GetCharacterID(), ownerID, victimID);
    Notify(pThief->GetCharacterID());
}

void CrimeWatch::NPCAggression(uint32 charID)
{
    if (!sConfig.crime.Enabled)
        return;
    m_records[charID].lastNPC = GetFileTimeNow();
}

bool CrimeWatch::CanTakeFrom(uint32 charID, uint32 ownerID)
{
    // fleet members in the same system share loot
    Client* pThief(sEntityList.FindClientByCharID(charID));
    Client* pOwner(sEntityList.FindClientByCharID(ownerID));
    if ((pThief == nullptr) or (pOwner == nullptr))
        return false;
    if ((pThief->GetFleetID() == 0) or (pThief->GetFleetID() != pOwner->GetFleetID()))
        return false;
    return (pThief->GetSystemID() == pOwner->GetSystemID());
}

/* ------------------------------------------------------------------------------------------------------------------
 *  timers
 * ------------------------------------------------------------------------------------------------------------------ */
uint32 CrimeWatch::WeaponsTimer(uint32 charID)
{
    std::map<uint32, Record>::iterator itr = m_records.find(charID);
    if (itr == m_records.end())
        return 0;
    int64 until(itr->second.lastAct + (int64)sConfig.crime.WeaponFlagTime * EvE::Time::Second);
    int64 now(GetFileTimeNow());
    if (until <= now)
        return 0;
    return (uint32)((until - now) / EvE::Time::Second) + 1;
}

uint32 CrimeWatch::LogoffTimer(uint32 charID, bool& npcOnly)
{
    npcOnly = false;
    if (!sConfig.crime.Enabled)
        return 0;
    std::map<uint32, Record>::iterator itr = m_records.find(charID);
    if (itr == m_records.end())
        return 0;
    if (Live(itr->second.lastPvP, FlagTime()))
        return sConfig.crime.AggFlagTime;
    if (Live(itr->second.lastNPC, (int64)sConfig.crime.LogoffNPCTime * EvE::Time::Second))
        npcOnly = true;
    return 0;
}

void CrimeWatch::CheckDockOrJump(Client* pClient, bool jump)
{
    if (!sConfig.crime.Enabled or (pClient == nullptr))
        return;
    uint32 left(WeaponsTimer(pClient->GetCharacterID()));
    if (left > 0)
        throw CustomError ("You can't %s for another %u seconds after an aggressive act.", (jump ? "jump" : "dock"), left);
    // a criminal is locked out of gates and stations in empire space, except in a capsule
    if (sConfig.crime.CriminalLockdown and !pClient->InPod() and IsCriminal(pClient->GetCharacterID()))
        if ((pClient->SystemMgr() != nullptr) and (SecurityClass(pClient->SystemMgr()->GetSystemSecurityRating()) != Crime::SecClass::Null))
            throw CustomError ("%s Traffic Control has refused your %s request.  Criminals may not use stargates or stations.",
                    pClient->SystemMgr()->GetName(), (jump ? "jump" : "docking"));
}

/* ------------------------------------------------------------------------------------------------------------------
 *  client data
 * ------------------------------------------------------------------------------------------------------------------ */
PyDict* CrimeWatch::MakeVictimDict(uint32 charID)
{
    // {victimID or None: time of the last act}.  the client works out the expiry itself (time + 15 minutes)
    PyDict* dict = new PyDict();
    std::map<uint32, Record>::iterator itr = m_records.find(charID);
    if (itr == m_records.end())
        return dict;
    for (auto cur : itr->second.victims) {
        if (!Live(cur.second, (cur.first == 0 ? CrimTime() : FlagTime())))
            continue;
        if (cur.first == 0) {
            dict->SetItem(PyStatic.NewNone(), new PyLong(cur.second));
        } else {
            dict->SetItem(new PyInt(cur.first), new PyLong(cur.second));
        }
    }
    return dict;
}

PyDict* CrimeWatch::GetCriminalTimeStamps(uint32 charID)
{
    return MakeVictimDict(charID);
}

void CrimeWatch::GetAggressors(SystemManager* pSystem, std::map<int32, PyRep*>& into)
{
    if (!sConfig.crime.Enabled or (pSystem == nullptr))
        return;
    std::vector<Client*> clients;
    pSystem->GetClientList(clients);
    for (auto cur : clients) {
        PyDict* dict(MakeVictimDict(cur->GetCharacterID()));
        if (dict->empty()) {
            PyDecRef(dict);
            continue;
        }
        into[cur->GetCharacterID()] = dict;
    }
}

void CrimeWatch::SendToSystem(SystemManager* pSystem, uint32 charID, PyDict* victims)
{
    // OnAggressionChange(solarsystemID, {aggressorID: {victimID or None: time}})  the client replaces the aggressor's whole dict
    std::vector<Client*> clients;
    pSystem->GetClientList(clients);
    for (auto cur : clients) {
        PyDict* aggressors = new PyDict();
        PyIncRef(victims);
        aggressors->SetItem(new PyInt(charID), victims);
        PyTuple* payload = new PyTuple(2);
            payload->SetItem(0, new PyInt(pSystem->GetID()));
            payload->SetItem(1, aggressors);
        cur->SendNotification("OnAggressionChange", "charid", &payload);
    }
}

void CrimeWatch::Notify(uint32 charID)
{
    Client* pClient(sEntityList.FindClientByCharID(charID));
    if ((pClient == nullptr) or (pClient->SystemMgr() == nullptr))
        return;
    PyDict* victims(MakeVictimDict(charID));
    SendToSystem(pClient->SystemMgr(), charID, victims);
    PyDecRef(victims);
}

/* ------------------------------------------------------------------------------------------------------------------
 *  events
 * ------------------------------------------------------------------------------------------------------------------ */
void CrimeWatch::OnEnterSpace(Client* pClient)
{
    if (!sConfig.crime.Enabled or (pClient == nullptr) or !pClient->IsInSpace())
        return;
    SystemManager* pSystem(pClient->SystemMgr());
    if (pSystem == nullptr)
        return;
    uint32 me(pClient->GetCharacterID());
    float trueSec(pSystem->GetSystemSecurityRating());
    uint8 secClass(SecurityClass(trueSec));

    if ((secClass == Crime::SecClass::High) and IsCriminal(me)) {
        // undocking or jumping in high-sec 'refreshes CONCORD's memory', and they come for the ship
        Flag(me, 0, GetFileTimeNow());
        if (!pClient->InPod())
            pSystem->GetLaw()->ReportCriminal(pClient);
    }
    // the pilots in this system learn about our flags
    Notify(me);

    // faction police chase pilots with low security status in their high-sec systems:
    //   -2.0 in 1.0 systems, down to -4.5 in 0.5 systems.  they can be escaped, and don't kill pods (npc/ai/LawRules.h)
    if (sConfig.crime.FactionPolice and (secClass == Crime::SecClass::High) and !pClient->InPod())
        if (LawRules::PoliceWanted(pClient->GetSecurityRating(), SecurityLevel(trueSec)))
            pSystem->GetLaw()->ReportPoliceTarget(pClient);

    // faction navies (crime.FactionNavy):  pilots with bad standing with the system's faction, or with the npc aggression timer
    if (sConfig.crime.FactionNavy and (secClass == Crime::SecClass::High) and !pClient->InPod()) {
        SystemData data;
        if (sDataMgr.GetSystemData(pSystem->GetID(), data) and (data.factionID != 0))
            if (LawRules::NavyWanted(StandingDB::GetStanding(data.factionID, me), sConfig.crime.FactionNavyStanding)
            or HasFactionAggression(me, data.factionID))
                pSystem->GetLaw()->ReportNavyTarget(pClient, data.factionID);
    }
}

void CrimeWatch::CanSwitchShip(Client* pClient)
{
    if (!sConfig.crime.Enabled or (pClient == nullptr))
        return;
    // Crucible: a criminal may not switch ships, so the evidence stays.  the weapons timer lock is the Retribution rule
    if (IsCriminal(pClient->GetCharacterID()))
        throw UserError ("CannotEjectInvolvedInCrime");
    if (sConfig.crime.SwitchShipWeaponsTimer) {
        uint32 left(WeaponsTimer(pClient->GetCharacterID()));
        if (left > 0)
            throw CustomError ("You can't switch ships for another %u seconds after an aggressive act.", left);
    }
}

void CrimeWatch::OnShipDestroyed(uint32 victimCharID, bool pod, float victimSec, uint32 killerCharID, SystemManager* pSystem, bool selfDestruct/*false*/)
{
    if (!sConfig.crime.Enabled or (pSystem == nullptr))
        return;
    // nobody killed a self-destructed ship
    if (selfDestruct) {
        if (pod)
            m_illegal.erase(victimCharID);
        return;
    }
    float trueSec(pSystem->GetSystemSecurityRating());
    uint8 secClass(SecurityClass(trueSec));
    float level(SecurityLevel(trueSec));

    std::map<uint32, std::map<uint32, int64>>::iterator itr = m_illegal.find(victimCharID);
    if ((itr != m_illegal.end()) and (secClass != Crime::SecClass::Null)) {
        for (auto cur : itr->second) {
            if (!Live(cur.second, FlagTime()))
                continue;
            Client* pAggressor(sEntityList.FindClientByCharID(cur.first));
            // the ship kill penalty only lands if the victim didn't fight back.  podding is always a crime
            bool foughtBack(IsFlaggedTo(victimCharID, cur.first));
            if (pAggressor != nullptr) {
                if (pod) {
                    SecPenalty(pAggressor, victimSec, sConfig.crime.SecPodKillPenalty, sConfig.crime.SecPodKillMinPenalty, level, "pod kill");
                } else if (!foughtBack) {
                    SecPenalty(pAggressor, victimSec, sConfig.crime.SecShipKillPenalty, 0.0f, level, "ship kill");
                }
            }
            // kill rights: an illegally destroyed ship whose pilot didn't fight back.  not for pods
            if (!pod and !foughtBack)
                GrantKillRight(victimCharID, cur.first);
        }
        if (pod)
            m_illegal.erase(itr);
    }

    // a kill right is used up when its holder destroys the target's ship
    if ((killerCharID != 0) and !pod) {
        std::map<uint32, std::map<uint32, int64>>::iterator kItr = m_killRights.find(killerCharID);
        if ((kItr != m_killRights.end()) and (kItr->second.find(victimCharID) != kItr->second.end()))
            RemoveKillRight(killerCharID, victimCharID);
    }
}

float CrimeWatch::OnNPCKilled(Character* pChar, InventoryItemRef npcRef, SystemManager* pSystem)
{
    if ((pChar == nullptr) or (pSystem == nullptr))
        return 0.0f;
    if (pSystem->GetSystemSecurityRating() <= 0)
        return 0.0f;
    float killBonus(npcRef->GetAttribute(AttrEntitySecurityStatusKillBonus).get_float());
    if (killBonus <= 0)
        return 0.0f;

    // gain = (10 - current) * the npc's security percentage.  only the best npc per system counts every 15 minutes
    float oldSec(pChar->GetSecurityRating());
    EvilNumber maxGain(10);
    bool capped(npcRef->HasAttribute(AttrEntitySecurityMaxGain, maxGain));
    if (capped and (oldSec >= maxGain.get_float()))
        return 0.0f;
    float gain((10.0f - oldSec) * killBonus / 100.0f);
    gain *= (1.0f + (0.05f * pChar->GetSkillLevel(EvESkill::FastTalk, true)));
    gain *= sConfig.rates.secRate;

    std::pair<int64, float>& window = m_secGain[pChar->itemID()][pSystem->GetID()];
    if (!Live(window.first, (int64)sConfig.crime.SecGainWindow * EvE::Time::Second)) {
        window.first = GetFileTimeNow();
        window.second = 0.0f;
    }
    if (gain <= window.second)
        return 0.0f;
    float award(gain - window.second);
    window.second = gain;

    float newSec(oldSec + award);
    if (capped and (newSec > maxGain.get_float()))
        newSec = maxGain.get_float();
    std::string msg("Security status gained for killing pirates in ");
    msg += pSystem->GetNameStr();
    SetSecurityStatus(pChar->itemID(), newSec, msg, Standings::LawEnforcement);
    return award;
}

/* ------------------------------------------------------------------------------------------------------------------
 *  security status
 * ------------------------------------------------------------------------------------------------------------------ */
/* Crucible penalties (Empyrean Age 1.1 values): aggression 1.5%, ship kill 6%, pod kill 37.5% (never under 12.5%).
 *  each is a share of the distance down to -10:  new = (old + 10) * (1 - pct / 100) - 10
 *  scaled by the system's security level, and by 1% per point the victim's status is above (or below) the aggressor's
 */
void CrimeWatch::SecPenalty(Client* pAggressor, float victimSec, float basePercent, float minPercent, float level, const char* what)
{
    if ((pAggressor == nullptr) or (basePercent <= 0) or (level <= 0))
        return;
    float aggSec(pAggressor->GetSecurityRating());
    float pct(basePercent * level * (1.0f + ((victimSec - aggSec) / 100.0f)));
    if (pct < minPercent)
        pct = minPercent;
    if (pct <= 0)
        return;
    float loss((aggSec + 10.0f) * (pct / 100.0f) * sConfig.rates.secRate);
    std::string msg("Security status lost for ");
    msg += what;
    msg += " in ";
    msg += pAggressor->SystemMgr()->GetNameStr();
    uint16 eventType(Standings::CombatAggression);
    if (strcmp(what, "pod kill") == 0) {
        eventType = Standings::CombatPodKill;
    } else if (strcmp(what, "ship kill") == 0) {
        eventType = Standings::CombatShipKill;
    }
    SetSecurityStatus(pAggressor->GetCharacterID(), aggSec - loss, msg, eventType);
}

void CrimeWatch::SetSecurityStatus(uint32 charID, float newSec, const std::string& reason, uint16 eventType)
{
    if (newSec > 10.0f)
        newSec = 10.0f;
    if (newSec < -10.0f)
        newSec = -10.0f;

    Client* pClient(sEntityList.FindClientByCharID(charID));
    CharacterRef cRef(pClient != nullptr ? pClient->GetChar() : sItemFactory.GetCharacterRef(charID));
    if (cRef.get() == nullptr)
        return;
    float oldSec(cRef->GetSecurityRating());
    if (newSec == oldSec)
        return;
    cRef->secStatusChange(newSec - oldSec);
    // in memory now.  the db copy is what other pilots' clients and logins read
    DBerror err;
    sDatabase.RunQuery(err, "UPDATE chrCharacters SET securityRating = %f WHERE characterID = %u", cRef->GetSecurityRating(), charID);
    // the character sheet's security status history
    StandingDB::SaveStandingChanges(corpCONCORD, charID, eventType, newSec - oldSec, reason);
    _log(CRIME__INFO, "Security status of %u: %.4f -> %.4f (%s)", charID, oldSec, cRef->GetSecurityRating(), reason.c_str());

    if (pClient != nullptr) {
        // the client's security status is its standing from CONCORD
        PyTuple* payload = new PyTuple(3);
            payload->SetItem(0, new PyInt(corpCONCORD));
            payload->SetItem(1, new PyInt(charID));
            payload->SetItem(2, new PyFloat(cRef->GetSecurityRating()));
        pClient->SendNotification("OnStandingSet", "charid", &payload);
    }
}

/* ------------------------------------------------------------------------------------------------------------------
 *  kill rights
 * ------------------------------------------------------------------------------------------------------------------ */
PyTuple* CrimeWatch::GetKillRights(uint32 charID)
{
    // (killRights, killedRights): {targetID: time} I may kill, {holderID: time} who may kill me
    int64 now(GetFileTimeNow());
    PyDict* mine = new PyDict();
    PyDict* onMe = new PyDict();
    for (auto holder : m_killRights)
        for (auto target : holder.second) {
            if (target.second <= now)
                continue;
            if (holder.first == charID)
                mine->SetItem(new PyInt(target.first), new PyLong(target.second));
            if (target.first == charID)
                onMe->SetItem(new PyInt(holder.first), new PyLong(target.second));
        }
    PyTuple* tuple = new PyTuple(2);
        tuple->SetItem(0, mine);
        tuple->SetItem(1, onMe);
    return tuple;
}

void CrimeWatch::GrantKillRight(uint32 holderID, uint32 targetID)
{
    int64 expires(GetFileTimeNow() + (int64)sConfig.crime.KillRightDays * EvE::Time::Day);
    m_killRights[holderID][targetID] = expires;
    DBerror err;
    sDatabase.RunQuery(err, "REPLACE INTO crimeKillRights (holderID, targetID, expiresAt) VALUES (%u, %u, %lli)", holderID, targetID, expires);
    _log(CRIME__INFO, "%u gets a kill right on %u.", holderID, targetID);
    SendKillRightChange(true, expires, holderID, targetID);
}

void CrimeWatch::RemoveKillRight(uint32 holderID, uint32 targetID)
{
    std::map<uint32, std::map<uint32, int64>>::iterator itr = m_killRights.find(holderID);
    if (itr == m_killRights.end())
        return;
    itr->second.erase(targetID);
    DBerror err;
    sDatabase.RunQuery(err, "DELETE FROM crimeKillRights WHERE holderID = %u AND targetID = %u", holderID, targetID);
    _log(CRIME__INFO, "%u used their kill right on %u.", holderID, targetID);
    SendKillRightChange(false, GetFileTimeNow(), holderID, targetID);
}

void CrimeWatch::SendKillRightChange(bool set, int64 when, uint32 holderID, uint32 targetID)
{
    // consider.OnKillRightChange(set, whenTimestamp, fromID, toID)  fromID holds the right on toID
    for (uint32 id : {holderID, targetID}) {
        Client* pClient(sEntityList.FindClientByCharID(id));
        if (pClient == nullptr)
            continue;
        PyTuple* payload = new PyTuple(4);
            payload->SetItem(0, new PyBool(set));
            payload->SetItem(1, new PyLong(when));
            payload->SetItem(2, new PyInt(holderID));
            payload->SetItem(3, new PyInt(targetID));
        pClient->SendNotification("OnKillRightChange", "charid", &payload);
    }
}
