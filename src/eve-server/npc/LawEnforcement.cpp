/**
 * @name LawEnforcement.cpp
 *   CONCORD, faction police, gate/station sentries and ships left in space by pilots who log off.
 *   see LawEnforcement.h
 */

#include "eve-server.h"

#include "Client.h"
#include "EntityList.h"
#include "EVEServerConfig.h"
#include "StaticDataMgr.h"
#include "inventory/AttributeEnum.h"
#include "npc/LawEnforcement.h"
#include "npc/NPC.h"
#include "npc/Sentry.h"
#include "ship/Ship.h"
#include "system/CrimeWatch.h"
#include "system/DestinyManager.h"
#include "system/SystemBubble.h"
#include "system/SystemManager.h"
#include "system/TargetManager.h"

namespace {
    // CONCORD police (group 301).  the commander's damage multiplier (100000) makes its weapon effectively one-shot
    const uint16 typeConcordOfficer     = 3863;     // frigate.  -95% speed web
    const uint16 typeConcordCaptain     = 3885;     // cruiser.  -95% speed web
    const uint16 typeConcordCommander   = 11125;    // battleship

    const uint8 wholeResponse = 0;
    const uint8 firstWave     = 1;
    const uint8 commanderWave = 2;

    const float tackleScramble = 100.0f;    // more than any number of warp core stabilizers

    // faction police (group 182): a big ship and two smaller ones per empire
    void GetPoliceTypes(uint32 factionID, std::vector<uint16>& types) {
        switch (factionID) {
            case factionAmarr:      types = {11515, 9960, 9962};  break;   // Amarr Surveillance General Major, Sergeant Major, Sergeant
            case factionAmmatar:    types = {11517, 10099, 10100}; break;  // Ammatar Surveillance
            case factionKhanid:     types = {11518, 10674, 9988};  break;  // Khanid Surveillance
            case factionCaldari:    types = {11521, 9971, 10660};  break;  // Caldari Police Commissioner, 1st and 2nd Lieutenant
            case factionMinmatar:   types = {11520, 9977, 9978};   break;  // Minmatar Security High Captain, 2nd and 1st Rank
            case factionGallente:
            default:                types = {11519, 9983, 9984};   break;  // Gallente Police Major, Master Sergeant, Captain
        }
    }

    // gate and station guns (group 99)
    uint16 GetSentryType(uint32 factionID) {
        switch (factionID) {
            case factionAmarr:
            case factionAmmatar:
            case factionKhanid:     return 1194;    // Amarr Sentry Gun
            case factionCaldari:    return 3740;    // Caldari Sentry Gun I
            case factionMinmatar:   return 3743;    // Minmatar Sentry Gun
            case factionGallente:
            default:                return 3742;    // Gallente Sentry Gun
        }
    }

    // takes an entity out of play: other entities' locks on it, then the system
    void RemoveLawNPC(SystemManager* pSystem, NPC* pNPC) {
        SystemEntity::SetUnkillable(pNPC->GetID(), false);
        if (pNPC->IsDead()) {
            pSystem->RemoveDeadNPC(pNPC);       // already removed from space when it died
        } else {
            if (pNPC->TargetMgr() != nullptr)
                pNPC->TargetMgr()->ClearFromTargets();
            pSystem->RemoveNPC(pNPC);
        }
        delete pNPC;
    }
}

LawEnforcement::LawEnforcement(SystemManager* pSystem, EVEServiceManager& services)
: m_system(pSystem),
m_services(services)
{
}

/* ------------------------------------------------------------------------------------------------------------------
 *  sentries
 * ------------------------------------------------------------------------------------------------------------------ */
