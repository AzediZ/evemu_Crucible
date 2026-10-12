/**
 * @name WarManager.h
 *   wars between player corporations and alliances
 *
 * Crucible (pre-Inferno) war rules:
 *  - a corporation declares war by corporation vote (the CEO implements the sanctioned action), an alliance through its
 *    executor's directors.  a corporation in an alliance fights the alliance's wars, never its own.
 *  - the declaration costs base x (wars the declarer has declared + 1) x (wars declared on the target + 1), mutual wars
 *    not counted (client WarDeclareConfirm text), base 2M isk corp on corp and 50M when an alliance is involved.
 *    the same cost is billed every week; an unpaid bill cancels the war.
 *  - hostilities start 24 hours after the declaration, and a war that ends (retracted, surrendered, cancelled) can
 *    still be fought for 24 hours (client util.IsWarInHostileState; both are hard-coded in the client).
 *  - only the defender may make a war mutual (no bills then), only the declarer may retract it, and the defender
 *    surrenders through a CEO trade with the other side's CEO.
 *  - a corporation at war that joins an alliance hands the war to the alliance; a corporation that leaves an alliance
 *    stays at war with the alliance's enemies for 24 hours.
 *  war targets may be attacked anywhere without CONCORD's involvement (client consider.CanAttackFreely).
 */

#ifndef EVEMU_FACTION_WARMANAGER_H_
#define EVEMU_FACTION_WARMANAGER_H_

#include "eve-server.h"

#include "faction/WarDB.h"

class Client;
class PyDict;
class PyRep;
class PyTuple;
struct BillData;

namespace War {
    static const int64 SpoolUp = 24 * EvE::Time::Hour;     // client util.IsWarInHostileState
    static const int64 Grace = 24 * EvE::Time::Hour;       // ended wars stay hostile this long

    namespace End {
        enum : uint8 {
            None            = 0,
            Retracted       = 1,
            Surrendered     = 2,
            Invalidated     = 3,    // an owner closed, or a GM ended it
            Unpaid          = 4,
            LeftAlliance    = 5,    // the temporary war of a corporation that left an alliance at war
            Merged          = 6     // the corporation joined an alliance already at war with the declarer
        };
    }

    struct Data {
        uint32 warID = 0;
        uint32 declaredByID = 0;
        uint32 againstID = 0;
        uint32 retractedBy = 0;         // 0 = NULL
        uint32 billID = 0;              // the war's unpaid bill.  0 = NULL
        int64 timeDeclared = 0;
        int64 timeFinished = 0;         // end of the grace period.  0 = NULL
        int64 retracted = 0;            // time the war ended (any reason).  0 = NULL
        int64 nextBillTime = 0;         // due date of the next weekly payment
        double weeklyCost = 0.0;
        bool mutual = false;
        bool legalNotified = false;
        uint8 endReason = War::End::None;
    };
}

