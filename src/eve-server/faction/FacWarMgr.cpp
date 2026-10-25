/**
 * @name FacWarMgr.cpp
 *   factional warfare's militias.  see FacWarMgr.h
 *
 * @date:          22 October 2026
 */

#include "eve-server.h"

#include "EVE_Corp.h"

#include "Client.h"
#include "EntityList.h"
#include "EVEServerConfig.h"
#include "StaticDataMgr.h"
#include "character/Character.h"
#include "character/CharacterDB.h"
#include "corporation/CorpMgr.h"
#include "corporation/CorporationDB.h"
#include "faction/FacWarMgr.h"
#include "mail/MailMgr.h"
#include "standing/StandingDB.h"

/*
 * FACWAR__ERROR
 * FACWAR__WARNING
 * FACWAR__INFO
 */

namespace {
    // facwarMinStandingsToJoin
    const float MinStanding = 0.5f;
    // a character enlists once in a day at most
    const int64 RejoinWait = EvE::Time::Day;
    // the writes' errors (logged by the database layer)
    DBerror s_err;

    std::string FactionName(uint32 factionID)                   { return sDataMgr.GetOwnerName(factionID); }

    // a mail to a player
    void Tell(uint32 charID, const std::string& title, const std::string& body)
    {
        if (!IsCharacterID(charID))
            return;
        MailMgr::SendMail(charID, charID, title, body);
    }

    void AddChange(PyDict* change, const char* key, PyRep* oldValue, PyRep* newValue)
    {
        PyTuple* tuple = new PyTuple(2);
            tuple->SetItem(0, oldValue);
            tuple->SetItem(1, newValue);
        change->SetItemString(key, tuple);
    }

    PyRep* NullableInt(uint32 value)                            { return ((value == 0) ? PyStatic.NewNone() : new PyInt(value)); }

    void SaveEnlistment(uint32 ownerID, uint32 factionID, uint8 status, int64 startDate, int64 effectiveAt)
    {
        DBerror err;
        if (!sDatabase.RunQuery(err, "REPLACE INTO facWarEnlistments (ownerID, factionID, status, startDate, effectiveAt, warnedAt)"
                                     " VALUES (%u, %u, %u, %lli, %lli, 0)", ownerID, factionID, status, startDate, effectiveAt))
            _log(DATABASE__ERROR, "FacWarMgr:  enlistment of %u not saved:  %s", ownerID, err.c_str());
    }
}

FacWarMgr::FacWarMgr()
: m_nextComplexID(1),
m_seeded(false),
m_taken(0),
m_killsPaid(0),
m_killPoints(0),
m_lastSave(0),
m_lastCheckDay(0)
{
}

int FacWarMgr::Initialize()
{
    DBQueryResult res;
    DBResultRow row;
    if (sDatabase.RunQuery(res, "SELECT corporationID, warFactionID FROM crpCorporation WHERE warFactionID IS NOT NULL AND warFactionID <> 0"))
        while (res.GetRow(row))
            m_corpFaction[row.GetUInt(0)] = row.GetUInt(1);
    // the militia corporations are their faction's, whatever the database says
    for (uint32 factionID : {(uint32)factionCaldari, (uint32)factionMinmatar, (uint32)factionAmarr, (uint32)factionGallente})
        m_corpFaction[MilitiaCorpOf(factionID)] = factionID;
    // the corporations' joins and leaves waiting for downtime
    if (sDatabase.RunQuery(res, "SELECT ownerID, factionID, status, startDate, effectiveAt FROM facWarEnlistments WHERE status <> %u", FacWar::CorpStatus::Active))
        while (res.GetRow(row)) {
            if (!IsPlayerCorp(row.GetUInt(0)))
                continue;
            Request r;
            r.factionID = row.GetUInt(1);
            r.status = (uint8)row.GetUInt(2);
            r.askedAt = row.GetInt64(3);
            r.effectiveAt = row.GetInt64(4);
            m_requests[row.GetUInt(0)] = r;
        }
    // the war zones, their complexes and who holds what (FacWarSystems.cpp)
    LoadSystems();
    uint32 contested(0);
    for (auto& cur : m_systems)
        if (cur.second.points > 0)
            ++contested;
    sLog.Blue("        FacWarMgr", "Militias Initialized:  %u corporations in militias, %u joins or leaves waiting for downtime, %u war zone systems (%u contested).",
              (uint32)m_corpFaction.size(), (uint32)m_requests.size(), (uint32)m_zone.size(), contested);
    return 1;
}

