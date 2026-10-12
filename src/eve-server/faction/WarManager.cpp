/**
 * @name WarManager.cpp
 *   wars between player corporations and alliances.  see WarManager.h for the rules.
 *
 * sources:  the client (war_cso_wars, corp_ui_wars, eveCfg.IsWarInHostileState, consider.CanAttackFreely, the war
 *  notifications and message texts), EVE University's DecShield archive (cost formula) and pre-Inferno forum posts
 *  (2M / 50M isk bases).
 */

#include "eve-server.h"

#include "EVE_Corp.h"
#include "EVE_Mail.h"

#include "Client.h"
#include "EntityList.h"
#include "EVEServerConfig.h"
#include "StaticDataMgr.h"
#include "account/AccountService.h"
#include "alliance/AllianceDB.h"
#include "corporation/BillingMgr.h"
#include "corporation/CorpMgr.h"
#include "corporation/CorporationDB.h"
#include "faction/WarDB.h"
#include "faction/WarManager.h"
#include "mail/NotificationMgr.h"

/*
 * WAR__ERROR
 * WAR__WARNING
 * WAR__INFO
 * WAR__TRACE
 */

namespace {
    PyRep* NullableLong(int64 value)            { return (value == 0 ? PyStatic.NewNone() : new PyLong(value)); }
    PyRep* NullableInt(uint32 value)            { return (value == 0 ? PyStatic.NewNone() : new PyInt(value)); }

    // {column: (old, new)}
    void AddChange(PyDict* change, const char* column, PyRep* oldValue, PyRep* newValue)
    {
        PyTuple* pair = new PyTuple(2);
            pair->SetItem(0, oldValue);
            pair->SetItem(1, newValue);
        change->SetItemString(column, pair);
    }

    std::string OwnerName(uint32 ownerID)
    {
        return sDataMgr.GetOwnerName(ownerID);
    }
}

WarManager::WarManager()
: m_lastCleanup(0)
{
}

int WarManager::Initialize()
{
    WarDB::CreateTable();
    WarDB::LoadCorpAlliances(m_corpAlliance);

    std::vector<War::Data> wars;
    int64 since = GetFileTimeNow() - (int64)sConfig.war.HistoryDays * EvE::Time::Day;
    WarDB::LoadWars(since, wars);
    for (auto cur : wars) {
        m_wars[cur.warID] = cur;
        Index(cur);
    }

    // the bills of a war are paid or go overdue through the bill system
    sBillingMgr.RegisterHandlers(Corp::BillType::WarBill,
        [](const BillData& bill) { sWarMgr.OnWarBillPaid(bill); },
        [](const BillData& bill) { sWarMgr.OnWarBillOverdue(bill); });

    sLog.Blue("       WarManager", "War Manager %s.  %u wars loaded.", (sConfig.war.Enabled ? "enabled" : "disabled"), (uint32)m_wars.size());
    return 1;
}

/* ------------------------------------------------------------------------------------------------------------------
 *  owners
 * ------------------------------------------------------------------------------------------------------------------ */
uint32 WarManager::WarOwnerOf(uint32 corpID)
{
    if (!IsPlayerCorp(corpID))
        return 0;
    std::map<uint32, uint32>::const_iterator itr = m_corpAlliance.find(corpID);
    if (itr != m_corpAlliance.end())
        return itr->second;
    return corpID;
}

void WarManager::SetCorpAlliance(uint32 corpID, uint32 allianceID)
{
    if (allianceID == 0) {
        m_corpAlliance.erase(corpID);
    } else {
        m_corpAlliance[corpID] = allianceID;
    }
}

void WarManager::GetAllianceCorps(uint32 allianceID, std::vector<uint32>& into)
{
    for (auto cur : m_corpAlliance)
        if (cur.second == allianceID)
            into.push_back(cur.first);
}

/* ------------------------------------------------------------------------------------------------------------------
 *  queries
 * ------------------------------------------------------------------------------------------------------------------ */
bool WarManager::Hostile(const War::Data& w, int64 now) const
{
    // util.IsWarInHostileState:  (not retracted or now - retracted < 24h) and now - timeDeclared >= 24h
    if ((w.retracted != 0) and (now - w.retracted >= War::Grace))
        return false;
    return (now - w.timeDeclared >= War::SpoolUp);
}

bool WarManager::IsHostile(uint32 ownerA, uint32 ownerB)
{
    if ((ownerA == 0) or (ownerB == 0) or (ownerA == ownerB))
        return false;
    int64 now = GetFileTimeNow();
    auto range = m_byOwner.equal_range(ownerA);
    for (auto itr = range.first; itr != range.second; ++itr) {
        std::map<uint32, War::Data>::const_iterator wItr = m_wars.find(itr->second);
        if (wItr == m_wars.end())
            continue;
        const War::Data& w = wItr->second;
        if (((w.declaredByID == ownerB) or (w.againstID == ownerB)) and Hostile(w, now))
            return true;
    }
    return false;
}

