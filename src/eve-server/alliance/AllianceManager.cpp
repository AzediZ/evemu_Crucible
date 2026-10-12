/**
 * @name AllianceManager.cpp
 *   alliance membership.  see AllianceManager.h
 *
 * Crucible rules:  "Accepted applications take 24 hours to become effective" (client message), a corporation with wars it
 *  declared can't join (CanNotAdoptWarsAsCorpHasDesclaredWarsInProgress), executor support only after a week of
 *  membership (CanNotDeclareExecutorInFirstWeek), and the corporation with the support of at least half of the members
 *  becomes executor at downtime (EVE University "Alliance", rule unchanged since 2008).  an alliance whose executor left
 *  has none ("in turmoil") until then.
 */

#include "eve-server.h"

#include "EVE_Corp.h"
#include "EVE_Roles.h"

#include "Client.h"
#include "EntityList.h"
#include "EVEServerConfig.h"
#include "alliance/AllianceBound.h"
#include "alliance/AllianceDB.h"
#include "alliance/AllianceManager.h"
#include "character/Character.h"
#include "corporation/BillingMgr.h"
#include "corporation/CorpMgr.h"
#include "corporation/CorporationDB.h"
#include "faction/WarManager.h"
#include "mail/MailMgr.h"
#include "system/sov/SovereigntyDataMgr.h"
#include "StaticDataMgr.h"

/*
 * ALLY__ERROR
 * ALLY__WARNING
 * ALLY__INFO
 * ALLY__TRACE
 */

namespace {
    PyRep* NullableInt(uint32 value)            { return (value == 0 ? PyStatic.NewNone() : new PyInt(value)); }

    void AddChange(PyDict* change, const char* key, PyRep* oldValue, PyRep* newValue)
    {
        PyTuple* tuple = new PyTuple(2);
            tuple->SetItem(0, oldValue);
            tuple->SetItem(1, newValue);
        change->SetItemString(key, tuple);
    }

    std::string AllianceName(uint32 allianceID)
    {
        return sDataMgr.GetOwnerName(allianceID);
    }
}

AllianceManager::AllianceManager()
: m_lastTallyDay(0)
{
}

int AllianceManager::Initialize()
{
    sLog.Blue("  AllianceManager", "Alliance Manager Initialized.  Joins take %u hours, executors are counted at %02u:00.",
              sConfig.alliance.JoinDelayHours, sConfig.alliance.ExecutorTallyHour);
    return 1;
}

void AllianceManager::Process()
{
    int64 now = GetFileTimeNow();
    ProcessPendingJoins(now);

    // the executor count, once a day at downtime (and once after a start past that hour)
    int64 day(now / EvE::Time::Day);
    int64 hour((now / EvE::Time::Hour) % 24);
    if ((day != m_lastTallyDay) and (hour >= (int64)sConfig.alliance.ExecutorTallyHour)) {
        m_lastTallyDay = day;
        TallyExecutors();
    }
}

/* ------------------------------------------------------------------------------------------------------------------
 *  membership
 * ------------------------------------------------------------------------------------------------------------------ */
void AllianceManager::ProcessPendingJoins(int64 now)
{
    std::map<uint32, std::pair<uint32, uint32>> due;
    AllianceDB::GetDueApplications(now, due);
    AllianceDB db;
    for (auto cur : due) {
        uint32 corpID(cur.second.first), allianceID(cur.second.second);
        Alliance::ApplicationInfo info = Alliance::ApplicationInfo();
        db.GetCurrentApplicationInfo(allianceID, corpID, info);

        // the corp joined another alliance meanwhile, or closed, or the alliance did:  the application lapses
        CorpData data = CorpData();
            data.corporationID = corpID;
        CorporationDB::GetCorpData(data);
        bool lapsed((data.allianceID != 0) or !IsPlayerCorp(corpID) or (CorporationDB::GetCorporationCEO(corpID) == 0));
        std::vector<uint32> members;
        AllianceDB::GetMemberCorps(allianceID, members);
        if (members.empty())
            lapsed = true;
        if (lapsed) {
            if (info.valid and (info.appID == cur.first)) {
                Alliance::ApplicationInfo after = info;
                after.state = EveAlliance::AppStatus::AppRejected;
                db.UpdateApplication(after);
                SendApplicationChanged(info, after);
            }
            AllianceDB::SetApplicationEffectiveAt(cur.first, 0);
            _log(ALLY__INFO, "AllianceManager::ProcessPendingJoins() - application %u of corp %u to alliance %u lapsed", cur.first, corpID, allianceID);
            continue;
        }
        // wars the corp declared can't be adopted:  it waits until they're over
        if (sWarMgr.HasDeclaredWars(corpID)) {
            if (m_heldApplications.insert(cur.first).second)
                _log(ALLY__WARNING, "AllianceManager::ProcessPendingJoins() - corp %u can't join alliance %u while it has declared wars in progress", corpID, allianceID);
            continue;
        }
        m_heldApplications.erase(cur.first);
        JoinAlliance(corpID, allianceID, false);
    }
}