void FacWarMgr::Process()
{
    int64 now(GetFileTimeNow());
    // the corporations' joins and leaves at downtime
    std::vector<uint32> due;
    for (auto& cur : m_requests)
        if (cur.second.effectiveAt <= now)
            due.push_back(cur.first);
    for (uint32 corpID : due) {
        Request r(m_requests[corpID]);
        m_requests.erase(corpID);
        if (r.status == FacWar::CorpStatus::Joining) {
            // it might have joined an alliance since, or closed
            CorpData data = CorpData();
                data.corporationID = corpID;
            CorporationDB::GetCorpData(data);
            uint32 ceoID(CorporationDB::GetCorporationCEO(corpID));
            if ((data.allianceID != 0) or (ceoID == 0)) {
                sDatabase.RunQuery(s_err, "DELETE FROM facWarEnlistments WHERE ownerID = %u", corpID);
                _log(FACWAR__INFO, "FacWarMgr:  corp %u's enlistment in the %s militia lapsed", corpID, FactionName(r.factionID).c_str());
                continue;
            }
            SaveEnlistment(corpID, r.factionID, FacWar::CorpStatus::Active, now, 0);
            SetCorpMilitia(corpID, r.factionID);
            Tell(ceoID, "Enlisted in the " + FactionName(r.factionID) + " militia", "Your corporation is now a member of the "
                 + FactionName(r.factionID) + " militia.  Its members may fight the militias it is at war with anywhere, without CONCORD.");
        } else {
            sDatabase.RunQuery(s_err, "DELETE FROM facWarEnlistments WHERE ownerID = %u", corpID);
            SetCorpMilitia(corpID, 0);
            Tell(CorporationDB::GetCorporationCEO(corpID), "Left the " + FactionName(r.factionID) + " militia",
                 "Your corporation is no longer a member of the " + FactionName(r.factionID) + " militia.");
        }
    }
    // once a day at downtime (and once after a start past that hour):  the members' standings
    int64 day(now / EvE::Time::Day);
    int64 hour((now / EvE::Time::Hour) % 24);
    if ((day != m_lastCheckDay) and (hour >= (int64)sConfig.alliance.ExecutorTallyHour)) {
        m_lastCheckDay = day;
        CheckStandings(now);
    }

    /* the war zones (FacWarSystems.cpp):  their first complexes, systems whose bunker fell before the last downtime change
     *  hands, taken complexes are replaced, and what changed is saved */
    if (!m_seeded) {
        m_seeded = true;
        for (auto& cur : m_systems)
            for (uint8 i = 0; i < ComplexesPerSystem; ++i)
                AddComplex(cur.first);
        _log(FACWAR__INFO, "FacWarMgr:  %u complexes in %u war zone systems", (uint32)m_complexes.size(), (uint32)m_systems.size());
    }
    bool flip(false);
    for (auto& cur : m_systems)
        if ((cur.second.capturedBy != 0) and (NextDowntime(cur.second.capturedAt) <= now))
            flip = true;
    if (flip)
        FlipConquered();
    while (!m_newComplexes.empty() and (m_newComplexes.begin()->first <= now)) {
        m_newComplexes.erase(m_newComplexes.begin());
        // a random war zone system with fewer than three
        std::vector<uint32> room;
        std::unordered_map<uint32, uint8> count;
        for (auto& cur : m_complexes)
            ++count[cur.second.systemID];
        for (auto& cur : m_systems)
            if (count[cur.first] < 3)
                room.push_back(cur.first);
        if (!room.empty())
            AddComplex(room[MakeRandomInt(0, (int)room.size() - 1)]);
    }
    if ((now - m_lastSave) >= EvE::Time::Minute) {
        m_lastSave = now;
        for (auto& cur : m_systems)
            if (cur.second.dirty)
                SaveSystem(cur.first);
    }
}

/* ------------------------------------------------------------------------------------------------------------------
 *  the militias
 * ------------------------------------------------------------------------------------------------------------------ */
bool FacWarMgr::IsWarFaction(uint32 factionID)
{
    return (MilitiaCorpOf(factionID) != 0);
}

uint32 FacWarMgr::MilitiaCorpOf(uint32 factionID)
{
    // facFactions.militiaCorporationID
    switch (factionID) {
        case factionAmarr:      return 1000179;     // 24th Imperial Crusade
        case factionCaldari:    return 1000180;     // State Protectorate
        case factionGallente:   return 1000181;     // Federal Defense Union
        case factionMinmatar:   return 1000182;     // Tribal Liberation Force
        default:                return 0;
    }
}