bool WarManager::AreAtWar(uint32 ownerA, uint32 ownerB)
{
    if ((ownerA == 0) or (ownerB == 0))
        return false;
    auto range = m_byOwner.equal_range(ownerA);
    for (auto itr = range.first; itr != range.second; ++itr) {
        std::map<uint32, War::Data>::const_iterator wItr = m_wars.find(itr->second);
        if (wItr == m_wars.end())
            continue;
        const War::Data& w = wItr->second;
        if (((w.declaredByID == ownerB) or (w.againstID == ownerB)) and Active(w))
            return true;
    }
    return false;
}

bool WarManager::HasDeclaredWars(uint32 ownerID)
{
    auto range = m_byOwner.equal_range(ownerID);
    for (auto itr = range.first; itr != range.second; ++itr) {
        std::map<uint32, War::Data>::const_iterator wItr = m_wars.find(itr->second);
        if ((wItr != m_wars.end()) and (wItr->second.declaredByID == ownerID) and Active(wItr->second))
            return true;
    }
    return false;
}

bool WarManager::IsInCharge(uint32 ownerID, Client* pClient)
{
    // corp_ui_wars.IsInCharge:  the executor corp's directors for an alliance, the active CEO for a corporation
    if (pClient == nullptr)
        return false;
    uint32 corpID(pClient->GetCorporationID()), charID(pClient->GetCharacterID());
    if (IsAlliance(ownerID)) {
        if ((WarOwnerOf(corpID) != ownerID) or (AllianceDB::GetExecutorID(ownerID) != corpID))
            return false;
        return (((pClient->GetCorpRole() & Corp::Role::Director) == Corp::Role::Director) or sCorpMgr.IsActiveCEO(corpID, charID));
    }
    return ((corpID == ownerID) and sCorpMgr.IsActiveCEO(corpID, charID));
}

double WarManager::GetCostOfWar(uint32 declarerID, uint32 againstID, uint32 excludeWarID/*0*/)
{
    // "War declaration price depends upon the number of non-mutual wars declared against the victim or non-mutual wars
    //  declared by you"  (WarDeclareConfirm):  base x (N + 1) x (W + 1)
    double base(sConfig.war.CorpWarCost);
    if (IsAlliance(declarerID) or IsAlliance(againstID))
        base = sConfig.war.AllianceWarCost;

    uint32 declared(0), against(0);
    auto range = m_byOwner.equal_range(declarerID);
    for (auto itr = range.first; itr != range.second; ++itr) {
        std::map<uint32, War::Data>::const_iterator wItr = m_wars.find(itr->second);
        if (wItr == m_wars.end())
            continue;
        const War::Data& w = wItr->second;
        if ((w.warID != excludeWarID) and (w.declaredByID == declarerID) and Active(w) and !w.mutual)
            ++declared;
    }
    range = m_byOwner.equal_range(againstID);
    for (auto itr = range.first; itr != range.second; ++itr) {
        std::map<uint32, War::Data>::const_iterator wItr = m_wars.find(itr->second);
        if (wItr == m_wars.end())
            continue;
        const War::Data& w = wItr->second;
        if ((w.warID != excludeWarID) and (w.againstID == againstID) and Active(w) and !w.mutual)
            ++against;
    }
    return base * (declared + 1) * (against + 1);
}

PyRep* WarManager::GetWars(uint32 ownerID)
{
    int64 since = GetFileTimeNow() - (int64)sConfig.war.HistoryDays * EvE::Time::Day;
    return WarDB::GetWarsRowset(ownerID, since);
}

/* ------------------------------------------------------------------------------------------------------------------
 *  declarations and changes
 * ------------------------------------------------------------------------------------------------------------------ */