void AllianceManager::JoinAlliance(uint32 corpID, uint32 allianceID, bool creator)
{
    AllianceDB db;
    if (!db.UpdateCorpAlliance(allianceID, corpID)) {
        _log(ALLY__ERROR, "AllianceManager::JoinAlliance() - corp %u could not join alliance %u", corpID, allianceID);
        return;
    }
    db.AddEmployment(allianceID, corpID);

    // the application is effective now
    if (!creator) {
        Alliance::ApplicationInfo info = Alliance::ApplicationInfo();
        if (db.GetCurrentApplicationInfo(allianceID, corpID, info) and (info.state != EveAlliance::AppStatus::AppEffective)) {
            Alliance::ApplicationInfo after = info;
            after.state = EveAlliance::AppStatus::AppEffective;
            db.UpdateApplication(after);
            AllianceDB::SetApplicationEffectiveAt(info.appID, 0);
            SendApplicationChanged(info, after);
        }
    }

    // wars against the corp become the alliance's
    sWarMgr.OnCorpJoinedAlliance(corpID, allianceID);
    UpdateMemberSessions(corpID, allianceID);

    PyDict* change = new PyDict();
        AddChange(change, "corporationID", new PyInt(corpID), new PyInt(corpID));
        AddChange(change, "allianceID", PyStatic.NewNone(), new PyInt(allianceID));
    sCorpMgr.BroadcastCorpChange(corpID, change);

    // the members' lists get the new corp, everyone's alliance row the new member count
    SendMemberChanged(allianceID, corpID, MemberAdded, 0, AllianceDB::GetChosenExecutor(corpID));
    SendAllianceChanged(allianceID);

    if (!creator) {
        uint32 ceoID(CorporationDB::GetCorporationCEO(corpID));
        uint32 senderID(CorporationDB::GetCorporationCEO(AllianceDB::GetExecutorID(allianceID)));
        if (IsCharacterID(ceoID)) {
            std::string body = "Your corporation is now a member of " + AllianceName(allianceID) + ".";
            MailMgr::SendMail(IsCharacterID(senderID) ? senderID : ceoID, ceoID, "Application to alliance accepted", body);
        }
    }

    _log(ALLY__INFO, "AllianceManager::JoinAlliance() - corp %u joined alliance %u%s", corpID, allianceID, (creator ? " (creator)" : ""));
}

void AllianceManager::LeaveAlliance(uint32 corpID, uint32 allianceID)
{
    if (!AllianceDB::IsMember(allianceID, corpID))
        return;

    bool wasExecutor(AllianceDB::GetExecutorID(allianceID) == corpID);
    std::map<uint32, uint32> support;
    AllianceDB::GetExecutorSupport(allianceID, support);

    AllianceDB db;
    db.DeleteMember(allianceID, corpID);
    if (wasExecutor)
        AllianceDB::SetExecutor(allianceID, 0);

    // gone from the members' lists (its own members' too), and those that supported it support nobody now
    SendMemberChanged(allianceID, corpID, MemberRemoved, support[corpID], 0, corpID);
    for (auto cur : support)
        if ((cur.first != corpID) and (cur.second == corpID))
            SendMemberChanged(allianceID, cur.first, MemberUpdated, corpID, 0);

    // "you will still be at war with the alliance but also temporarily at war with the corporation that left"
    sWarMgr.OnCorpLeftAlliance(corpID, allianceID);
    UpdateMemberSessions(corpID, 0);
    // sovereignty is held thru the members' claim units:  the systems this corp claimed for the alliance are lost
    svDataMgr.DropAllianceClaims(allianceID, corpID);

    PyDict* change = new PyDict();
        AddChange(change, "corporationID", new PyInt(corpID), new PyInt(corpID));
        AddChange(change, "allianceID", new PyInt(allianceID), PyStatic.NewNone());
    sCorpMgr.BroadcastCorpChange(corpID, change);

    std::vector<uint32> members;
    AllianceDB::GetMemberCorps(allianceID, members);
    if (members.empty()) {
        // nobody left:  the alliance closes, and its wars and bills with it
        AllianceDB::CloseAlliance(allianceID);
        sWarMgr.OnOwnerClosed(allianceID);
        sBillingMgr.CancelBillsOf(allianceID);
        // and whatever sovereignty it still claims
        svDataMgr.DropAllianceClaims(allianceID);
        SendAllianceChanged(allianceID, corpID);
        _log(ALLY__INFO, "AllianceManager::LeaveAlliance() - alliance %u closed, its last member %u left", allianceID, corpID);
    } else {
        SendAllianceChanged(allianceID);
    }

    _log(ALLY__INFO, "AllianceManager::LeaveAlliance() - corp %u left alliance %u%s", corpID, allianceID, (wasExecutor ? " (was executor)" : ""));
}