uint32 FacWarMgr::FactionOfMilitiaCorp(uint32 corpID)
{
    switch (corpID) {
        case 1000179:   return factionAmarr;
        case 1000180:   return factionCaldari;
        case 1000181:   return factionGallente;
        case 1000182:   return factionMinmatar;
        default:        return 0;
    }
}

bool FacWarMgr::AreEnemies(uint32 factionA, uint32 factionB)
{
    auto side = [](uint32 factionID) -> int {
        if ((factionID == factionAmarr) or (factionID == factionCaldari))
            return 1;
        if ((factionID == factionGallente) or (factionID == factionMinmatar))
            return 2;
        return 0;
    };
    int a(side(factionA)), b(side(factionB));
    return ((a != 0) and (b != 0) and (a != b));
}

uint32 FacWarMgr::ZoneEnemyOf(uint32 factionID)
{
    switch (factionID) {
        case factionAmarr:      return factionMinmatar;
        case factionMinmatar:   return factionAmarr;
        case factionCaldari:    return factionGallente;
        case factionGallente:   return factionCaldari;
        default:                return 0;
    }
}

uint32 FacWarMgr::WarFactionOf(uint32 corpID) const
{
    std::unordered_map<uint32, uint32>::const_iterator itr = m_corpFaction.find(corpID);
    return ((itr == m_corpFaction.end()) ? 0 : itr->second);
}

bool FacWarMgr::EnemyCorps(uint32 corpA, uint32 corpB) const
{
    if (corpA == corpB)
        return false;
    return AreEnemies(WarFactionOf(corpA), WarFactionOf(corpB));
}

bool FacWarMgr::SameMilitia(uint32 corpA, uint32 corpB) const
{
    uint32 a(WarFactionOf(corpA));
    return ((a != 0) and (a == WarFactionOf(corpB)));
}

void FacWarMgr::CorpsOf(uint32 factionID, std::vector<uint32>& into) const
{
    uint32 militia(MilitiaCorpOf(factionID));
    if (militia == 0)
        return;
    into.push_back(militia);
    for (auto& cur : m_corpFaction)
        if ((cur.second == factionID) and (cur.first != militia))
            into.push_back(cur.first);
}

bool FacWarMgr::InWarZone(uint32 systemID, uint32 factionID) const
{
    std::unordered_map<uint32, uint32>::const_iterator itr = m_zone.find(systemID);
    if ((itr == m_zone.end()) or (factionID == 0))
        return false;
    return ((itr->second == factionID) or (itr->second == ZoneEnemyOf(factionID)));
}

/* ------------------------------------------------------------------------------------------------------------------
 *  enlisting
 * ------------------------------------------------------------------------------------------------------------------ */
float FacWarMgr::StandingOf(uint32 charID, uint32 factionID)
{
    return StandingDB::GetStanding(factionID, charID);
}

float FacWarMgr::CorpStandingOf(uint32 corpID, uint32 factionID)
{
    DBQueryResult res;
    DBResultRow row;
    if (!sDatabase.RunQuery(res, "SELECT AVG(s.standing) FROM repStandings AS s JOIN chrCharacters AS c ON c.characterID = s.toID"
                                 " WHERE s.fromID = %u AND c.corporationID = %u AND s.standing <> 0", factionID, corpID)
        or !res.GetRow(row) or row.IsNull(0))
        return 0.0f;
    return (float)row.GetDouble(0);
}

bool FacWarMgr::CanEnlist(uint32 charID, uint32 factionID, bool letter, std::string& err) const
{
    if (!IsWarFaction(factionID)) {
        err = "That faction has no militia.";
        return false;
    }
    uint32 corpID(CharacterDB::GetCorpID(charID));
    if (WarFactionOf(corpID) != 0) {
        err = "You are already in a militia.";
        return false;
    }
    if (IsPlayerCorp(corpID)) {
        err = "Your corporation's CEO or a director enlists the corporation.  You can enlist on your own only while you are in an NPC corporation.";
        return false;
    }
    float standing(StandingOf(charID, factionID));
    if (!letter and (standing < MinStanding)) {
        char buf[200];
        snprintf(buf, sizeof(buf), "The %s militia takes those whose standing to it is %.1f or more.  Yours is %.2f.", FactionName(factionID).c_str(),
                 MinStanding, standing);
        err = buf;
        return false;
    }
    DBQueryResult res;
    DBResultRow row;
    if (sDatabase.RunQuery(res, "SELECT startDate FROM facWarEnlistments WHERE ownerID = %u", charID) and res.GetRow(row)
        and ((GetFileTimeNow() - row.GetInt64(0)) < RejoinWait)) {
        err = "You enlisted in a militia less than 24 hours ago.";
        return false;
    }
    return true;
}