void WarManager::ValidateDeclaration(uint32 declarerID, uint32 againstID)
{
    if (!sConfig.war.Enabled)
        throw CustomError("Wars are disabled on this server.");
    if ((!IsPlayerCorp(declarerID) and !IsAlliance(declarerID)) or !WarDB::IsOwnerAlive(declarerID))
        throw UserError("CrpCanNotSanctionWarOwner");
    if ((againstID == declarerID) or (WarOwnerOf(againstID) == declarerID))
        throw UserError("CanNotDeclareWarAgainstSelf");
    if (!IsPlayerCorp(againstID) and !IsAlliance(againstID))
        throw UserError("CrpCantDeclareWarOnNPCCorp");
    if (!WarDB::IsOwnerAlive(againstID))
        throw UserError("CanNotDeclareWarAgainstThem");

    // a corporation in an alliance goes to war through the alliance, and wars on a corporation in one are the alliance's
    bool memberTarget(IsPlayerCorp(againstID) and (WarOwnerOf(againstID) != againstID));
    if (IsPlayerCorp(declarerID)) {
        if (WarOwnerOf(declarerID) != declarerID)
            throw UserError("CrpCanNotSanctionWarOwner");
        if (memberTarget)
            throw UserError("CrpCanNotSanctionWarAgainstOwner");
    } else if (memberTarget) {
        throw UserError("CanNotDeclareWarAgainstThem");
    }

    if (AreAtWar(declarerID, againstID))
        throw UserError("AlreadyAtWarWithThem");

    if (sConfig.war.MaxDeclaredWars > 0) {
        uint32 count(0);
        auto range = m_byOwner.equal_range(declarerID);
        for (auto itr = range.first; itr != range.second; ++itr) {
            std::map<uint32, War::Data>::const_iterator wItr = m_wars.find(itr->second);
            if ((wItr != m_wars.end()) and (wItr->second.declaredByID == declarerID) and Active(wItr->second))
                ++count;
        }
        if (count >= sConfig.war.MaxDeclaredWars)
            throw UserError("CrpTooManyWarDeclarations");
    }
}

uint32 WarManager::DeclareWar(uint32 declarerID, uint32 againstID, uint32 charID, bool charge/*true*/)
{
    ValidateDeclaration(declarerID, againstID);

    double cost = GetCostOfWar(declarerID, againstID);
    if (charge) {
        uint32 payerID = PayerCorp(declarerID);
        if (payerID == 0)
            throw CustomError("Your alliance has no executor to pay for the war.");
        std::string reason = "War declaration against ";
        reason += OwnerName(againstID);
        // throws NotEnoughMoneyCorp when the corp can't pay
        AccountService::TransferFunds(payerID, corpCONCORD, cost, reason, Journal::EntryType::WarFee, againstID,
                                      Account::KeyType::Cash, Account::KeyType::Cash, sEntityList.FindClientByCharID(charID));
    }

    int64 now = GetFileTimeNow();
    War::Data w = War::Data();
        w.declaredByID = declarerID;
        w.againstID = againstID;
        w.timeDeclared = now;
        w.nextBillTime = now + (int64)sConfig.war.BillDays * EvE::Time::Day;
        w.weeklyCost = cost;
    w.warID = WarDB::InsertWar(w);
    if (w.warID == 0)
        throw CustomError("The war could not be declared.  Please try again later.");

    m_wars[w.warID] = w;
    Index(w);
    SendWarChanged(nullptr, &w);

    // the declarer is told the cost ("the weekly cost is..."), the target isn't
    PyDict* data = new PyDict();
        data->SetItemString("declaredByID", new PyInt(declarerID));
        data->SetItemString("againstID", new PyInt(againstID));
        data->SetItemString("cost", new PyFloat(cost));
        data->SetItemString("delayHours", new PyInt((int32)(War::SpoolUp / EvE::Time::Hour)));
        data->SetItemString("hostileState", new PyBool(false));
    Notify(declarerID, Notify::Types::CorpWarDeclared, Notify::Types::AllWarDeclared, data);
    data = new PyDict();
        data->SetItemString("declaredByID", new PyInt(declarerID));
        data->SetItemString("againstID", new PyInt(againstID));
        data->SetItemString("cost", new PyInt(0));
        data->SetItemString("delayHours", new PyInt((int32)(War::SpoolUp / EvE::Time::Hour)));
        data->SetItemString("hostileState", new PyBool(false));
    Notify(againstID, Notify::Types::CorpWarDeclared, Notify::Types::AllWarDeclared, data);

    _log(WAR__INFO, "WarManager::DeclareWar() - %s(%u) declared war %u on %s(%u) (by %u, cost %.2f%s)", OwnerName(declarerID).c_str(),
         declarerID, w.warID, OwnerName(againstID).c_str(), againstID, charID, cost, (charge ? "" : ", not charged"));
    return w.warID;
}

void WarManager::RetractWar(uint32 warID, uint32 ownerID)
{
    War::Data& w = Find(warID);
    if (w.declaredByID != ownerID)
        throw UserError("CanNotRetractWarYouDidNotDeclare");
    if (!Active(w))
        throw UserError("CanNotRetractRetractedWar");
    EndWarNotify(w, ownerID, War::End::Retracted);
}

