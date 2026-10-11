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

#include <ios>
#include <iostream>
#include <fstream>

#include "ConsoleCommands.h"
#include "../eve-common/EVEVersion.h"
// after eve-server.h, which brings in winsock2.h and windows.h (boost.asio) in the right order
#if _WIN32
#include <psapi.h>
#include <tlhelp32.h>
#endif
#include "StatisticMgr.h"
#include "effects/EffectsProcessor.h"
#include "inventory/ItemDB.h"
#include "market/MarketDB.h"
#include "market/MarketMgr.h"
#include "missions/MissionDataMgr.h"
#include "threading/Threading.h"
#include "chat/LSCService.h"
#include "services/ServiceManager.h"
#include "ship/Ship.h"
// #include "testing/test.h"


ConsoleCommand::ConsoleCommand()
 :pCommand(nullptr),
  m_services(nullptr),
  buf(nullptr),
  tv(timeval()),
  m_haltServer(false),
  m_dbError(false),
  m_hearAll(false)
{
}

void ConsoleCommand::Initialize(CommandDispatcher* cd, EVEServiceManager* svc)
{
    pCommand = cd;
    m_services = svc;
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    UpdateStatus();	//initial status setting
    sLog.Blue( "   ConsoleCommand", "Console Commands Initialized." );
    sLog.Yellow( "   ConsoleCommand", "Enter 'h' for current list of supported commands." );

    m_inputThread = new std::thread([&] {
        std::string temp;
        while (true) {
            std::getline(std::cin, temp);
            std::lock_guard<std::mutex> lock(m_inputMutex);
            m_input.push_back(std::move(temp));
            m_inputCondition.notify_one();
        }
    });
}

bool ConsoleCommand::Process() {
    if (m_haltServer) {
        if (!m_dbError)
            sEntityList.Shutdown();
        return false;
    }

    {
        // Critical section
        std::unique_lock<std::mutex> lock(m_inputMutex);
        if (m_inputCondition.wait_for(lock, std::chrono::seconds(0), [&] { return !m_input.empty(); })) {
            // Get a new batch of inputs to process
            std::swap(m_input, m_inputToProcess);
        }
    }

    bool continueRunning = true;
    for (auto&& input : m_inputToProcess)
    {
        continueRunning &= HandleCommand(input.c_str());
    }
    m_inputToProcess.clear();
    
    return continueRunning;
}

namespace {
    // console commands:  the full word or its letter
    struct ConsoleCmd {
        const char* word;
        char letter;
    };
    static const ConsoleCmd s_consoleCmds[] = {
        {"exit", 'x'}, {"help", 'h'}, {"hello", 'e'}, {"clients", 'c'}, {"status", 's'}, {"version", 'v'},
        {"info", 'i'}, {"information", 'i'}, {"save", 'a'}, {"broadcast", 'b'}, {"note", 'n'}, {"message", 'm'},
        {"profile", 'p'}, {"roles", 'o'}, {"commands", 'k'}, {"test", 't'}, {"effects", 'f'}, {"threads", 'd'},
        {"logs", 'l'}, {"stats", 'q'}, {"hear", 'r'}
    };
}