bool FacWarMgr::Enlist(uint32 charID, uint32 factionID, bool letter, std::string& err)
{
    if (!CanEnlist(charID, factionID, letter, err))
        return false;
    uint32 militia(MilitiaCorpOf(factionID));
    SaveEnlistment(charID, factionID, FacWar::CorpStatus::Active, GetFileTimeNow(), 0);
    // employment history, session (warfactionid), and the ship's slim item
    sCorpMgr.MoveMemberToCorp(charID, militia);
    _log(FACWAR__INFO, "FacWarMgr:  %s(%u) enlisted in the %s militia%s", sDataMgr.GetOwnerName(charID).c_str(), charID, FactionName(factionID).c_str(),
         (letter ? " with a letter of recommendation" : ""));
    return true;
}

bool FacWarMgr::CanEnlistCorp(uint32 corpID, uint32 factionID, std::string& err) const
{
    if (!IsWarFaction(factionID)) {
        err = "That faction has no militia.";
        return false;
    }
    if (!IsPlayerCorp(corpID)) {
        err = "Only a player corporation can enlist.";
        return false;
    }
    if (WarFactionOf(corpID) != 0) {
        err = "Your corporation is already in a militia.";
        return false;
    }
    if (m_requests.find(corpID) != m_requests.end()) {
        err = "Your corporation has already asked to join a militia.";
        return false;
    }
    CorpData data = CorpData();
        data.corporationID = corpID;
    CorporationDB::GetCorpData(data);
    if (data.allianceID != 0) {
        err = "A corporation in an alliance can't enlist.";
        return false;
    }
    float standing(CorpStandingOf(corpID, factionID));
    if (standing < MinStanding) {
        char buf[200];
        snprintf(buf, sizeof(buf), "The %s militia takes corporations whose standing to it is %.1f or more.  Your corporation's is %.2f.",
                 FactionName(factionID).c_str(), MinStanding, standing);
        err = buf;
        return false;
    }
    return true;
}

bool FacWarMgr::AskToJoin(uint32 corpID, uint32 factionID, uint32 byCharID, std::string& err)
{
    if (!CanEnlistCorp(corpID, factionID, err))
        return false;
    int64 now(GetFileTimeNow());
    Request r;
    r.factionID = factionID;
    r.status = FacWar::CorpStatus::Joining;
    r.askedAt = now;
    r.effectiveAt = NextDowntime(now);
    m_requests[corpID] = r;
    SaveEnlistment(corpID, factionID, r.status, r.askedAt, r.effectiveAt);
    _log(FACWAR__INFO, "FacWarMgr:  corp %u asks to join the %s militia (by %u), at the next downtime", corpID, FactionName(factionID).c_str(), byCharID);
    return true;
}

bool FacWarMgr::AskToLeave(uint32 corpID, uint32 factionID, uint32 byCharID, std::string& err)
{
    if (!IsPlayerCorp(corpID) or (WarFactionOf(corpID) == 0) or ((factionID != 0) and (WarFactionOf(corpID) != factionID))) {
        err = "Your corporation isn't in that militia.";
        return false;
    }
    if (m_requests.find(corpID) != m_requests.end()) {
        err = "Your corporation has already asked to leave its militia.";
        return false;
    }
    int64 now(GetFileTimeNow());
    Request r;
    r.factionID = WarFactionOf(corpID);
    r.status = FacWar::CorpStatus::Leaving;
    r.askedAt = now;
    r.effectiveAt = NextDowntime(now);
    m_requests[corpID] = r;
    SaveEnlistment(corpID, r.factionID, r.status, r.askedAt, r.effectiveAt);
    _log(FACWAR__INFO, "FacWarMgr:  corp %u asks to leave the %s militia (by %u), at the next downtime", corpID, FactionName(r.factionID).c_str(), byCharID);
    return true;
}