void WarManager::SetMutual(uint32 warID, uint32 ownerID, bool mutual)
{
    War::Data& w = Find(warID);
    if (w.againstID != ownerID)
        throw UserError("CanNotChangeMutualWarWrongParty");
    if (!Active(w))
        throw UserError("CanNotChangeEndedWar");
    if (w.mutual == mutual)
        return;

    War::Data before = w;
    w.mutual = mutual;
    int64 now = GetFileTimeNow();
    if (mutual) {
        // mutual wars aren't billed
        if (w.billID != 0) {
            sBillingMgr.CancelBill(w.billID);
            w.billID = 0;
        }
    } else {
        // billing starts again at the next weekly anniversary of the declaration
        int64 period((int64)sConfig.war.BillDays * EvE::Time::Day);
        int64 periods((now - w.timeDeclared + period - 1) / period);
        if (periods < 1)
            periods = 1;
        w.nextBillTime = w.timeDeclared + periods * period;
    }
    WarDB::SaveWar(w);
    SendWarChanged(&before, &w);
    _log(WAR__INFO, "WarManager::SetMutual() - war %u is %smutual", warID, (mutual ? "" : "no longer "));
}

void WarManager::CheckSurrender(uint32 warID, uint32 surrenderingCharID, uint32 otherCharID)
{
    std::map<uint32, War::Data>::iterator itr = m_wars.find(warID);
    if (itr == m_wars.end())
        throw UserError("NoSuchWar");
    War::Data& w = itr->second;

    Client* pClient(sEntityList.FindClientByCharID(surrenderingCharID));
    if (pClient == nullptr)
        throw UserError("NotYourWarToEnd");
    uint32 ownerID = WarOwnerOf(pClient->GetCorporationID());
    if ((ownerID != w.declaredByID) and (ownerID != w.againstID))
        throw UserError("CanNotSurenderWarYouAreNotIn");
    if (!Active(w))
        throw UserError("CanNotSurrenderAsWarIsOver");
    if (!IsInCharge(ownerID, pClient))
        throw UserError(IsAlliance(ownerID) ? "MustBeAllianceCEOToEndWar" : "MustBeActiveCEOToEndWar");
    // the side the war was declared on surrenders;  the declarer retracts
    if (ownerID != w.againstID)
        throw UserError("NotYourWarToEnd");

    uint32 otherID(ownerID == w.declaredByID ? w.againstID : w.declaredByID);
    if (IsAlliance(otherID) and (AllianceDB::GetExecutorID(otherID) == 0))
        throw UserError("CanNotSurrenderToAllianceInTurmoil");
    if (otherCharID == 0)
        return;
    // windowsvc.StartCEOTradeSession trades with the CEO of the other corp, or of the other alliance's executor
    Client* pOther(sEntityList.FindClientByCharID(otherCharID));
    if ((pOther == nullptr) or !IsInCharge(otherID, pOther))
        throw UserError("NotYourWarToEnd");
}

void WarManager::OnSurrenderTrade(uint32 warID, uint32 charID)
{
    std::map<uint32, War::Data>::iterator itr = m_wars.find(warID);
    if ((itr == m_wars.end()) or !Active(itr->second))
        return;
    Client* pClient(sEntityList.FindClientByCharID(charID));
    if (pClient == nullptr)
        return;
    War::Data& w = itr->second;
    // the defender surrenders (corp_ui_wars offers Surrender to the war's target only)
    uint32 ownerID = WarOwnerOf(pClient->GetCorporationID());
    if ((ownerID != w.declaredByID) and (ownerID != w.againstID))
        return;
    EndWarNotify(w, w.againstID, War::End::Surrendered);
    _log(WAR__INFO, "WarManager::OnSurrenderTrade() - %s(%u) surrendered to %s(%u) in war %u", OwnerName(w.againstID).c_str(), w.againstID,
         OwnerName(w.declaredByID).c_str(), w.declaredByID, warID);
}

/* ------------------------------------------------------------------------------------------------------------------
 *  membership
 * ------------------------------------------------------------------------------------------------------------------ */
void WarManager::OnCorpJoinedAlliance(uint32 corpID, uint32 allianceID)
{
    // "If you are at war with a corporation that joins an alliance then the war will change from being against the
    //  corporation to being against the alliance."  the corp can't have declared wars of its own (checked on acceptance)
    SetCorpAlliance(corpID, allianceID);

    std::vector<uint32> wars;
    auto range = m_byOwner.equal_range(corpID);
    for (auto itr = range.first; itr != range.second; ++itr)
        wars.push_back(itr->second);

    int64 now = GetFileTimeNow();
    for (auto cur : wars) {
        std::map<uint32, War::Data>::iterator itr = m_wars.find(cur);
        if ((itr == m_wars.end()) or !Active(itr->second))
            continue;
        War::Data& w = itr->second;
        if (w.againstID != corpID) {
            // a war the corp declared (only reachable from /war or old data):  it can't be the alliance's, so it ends
            EndWarNotify(w, corpID, War::End::Invalidated);
            continue;
        }
        if (AreAtWar(w.declaredByID, allianceID)) {
            // already at war with the alliance:  this one is folded into that
            EndWar(w, corpID, War::End::Merged, now);
            NotifyBoth(w, Notify::Types::CorpWarInvalidated, Notify::Types::AllWarInvalidated);
            continue;
        }
        Unindex(w);
        w.againstID = allianceID;
        WarDB::SaveWar(w);
        Index(w);
        // a full add to both sides:  the alliance's members haven't got it, the declarer's row is replaced
        SendWarChanged(nullptr, &w);
        _log(WAR__INFO, "WarManager::OnCorpJoinedAlliance() - war %u of %u against %u is now against alliance %u",
             w.warID, w.declaredByID, corpID, allianceID);
    }
}

