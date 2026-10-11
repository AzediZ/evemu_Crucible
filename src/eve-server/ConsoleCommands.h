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
    Author:	Allan
    Thanks to:	avianrr  for the idea
*/

#ifndef EVEMU_EVESERVER_CONSOLECOMMANDS_H_
#define EVEMU_EVESERVER_CONSOLECOMMANDS_H_

#define BUFLEN 256

#include "eve-server.h"
#include "EVEServerConfig.h"

#include "Client.h"
#include "EntityList.h"
#include "ServiceDB.h"
#include "admin/CommandDispatcher.h"
#include "chat/LSCChannel.h"
#include "inventory/ItemFactory.h"
#include "system/DestinyManager.h"
#include "system/SystemBubble.h"
#include "system/SystemManager.h"


class EVEServiceManager;

class ConsoleCommand
: public Singleton<ConsoleCommand>
{
  public:
    ConsoleCommand();
    ~ConsoleCommand()                                   { /* do nothing here */ }

    void Initialize(CommandDispatcher* cd, EVEServiceManager* svc);

    // this calls multiple methods that get/process/save data for running server
    void UpdateStatus();
    void HaltServer(bool dbError=false)                 { m_haltServer = true; m_dbError = dbError; }

    bool Process();
    bool IsShutdown()                                   { return m_haltServer; }
    // avoid using db after it has lost connection.
    bool IsDbError()                                    { return m_dbError; }
    // 'hear all':  echo every chat message to the console
    bool HearAll() const                                { return m_hearAll; }

private:
    // we do not own any of these...
    CommandDispatcher* pCommand;
    EVEServiceManager* m_services;

    char* buf;
    fd_set fds;
    struct timeval tv;
    bool m_haltServer;
    bool m_dbError;
    bool m_hearAll;

    bool HandleCommand(const char* buf);
    void SaveAll();
    void Status(std::string& state, int64& threads, float& vm_usage, float& resident_set, float& user, float& kernel);

    void Test();
    void FxProc(uint8 idx=0);

    std::condition_variable m_inputCondition;
    std::mutex m_inputMutex;
    std::deque<std::string> m_input;
    std::deque<std::string> m_inputToProcess;
    std::thread* m_inputThread;
};

//Singleton
#define sConsole \
    ( ConsoleCommand::get() )

#endif  // EVEMU_EVESERVER_CONSOLECOMMANDS_H_