bool FacWarMgr::Withdraw(uint32 corpID, bool join, std::string& err)
{
    std::map<uint32, Request>::iterator itr = m_requests.find(corpID);
    if ((itr == m_requests.end()) or (itr->second.status != (join ? FacWar::CorpStatus::Joining : FacWar::CorpStatus::Leaving))) {
        err = (join ? "Your corporation hasn't asked to join a militia." : "Your corporation hasn't asked to leave its militia.");
        return false;
    }
    uint32 factionID(itr->second.factionID);
    m_requests.erase(itr);
    if (join) {
        sDatabase.RunQuery(s_err, "DELETE FROM facWarEnlistments WHERE ownerID = %u", corpID);
    } else {
        // it stays, as it enlisted
        sDatabase.RunQuery(s_err, "UPDATE facWarEnlistments SET status = %u, effectiveAt = 0 WHERE ownerID = %u", FacWar::CorpStatus::Active, corpID);
    }
    _log(FACWAR__INFO, "FacWarMgr:  corp %u withdrew its %s the %s militia", corpID, (join ? "joining" : "leaving"), FactionName(factionID).c_str());
    return true;
}

bool FacWarMgr::StatusOf(uint32 charID, uint32 corpID, uint32& factionID, uint8& status, int64& since) const
{
    factionID = 0;
    status = FacWar::CorpStatus::Active;
    since = 0;
    std::map<uint32, Request>::const_iterator req = m_requests.find(corpID);
    if (req != m_requests.end()) {
        factionID = req->second.factionID;
        status = req->second.status;
        since = req->second.effectiveAt;
        return true;
    }
    factionID = WarFactionOf(corpID);
    if (factionID == 0)
        return false;
    // when it enlisted:  the character in a militia corporation, else its corporation
    DBQueryResult res;
    DBResultRow row;
    if (sDatabase.RunQuery(res, "SELECT startDate FROM facWarEnlistments WHERE ownerID = %u", (IsMilitiaCorp(corpID) ? charID : corpID)) and res.GetRow(row))
        since = row.GetInt64(0);
    return true;
}

int64 FacWarMgr::NextDowntime(int64 now)
{
    int64 at(now - (now % EvE::Time::Day) + (int64)sConfig.alliance.ExecutorTallyHour * EvE::Time::Hour);
    if (at <= now)
        at += EvE::Time::Day;
    return at;
}

/* ------------------------------------------------------------------------------------------------------------------
 *  membership changes
 * ------------------------------------------------------------------------------------------------------------------ */
void FacWarMgr::SetCorpMilitia(uint32 corpID, uint32 factionID)
{
    uint32 before(WarFactionOf(corpID));
    if (!sDatabase.RunQuery(s_err, "UPDATE crpCorporation SET warFactionID = %u WHERE corporationID = %u", factionID, corpID)) {
        _log(FACWAR__ERROR, "FacWarMgr:  corp %u's militia not saved", corpID);
        return;
    }
    if (factionID == 0)
        m_corpFaction.erase(corpID);
    else
        m_corpFaction[corpID] = factionID;
    // the online members' sessions (warfactionid) and their ships' slim items
    std::vector<Client*> list;
    sEntityList.GetCorpClients(list, corpID);
    for (auto cur : list) {
        if ((cur == nullptr) or (cur->GetChar().get() == nullptr))
            continue;
        CorpData data = cur->GetChar()->GetCorpData();
        data.warFactionID = factionID;
        cur->GetChar()->UpdateCorpData(data);
    }
    PyDict* change = new PyDict();
        AddChange(change, "corporationID", new PyInt(corpID), new PyInt(corpID));
        AddChange(change, "warFactionID", NullableInt(before), NullableInt(factionID));
    sCorpMgr.BroadcastCorpChange(corpID, change);
    _log(FACWAR__INFO, "FacWarMgr:  corp %u %s the %s militia", corpID, ((factionID != 0) ? "joined" : "left"),
         FactionName((factionID != 0) ? factionID : before).c_str());
}