void WarManager::OnCorpLeftAlliance(uint32 corpID, uint32 allianceID)
{
    // "...you will still be at war with the alliance but also temporarily at war with the corporation that left.  The war
    //  against the corporation that leaves an alliance lasts 24 hours"
    SetCorpAlliance(corpID, 0);

    std::vector<War::Data> wars;
    auto range = m_byOwner.equal_range(allianceID);
    for (auto itr = range.first; itr != range.second; ++itr) {
        std::map<uint32, War::Data>::const_iterator wItr = m_wars.find(itr->second);
        if ((wItr != m_wars.end()) and Active(wItr->second))
            wars.push_back(wItr->second);
    }

    int64 now = GetFileTimeNow();
    for (auto cur : wars) {
        War::Data w = War::Data();
            w.declaredByID = (cur.declaredByID == allianceID ? corpID : cur.declaredByID);
            w.againstID = (cur.againstID == allianceID ? corpID : cur.againstID);
            w.timeDeclared = cur.timeDeclared;       // hostile now if the alliance's war is
            w.retracted = now;
            w.retractedBy = corpID;
            w.timeFinished = now + War::Grace;
            w.mutual = cur.mutual;
            w.endReason = War::End::LeftAlliance;
            w.legalNotified = true;
            w.nextBillTime = 0;
        w.warID = WarDB::InsertWar(w);
        if (w.warID == 0)
            continue;
        m_wars[w.warID] = w;
        Index(w);
        SendWarChanged(nullptr, &w);
        _log(WAR__INFO, "WarManager::OnCorpLeftAlliance() - corp %u left alliance %u:  24 hour war %u between %u and %u",
             corpID, allianceID, w.warID, w.declaredByID, w.againstID);
    }
}

void WarManager::OnOwnerClosed(uint32 ownerID)
{
    std::vector<uint32> wars;
    auto range = m_byOwner.equal_range(ownerID);
    for (auto itr = range.first; itr != range.second; ++itr)
        wars.push_back(itr->second);
    for (auto cur : wars) {
        std::map<uint32, War::Data>::iterator itr = m_wars.find(cur);
        if ((itr != m_wars.end()) and Active(itr->second))
            EndWarNotify(itr->second, ownerID, War::End::Invalidated);
    }
}

/* ------------------------------------------------------------------------------------------------------------------
 *  bills
 * ------------------------------------------------------------------------------------------------------------------ */
uint32 WarManager::PayerCorp(uint32 ownerID)
{
    return BillingMgr::PayingCorpFor(ownerID);
}

int64 WarManager::BillIssueTime(const War::Data& w) const
{
    // a day into the week the bill pays for, so it is due (and auto-paid a day ahead) at the end of that week
    if (sConfig.war.BillDays > 1)
        return w.nextBillTime - (int64)(sConfig.war.BillDays - 1) * EvE::Time::Day;
    return w.nextBillTime - EvE::Time::Day / 2;
}

void WarManager::IssueBill(War::Data& w, int64 now)
{
    // "The weekly cost is {cost} and you will receive a bill which needs to be paid promptly to maintain the war."
    double amount = GetCostOfWar(w.declaredByID, w.againstID, w.warID);
    uint32 billID = sBillingMgr.CreateBill(Corp::BillType::WarBill, w.declaredByID, corpCONCORD, amount, w.nextBillTime,
                                           w.againstID, w.warID);
    if (billID == 0) {
        _log(WAR__ERROR, "WarManager::IssueBill() - the bill for war %u could not be created", w.warID);
        return;
    }
    War::Data before = w;
    w.billID = billID;
    w.weeklyCost = amount;
    WarDB::SaveWar(w);
    SendWarChanged(&before, &w);
    _log(WAR__TRACE, "WarManager::IssueBill() - bill %u of %.2f for war %u, due %s", billID, amount, w.warID, Win32TimeToString(w.nextBillTime).c_str());
}