bool ConsoleCommand::HandleCommand(const char* buf)
{
    // split into the command (first word, any case) and the rest
    std::string line(buf);
    size_t start(line.find_first_not_of(" \t\r\n"));
    if (start == std::string::npos)
        return true;
    size_t end(line.find_first_of(" \t\r\n", start));
    std::string word(line.substr(start, (end == std::string::npos ? std::string::npos : end - start)));
    for (auto& c : word)
        c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    std::string args;
    if (end != std::string::npos) {
        size_t argStart(line.find_first_not_of(" \t", end));
        if (argStart != std::string::npos)
            args = line.substr(argStart);
        while (!args.empty() and ((args.back() == '\r') or (args.back() == '\n') or (args.back() == ' ')))
            args.pop_back();
    }

    char cmd(0);
    uint8 fxIdx(0);
    if ((word.size() == 2) and (word[0] == 'f') and isdigit(static_cast<unsigned char>(word[1]))) {
        // f<digit> runs that effects set
        cmd = 'f';
        fxIdx = word[1] - '0';
    } else {
        for (const auto& cur : s_consoleCmds) {
            if ((word == cur.word) or ((word.size() == 1) and (word[0] == cur.letter))) {
                cmd = cur.letter;
                break;
            }
        }
        if ((cmd == 'f') and !args.empty() and isdigit(static_cast<unsigned char>(args[0])))
            fxIdx = args[0] - '0';
    }

    if (cmd == 'x') {
        sLog.Warning("  EVEmu", "EXIT called.  Breaking out of Main Loop.");
        m_haltServer = true;
        return false;
    }
    else if (cmd == 'h') {
        sLog.Green("  EVEmu", "Current Console Commands and Descriptions (type the word or its letter): ");
        sLog.Warning("", "");
        sLog.Warning("           (h)elp", " Displays this dialog.");
        sLog.Warning("          h(e)llo", " Displays the 'Hello World' message.");
        sLog.Warning("           e(x)it", " Exits the server, saving all loaded items and loging out all connected clients.");
        sLog.Warning("        (c)lients", " Displays connected clients, showing account, character, and ip.");
        sLog.Warning("         (s)tatus", " Displays Server's System Status, showing threads, memory, and cpu time.");
        sLog.Warning("        (v)ersion", " Displays server version.");
        sLog.Warning("    (i)nformation", " Displays server information: version, memory, uptime, clients, items, systems, bubbles.");
        sLog.Warning("           s(a)ve", " Immediatly saves all loaded items, online characters and their ships.");
        sLog.Warning("(b)roadcast <msg>", " Broadcasts a message to all clients thru their Local chat channel.");
        sLog.Warning("     (n)ote <msg>", " Broadcasts a message to all clients thru a notification window.");
        sLog.Warning("  (m)essage <msg>", " Broadcasts a message to all clients thru a message window.");
        sLog.Warning("        (p)rofile", " Prints a profile of current server runtimes.");
        sLog.Warning("          r(o)les", " Prints a list of common roles and their values.");
        sLog.Warning("       commands(k)", " Prints a list of currently loaded Commands and their required role. (long list)");
        sLog.Warning("           (t)est", " Prints the current test object *varies*");
        sLog.Warning("   e(f)fects [n]", " Compiles and prints item effects.  f<n> works too (f1 = modules...)");
        sLog.Warning("        threa(d)s", " Prints a list of current threads.");
        sLog.Warning("    reload (l)ogs", " Reloads log.ini to change values without restarting server.");
        sLog.Warning("  stats (q)uery", " Prints current statistic data.");
        sLog.Warning("       hea(r) all", " Toggles echoing all chat msgs to console.");
    }
    else if (cmd == 'e') {
        sLog.Green("  EVEmu", "Server Hello:");
        sLog.Magenta("      Server Says", " Hello World!");
    }
    else if (cmd == 'c') {
        sLog.Green("  EVEmu", "Client Connection Information");
        uint8 clients = sEntityList.GetClientCount();
        sLog.Warning("      Connections", " %u Current Clients Online", clients);
        sLog.Warning("      Connections", " %u Clients Connected since startup.", sEntityList.GetConnections());
        if (clients) {
            std::vector<Client*> list;
            sEntityList.GetClients(list);
            for (auto cur : list) {
                if (cur->GetChar().get() == nullptr) {
                    sLog.Magenta("      Connections", " Account %u Connected, but no character selected yet.", cur->GetUserID());
                    continue;
                }
                else
                    sLog.Warning("      Connections", " [(%u)%u] %s in %s(%u).  %u Minutes Online.", \
                        cur->GetUserID(), cur->GetCharacterID(), cur->GetName(), \
                        cur->GetSystemName().c_str(), cur->GetLocationID(), cur->GetChar()->OnlineTime());
            }
        }
    }
    else if (cmd == 's') {
        std::string state = "";
        int64 threads(0);
        uint8 aThreads = std::thread::hardware_concurrency();
        float vm(0.0f), rss(0.0f), user(0.0f), kernel(0.0f);
        Status(state, threads, vm, rss, user, kernel);
        sLog.Warning("    Server Status", "  S: %s | T: %d(%u) | RSS: %.3fMb | VM: %.3fMb | U: %.2f | K: %.2f", \
            state.data(), threads, aThreads, rss, vm, user, kernel);
        sLog.Warning("      Connections", " %u Current Clients Online.", sEntityList.GetClientCount());
        sLog.Warning("      Connections", " %u Clients Connected since startup.", sEntityList.GetConnections());
        if (sConfig.debug.UseProfiling) {
            sLog.Green(" Server Profiling", "Enabled.");
        }
        else {
            sLog.Warning(" Server Profiling", "Disabled.");
        }
        if (sConfig.npc.StaticSpawns) {
            sLog.Green("    Static Spawns", "Enabled.");
        }
        else {
            sLog.Warning("    Static Spawns", "Disabled.");
        }
        if (sConfig.npc.RoamingSpawns) {
            sLog.Green("   Roaming Spawns", "Enabled.");
        }
        else {
            sLog.Warning("   Roaming Spawns", "Disabled.");
        }
        if (sConfig.rates.secRate != 1.0) {
            sLog.Green("        SecStatus", "Enabled at %.0f%%.", (sConfig.rates.secRate * 100));
        }
        else {
            sLog.Warning("        SecStatus", "Normal.");
        }
        if (sConfig.rates.npcBountyMultiply != 1.0) {
            sLog.Green("          Bountys", "Enabled at %.0f%%.", (sConfig.rates.npcBountyMultiply * 100));
        }
        else {
            sLog.Warning("          Bountys", "Normal.");
        }
        if (sConfig.rates.damageRate != 1.0) {
            sLog.Green("      All Damages", "Enabled at %.0f%%.", (sConfig.rates.damageRate * 100));
        }
        else {
            sLog.Warning("      All Damages", "Normal.");
        }
        if (sConfig.rates.missileRoF != 1.0) {
            sLog.Green("      Missile Dmg", "Enabled at %.0f%%.", (sConfig.rates.missileRoF * 100));
        }
        else {
            sLog.Warning("      Missile Dmg", "Normal.");
        }
        if (sConfig.rates.turretRoF != 1.0) {
            sLog.Green("       Turret Dmg", "Enabled at %.0f%%.", (sConfig.rates.turretRoF * 100));
        }
        else {
            sLog.Warning("       Turret Dmg", "Normal.");
        }
    }
    else if (cmd == 'v') {
        sLog.Green("  EVEmu", "Server Version:");
        sLog.Warning("  Server Revision", " %s", EVEMU_REVISION);
        sLog.Warning("       Build Date", " %s", EVEMU_BUILD_DATE);
    }
    else if (cmd == 'i') {
        sLog.Green("  EVEmu", "Server Information:");
        sLog.Warning("  Server Revision", " %s", EVEMU_REVISION);
        sLog.Warning("       Build Date", " %s", EVEMU_BUILD_DATE);
        //  memory
        std::string state = "";
        int64 threads(0);
        uint8 aThreads = std::thread::hardware_concurrency();
        float vm(0.0f), rss(0.0f), user(0.0f), kernel(0.0f);
        Status(state, threads, vm, rss, user, kernel);
        sLog.Warning("     Memory Usage", " RSS: %.3fMb  VM: %.3fMb", rss, vm);
        sLog.Warning("    Server Status", "  S: %s | T: %lli(%u) | U: %.2f | K: %.2f", \
            state.data(), threads, aThreads, user, kernel);
        std::string uptime;
        sEntityList.GetUpTime(uptime);
        sLog.Warning("    Server UpTime", " %s", uptime.c_str());
        //  loaded items
        sLog.Warning("     Loaded Items", " %u", sItemFactory.Count());
        //  loaded NPCs
        sLog.Warning("      Loaded NPCs", " %u", sEntityList.GetNPCCount());
        //  loaded systems
        sLog.Warning("   Active Systems", " %u", sEntityList.GetSystemCount());
        //  loaded stations
        sLog.Warning("  Active Stations", " %u", sEntityList.GetStationCount());
        //  loaded bubbles
        sLog.Warning("   Active Bubbles", " %u", sBubbleMgr.Count());
        //  current clients
        sLog.Warning("      Connections", " %u Current Clients Online.", sEntityList.GetClientCount());
        sLog.Warning("      Connections", " %u Clients Connected since startup.", sEntityList.GetConnections());
    }
    else if (cmd == 'a') {
        sLog.Green("  EVEmu", "Server SaveAll:");
        SaveAll();
    }
    else if (cmd == 'b') {
        sLog.Green("  EVEmu", "Server Broadcast:");
        if (args.empty()) {
            sLog.Error(" Server Broadcast", " Usage: broadcast <message>");
        } else if (m_services == nullptr) {
            sLog.Error(" Server Broadcast", " Chat service is not available.");
        } else {
            LSCService* pLSC(m_services->Lookup <LSCService>("LSC"));
            uint32 count(pLSC == nullptr ? 0 : pLSC->BroadcastSystemMessage(args));
            sLog.Warning(" Server Broadcast", " [EVE System] %s", args.c_str());
            sLog.Warning("  Console Command", " Broadcast sent to %u Local channels.", count);
        }
    }
    else if (cmd == 'n') {
        sLog.Green("  EVEmu", "Server Notify:");
        std::vector<Client*> list;
        sEntityList.GetClients(list);
        for (auto cur : list)
            cur->SendNotifyMsg("%s", args.c_str());
        sLog.Warning("  Console Command", " Notification sent to all online clients.");
    }
    else if (cmd == 'm') {
        sLog.Green("  EVEmu", "Server Modal Message:");
        std::vector<Client*> list;
        sEntityList.GetClients(list);
        for (auto cur : list)
            cur->SendInfoModalMsg("%s", args.c_str());
        sLog.Warning("  Console Command", " Modal Message sent to all online clients.");
    }
    else if (cmd == 'p') {
        sLog.Green("  EVEmu", "Server Profile:");
        if (sConfig.debug.UseProfiling) {
            sLog.Warning("    Current Stamp", " %u", sEntityList.GetStamp());
            std::string uptime;
            sEntityList.GetUpTime(uptime);
            sLog.Warning("    Server UpTime", " %s", uptime.c_str());
            sLog.Warning("      Connections", " %u Current Clients Online.", sEntityList.GetClientCount());
            sLog.Warning("      Connections", " %u Clients Connected since startup.", sEntityList.GetConnections());
            sProfiler.PrintProfile();
        }
        else {
            sLog.Error("   Server Profile", "Profiling is turned off.");
        }
    }
    else if (cmd == 'r') {
        // console chat echo (LSCService::SendMessage checks HearAll())
        m_hearAll = !m_hearAll;
        sLog.Green("  EVEmu", "Hear all chat: %s", (m_hearAll ? "on" : "off"));
    }
    else if (cmd == 'o') {
        sLog.Green("  EVEmu", "Common Account Roles:");
        sLog.Warning("     Acct::Role::DEV", " %lli(%p)", Acct::Role::DEV, Acct::Role::DEV);
        sLog.Warning("     Acct::Role::STD", " %lli(%p)", Acct::Role::STD, Acct::Role::STD);
        sLog.Warning("     Acct::Role::VIP", " %lli(%p)", Acct::Role::VIP, Acct::Role::VIP);
        sLog.Warning("    Acct::Role::VIP+", " %lli(%p)", Acct::Role::EPLAYER, Acct::Role::EPLAYER);
        sLog.Warning("    Acct::Role::VIEW", " %lli(%p)", Acct::Role::VIEW, Acct::Role::VIEW);
        sLog.Warning("    Acct::Role::BOSS", " %lli(%p)", Acct::Role::BOSS, Acct::Role::BOSS);
        sLog.Warning("   Acct::Role::SLASH", " %lli(%p)", Acct::Role::SLASH, Acct::Role::SLASH);
        sLog.Warning(" Acct::Role::CREATOR", " %lli(%p)", Acct::Role::CREATOR, Acct::Role::CREATOR);
        std::printf("\n");     // spacer
        sLog.Green("  EVEmu", "Common Corp Roles:");
        sLog.Warning("     Corp::Role::All", " %lli(%p)", Corp::Role::All, Corp::Role::All);
        sLog.Warning("    Corp::Role::Cont", " %lli(%p)", Corp::Role::AllContainer, Corp::Role::AllContainer);
        sLog.Warning("   Corp::Role::Admin", " %lli(%p)", Corp::Role::Admin, Corp::Role::Admin);
        sLog.Warning("  Corp::Role::Hangar", " %lli(%p)", Corp::Role::AllHangar, Corp::Role::AllHangar);
        sLog.Warning(" Corp::Role::Account", " %lli(%p)", Corp::Role::AllAccount, Corp::Role::AllAccount);
        sLog.Warning("Corp::Role::Starbase", " %lli(%p)", Corp::Role::AllStarbase, Corp::Role::AllStarbase);
    }
    else if (cmd == 'k') {
        pCommand->ListCommands();
    }
    else if (cmd == 't') {
        Test();
    }
    else if (cmd == 'f') {
        FxProc(fxIdx);
    }
    else if (cmd == 'd') {
        uint8 maxCount = sConfig.server.MaxThreadReport;
        uint8 count = sThread.Count();
        sLog.Blue("   Active Threads", "There are %u active threads running in the server.", count);
        if (count > maxCount) {
            sLog.Warning("   Active Threads", "Individual thread IDs are not displayed for more than %u active threads.", maxCount);
        }
        else {
            sThread.ListThreads();
        }
    }
    else if (cmd == 'l') {
        /*
        sLog.~NewLog();
        sLog.InitializeLogging(sConfig.files.logDir);
        */
        if (load_log_settings(sConfig.files.logSettings.c_str())) {
            sLog.Green("  EVEmu", "Log settings reloaded from %s", sConfig.files.logSettings.c_str());
            // reset config switches based on log settings
            sConfig.debug.StackTrace = is_log_enabled(SERVER__STACKTRACE);
            sConfig.debug.BeanCount = is_log_enabled(SERVER__BEANCOUNT);
            sConfig.debug.IsTestServer = is_log_enabled(SERVER__TESTSERVER);
        }
        else {
            sLog.Warning("  EVEmu", "Unable to reload settings from %s", sConfig.files.logSettings.c_str());
        }
    }
    else if (cmd == 'q') {
        sLog.Green("  EVEmu", "Server Statistic Data:");
        sStatMgr.PrintInfo();
    }
    else {
        sLog.Error("  EVEmu", "Command not recognized: %s", buf);
    }

    return true;
}