void FacWarMgr::CheckStandings(int64 now)
{
    uint32 warned(0), out(0);
    // those who enlisted on their own:  their militia corporation's members
    for (uint32 factionID : {(uint32)factionCaldari, (uint32)factionMinmatar, (uint32)factionAmarr, (uint32)factionGallente}) {
        DBQueryResult res;
        DBResultRow row;
        if (!sDatabase.RunQuery(res, "SELECT c.characterID, IFNULL(s.standing, 0), IFNULL(e.warnedAt, 0) FROM chrCharacters AS c"
                                     " LEFT JOIN repStandings AS s ON s.fromID = %u AND s.toID = c.characterID"
                                     " LEFT JOIN facWarEnlistments AS e ON e.ownerID = c.characterID"
                                     " WHERE c.corporationID = %u", factionID, MilitiaCorpOf(factionID)))
            continue;
        while (res.GetRow(row)) {
            uint32 charID(row.GetUInt(0));
            float standing((float)row.GetDouble(1));
            int64 warnedAt(row.GetInt64(2));
            if (standing >= MinStanding) {
                if (warnedAt != 0)
                    sDatabase.RunQuery(s_err, "UPDATE facWarEnlistments SET warnedAt = 0 WHERE ownerID = %u", charID);
                continue;
            }
            if (warnedAt == 0) {
                sDatabase.RunQuery(s_err, "INSERT INTO facWarEnlistments (ownerID, factionID, status, startDate, effectiveAt, warnedAt) VALUES (%u, %u, %u, 0, 0, %lli)"
                                   " ON DUPLICATE KEY UPDATE warnedAt = %lli", charID, factionID, FacWar::CorpStatus::Active, now, now);
                Tell(charID, "Your standing with the " + FactionName(factionID), "Your standing to the " + FactionName(factionID)
                     + " has fallen below what its militia takes.  Unless it is back up by the next downtime, you will be removed from the militia.");
                ++warned;
                continue;
            }
            // a day after the warning
            if ((now - warnedAt) < (EvE::Time::Day - EvE::Time::Hour))
                continue;
            sDatabase.RunQuery(s_err, "UPDATE facWarEnlistments SET warnedAt = 0 WHERE ownerID = %u", charID);
            sCorpMgr.LeaveCorp(charID, false, charID);
            Tell(charID, "Removed from the " + FactionName(factionID) + " militia", "Your standing to the " + FactionName(factionID)
                 + " stayed below what its militia takes, and you have been removed from it.");
            ++out;
        }
    }
    // the corporations that enlisted
    std::vector<std::pair<uint32, uint32>> corps;
    for (auto& cur : m_corpFaction)
        if (IsPlayerCorp(cur.first) and (m_requests.find(cur.first) == m_requests.end()))
            corps.push_back(cur);
    for (auto& cur : corps) {
        uint32 corpID(cur.first), factionID(cur.second);
        DBQueryResult res;
        DBResultRow row;
        int64 warnedAt(0);
        if (sDatabase.RunQuery(res, "SELECT warnedAt FROM facWarEnlistments WHERE ownerID = %u", corpID) and res.GetRow(row))
            warnedAt = row.GetInt64(0);
        uint32 ceoID(CorporationDB::GetCorporationCEO(corpID));
        if (CorpStandingOf(corpID, factionID) >= MinStanding) {
            if (warnedAt != 0)
                sDatabase.RunQuery(s_err, "UPDATE facWarEnlistments SET warnedAt = 0 WHERE ownerID = %u", corpID);
            continue;
        }
        if (warnedAt == 0) {
            sDatabase.RunQuery(s_err, "INSERT INTO facWarEnlistments (ownerID, factionID, status, startDate, effectiveAt, warnedAt) VALUES (%u, %u, %u, 0, 0, %lli)"
                               " ON DUPLICATE KEY UPDATE warnedAt = %lli", corpID, factionID, FacWar::CorpStatus::Active, now, now);
            Tell(ceoID, "Your corporation's standing with the " + FactionName(factionID), "Your corporation's standing to the " + FactionName(factionID)
                 + " has fallen below what its militia takes.  Unless it is back up by the next downtime, your corporation will be removed from the militia.");
            ++warned;
            continue;
        }
        if ((now - warnedAt) < (EvE::Time::Day - EvE::Time::Hour))
            continue;
        sDatabase.RunQuery(s_err, "DELETE FROM facWarEnlistments WHERE ownerID = %u", corpID);
        SetCorpMilitia(corpID, 0);
        Tell(ceoID, "Removed from the " + FactionName(factionID) + " militia", "Your corporation's standing to the " + FactionName(factionID)
             + " stayed below what its militia takes, and it has been removed from the militia.");
        ++out;
    }
    if ((warned + out) > 0)
        _log(FACWAR__INFO, "FacWarMgr:  standing check:  %u warned, %u removed from their militia", warned, out);
}