void LawEnforcement::Init()
{
    if (!sConfig.crime.Enabled or !sConfig.crime.Sentries)
        return;
    if (CrimeWatch::SecurityClass(m_system->GetSystemSecurityRating()) == Crime::SecClass::Null)
        return;
    SystemData data;
    if (!sDataMgr.GetSystemData(m_system->GetID(), data) or (data.factionID == 0))
        return;
    uint16 typeID(GetSentryType(data.factionID));
    uint32 corpID(sDataMgr.GetFactionCorp(data.factionID));

    for (auto cur : m_system->GetGates())
        SpawnSentries(cur.second, sConfig.crime.SentriesPerGate, typeID, data.factionID, corpID);
    for (auto cur : m_system->GetEntities())
        if (cur.second->IsStationSE())
            SpawnSentries(cur.second, sConfig.crime.SentriesPerStation, typeID, data.factionID, corpID);

    _log(CRIME__TRACE, "%s: %lu sentry guns placed.", m_system->GetName(), m_sentries.size());
}

void LawEnforcement::SpawnSentries(SystemEntity* pSE, uint8 count, uint16 typeID, uint32 factionID, uint32 corpID)
{
    if (pSE == nullptr)
        return;
    for (uint8 i = 0; i < count; ++i) {
        GPoint pos(pSE->GetPosition());
        pos.MakeRandomPointOnSphere(pSE->GetRadius() + MakeRandomFloat(5000, 15000));
        ItemData idata(typeID, corpID, m_system->GetID(), flagNone, "", pos);
        InventoryItemRef iRef(sItemFactory.SpawnItem(idata));
        if (iRef.get() == nullptr)
            continue;
        FactionData fData = FactionData();
            fData.allianceID = factionID;
            fData.factionID = factionID;
            fData.corporationID = corpID;
            fData.ownerID = corpID;
        Sentry* pSentry = new Sentry(iRef, m_services, m_system, fData);
        pSentry->SetLawSentry(true);
        m_system->AddEntity(pSentry, false);
        // gate guns can't be destroyed
        SystemEntity::SetUnkillable(pSentry->GetID(), true);
        m_sentries.push_back(pSentry);
    }
}

/* ------------------------------------------------------------------------------------------------------------------
 *  responses
 * ------------------------------------------------------------------------------------------------------------------ */
uint32 LawEnforcement::ConcordDelay()
{
    // response seconds for 1.0, 0.9, 0.8, 0.7, 0.6, 0.5
    std::vector<uint32> table;
    std::stringstream ss(sConfig.crime.ConcordResponse);
    std::string item;
    while (std::getline(ss, item, ','))
        table.push_back(atoi(item.c_str()));
    if (table.size() < 6)
        table = {2, 5, 10, 15, 20, 25};     // the default:  closest pre-Crucible measurements (npc ai plan, open question 3)
    float level(CrimeWatch::SecurityLevel(m_system->GetSystemSecurityRating()));
    int idx((int)roundf((1.0f - level) * 10.0f));
    if (idx < 0)
        idx = 0;
    if (idx > 5)
        idx = 5;
    return table[idx];
}

void LawEnforcement::ReportCriminal(Client* pClient)
{
    if (!sConfig.crime.Concord or (pClient == nullptr))
        return;
    Report(pClient->GetCharacterID(), pClient->GetName(), Law::Concord, ConcordDelay());
}

void LawEnforcement::ReportCriminalEntity(SystemEntity* pSE)
{
    if (!sConfig.crime.Concord or (pSE == nullptr))
        return;
    Report(pSE->GetID(), pSE->GetName(), Law::Concord, ConcordDelay());
}

void LawEnforcement::ReportPoliceTarget(Client* pClient)
{
    if (!sConfig.crime.FactionPolice or (pClient == nullptr))
        return;
    Report(pClient->GetCharacterID(), pClient->GetName(), Law::Police, sConfig.crime.PoliceDelay);
}

void LawEnforcement::Report(uint32 id, const char* name, uint8 kind, uint32 delaySeconds)
{
    uint64_t key((uint64_t)id * 4 + kind);
    if (m_responses.find(key) != m_responses.end())
        return;     // already on their way (or here)
    Response& r = m_responses[key];
    r.kind = kind;
    r.charID = id;
    r.due = GetFileTimeNow() + (int64)delaySeconds * EvE::Time::Second;
    _log(CRIME__INFO, "%s dispatched for %s(%u) in %s, arriving in %us.", (kind == Law::Concord ? "CONCORD" : "Faction police"),
            name, id, m_system->GetName(), delaySeconds);
}