void WarManager::OnWarBillPaid(const BillData& bill)
{
    std::map<uint32, War::Data>::iterator itr = m_wars.find(bill.externalID2);
    if ((itr == m_wars.end()) or (itr->second.billID != bill.billID))
        return;
    War::Data& w = itr->second;
    War::Data before = w;
    w.billID = 0;
    w.nextBillTime += (int64)sConfig.war.BillDays * EvE::Time::Day;
    WarDB::SaveWar(w);
    SendWarChanged(&before, &w);
    _log(WAR__TRACE, "WarManager::OnWarBillPaid() - war %u paid until %s", w.warID, Win32TimeToString(w.nextBillTime).c_str());
}

void WarManager::OnWarBillOverdue(const BillData& bill)
{
    // "If the bill is not paid before it is due then the war will be cancelled."  (the bill system already cancelled the bill)
    std::map<uint32, War::Data>::iterator itr = m_wars.find(bill.externalID2);
    if ((itr == m_wars.end()) or (itr->second.billID != bill.billID) or !Active(itr->second))
        return;
    // EndWar clears billID (the client's row too);  cancelling the already cancelled bill again does nothing
    War::Data& w = itr->second;
    EndWarNotify(w, w.declaredByID, War::End::Unpaid);
    _log(WAR__INFO, "WarManager::OnWarBillOverdue() - war %u of %u against %u cancelled, bill %u not paid", w.warID, w.declaredByID, w.againstID, bill.billID);
}

/* ------------------------------------------------------------------------------------------------------------------
 *  ending
 * ------------------------------------------------------------------------------------------------------------------ */
void WarManager::EndWar(War::Data& w, uint32 byID, uint8 reason, int64 now)
{
    War::Data before = w;
    w.retracted = now;
    w.retractedBy = byID;
    w.timeFinished = now + War::Grace;
    w.endReason = reason;
    // "If the war is cancelled then do not pay any outstanding bills for that war."
    if (w.billID != 0) {
        sBillingMgr.CancelBill(w.billID);
        w.billID = 0;
    }
    WarDB::SaveWar(w);
    SendWarChanged(&before, &w);
}

void WarManager::EndWarNotify(War::Data& w, uint32 byID, uint8 reason)
{
    EndWar(w, byID, reason, GetFileTimeNow());
    switch (reason) {
        case War::End::Retracted:
            NotifyBoth(w, Notify::Types::CorpWarRetracted, Notify::Types::AllWarRetracted);
            break;
        case War::End::Surrendered:
            NotifyBoth(w, Notify::Types::CorpWarSurrender, Notify::Types::AllWarSurrender);
            break;
        default:
            // "CONCORD invalidates war"
            NotifyBoth(w, Notify::Types::CorpWarInvalidated, Notify::Types::AllWarInvalidated);
            break;
    }
    _log(WAR__INFO, "WarManager::EndWar() - war %u of %u against %u ended by %u (reason %u)", w.warID, w.declaredByID, w.againstID, byID, reason);
}

/* ------------------------------------------------------------------------------------------------------------------
 *  tick
 * ------------------------------------------------------------------------------------------------------------------ */
void WarManager::Process()
{
    int64 now = GetFileTimeNow();
    for (auto& cur : m_wars) {
        War::Data& w = cur.second;
        if (!Active(w))
            continue;

        // fighting is legal now
        if (!w.legalNotified and (now - w.timeDeclared >= War::SpoolUp)) {
            w.legalNotified = true;
            WarDB::SaveWar(w);
            // CorpWarFightingLegal {declaredByID, againstID, cost} / AllWarDeclared with hostileState (bodyWarLegal)
            for (uint8 side = 0; side < 2; ++side) {
                uint32 ownerID(side == 0 ? w.declaredByID : w.againstID);
                PyDict* data = new PyDict();
                    data->SetItemString("declaredByID", new PyInt(w.declaredByID));
                    data->SetItemString("againstID", new PyInt(w.againstID));
                    if ((side == 0) and !w.mutual) {
                        data->SetItemString("cost", new PyFloat(w.weeklyCost));
                    } else {
                        data->SetItemString("cost", new PyInt(0));
                    }
                    data->SetItemString("delayHours", new PyInt(0));
                    data->SetItemString("hostileState", new PyBool(true));
                Notify(ownerID, Notify::Types::CorpWarFightingLegal, Notify::Types::AllWarDeclared, data);
            }
            _log(WAR__INFO, "WarManager::Process() - war %u of %u against %u is now hostile", w.warID, w.declaredByID, w.againstID);
        }

        // weekly bills (mutual wars aren't billed)
        if (sConfig.corp.BillsEnabled and !w.mutual and (w.billID == 0) and (w.nextBillTime > 0) and (now >= BillIssueTime(w)))
            IssueBill(w, now);
    }

    // finished wars leave the lists after HistoryDays
    if (now - m_lastCleanup >= EvE::Time::Hour) {
        m_lastCleanup = now;
        int64 since = now - (int64)sConfig.war.HistoryDays * EvE::Time::Day;
        for (std::map<uint32, War::Data>::iterator itr = m_wars.begin(); itr != m_wars.end(); ) {
            if ((itr->second.timeFinished != 0) and (itr->second.timeFinished <= since)) {
                Unindex(itr->second);
                itr = m_wars.erase(itr);
            } else {
                ++itr;
            }
        }
    }
}

