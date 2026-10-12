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
#include "npc/NPCAI.h"
#include "npc/Sentry.h"
#include "npc/ai/AggroGroup.h"
#include "npc/ai/LawRules.h"
#include "npc/ai/NpcMath.h"
#include "ship/Ship.h"
#include "ship/modules/EwarModule.h"
#include "system/CrimeWatch.h"
#include "system/DestinyManager.h"
#include "system/SystemBubble.h"
#include "system/SystemManager.h"
#include "system/TargetManager.h"
#include "system/cosmicMgrs/SpawnMgr.h"

namespace {
    // CONCORD police (group 301).  the commander's damage multiplier (100000) makes its weapon effectively one-shot
    const uint16 typeConcordOfficer     = 3863;     // frigate.  -95% speed web
    const uint16 typeConcordCaptain     = 3885;     // cruiser.  -95% speed web
    const uint16 typeConcordCommander   = 11125;    // battleship

    const uint8 wholeResponse = 0;
    const uint8 firstWave     = 1;
    const uint8 commanderWave = 2;

    const float tackleScramble = 100.0f;    // more than any number of warp core stabilizers (crime.ConcordScriptedTackle)

    const uint16 effectConcordTargetJam = 3710;     // concordTargetJam (not in EVEEffectID)

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

    /* faction navy (group 288 'Faction Drone'):  a battleship, a cruiser and a frigate with a web per empire.  the ones with
     *  complete combat data (signature, ewar and repair effects).  none for factions without a navy of their own */
    void GetNavyTypes(uint32 factionID, std::vector<uint16>& types) {
        switch (factionID) {
            case factionAmarr:      types = {11137, 10001, 9998};  break;   // Imperial Navy Fleet Marshall, Colonel, Sergeant
            case factionAmmatar:    types = {10089, 10669, 10084}; break;   // Ammatar Navy General, Colonel, Sergeant
            case factionKhanid:     types = {10083, 10082, 10076}; break;   // Khanid Navy General, Colonel, Sergeant
            case factionCaldari:    types = {11138, 10050, 10651}; break;   // Caldari Navy Fleet Admiral, Vice Admiral, Captain 3rd Rank
            case factionMinmatar:   types = {11140, 10078, 10663}; break;   // Republic Fleet High Commander, High Captain, Private 1st Rank
            case factionGallente:   types = {11139, 10056, 10652}; break;   // Federation Navy Fleet General, Fleet Major, First Sergeant
            default:                types.clear();                 break;
        }
    }

    // a value of the type's attribute, or dogma's default when it doesn't have it
    double AttrOrDefault(InventoryItemRef iRef, uint16 attrID) {
        EvilNumber value;
        if (iRef->HasAttribute(attrID, value))
            return value.get_double();
        return sDataMgr.GetAttributeDefault(attrID);
    }