SystemEntity* LawEnforcement::FindTarget(uint32 charID)
{
    // a criminal that isn't a player (GM testing): the entity itself
    if (!IsCharacterID(charID)) {
        SystemEntity* pSE(m_system->GetSE(charID));
        if ((pSE == nullptr) or pSE->IsDead() or (pSE->SysBubble() == nullptr) or (pSE->DestinyMgr() == nullptr))
            return nullptr;
        return pSE;
    }
    // the ship they're flying here.  never a pod
    Client* pClient(sEntityList.FindClientByCharID(charID));
    if ((pClient != nullptr) and pClient->IsInSpace() and (pClient->SystemMgr() == m_system) and !pClient->InPod()) {
        ShipSE* pShipSE(pClient->GetShipSE());
        if ((pShipSE != nullptr) and !pShipSE->IsDead() and (pShipSE->SysBubble() != nullptr))
            return pShipSE;
    }
    // or the ship they left in space when they logged off
    for (auto cur : m_loggedOff)
        if ((cur.second.charID == charID) and !cur.second.pShipSE->IsDead() and (cur.second.pShipSE->SysBubble() != nullptr)
        and (cur.second.pShipSE->GetSelf()->groupID() != EVEDB::invGroups::Capsule))
            return cur.second.pShipSE;
    return nullptr;
}

NPC* LawEnforcement::SpawnNPC(uint16 typeID, const GPoint& pos, uint32 factionID, uint32 corpID, uint32 targetID, uint8 role)
{
    ItemData idata(typeID, corpID, m_system->GetID(), flagNone, "", pos);
    InventoryItemRef iRef(sItemFactory.SpawnItem(idata));
    if (iRef.get() == nullptr) {
        _log(CRIME__ERROR, "LawEnforcement: unable to spawn type %u in %s.", typeID, m_system->GetName());
        return nullptr;
    }
    FactionData data = FactionData();
        data.allianceID = factionID;
        data.factionID = factionID;
        data.corporationID = corpID;
        data.ownerID = corpID;
    NPC* pNPC = new NPC(iRef, m_services, m_system, data);
    if (!pNPC->Load()) {
        SafeDelete(pNPC);
        iRef->Delete();
        return nullptr;
    }
    pNPC->SetLawTarget(targetID, role);
    m_system->AddNPC(pNPC);
    pNPC->DestinyMgr()->SetPosition(pos, true);
    if (role == Law::Concord)
        SystemEntity::SetUnkillable(pNPC->GetID(), true);   // CONCORD can't be beaten
    return pNPC;
}

void LawEnforcement::SpawnWave(Response& r, SystemEntity* pTarget, uint8 wave)
{
    std::vector<uint16> types;
    uint32 factionID(0), corpID(0);
    if (r.kind == Law::Concord) {
        factionID = factionCONCORD;
        corpID = corpCONCORD;
        if (wave == wholeResponse) {
            types = {typeConcordCommander, typeConcordCaptain, typeConcordCaptain};
        } else if (wave == firstWave) {
            types = {typeConcordOfficer, typeConcordOfficer, typeConcordCaptain};
        } else {
            types = {typeConcordCommander};
        }
    } else {
        SystemData data;
        sDataMgr.GetSystemData(m_system->GetID(), data);
        factionID = data.factionID;
        corpID = sDataMgr.GetFactionCorp(factionID);
        GetPoliceTypes(factionID, types);
    }

    for (auto typeID : types) {
        GPoint pos(pTarget->GetPosition());
        pos.MakeRandomPointOnSphere(MakeRandomFloat(4000, 9000));
        NPC* pNPC(SpawnNPC(typeID, pos, factionID, corpID, r.shipID, r.kind));
        if (pNPC != nullptr) {
            r.npcs.push_back(pNPC);
            m_npcResponse[pNPC] = (uint64_t)r.charID * 4 + r.kind;
        }
    }
    _log(CRIME__INFO, "%s arrived for %s(%u) in %s (%lu ships).", (r.kind == Law::Concord ? "CONCORD" : "Faction police"),
            pTarget->GetName(), r.shipID, m_system->GetName(), r.npcs.size());
}