/* ------------------------------------------------------------------------------------------------------------------
 *  gm
 * ------------------------------------------------------------------------------------------------------------------ */
void WarManager::GMSkipSpoolUp(uint32 warID)
{
    War::Data& w = Find(warID);
    if (!Active(w))
        throw CustomError("War %u has ended.", warID);
    int64 now = GetFileTimeNow();
    if (now - w.timeDeclared >= War::SpoolUp)
        throw CustomError("War %u is already hostile.", warID);
    War::Data before = w;
    w.timeDeclared = now - War::SpoolUp;
    w.legalNotified = false;
    w.nextBillTime = w.timeDeclared + (int64)sConfig.war.BillDays * EvE::Time::Day;
    WarDB::SaveWar(w);
    SendWarChanged(&before, &w);
    // the "fighting is legal" notifications go out on the next tick
}

void WarManager::GMRetractWar(uint32 warID)
{
    War::Data& w = Find(warID);
    RetractWar(warID, w.declaredByID);
}

void WarManager::GMSetMutual(uint32 warID, bool mutual)
{
    War::Data& w = Find(warID);
    SetMutual(warID, w.againstID, mutual);
}

void WarManager::GMEndWar(uint32 warID)
{
    War::Data& w = Find(warID);
    if (!Active(w))
        throw CustomError("War %u has already ended.", warID);
    EndWarNotify(w, corpCONCORD, War::End::Invalidated);
}

void WarManager::GMList(uint32 ownerID, std::string& out)
{
    int64 now = GetFileTimeNow();
    out = "Wars of ";
    out += OwnerName(ownerID);
    out += " (" + std::to_string(ownerID) + "):<br>";
    uint16 count(0);
    auto range = m_byOwner.equal_range(ownerID);
    for (auto itr = range.first; itr != range.second; ++itr) {
        std::map<uint32, War::Data>::const_iterator wItr = m_wars.find(itr->second);
        if (wItr == m_wars.end())
            continue;
        const War::Data& w = wItr->second;
        out += std::to_string(w.warID) + ": " + OwnerName(w.declaredByID) + " -> " + OwnerName(w.againstID);
        if (!Active(w)) {
            out += (Hostile(w, now) ? "  ended, grace until " : "  ended ") + Win32TimeToString(w.timeFinished);
        } else if (Hostile(w, now)) {
            out += "  hostile";
        } else {
            out += "  hostile from " + Win32TimeToString(w.timeDeclared + War::SpoolUp);
        }
        if (w.mutual)
            out += "  mutual";
        else if (Active(w))
            out += "  next bill " + Win32TimeToString(w.nextBillTime) + (w.billID != 0 ? " (bill " + std::to_string(w.billID) + " unpaid)" : "");
        out += "<br>";
        ++count;
    }
    if (count == 0)
        out += "none";
}

/* ------------------------------------------------------------------------------------------------------------------
 *  helpers
 * ------------------------------------------------------------------------------------------------------------------ */
War::Data& WarManager::Find(uint32 warID)
{
    std::map<uint32, War::Data>::iterator itr = m_wars.find(warID);
    if (itr == m_wars.end())
        throw UserError("NoSuchWar");
    return itr->second;
}

void WarManager::Index(const War::Data& w)
{
    m_byOwner.emplace(w.declaredByID, w.warID);
    m_byOwner.emplace(w.againstID, w.warID);
}

void WarManager::Unindex(const War::Data& w)
{
    for (uint32 ownerID : {w.declaredByID, w.againstID}) {
        auto range = m_byOwner.equal_range(ownerID);
        for (auto itr = range.first; itr != range.second; ) {
            if (itr->second == w.warID) {
                itr = m_byOwner.erase(itr);
            } else {
                ++itr;
            }
        }
    }
}

void WarManager::SendWarChanged(const War::Data* before, const War::Data* after)
{
    const War::Data* ids(after != nullptr ? after : before);
    if (ids == nullptr)
        return;
    SendWarChanged(before, after, ids->declaredByID, ids->againstID);
}

