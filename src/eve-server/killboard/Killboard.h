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

#ifndef __KILLBOARD_H__INCL__
#define __KILLBOARD_H__INCL__

#include "eve-common.h"
#include "utils/Singleton.h"
#include "killboard/KillboardData.h"

struct KillData;

/* the killboard:  kill reports (the Retribution kill report as a page), a zkillboard-like board with pilot, corporation,
 *  alliance, ship and system pages and top lists, and battle reports.  the image server hands it every request under /kb/,
 *  on its own thread, and the pages open in the in-game browser (CCPEVE show info links work there).
 */

struct KbRequest {
    std::string path;                               // after "/kb", "/" at least
    std::map<std::string, std::string> query;       // decoded
    std::map<std::string, std::string> headers;     // names lower case with '-' as '_' (eve_charid, host, user_agent)
};

struct KbResponse {
    int status = 200;
    std::string contentType = "text/html; charset=utf-8";
    std::string body;
};

class Killboard : public Singleton<Killboard>
{
public:
    Killboard();

    void Handle(const KbRequest& req, KbResponse& rsp);

    // the kill mails:  a link to the report of a kill just saved, for everyone involved
    static void MailKillReport(uint32 killID, const KillData& data);

private:
    // pages
    void PageHome(const KbRequest& req, std::string& out);
    void PageKill(uint32 killID, const KbRequest& req, std::string& out);
    void PageEntity(KbFilter::Type type, uint32 id, const KbRequest& req, std::string& out);
    void PageBattles(const KbRequest& req, std::string& out);
    void PageBattle(uint32 killID, const KbRequest& req, std::string& out);
    void PageSearch(const KbRequest& req, std::string& out);
    void PageNotFound(const std::string& what, const KbRequest& req, std::string& out);

    // page parts
    void Header(const std::string& title, const KbRequest& req, std::string& out);
    void Footer(std::string& out);
    void KillTable(const std::vector<const KbKill*>& kills, KbFilter::Type type, uint32 id, std::string& out);
    void Pager(const std::string& base, size_t page, size_t total, size_t perPage, std::string& out);
    std::string OwnerLink(uint32 ownerID);
    std::string KillmailText(const KbKill& kill);

    std::mutex m_lock;
    KillboardData m_data;
    bool m_notFound;            // the page being built is a "not found" page (answered with a 404)
};

#define sKillboard \
    ( Killboard::get() )

#endif  // __KILLBOARD_H__INCL__
