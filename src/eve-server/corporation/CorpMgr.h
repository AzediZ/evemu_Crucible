/*
    ------------------------------------------------------------------------------------
    LICENSE:
    ------------------------------------------------------------------------------------
    This file is part of EVEmu: EVE Online Server Emulator
    Copyright 2006 - 2021 The EVEmu Team
    For the latest information visit https://evemu.dev
    ------------------------------------------------------------------------------------
    This program is free software; you can redistribute it and/or modify it under
    the terms of the GNU Lesser General Public License as published by the Free Software
    Foundation; either version 2 of the License, or (at your option) any later
    version.

    This program is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License along with
    this program; if not, write to the Free Software Foundation, Inc., 59 Temple
    Place - Suite 330, Boston, MA 02111-1307, USA, or go to
    http://www.gnu.org/copyleft/lesser.txt.
    ------------------------------------------------------------------------------------
*/

/* Player corporation management shared by the corp services and the 5 minute timer:
 *  member roles and titles (the session roles are the member's own roles joined with their titles' roles, and every role
 *  for the active CEO), member edits with the client's permission rules, membership moves, CEO changes, votes and
 *  sanctioned actions, and the corp notifications.
 */

#ifndef EVEMU_CORPORATION_CORPMGR_H_
#define EVEMU_CORPORATION_CORPMGR_H_

#include "../eve-server.h"

#include "EVE_Roles.h"
#include "POD_containers.h"
#include "corporation/CorpData.h"

class Client;
class PyDict;
class PyRep;