void AllianceManager::DisbandAlliance(uint32 allianceID)
{
    std::vector<uint32> members;
    AllianceDB::GetMemberCorps(allianceID, members);
    // the executor last, so the others don't see it in turmoil first
    uint32 executorID(AllianceDB::GetExecutorID(allianceID));
    for (auto cur : members)
        if (cur != executorID)
            LeaveAlliance(cur, allianceID);
    if (executorID != 0)
        LeaveAlliance(executorID, allianceID);
    if (members.empty()) {
        AllianceDB::CloseAlliance(allianceID);
        sWarMgr.OnOwnerClosed(allianceID);
        sBillingMgr.CancelBillsOf(allianceID);
        svDataMgr.DropAllianceClaims(allianceID);
    }
    _log(ALLY__INFO, "AllianceManager::DisbandAlliance() - alliance %u disbanded", allianceID);
}

void AllianceManager::UpdateMemberSessions(uint32 corpID, uint32 allianceID)
{
    std::vector<Client*> list;
    sEntityList.GetCorpClients(list, corpID);
    for (auto cur : list) {
        if ((cur == nullptr) or (cur->GetChar().get() == nullptr))
            continue;
        CorpData data = cur->GetChar()->GetCorpData();
        data.allianceID = allianceID;
        // saves, and the session's allianceid (cleared when 0) and the ship's slim item follow
        cur->GetChar()->UpdateCorpData(data);
    }
}

/* ------------------------------------------------------------------------------------------------------------------
 *  executor
 * ------------------------------------------------------------------------------------------------------------------ */
void AllianceManager::TallyExecutors()
{
    std::vector<uint32> alliances;
    AllianceDB::GetLiveAlliances(alliances);
    for (auto cur : alliances)
        TallyExecutor(cur);
}

void AllianceManager::TallyExecutor(uint32 allianceID)
{
    std::map<uint32, uint32> support;
    AllianceDB::GetExecutorSupport(allianceID, support);
    if (support.empty())
        return;

    std::map<uint32, uint32> votes;
    for (auto cur : support)
        if ((cur.second != 0) and (support.find(cur.second) != support.end()))
            ++votes[cur.second];

    uint32 current(AllianceDB::GetExecutorID(allianceID)), best(0), bestVotes(0);
    bool keep(false);
    for (auto cur : votes) {
        if (2 * cur.second < support.size())
            continue;       // less than half of the members
        if (cur.first == current)
            keep = true;
        if (cur.second > bestVotes) {       // ties: the lowest corpID (map order)
            best = cur.first;
            bestVotes = cur.second;
        }
    }
    if (keep or (best == 0) or (best == current))
        return;

    AllianceDB::SetExecutor(allianceID, best);
    SendAllianceChanged(allianceID);
    _log(ALLY__INFO, "AllianceManager::TallyExecutor() - alliance %u executor %u -> %u (%u of %u members)", allianceID, current, best,
         bestVotes, (uint32)support.size());
}

/* ------------------------------------------------------------------------------------------------------------------
 *  permissions
 * ------------------------------------------------------------------------------------------------------------------ */
bool AllianceManager::IsCEOOrDirector(Client* pClient)
{
    if (pClient == nullptr)
        return false;
    if ((pClient->GetCorpRole() & Corp::Role::Director) == Corp::Role::Director)
        return true;
    return sCorpMgr.IsActiveCEO(pClient->GetCorporationID(), pClient->GetCharacterID());
}

bool AllianceManager::IsExecutorDirector(Client* pClient, uint32 allianceID)
{
    if ((pClient == nullptr) or (allianceID == 0) or ((uint32)pClient->GetAllianceID() != allianceID))
        return false;
    if (AllianceDB::GetExecutorID(allianceID) != (uint32)pClient->GetCorporationID())
        return false;
    return IsCEOOrDirector(pClient);
}

void AllianceManager::RequireExecutorDirector(Client* pClient, uint32 allianceID)
{
    if (!IsExecutorDirector(pClient, allianceID))
        throw UserError("CrpAccessDenied").AddFormatValue("reason", new PyString("Only directors of the alliance's executor corporation can do that."));
}