class WarManager
: public Singleton<WarManager>
{
public:
    WarManager();

    int Initialize();
    // once a minute:  the "fighting is legal" notifications, weekly bills, dropping old wars
    void Process();

    /* owners */
    // the alliance a player corporation fights wars as, or the corporation.  0 for npc corporations
    uint32 WarOwnerOf(uint32 corpID);
    // keeps the corp -> alliance map current.  called by AllianceManager only
    void SetCorpAlliance(uint32 corpID, uint32 allianceID);
    // the member corporations of an alliance, from the map
    void GetAllianceCorps(uint32 allianceID, std::vector<uint32>& into);

    /* queries */
    // a war between them can be fought now (either direction)
    bool IsHostile(uint32 ownerA, uint32 ownerB);
    // a war between them that hasn't ended (spool-up included)
    bool AreAtWar(uint32 ownerA, uint32 ownerB);
    // the owner has declared wars that haven't ended
    bool HasDeclaredWars(uint32 ownerID);
    // corporation:  its active CEO.  alliance:  a director (or the CEO) of its executor corporation
    bool IsInCharge(uint32 ownerID, Client* pClient);
    double GetCostOfWar(uint32 declarerID, uint32 againstID, uint32 excludeWarID = 0);
    // warRegistry.GetWars:  dbutil.CIndexedRowset keyed by warID
    PyRep* GetWars(uint32 ownerID);

    /* actions.  these throw the client's UserErrors */
    void ValidateDeclaration(uint32 declarerID, uint32 againstID);
    // charges the declaration (unless charge is false) and returns the warID
    uint32 DeclareWar(uint32 declarerID, uint32 againstID, uint32 charID, bool charge = true);
    void RetractWar(uint32 warID, uint32 ownerID);
    void SetMutual(uint32 warID, uint32 ownerID, bool mutual);
    // trademgr.InitiateTrade(charID, warID):  the surrendering side's leader trades with the other side's
    void CheckSurrender(uint32 warID, uint32 surrenderingCharID, uint32 otherCharID);
    // the surrender trade completed.  charID is either trader
    void OnSurrenderTrade(uint32 warID, uint32 charID);

    /* membership (AllianceManager, CorpMgr) */
    void OnCorpJoinedAlliance(uint32 corpID, uint32 allianceID);
    void OnCorpLeftAlliance(uint32 corpID, uint32 allianceID);
    // a corporation closed or an alliance dissolved:  its wars are invalidated
    void OnOwnerClosed(uint32 ownerID);

    /* bills (BillingMgr handlers) */
    void OnWarBillPaid(const BillData& bill);
    void OnWarBillOverdue(const BillData& bill);

    /* gm (/war) */
    void GMSkipSpoolUp(uint32 warID);
    // as the declarer's retraction, with its notifications
    void GMRetractWar(uint32 warID);
    // as the defender
    void GMSetMutual(uint32 warID, bool mutual);
    // CONCORD invalidates it
    void GMEndWar(uint32 warID);
    void GMList(uint32 ownerID, std::string& out);

private:
    bool Hostile(const War::Data& w, int64 now) const;
    bool Active(const War::Data& w) const               { return (w.retracted == 0); }
    // throws NoSuchWar
    War::Data& Find(uint32 warID);
    void Index(const War::Data& w);
    void Unindex(const War::Data& w);
    // sets the end, saves, cancels its bill, tells both sides.  no notification
    void EndWar(War::Data& w, uint32 byID, uint8 reason, int64 now);
    // the war ended with the notification for its reason to both sides
    void EndWarNotify(War::Data& w, uint32 byID, uint8 reason);
    // the corporation that pays for an owner:  itself, or the executor of the alliance (0 = none)
    uint32 PayerCorp(uint32 ownerID);
    void IssueBill(War::Data& w, int64 now);
    int64 BillIssueTime(const War::Data& w) const;

    // OnWarChanged(warID, declaredByID, againstID, {column: (old, new)}).  before nullptr = added, after nullptr = removed
    void SendWarChanged(const War::Data* before, const War::Data* after, uint32 toOwnerA, uint32 toOwnerB);
    void SendWarChanged(const War::Data* before, const War::Data* after);
    void GetOwnerClients(uint32 ownerID, std::vector<Client*>& into);
    // the persistent war notification, Corp* type for a corporation's members, All* type for an alliance's
    void Notify(uint32 ownerID, uint16 corpType, uint16 allianceType, PyDict* data);
    void NotifyBoth(const War::Data& w, uint16 corpType, uint16 allianceType);

    std::map<uint32, War::Data> m_wars;             // warID -> war (wars not finished more than HistoryDays ago)
    std::multimap<uint32, uint32> m_byOwner;        // ownerID -> warID (both sides)
    std::map<uint32, uint32> m_corpAlliance;        // player corpID -> allianceID (members only)
    int64 m_lastCleanup;
};

#define sWarMgr \
( WarManager::get() )

#endif  // EVEMU_FACTION_WARMANAGER_H_