    // a chance attribute:  0..1, a few types use percent
    float Chance(double value) {
        if (value > 1.0)
            value /= 100.0;
        return static_cast<float>(value);
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

    // takes an entity in space out of play: its ewar and group, other entities' locks on it, then the system
    void RemoveLawNPC(SystemManager* pSystem, NPC* pNPC) {
        SystemEntity::SetUnkillable(pNPC->GetID(), false);
        // no killer:  nobody is alerted (law npcs never alert anyway)
        pNPC->GetAIMgr()->OnKilled(nullptr);
        if (pNPC->TargetMgr() != nullptr)
            pNPC->TargetMgr()->ClearFromTargets();
        pSystem->RemoveNPC(pNPC);
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
        // Crimewatch 1:  aggression is kept per guarded gate or station
        pSentry->SetLawSentry(true, pSE->GetID());
        m_system->AddEntity(pSentry, false);
        // gate guns can't be destroyed
        SystemEntity::SetUnkillable(pSentry->GetID(), true);
        m_sentries.push_back(pSentry->GetID());
    }
}

/* ------------------------------------------------------------------------------------------------------------------
 *  responses
 * ------------------------------------------------------------------------------------------------------------------ */
uint32 LawEnforcement::ConcordDelay()
{
    // response seconds for 1.0, 0.9, 0.8, 0.7, 0.6, 0.5 (default "2,5,10,15,20,25":  the closest pre-Crucible measurements,
    //  owner decision 3).  npc/ai/LawRules.h
    return LawRules::ConcordDelaySeconds(sConfig.crime.ConcordResponse,
                                         CrimeWatch::SecurityLevel(m_system->GetSystemSecurityRating()));
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

void LawEnforcement::ReportNavyTarget(Client* pClient, uint32 factionID)
{
    if (!sConfig.crime.FactionNavy or (pClient == nullptr) or (factionID == 0))
        return;
    std::vector<uint16> types;
    GetNavyTypes(factionID, types);
    if (types.empty())
        return;     // no navy of their own
    Report(pClient->GetCharacterID(), pClient->GetName(), Law::Navy, sConfig.crime.PoliceDelay, factionID);
}

const char* LawEnforcement::KindName(uint8 kind)
{
    switch (kind) {
        case Law::Concord:  return "CONCORD";
        case Law::Police:   return "Faction police";
        case Law::Navy:     return "Faction navy";
    }
    return "Law enforcement";
}

void LawEnforcement::Report(uint32 id, const char* name, uint8 kind, uint32 delaySeconds, uint32 factionID/*0*/)
{
    uint64_t key((uint64_t)id * 4 + kind);
    if (m_responses.find(key) != m_responses.end())
        return;     // already on their way (or here)
    Response& r = m_responses[key];
    r.kind = kind;
    r.charID = id;
    r.factionID = factionID;
    r.due = GetFileTimeNow() + (int64)delaySeconds * EvE::Time::Second;
    _log(CRIME__INFO, "%s dispatched for %s(%u) in %s, arriving in %us.", KindName(kind), name, id, m_system->GetName(), delaySeconds);
}

bool LawEnforcement::UsesTypeEwar(NPC* pNPC)
{
    if ((pNPC == nullptr) or !pNPC->IsLawEnforcer())
        return false;
    // CONCORD's scram, web and jam are LawEnforcement's (concord* effects, or the scripted tackle).  NPCEwar runs its neut
    if (pNPC->IsConcord())
        return !sConfig.crime.ConcordScriptedTackle;
    // police and navy:  their data ewar (scram, web, ecm, damps, neuts), whatever npc.UseEwar says (npc ai plan 4.9)
    return true;
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
    } else if (r.kind == Law::Navy) {
        factionID = r.factionID;
        corpID = sDataMgr.GetFactionCorp(factionID);
        GetNavyTypes(factionID, types);
    } else {
        SystemData data;
        sDataMgr.GetSystemData(m_system->GetID(), data);
        factionID = data.factionID;
        corpID = sDataMgr.GetFactionCorp(factionID);
        GetPoliceTypes(factionID, types);
    }

    /* the response is one aggro group (npc ai plan 6.5).  it only gives the ships one identity (/npcai, phase 3's member walks):
     *  law npcs never alert or join a group, LawEnforcement coordinates them */
    uint64_t key((uint64_t)r.charID * 4 + r.kind);
    if ((r.group.get() == nullptr) and (m_system->GetSpawnMgr() != nullptr))
        r.group = m_system->GetSpawnMgr()->GetAggroGroups().Get(NpcAI::GroupKind::Law, key);
    for (auto typeID : types) {
        GPoint pos(pTarget->GetPosition());
        pos.MakeRandomPointOnSphere(MakeRandomFloat(4000, 9000));
        NPC* pNPC(SpawnNPC(typeID, pos, factionID, corpID, r.shipID, r.kind));
        if (pNPC != nullptr) {
            r.npcs.push_back(pNPC->GetID());
            m_npcResponse[pNPC->GetID()] = key;
            if (r.group.get() != nullptr)
                pNPC->GetAIMgr()->SetGroup(r.group);
            // CONCORD's data tackle (npc ai plan N6)
            if ((r.kind == Law::Concord) and !sConfig.crime.ConcordScriptedTackle)
                BuildTackle(pNPC, r.tackle[pNPC->GetID()]);
        }
    }
    _log(CRIME__INFO, "%s arrived for %s(%u) in %s (%lu ships).", KindName(r.kind), pTarget->GetName(), r.shipID,
            m_system->GetName(), r.npcs.size());
}

void LawEnforcement::Retarget(Response& r, uint32 shipID)
{
    // they're in another ship now (boarded one).  the old one is gone, and so is its tackle
    ReleaseTackle(r);
    r.shipID = shipID;
    for (auto cur : r.npcs) {
        NPC* pNPC(GetLawNPC(cur));
        if (pNPC != nullptr)
            pNPC->SetLawTarget(shipID, r.kind);
    }
}

NPC* LawEnforcement::GetLawNPC(uint32 npcID)
{
    SystemEntity* pSE(m_system->GetSE(npcID));
    if ((pSE == nullptr) or !pSE->IsNPCSE() or pSE->IsDead())
        return nullptr;
    return pSE->GetNPCSE();
}

void LawEnforcement::ForgetLawNPC(uint32 npcID)
{
    m_npcResponse.erase(npcID);
    SystemEntity::SetUnkillable(npcID, false);
    /* killed, or a GM /unspawn:  out of space already (SystemEntity::Delete()), its item deleted.  the object itself is still
     *  in the system's npc list (nothing else frees it):  ours to free */
    NPC* pNPC(m_system->GetNPCSE(npcID));
    if (pNPC == nullptr)
        return;
    m_system->RemoveDeadNPC(pNPC);
    delete pNPC;
}

void LawEnforcement::PruneNPCs(Response& r)
{
    std::vector<uint32>::iterator nItr = r.npcs.begin();
    while (nItr != r.npcs.end()) {
        if (GetLawNPC(*nItr) != nullptr) {
            ++nItr;
            continue;
        }
        // police can be killed, and a GM can remove any of them
        std::map<uint32, std::vector<TackleEffect>>::iterator tItr(r.tackle.find(*nItr));
        if (tItr != r.tackle.end()) {
            for (auto& fx : tItr->second)
                if (fx.targetID != 0)
                    EndTackle(*nItr, fx);
            r.tackle.erase(tItr);
        }
        ForgetLawNPC(*nItr);
        nItr = r.npcs.erase(nItr);
    }
}

void LawEnforcement::OnLawAttack(NPC* pNPC, SystemEntity* pTarget)
{
    if ((pNPC == nullptr) or (pTarget == nullptr) or (pTarget->DestinyMgr() == nullptr))
        return;
    Response* pResponse(nullptr);
    std::unordered_map<uint32, uint64_t>::iterator nItr(m_npcResponse.find(pNPC->GetID()));
    if (nItr != m_npcResponse.end()) {
        std::map<uint64_t, Response>::iterator rItr(m_responses.find(nItr->second));
        if ((rItr != m_responses.end())
        and (std::find(rItr->second.npcs.begin(), rItr->second.npcs.end(), pNPC->GetID()) != rItr->second.npcs.end()))
            pResponse = &rItr->second;
    }
    if ((pResponse == nullptr) or (pResponse->shipID != pTarget->GetID()))
        return;
    Response& r = *pResponse;

    // police pursuit jams cloaks:  out of cloak, and no cloaking while CONCORD is on it
    m_pursued.insert(r.shipID);
    if (pTarget->IsShipSE() and pTarget->DestinyMgr()->IsModuleCloaked())
        pTarget->GetShipSE()->Decloak("CloakingDisengagedPolicePursuit");

    // the rest is the scripted tackle.  otherwise CONCORD's data ewar does it (ProcessTackle, NPCEwar)
    if (!sConfig.crime.ConcordScriptedTackle)
        return;
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
    m_pursued.erase(r.shipID);
    // CONCORD's data tackle ends with it (it starts again on the next ship)
    for (auto& cur : r.tackle)
        for (auto& fx : cur.second) {
            if (fx.targetID != 0)
                EndTackle(cur.first, fx);
            fx.nextAt = 0;
        }
}

/* ------------------------------------------------------------------------------------------------------------------
 *  CONCORD's data tackle (npc ai plan 4.8, N6)
 *   concordWarpScramble        entityWarpScrambleChance (per ship)   warpScrambleDuration      warpScrambleRange      strength (105)
 *   concordModifyTargetSpeed   modifyTargetSpeedChance (per ship)    modifyTargetSpeedDuration modifyTargetSpeedRange speedFactor (20)
 *   concordTargetJam           entityTargetJamDurationChance (each cycle)  entityTargetJamDuration  936 / 953 falloff
 *  the usage chances are rolled once per ship (plan decision 6).  the jam has no jammer strength in the data:  it isn't a
 *  contest with the target's sensors, it lands whenever the cycle's chance and range rolls do
 * ------------------------------------------------------------------------------------------------------------------ */
void LawEnforcement::BuildTackle(NPC* pNPC, std::vector<TackleEffect>& into)
{
    into.clear();
    InventoryItemRef iRef(pNPC->GetSelf());
    const ItemType& type(iRef->type());
    NpcRng& rng(pNPC->GetAIMgr()->GetRng());

    if (type.HasEffect(EVEEffectID::concordWarpScramble) and (rng.Uniform() < Chance(AttrOrDefault(iRef, AttrEntityWarpScrambleChance)))) {
        TackleEffect fx;
            fx.kind = Ewar::Scramble;
            fx.guid = "effects.WarpScramble";
            fx.duration = static_cast<uint32>(AttrOrDefault(iRef, AttrWarpScrambleDuration));
            fx.range = AttrOrDefault(iRef, AttrWarpScrambleRange);
            fx.strength = static_cast<float>(AttrOrDefault(iRef, AttrWarpScrambleStrength));
        if (fx.strength <= 0.0f)
            fx.strength = 1.0f;
        into.push_back(fx);
    }
    if (type.HasEffect(EVEEffectID::concordModifyTargetSpeed) and (iRef->GetAttribute(AttrSpeedFactor).get_float() < 0.0f)
    and (rng.Uniform() < Chance(AttrOrDefault(iRef, AttrModifyTargetSpeedChance)))) {
        TackleEffect fx;
            fx.kind = Ewar::Web;
            fx.guid = "effects.ModifyTargetSpeed";
            fx.duration = static_cast<uint32>(AttrOrDefault(iRef, AttrModifyTargetSpeedDuration));
            fx.range = AttrOrDefault(iRef, AttrModifyTargetSpeedRange);
        into.push_back(fx);
    }
    if (type.HasEffect(effectConcordTargetJam)) {
        TackleEffect fx;
            fx.kind = Ewar::Jam;
            fx.guid = "effects.ElectronicAttributeModifyTarget";     // the effect has no guid:  the player ecm beam
            fx.duration = static_cast<uint32>(AttrOrDefault(iRef, AttrEntityTargetJamDuration));
            fx.chance = Chance(AttrOrDefault(iRef, AttrEntityTargetJamDurationChance));
            fx.range = AttrOrDefault(iRef, AttrEntityTargetJamMaxRange);
            fx.falloff = AttrOrDefault(iRef, AttrEntityTargetJamFallOff);
        if (fx.chance > 0.0f)
            into.push_back(fx);
    }
    std::vector<TackleEffect>::iterator itr(into.begin());
    while (itr != into.end()) {
        itr->typeID = static_cast<uint16>(iRef->typeID());
        if (itr->duration < 1000)
            itr->duration = 1000;
        if (itr->range <= 0.0) {
            itr = into.erase(itr);
        } else {
            ++itr;
        }
    }
    _log(CRIME__TRACE, "%s(%u): %lu CONCORD tackle effects.", pNPC->GetName(), pNPC->GetID(), into.size());
}

void LawEnforcement::ProcessTackle(Response& r, SystemEntity* pTarget)
{
    int64 now(GetSteadyTime());
    for (auto& cur : r.tackle) {
        NPC* pNPC(GetLawNPC(cur.first));
        // only once it has the ship locked
        bool locked((pNPC != nullptr) and (pNPC->TargetMgr() != nullptr)
                    and (pNPC->TargetMgr()->GetTarget(pTarget->GetID(), true) != nullptr));
        for (auto& fx : cur.second) {
            if (!locked) {
                if (fx.targetID != 0)
                    EndTackle(cur.first, fx);
                fx.nextAt = 0;
                continue;
            }
            // the first cycle right away, then every duration (drift free)
            if (fx.nextAt == 0)
                fx.nextAt = now;
            if (now < fx.nextAt)
                continue;
            if ((now - fx.nextAt) > (2 * (int64)fx.duration)) {
                fx.nextAt = now + fx.duration;
            } else {
                fx.nextAt += fx.duration;
            }
            NpcRng& rng(pNPC->GetAIMgr()->GetRng());
            double surface(pNPC->GetPosition().distance(pTarget->GetPosition()) - pNPC->GetRadius() - pTarget->GetRadius());
            if ((rng.Uniform() >= fx.chance) or (rng.Uniform() >= NpcMath::EwarRangeChance(surface, fx.range, fx.falloff))) {
                // not this cycle
                if (fx.targetID != 0)
                    EndTackle(cur.first, fx);
                continue;
            }
            if (fx.targetID == 0)
                StartTackle(pNPC, fx, pTarget);
            switch (fx.kind) {
                case Ewar::Scramble: {
                    // one second of overlap keeps it from dropping between cycles
                    pTarget->GetEwar(true)->Apply(pNPC->GetID(), Ewar::Scramble, fx.duration + 1000, fx.strength);
                } break;
                case Ewar::Web: {
                    pTarget->GetEwar(true)->Apply(pNPC->GetID(), Ewar::Web, fx.duration + 1000, 0, 0, pNPC->GetSelf());
                } break;
                case Ewar::Jam: {
                    Ewar::EndJam(pNPC, 0, pTarget);
                    Ewar::StartJam(pNPC, 0, pTarget, fx.duration);
                } break;
            }
        }
    }
}

void LawEnforcement::StartTackle(NPC* pNPC, TackleEffect& fx, SystemEntity* pTarget)
{
    fx.targetID = pTarget->GetID();
    DestinyManager* pDestiny(pNPC->DestinyMgr() != nullptr ? pNPC->DestinyMgr() : pTarget->DestinyMgr());
    if (pDestiny != nullptr)
        pDestiny->SendSpecialEffect(pNPC->GetID(), pNPC->GetID(), pNPC->GetSelf()->typeID(), fx.targetID, 0, fx.guid,
                                    true, true, true, fx.duration, 1000);
    // EwarState tells the pilot about scrams and webs.  the jammer's icon is ours to send
    if (fx.kind == Ewar::Jam)
        Ewar::SendEwarNotify(true, pNPC->GetID(), pNPC->GetID(), pTarget, "electronic");
}

void LawEnforcement::EndTackle(uint32 npcID, TackleEffect& fx)
{
    uint32 targetID(fx.targetID);
    fx.targetID = 0;
    SystemEntity* pTarget(m_system->GetSE(targetID));
    if (pTarget == nullptr)
        return;
    // the ship that ran it may be gone already (killed, GM /unspawn):  the target's bubble sees the fx stop
    NPC* pNPC(GetLawNPC(npcID));
    DestinyManager* pDestiny(((pNPC != nullptr) and (pNPC->DestinyMgr() != nullptr)) ? pNPC->DestinyMgr() : pTarget->DestinyMgr());
    if (pDestiny != nullptr)
        pDestiny->SendSpecialEffect(npcID, npcID, fx.typeID, targetID, 0, fx.guid, true, false, false, 0, 0);
    // (Ewar::EndJam() is this Remove() for the jam)
    if (pTarget->GetEwar() != nullptr)
        pTarget->GetEwar()->Remove(npcID, fx.kind);
    if (fx.kind == Ewar::Jam)
        Ewar::SendEwarNotify(false, npcID, npcID, pTarget, "electronic");
}

void LawEnforcement::Despawn(Response& r)
{
    ReleaseTackle(r);
    r.tackle.clear();
    for (auto cur : r.npcs) {
        NPC* pNPC(GetLawNPC(cur));
        if (pNPC == nullptr) {
            ForgetLawNPC(cur);
            continue;
        }
        m_npcResponse.erase(cur);
        RemoveLawNPC(m_system, pNPC);
    }
    r.npcs.clear();
    // the group goes with its last member (a new arrival makes a new one)
    r.group.reset();
}

void LawEnforcement::Process()
{
    int64 now(GetFileTimeNow());
    ProcessLoggedOff(now);

    std::map<uint64_t, Response>::iterator itr = m_responses.begin();
    while (itr != m_responses.end()) {
        Response& r = itr->second;

        // police can be killed, and a GM can take any of them out of space
        PruneNPCs(r);

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

        NPC* pFront(GetLawNPC(r.npcs.front()));
        if ((pFront == nullptr) or (pTarget->SysBubble() != pFront->SysBubble())) {
            if (r.kind == Law::Concord) {
                // got off the grid.  CONCORD arrives where they went (its tackle goes with it, Despawn)
                Despawn(r);
                r.started = 0;
                r.nextWave = 0;
                r.due = now + 3 * EvE::Time::Second;
            } else {
                // the police (and navy) can be escaped
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

        // CONCORD's data tackle (crime.ConcordScriptedTackle false)
        if (!r.tackle.empty())
            ProcessTackle(r, pTarget);

        // police and navy give up after a while
        if ((r.kind != Law::Concord) and (now > (r.started + (int64)sConfig.crime.PoliceTimeout * EvE::Time::Second)))
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
    for (auto& cur : m_responses) {
        // (the ones a GM took out of space aren't the system's any more:  ours to free)
        for (auto npcID : cur.second.npcs)
            if (GetLawNPC(npcID) == nullptr)
                ForgetLawNPC(npcID);
        for (auto npcID : cur.second.npcs)
            SystemEntity::SetUnkillable(npcID, false);
    }
    for (auto sentryID : m_sentries)
        SystemEntity::SetUnkillable(sentryID, false);
    for (auto cur : m_loggedOff)
        cur.second.pShipSE->GetShipItemRef()->SaveShip();
    m_responses.clear();
    m_npcResponse.clear();
    m_loggedOff.clear();
    m_sentries.clear();
    m_jammed.clear();
    m_pursued.clear();
}