void LawEnforcement::Retarget(Response& r, uint32 shipID)
{
    // they're in another ship now (boarded one).  the old one is gone, and so is its tackle
    ReleaseTackle(r);
    r.shipID = shipID;
    for (auto cur : r.npcs)
        cur->SetLawTarget(shipID, r.kind);
}

void LawEnforcement::OnLawAttack(NPC* pNPC, SystemEntity* pTarget)
{
    if ((pNPC == nullptr) or (pTarget == nullptr) or (pTarget->DestinyMgr() == nullptr))
        return;
    Response* pResponse(nullptr);
    std::unordered_map<NPC*, uint64_t>::iterator nItr(m_npcResponse.find(pNPC));
    if (nItr != m_npcResponse.end()) {
        std::map<uint64_t, Response>::iterator rItr(m_responses.find(nItr->second));
        if ((rItr != m_responses.end())
        and (std::find(rItr->second.npcs.begin(), rItr->second.npcs.end(), pNPC) != rItr->second.npcs.end()))
            pResponse = &rItr->second;
    }
    if ((pResponse == nullptr) or (pResponse->shipID != pTarget->GetID()))
        return;
    Response& r = *pResponse;

    // police pursuit jams cloaks
    if (pTarget->IsShipSE() and pTarget->DestinyMgr()->IsModuleCloaked())
        pTarget->GetShipSE()->Decloak("CloakingDisengagedPolicePursuit");

    // warp scrambled
    if (r.scram <= 0) {
        InventoryItemRef shipRef(pTarget->GetSelf());
        shipRef->SetAttribute(AttrWarpScrambleStatus, shipRef->GetAttribute(AttrWarpScrambleStatus).get_float() + tackleScramble);
        r.scram = tackleScramble;
    }
    // webbed, by every ship that carries a web
    InventoryItemRef npcRef(pNPC->GetSelf());
    if (npcRef->HasAttribute(AttrSpeedFactor) and (npcRef->GetAttribute(AttrSpeedFactor).get_float() < 0)) {
        bool webbing(false);
        for (auto cur : r.webs)
            if (cur.get() == npcRef.get())
                webbing = true;
        if (!webbing) {
            pTarget->DestinyMgr()->WebbedMe(npcRef, true);
            r.webs.push_back(npcRef);
        }
    }
    // jammed: loses its locks and can't lock anything while CONCORD is on it
    if (!r.jammed and (pTarget->TargetMgr() != nullptr)) {
        // its own locks only (ClearAllTargets() also broke CONCORD's lock on it, so CONCORD had to lock again before its next shot)
        pTarget->TargetMgr()->LoseAllTargets();
        m_jammed.insert(r.shipID);
        r.jammed = true;
    }
}

void LawEnforcement::ReleaseTackle(Response& r)
{
    SystemEntity* pTarget(m_system->GetSE(r.shipID));
    if ((pTarget != nullptr) and !pTarget->IsDead()) {
        if (r.scram > 0) {
            InventoryItemRef shipRef(pTarget->GetSelf());
            shipRef->SetAttribute(AttrWarpScrambleStatus, shipRef->GetAttribute(AttrWarpScrambleStatus).get_float() - r.scram);
        }
        if (pTarget->DestinyMgr() != nullptr)
            for (auto cur : r.webs)
                pTarget->DestinyMgr()->WebbedMe(cur, false);
    }
    r.scram = 0.0f;
    r.webs.clear();
    if (r.jammed) {
        m_jammed.erase(r.shipID);
        r.jammed = false;
    }
}