void ConsoleCommand::SaveAll() {
    // this runs on the main thread (Process() is called from the main loop; the input thread only queues lines)
    sItemFactory.SaveItems();
    uint32 chars(0), ships(0);
    std::vector<Client*> list;
    sEntityList.GetClients(list);
    for (auto cur : list) {
        if (cur->GetChar().get() == nullptr)
            continue;
        cur->GetChar()->SaveFullCharacter();
        ++chars;
        if (cur->GetShip().get() != nullptr) {
            cur->GetShip()->SaveShip();
            ++ships;
        }
    }
    sLog.Warning("      Server Save", " Saved %u loaded items, %u online characters and %u ships.", sItemFactory.Count(), chars, ships);
}

void ConsoleCommand::Status(std::string& state, int64& threads, float& vm_usage, float& resident_set, float& user, float& kernel)
{
#if _WIN32
    state = "R";
    // memory, in Mb
    PROCESS_MEMORY_COUNTERS_EX pmc;
    memset(&pmc, 0, sizeof(pmc));
    if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
        resident_set = pmc.WorkingSetSize / 1048576.0f;
        vm_usage = pmc.PrivateUsage / 1048576.0f;
    } else {
        resident_set = 0.0f;
        vm_usage = 0.0f;
    }
    // threads of this process
    threads = 0;
    HANDLE snap(CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0));
    if (snap != INVALID_HANDLE_VALUE) {
        THREADENTRY32 te;
        te.dwSize = sizeof(te);
        DWORD pid(GetCurrentProcessId());
        if (Thread32First(snap, &te)) {
            do {
                if (te.th32OwnerProcessID == pid)
                    ++threads;
                te.dwSize = sizeof(te);
            } while (Thread32Next(snap, &te));
        }
        CloseHandle(snap);
    }
    if (threads < 1)
        threads = 1;
    /* cpu time.  FILETIME is in 100ns units.  the linux branch reports utime/CLK_TCK/100 (seconds / 100),
     *  so this does the same to keep the server stats table comparable */
    FILETIME created, exited, kernelTime, userTime;
    if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernelTime, &userTime)) {
        ULARGE_INTEGER k, u;
        k.LowPart = kernelTime.dwLowDateTime;
        k.HighPart = kernelTime.dwHighDateTime;
        u.LowPart = userTime.dwLowDateTime;
        u.HighPart = userTime.dwHighDateTime;
        kernel = static_cast<float>(k.QuadPart / 1.0e7 / 100.0);
        user = static_cast<float>(u.QuadPart / 1.0e7 / 100.0);
    } else {
        user = 0.0f;
        kernel = 0.0f;
    }
