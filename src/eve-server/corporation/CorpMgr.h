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

    /* CEO */
    // charID is the corp's CEO and a CEO vote is open (roles revoked for the vote)
    bool IsCEORevoked(uint32 corpID, uint32 charID);
    void InvalidateCEOVote(uint32 corpID)               { m_ceoVote.erase(corpID); }

    /* notifications (persistent, delivered at login) */
    // members of a player corp holding any of roleMask (0 = all, directors always).  consumes data
    void NotifyCorp(uint32 corpID, uint16 notifyType, uint32 senderID, PyDict* data, int64 roleMask = 0);
    void NotifyChar(uint32 charID, uint16 notifyType, uint32 senderID, PyDict* data);

private:
    void LoadTitles(uint32 corpID);

    std::map<uint32, std::map<int64, Corp::TitleRoles>> m_titles;   // corpID -> titleID -> roles
    std::map<uint32, bool> m_ceoVote;                               // corpID -> open CEO vote
};

#define sCorpMgr \
( CorpMgr::get() )

#endif  // EVEMU_CORPORATION_CORPMGR_H_