void LawEnforcement::Despawn(Response& r)
{
    ReleaseTackle(r);
    for (auto cur : r.npcs) {
        m_npcResponse.erase(cur);
        RemoveLawNPC(m_system, cur);
    }
    r.npcs.clear();
}

void LawEnforcement::Process()
{
    int64 now(GetFileTimeNow());
    ProcessLoggedOff(now);

    std::map<uint64_t, Response>::iterator itr = m_responses.begin();
    while (itr != m_responses.end()) {
        Response& r = itr->second;

        // police can be killed
        std::vector<NPC*>::iterator nItr = r.npcs.begin();
        while (nItr != r.npcs.end()) {
            if ((*nItr)->IsDead()) {
                m_npcResponse.erase(*nItr);
                RemoveLawNPC(m_system, *nItr);
                nItr = r.npcs.erase(nItr);
            } else {
                ++nItr;
            }
        }

        SystemEntity* pTarget(FindTarget(r.charID));

        // on the way
        if (r.started == 0) {
            if (now < r.due) {
                ++itr;
                continue;
            }
            if (pTarget == nullptr) {
                // not in a ship here: docked, gone, or in a pod.  undocking or jumping in reports them again
                if (now > (r.due + 60 * EvE::Time::Second)) {
                    itr = m_responses.erase(itr);
                    continue;
                }
                ++itr;
                continue;
            }
            r.shipID = pTarget->GetID();
            r.started = now;
            if ((r.kind == Law::Concord) and sConfig.crime.ConcordWaves) {
                SpawnWave(r, pTarget, firstWave);
                r.nextWave = now + (int64)sConfig.crime.ConcordWaveDelay * EvE::Time::Second;
            } else {
                SpawnWave(r, pTarget, wholeResponse);
            }
            ++itr;
            continue;
        }

        // done, waiting to leave
        if (r.leaveAt != 0) {
            if (now >= r.leaveAt) {
                Despawn(r);
                itr = m_responses.erase(itr);
            } else {
                ++itr;
            }
            continue;
        }

        if (r.npcs.empty()) {
            // police beaten off.  they don't come back until the pilot arrives somewhere again
            itr = m_responses.erase(itr);
            continue;
        }

        if (pTarget == nullptr) {
            // destroyed, docked, jumped or logged off and gone
            ReleaseTackle(r);
            r.leaveAt = now + (int64)(r.kind == Law::Concord ? sConfig.crime.ConcordLinger : 10) * EvE::Time::Second;
            ++itr;
            continue;
        }
        if (pTarget->GetID() != r.shipID)
            Retarget(r, pTarget->GetID());

        if (pTarget->SysBubble() != r.npcs.front()->SysBubble()) {
            if (r.kind == Law::Concord) {
                // got off the grid.  CONCORD arrives where they went
                Despawn(r);
                r.started = 0;
                r.nextWave = 0;
                r.due = now + 3 * EvE::Time::Second;
            } else {
                // the police can be escaped
                ReleaseTackle(r);
                r.leaveAt = now + 10 * EvE::Time::Second;
            }
            ++itr;
            continue;
        }

        if ((r.nextWave != 0) and (now >= r.nextWave)) {
            SpawnWave(r, pTarget, commanderWave);
            r.nextWave = 0;
        }

        if ((r.kind == Law::Police) and (now > (r.started + (int64)sConfig.crime.PoliceTimeout * EvE::Time::Second)))
            r.leaveAt = now;

        ++itr;
    }
}

/* ------------------------------------------------------------------------------------------------------------------
 *  logoff
 * ------------------------------------------------------------------------------------------------------------------ */