#else
    // the fields we want
    std::string ignore = "", run_state = "";
    int64 num_threads(0);  //this is saved from OS as long decimal....*sigh*  gotta allocate long int for it or weird shit happens.
    int64 vsize(0);      //in bytes
    int64 rss(0);			//in pages
    float utime(0.0f), stime(0.0f);

    // stat seems to give the most reliable results
    std::ifstream ifs ("/proc/self/stat", std::ios_base::in);
    ifs >> ignore >> ignore >> run_state >> ignore >> ignore >> ignore >> ignore >> ignore >> ignore >> ignore
        >> ignore >> ignore >> ignore >> utime >> stime >> ignore >> ignore >> ignore >> ignore >> num_threads
        >> ignore >> ignore >> vsize >> rss;
    ifs.close();

    if (sConfig.debug.IsTestServer)
        _log(SERVER__INFO, "ConsoleCommand::Status() proc/self/stat returns RSS: %lli, VM: %lli", rss, vsize);

    /*  state = One character from the string "RSDZTW" where
              R is running,
              S is sleeping in an interruptible wait,
              D is waiting in uninterruptible disk sleep,
              Z is zombie,
              T is traced or stopped (on a signal),
              W is paging
    */
    state = run_state;
    threads = num_threads;
    user = utime / sysconf(_SC_CLK_TCK) / 100.0f;
    kernel = stime / sysconf(_SC_CLK_TCK) / 100.0f;
    vm_usage     = ((vsize / sysconf(_SC_PAGE_SIZE)) / 1024.0f / 6);
    //rss (in pages) * page_size(in bytes, converted to k), then convert to Mb.
    resident_set = (rss * (sysconf(_SC_PAGE_SIZE) / 1024.0f) / 1024.0f);