class CorpMgr
: public Singleton<CorpMgr>
{
public:
    CorpMgr();

    int Initialize();
    // every ~5 minutes from EntityList::Process
    void Process();

    /* roles */
    // directors (and the active CEO, who holds Director in the session) have every role
    static bool HasRole(int64 roles, int64 role)        { return ((roles & Corp::Role::Director) == Corp::Role::Director) or ((roles & role) == role); }
    // the roles of every title in titleMask
    Corp::TitleRoles GetTitleRoles(uint32 corpID, int64 titleMask);
    void InvalidateTitles(uint32 corpID)                { m_titles.erase(corpID); }
    // a member's session roles from their corp data:  own | titles, every role for the active CEO, none for a CEO under a CEO vote
    void ComputeEffectiveRoles(uint32 charID, const CorpData& data, int64& all, int64& hq, int64& base, int64& other);
    // pushes an online member's corp data back into their session.  reloadCorp re-reads the corp's own fields (tax, hq, alliance)
    void RefreshMemberSession(uint32 charID, bool reloadCorp = false);
    // RefreshMemberSession for every online holder of the title
    void RefreshTitleHolders(uint32 corpID, int64 titleID);
    // OnCorporationMemberChanged(memberID, {column: (old, new)}) to the online members.  consumes change
    void SendMemberChanged(uint32 corpID, uint32 charID, PyDict* change);
    // applies one member edit with the client's permission rules.  throws CrpAccessDenied, or returns false when !throwOnDeny
    bool UpdateMember(Client* pCaller, uint32 corpID, const Corp::MemberUpdate& upd, bool throwOnDeny);
    // corp member title text
    static std::string GetMemberTitle(uint32 charID);

    // a member's corp data, online or from the db.  false when the character isn't found
    static bool LoadMemberData(uint32 charID, CorpData& data);
    // saves a member's corp data (and their session when online)
    static void SaveMemberData(uint32 charID, CorpData& data);

    /* membership */
    // CanLeaveCurrentCorporation:  false with the client's message key and its arguments (args is consumed by the caller)
    bool CanLeave(uint32 charID, std::string& errorKey, PyDict*& args);
    // CanBeKickedOut for someone else.  reason is set when false
    bool CanBeKicked(Client* pCaller, uint32 corpID, uint32 charID, std::string& reason);
    // moves the member to newCorpID (no roles, titles or base), employment history, corp events, member change broadcasts
    void MoveMemberToCorp(uint32 charID, uint32 newCorpID);
    // to the character's starter (school) NPC corp, with the corp's notifications
    void LeaveCorp(uint32 charID, bool kicked, uint32 kickerID);
    // the starter NPC corp of a character (school corp)
    static uint32 GetDefaultCorp(uint32 charID);

    /* CEO */
    // charID is the corp's CEO and a CEO vote is open (roles revoked for the vote)
    bool IsCEORevoked(uint32 corpID, uint32 charID);
    void InvalidateCEOVote(uint32 corpID)               { m_ceoVote.erase(corpID); }
    // skills, shares and the CEO vote cooldown (CantRunForCEOAtTheMoment)
    bool CanRunForCEO(uint32 corpID, uint32 charID);
    // members who could take over as CEO (Corporation Management and a skill member limit covering the corp)
    void GetPotentialCEOs(uint32 corpID, std::vector<uint32>& out);
    void ChangeCEO(uint32 corpID, uint32 newCeoID, bool byVote);
    // closes the corp:  wallet paid to the character shareholders, members to their starter corps, offices and their contents go
    void LiquidateCorp(uint32 corpID, uint32 byCharID);
    // member limit and allowed races from the CEO's skills (UpdateCorporationAbilities)
    void UpdateAbilities(uint32 corpID);
    // sum of level x corporationMemberBonus over the corp management skills
    static uint32 GetSkillMemberLimit(uint32 charID);
    static uint8 GetSkillLevel(uint32 charID, uint16 skillTypeID);
    // OnCorporationChanged(corpID, {column: (old, new)}) to the online members.  consumes change
    void BroadcastCorpChange(uint32 corpID, PyDict* change);

    /* shares */
    // moves shares of corpID between holders with the OnShareChange notifications.  throws NotEnoughShares
    void TransferShares(uint32 corpID, uint32 fromID, uint32 toID, uint32 count);
    // OnShareChange(shareholderID, corporationID, change) to the holder (a character, or the members of a holding corp)
    void SendShareChange(uint32 holderID, uint32 corpID, uint32 oldShares, uint32 newShares);
    // a voter's weight in corpID's votes:  their own shares, and as active CEO of a corp that corp's holding (its treasury for their own corp)
    uint32 GetVoteWeight(uint32 charID, uint32 corpID);
    // the active CEO:  CEO and not under a CEO vote
    bool IsActiveCEO(uint32 corpID, uint32 charID);

    /* votes and sanctioned actions */
    // a new vote case:  OnCorporationVoteCaseChanged add, CorpVote to the shareholders, and for a CEO vote the CEO's roles go
    void OnVoteCaseOpened(uint32 corpID, uint32 voteCaseID, uint32 proposerID);
    // closes vote cases past their end (every 5 minutes)
    void ProcessVotes();
    void CloseVote(uint32 voteCaseID);
    // UpdateSanctionedAction by the active CEO.  throws
    void ImplementSanction(Client* pCaller, uint32 voteCaseID);
    // OnCorporationVoteCaseChanged / OnSanctionedActionChanged to the members.  consume change
    void SendVoteCaseChanged(uint32 corpID, uint32 voteCaseID, PyDict* change);
    void SendSanctionChanged(uint32 corpID, uint32 voteCaseID, PyDict* change);

    /* dividends */
    // PayoutDividend:  to the shareholders by their shares (not the corp's own treasury), or evenly to the members.  throws
    void PayDividend(Client* pCaller, uint32 corpID, bool shareholders, double amount);

    /* notifications (persistent, delivered at login) */
    // members of a player corp holding any of roleMask (0 = all, directors always).  consumes data
    void NotifyCorp(uint32 corpID, uint16 notifyType, uint32 senderID, PyDict* data, int64 roleMask = 0);
    void NotifyChar(uint32 charID, uint16 notifyType, uint32 senderID, PyDict* data);

    /* offices */
    // deletes every office of the corp with its contents (closed corp)
    void ReleaseOffices(uint32 corpID);
    // deletes an item and everything inside it
    static void DeleteTree(InventoryItemRef iRef);

private:
    void LoadTitles(uint32 corpID);

    std::map<uint32, std::map<int64, Corp::TitleRoles>> m_titles;   // corpID -> titleID -> roles
    std::map<uint32, bool> m_ceoVote;                               // corpID -> open CEO vote
};

#define sCorpMgr \
( CorpMgr::get() )

#endif  // EVEMU_CORPORATION_CORPMGR_H_