void AllianceManager::RequireMember(Client* pClient, uint32 allianceID)
{
    if ((pClient == nullptr) or (allianceID == 0) or ((uint32)pClient->GetAllianceID() != allianceID)
    or !AllianceDB::IsMember(allianceID, pClient->GetCorporationID()))
        throw UserError("CrpAccessDenied").AddFormatValue("reason", new PyString("Your corporation is not a member of this alliance."));
}

/* ------------------------------------------------------------------------------------------------------------------
 *  notifications
 * ------------------------------------------------------------------------------------------------------------------ */
void AllianceManager::SendToAlliance(uint32 allianceID, const char* notifyType, PyTuple* payload, uint32 alsoCorpID/*0*/)
{
    std::vector<uint32> corps;
    AllianceDB::GetMemberCorps(allianceID, corps);
    if ((alsoCorpID != 0) and (std::find(corps.begin(), corps.end(), alsoCorpID) == corps.end()))
        corps.push_back(alsoCorpID);

    std::vector<Client*> list;
    for (auto cur : corps)
        sEntityList.GetCorpClients(list, cur);
    for (auto cur : list) {
        if (cur == nullptr)
            continue;
        PyTuple* copy = payload->Clone()->AsTuple();
        cur->SendNotification(notifyType, "clientID", &copy, false);
        _log(ALLY__TRACE, "%s sent to %s (%u)", notifyType, cur->GetName(), cur->GetCharID());
    }
    PyDecRef(payload);
}

void AllianceManager::SendAllianceChanged(uint32 allianceID, uint32 alsoCorpID/*0*/)
{
    // the whole row (old None, new everything):  all_cso_alliance replaces its cached row with it
    OnAllianceChanged ac;
    ac.allianceID = allianceID;
    AllianceDB db;
    if (!db.CreateAllianceChangePacket(ac, 0, allianceID)) {
        codelog(ALLY__ERROR, "Failed to create OnAllianceChanged notification for alliance %u.", allianceID);
        return;
    }
    SendToAlliance(allianceID, "OnAllianceChanged", ac.Encode(), alsoCorpID);
}

void AllianceManager::SendMemberChanged(uint32 allianceID, uint32 corpID, uint8 kind, uint32 oldChosen, uint32 newChosen, uint32 alsoCorpID/*0*/)
{
    // all_cso_members:  an add has all three columns with old None, a removal all new None, an update carries corporationID
    PyDict* change = new PyDict();
    switch (kind) {
        case MemberAdded: {
            AddChange(change, "corporationID", PyStatic.NewNone(), new PyInt(corpID));
            AddChange(change, "corporationName", PyStatic.NewNone(), new PyString(CorporationDB::GetCorpName(corpID)));
            AddChange(change, "chosenExecutorID", PyStatic.NewNone(), NullableInt(newChosen));
        } break;
        case MemberRemoved: {
            AddChange(change, "corporationID", new PyInt(corpID), PyStatic.NewNone());
            AddChange(change, "corporationName", new PyString(CorporationDB::GetCorpName(corpID)), PyStatic.NewNone());
            AddChange(change, "chosenExecutorID", NullableInt(oldChosen), PyStatic.NewNone());
        } break;
        default: {
            AddChange(change, "corporationID", new PyInt(corpID), new PyInt(corpID));
            AddChange(change, "chosenExecutorID", NullableInt(oldChosen), NullableInt(newChosen));
        } break;
    }
    PyTuple* payload = new PyTuple(3);
        payload->SetItem(0, new PyInt(allianceID));
        payload->SetItem(1, new PyInt(corpID));
        payload->SetItem(2, change);
    SendToAlliance(allianceID, "OnAllianceMemberChanged", payload, alsoCorpID);
}

void AllianceManager::SendApplicationChanged(const Alliance::ApplicationInfo& before, const Alliance::ApplicationInfo& after)
{
    OnAllianceApplicationChanged oaac;
        oaac.allianceID = after.allyID;
        oaac.corpID = after.corpID;
    AllianceBound::FillOAApplicationChange(oaac, before, after);

    // the applying corp and the executor corp (its directors decide)
    std::vector<Client*> list;
    sEntityList.GetCorpClients(list, after.corpID);
    uint32 executorID = AllianceDB::GetExecutorID(after.allyID);
    if ((executorID != 0) and (executorID != after.corpID))
        sEntityList.GetCorpClients(list, executorID);
    PyTuple* payload = oaac.Encode();
    for (auto cur : list) {
        if (cur == nullptr)
            continue;
        PyTuple* copy = payload->Clone()->AsTuple();
        cur->SendNotification("OnAllianceApplicationChanged", "clientID", &copy, false);
    }
    PyDecRef(payload);
}