#endif
}

void ConsoleCommand::Test()
{
    sLog.Green("   EVEmu", "Running Server Test Code:");
    //sLog.Warning(" Server Test Code", "No Test Code at this time.");

    //sMktMgr.GetCruPrices();
    //sLog.Blue("       MktPricing", "Base material Prices Updated");

    sMktMgr.SetBasePrice();

    // execute code to begin filling missing data in mission db.
    //  first step:  get courier missionIDs
    //sMissionDataMgr.UpdateMissionData();
    //MarketDB::UpdateHistory();
}

void ConsoleCommand::UpdateStatus() {
	std::string state = "";
	int64 threads(0);
	float vm(0.0f), rss(0.0f), user(0.0f), kernel(0.0f);
	Status(state, threads, vm, rss, user, kernel);
    if (sConfig.debug.IsTestServer)
        _log(SERVER__INFO, "Current Mem usage - RSS: %f, VM: %f", rss, vm);
    ServiceDB::SaveServerStats(threads + sThread.Count(), rss, vm, user, kernel, sItemFactory.Count(), sBubbleMgr.Count());
    sStatMgr.Process();
}

void ConsoleCommand::FxProc(uint8 idx/*0*/) {
    double start = GetTimeMSeconds();
    sLog.Green("  EVEmu", "FxProcess(%u)", idx);

    // load items from db
    std::map<uint16, std::string> typeIDs;
    switch(idx) {
        case 1: {
            sLog.Green("        FxProcess", "Parsing Effects for Module Items.");
            std::printf("\n");     // spacer
            ItemDB::GetItems(EVEDB::invCategories::Module, typeIDs);
        } break;
        case 2: {
            sLog.Green("        FxProcess", "Parsing Effects for Charge Items.");
            std::printf("\n");     // spacer
            ItemDB::GetItems(EVEDB::invCategories::Charge, typeIDs);
        } break;
        case 3: {
            sLog.Green("        FxProcess", "Parsing Effects for Subsystem Items.");
            std::printf("\n");     // spacer
            ItemDB::GetItems(EVEDB::invCategories::Subsystem, typeIDs);
        } break;
        case 4: {
            sLog.Green("        FxProcess", "Parsing Effects for Skill Items.");
            std::printf("\n");     // spacer
            ItemDB::GetItems(EVEDB::invCategories::Skill, typeIDs);
        } break;
        case 5: {
            sLog.Green("        FxProcess", "Parsing Effects for Implant Items.");
            std::printf("\n");     // spacer
            ItemDB::GetItems(EVEDB::invCategories::Implant, typeIDs); // this is implants and boosters
        } break;
        case 6: {
            sLog.Green("        FxProcess", "Parsing Effects for Ship Items.");
            std::printf("\n");     // spacer
            ItemDB::GetItems(EVEDB::invCategories::Ship, typeIDs);
        } break;
        case 9: {
            sLog.Green("        FxProcess", "Parsing Effects for All Possible Items.");
            std::printf("\n");     // spacer
            ItemDB::GetItems(EVEDB::invCategories::Module, typeIDs);
            ItemDB::GetItems(EVEDB::invCategories::Charge, typeIDs);
            ItemDB::GetItems(EVEDB::invCategories::Subsystem, typeIDs);
            ItemDB::GetItems(EVEDB::invCategories::Skill, typeIDs);
            ItemDB::GetItems(EVEDB::invCategories::Implant, typeIDs); // this is implants and boosters
            ItemDB::GetItems(EVEDB::invCategories::Ship, typeIDs);
        } break;
        default: {
            sLog.Green("        FxProcess", "Usage of Item Fx process reporting.");
            sLog.Green("        FxProcess", "fx where 'x' is a number corresponding to the item category you wish to process.");
            sLog.Green("        FxProcess", "1=Modules, 2=Charges, 3=Subsystems, 4=Skills, 5=Implants/Boosters, 6=Ships, 9=all");
            return;
        }
    }

    for (auto cur : typeIDs) {
        sLog.Yellow("CC::FxProc", "Processing %s (%u)", cur.second.c_str(), cur.first);
        // proc and print skill fx
        std::vector< TypeEffects > typeEffMap;
        sFxDataMgr.GetTypeEffect(cur.first, typeEffMap);
        for (auto cur2: typeEffMap)
            sFxProc.DecodeEffects(cur2.effectID);
    }

    sLog.Magenta("CC::FxProc", "- effects processed in %.3fms", (GetTimeMSeconds() - start));
}