void LawEnforcement::AddLoggedOffShip(ShipSE* pShipSE, uint32 charID, bool pvp, bool npc)
{
    if (pShipSE == nullptr)
        return;
    int64 now(GetFileTimeNow());
    LoggedOff& l = m_loggedOff[pShipSE->GetID()];
    l.pShipSE = pShipSE;
    l.charID = charID;
    l.loggedOff = now;
    l.pvp = pvp;
    if (pvp) {
        // stays where it is, 15 minutes after logoff or after the last attack on it, whichever is later
        l.removeAt = now + (int64)sConfig.crime.AggFlagTime * EvE::Time::Second;
    } else {
        // warps to a random spot and vanishes
        l.removeAt = now + (int64)(npc ? sConfig.crime.LogoffNPCTime : sConfig.crime.LogoffWarpTime) * EvE::Time::Second;
        if ((pShipSE->DestinyMgr() != nullptr) and (pShipSE->GetSelf()->GetAttribute(AttrWarpScrambleStatus) <= 0)) {
            GPoint dest(pShipSE->GetPosition());
            dest.MakeRandomPointOnSphere(0.5 * ONE_AU_IN_METERS);
            pShipSE->DestinyMgr()->WarpTo(dest);
        }
    }
    _log(CRIME__INFO, "%s(%u) left in space by %u for %us (%s).", pShipSE->GetName(), pShipSE->GetID(), charID,
            (uint32)((l.removeAt - now) / EvE::Time::Second), (pvp ? "player aggression" : (npc ? "npc aggression" : "logoff")));
}

void LawEnforcement::ProcessLoggedOff(int64 now)
{
    std::map<uint32, LoggedOff>::iterator itr = m_loggedOff.begin();
    while (itr != m_loggedOff.end()) {
        LoggedOff& l = itr->second;
        if (now < l.removeAt) {
            ++itr;
            continue;
        }
        // a ship that's still being fought over stays
        bool npcOnly(false);
        if (l.pvp and (sCrimeWatch.LogoffTimer(l.charID, npcOnly) > 0)) {
            ++itr;
            continue;
        }
        ShipSE* pShipSE(l.pShipSE);
        _log(CRIME__INFO, "%s(%u) of %u leaves space.", pShipSE->GetName(), pShipSE->GetID(), l.charID);
        ShipItemRef shipRef(pShipSE->GetShipItemRef());
        if (pShipSE->TargetMgr() != nullptr)
            pShipSE->TargetMgr()->ClearFromTargets();
        m_system->RemoveEntity(pShipSE);
        SafeDelete(pShipSE);
        // what the client did when its pilot logged off: save it and unload it
        shipRef->LogOut();
        itr = m_loggedOff.erase(itr);
    }
}

bool LawEnforcement::ReclaimLoggedOffShip(uint32 shipID, GPoint& position)
{
    std::map<uint32, LoggedOff>::iterator itr = m_loggedOff.find(shipID);
    if (itr == m_loggedOff.end())
        return false;
    ShipSE* pShipSE(itr->second.pShipSE);
    position = pShipSE->GetPosition();
    if (pShipSE->TargetMgr() != nullptr)
        pShipSE->TargetMgr()->ClearFromTargets();
    m_system->RemoveEntity(pShipSE);
    SafeDelete(pShipSE);
    m_loggedOff.erase(itr);
    return true;
}

uint32 LawEnforcement::GetLoggedOffOwner(uint32 shipID)
{
    std::map<uint32, LoggedOff>::iterator itr = m_loggedOff.find(shipID);
    if (itr == m_loggedOff.end())
        return 0;
    return itr->second.charID;
}

/* ------------------------------------------------------------------------------------------------------------------
 *  system
 * ------------------------------------------------------------------------------------------------------------------ */
bool LawEnforcement::IsBusy()
{
    return (!m_responses.empty() or !m_loggedOff.empty());
}

void LawEnforcement::Close()
{
    // the system deletes every entity, ours included
    for (auto& cur : m_responses)
        for (auto npc : cur.second.npcs)
            SystemEntity::SetUnkillable(npc->GetID(), false);
    for (auto cur : m_sentries)
        SystemEntity::SetUnkillable(cur->GetID(), false);
    for (auto cur : m_loggedOff)
        cur.second.pShipSE->GetShipItemRef()->SaveShip();
    m_responses.clear();
    m_npcResponse.clear();
    m_loggedOff.clear();
    m_sentries.clear();
    m_jammed.clear();
}
