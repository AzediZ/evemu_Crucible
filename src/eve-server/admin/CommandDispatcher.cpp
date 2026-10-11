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
    Author:        Zhur
    Updates:        Allan
*/

#include "eve-server.h"

#include "Client.h"

#include "admin/CommandDispatcher.h"
#include "ship/Ship.h"
#include "system/DestinyManager.h"

#include <set>

// command names are matched without case: the client sends some in mixed case (/spawnN)
static std::string CommandKey(const std::string& name) {
    std::string key(name);
    for (auto& c : key)
        c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return key;
}


CommandDispatcher::CommandDispatcher(EVEServiceManager& services)
: m_services( services )
{
    m_commands.clear();
}

CommandDispatcher::~CommandDispatcher() {
    for (auto cur : m_commands)
        SafeDelete(cur.second);
    m_commands.clear();
}

PyResult CommandDispatcher::Execute( Client* from, const char* msg )
{
    Seperator sep( &msg[1] );
    if (!sep.argCount()) {
        //empty command, return list of commands
        std::string reason = "Commands: ";

        std::map<std::string, CommandRecord *>::const_iterator cur = m_commands.begin();
        reason += "[";
        for(; cur != m_commands.end(); cur++)
            reason += "'" + cur->second->command + "',";
        reason += "]";

        throw UserError ("").AddFormatValue ("reason", new PyString (reason));
    }

    std::map<std::string, CommandRecord*>::const_iterator itr = m_commands.find( CommandKey(sep.arg( 0 )) );
    if (m_commands.end() == itr ) {
        _log(COMMAND__ERROR, "Unable to find command '%s' for %s", sep.arg( 0 ).c_str(), from->GetName() );
        throw CustomError ("Unknown command '%s'", sep.arg (0).c_str ());
    }

    CommandRecord* rec = itr->second;

    /* commands that move balls or add them to the caller's bubble break a warp in progress.
     *  this applies to everyone:  every slash user has GML.  /tr and /halt stay allowed, as they are the way out of a broken warp.
     *  groups adding movement or spawn commands should add their key here
     */
    static const std::set<std::string> s_noWarpCommands = {"goto", "hop", "warpto", "bubblewarp",
        "update", "addball", "addball2", "sendstate", "syncpos", "syncloc", "spawn", "spawnn", "entity",
        "cloak", "pos", "sov"};
    if (from->IsInSpace() and (from->GetShipSE() != nullptr) and (from->GetShipSE()->DestinyMgr() != nullptr)
    and from->GetShipSE()->DestinyMgr()->IsWarping() and (s_noWarpCommands.find(itr->first) != s_noWarpCommands.end())) {
        _log(COMMAND__MESSAGE, "Command '%s' requested by %s while warping.  Refused.", rec->command.c_str(), from->GetName());
        throw CustomError ("You can't use /%s while warping.", rec->command.c_str());
    }

    _log(COMMAND__INFO, "Request access to command '%s' with role %p for '%s' with role %p.",  rec->command.c_str(), rec->required_role, from->GetName(), from->GetAccountRole() );
    if ((from->GetAccountRole() & rec->required_role) != rec->required_role) {
        _log(COMMAND__ERROR, "Access denied to %s for command '%s'. --have role %p, need role %p", from->GetName(), rec->command.c_str(), from->GetAccountRole(), rec->required_role );
        throw CustomError ("Access denied to command '%s'", sep.arg (0).c_str ());
    }

    return ( *rec->function )( from, &m_db, m_services, sep );
}

void CommandDispatcher::AddCommand( const char* cmd, const char* desc, int64 required_role, CommandFunc function )
{
    std::string key(CommandKey(cmd));
    std::map<std::string, CommandRecord*>::iterator itr = m_commands.find( key );
    if (itr != m_commands.end())
        SafeDelete( itr->second );

    m_commands[key] = new CommandRecord( cmd, desc, required_role, function );
}

void CommandDispatcher::ListCommands() {
    sLog.Green("  EVEmu", "Currently Loaded %lu Commands:", m_commands.size());
    std::map<std::string, CommandDispatcher::CommandRecord*>::iterator itr = m_commands.begin();
    for (; itr != m_commands.end(); ++itr) {
        sLog.Magenta("    Call and Role", "%s - %p (%lli)",
                     itr->first.c_str(), itr->second->required_role, itr->second->required_role);
    }
}

void CommandDispatcher::Close() {
    for (auto cur : m_commands)
        SafeDelete(cur.second);
    m_commands.clear();
}