void WarManager::SendWarChanged(const War::Data* before, const War::Data* after, uint32 toOwnerA, uint32 toOwnerB)
{
    // war_cso_wars.OnWarChanged(warID, declaredByID, againstID, change):  an add has all 9 columns with old None, a removal
    //  all new None, an update only what changed.  an update always carries warID (unchanged) so the client never takes
    //  it for an add (all old None) or a removal (all new None)
    const War::Data* ids(before != nullptr ? before : after);
    if (ids == nullptr)
        return;

    PyDict* change = new PyDict();
    if ((before == nullptr) or (after == nullptr)) {
        bool add(before == nullptr);
        const War::Data& w = (add ? *after : *before);
        auto pair = [&](const char* column, PyRep* value) {
            if (add) {
                AddChange(change, column, PyStatic.NewNone(), value);
            } else {
                AddChange(change, column, value, PyStatic.NewNone());
            }
        };
        pair("warID", new PyInt(w.warID));
        pair("declaredByID", new PyInt(w.declaredByID));
        pair("againstID", new PyInt(w.againstID));
        pair("timeDeclared", new PyLong(w.timeDeclared));
        pair("timeFinished", NullableLong(w.timeFinished));
        pair("retracted", NullableLong(w.retracted));
        pair("retractedBy", NullableInt(w.retractedBy));
        pair("billID", NullableInt(w.billID));
        pair("mutual", new PyInt(w.mutual ? 1 : 0));
    } else {
        AddChange(change, "warID", new PyInt(after->warID), new PyInt(after->warID));
        if (before->declaredByID != after->declaredByID)
            AddChange(change, "declaredByID", new PyInt(before->declaredByID), new PyInt(after->declaredByID));
        if (before->againstID != after->againstID)
            AddChange(change, "againstID", new PyInt(before->againstID), new PyInt(after->againstID));
        if (before->timeDeclared != after->timeDeclared)
            AddChange(change, "timeDeclared", new PyLong(before->timeDeclared), new PyLong(after->timeDeclared));
        if (before->timeFinished != after->timeFinished)
            AddChange(change, "timeFinished", NullableLong(before->timeFinished), NullableLong(after->timeFinished));
        if (before->retracted != after->retracted)
            AddChange(change, "retracted", NullableLong(before->retracted), NullableLong(after->retracted));
        if (before->retractedBy != after->retractedBy)
            AddChange(change, "retractedBy", NullableInt(before->retractedBy), NullableInt(after->retractedBy));
        if (before->billID != after->billID)
            AddChange(change, "billID", NullableInt(before->billID), NullableInt(after->billID));
        if (before->mutual != after->mutual)
            AddChange(change, "mutual", new PyInt(before->mutual ? 1 : 0), new PyInt(after->mutual ? 1 : 0));
        if (change->size() < 2) {
            PyDecRef(change);
            return;     // nothing the client shows changed
        }
    }

    PyTuple* payload = new PyTuple(4);
        payload->SetItem(0, new PyInt(ids->warID));
        payload->SetItem(1, new PyInt(ids->declaredByID));
        payload->SetItem(2, new PyInt(ids->againstID));
        payload->SetItem(3, change);

    std::vector<Client*> clients;
    GetOwnerClients(toOwnerA, clients);
    if ((toOwnerB != 0) and (toOwnerB != toOwnerA))
        GetOwnerClients(toOwnerB, clients);
    std::set<Client*> sent;
    for (auto cur : clients) {
        if ((cur == nullptr) or !sent.insert(cur).second)
            continue;
        PyTuple* copy = payload->Clone()->AsTuple();
        cur->SendNotification("OnWarChanged", "clientID", &copy, false);
    }
    PyDecRef(payload);
}

void WarManager::GetOwnerClients(uint32 ownerID, std::vector<Client*>& into)
{
    if (IsPlayerCorp(ownerID)) {
        sEntityList.GetCorpClients(into, ownerID);
    } else if (IsAlliance(ownerID)) {
        std::vector<uint32> corps;
        GetAllianceCorps(ownerID, corps);
        for (auto cur : corps)
            sEntityList.GetCorpClients(into, cur);
    }
}

void WarManager::Notify(uint32 ownerID, uint16 corpType, uint16 allianceType, PyDict* data)
{
    // stored for every member (offline ones see it at login), sent by CONCORD
    if (IsAlliance(ownerID)) {
        NotificationMgr::SendToAlliance(ownerID, 0, allianceType, corpCONCORD, data);
    } else {
        NotificationMgr::SendToCorp(ownerID, 0, corpType, corpCONCORD, data);
    }
}

void WarManager::NotifyBoth(const War::Data& w, uint16 corpType, uint16 allianceType)
{
    // {declaredByID, againstID}  (ParamAllWarNotification)
    for (uint32 ownerID : {w.declaredByID, w.againstID}) {
        PyDict* data = new PyDict();
            data->SetItemString("declaredByID", new PyInt(w.declaredByID));
            data->SetItemString("againstID", new PyInt(w.againstID));
        Notify(ownerID, corpType, allianceType, data);
    }
}
