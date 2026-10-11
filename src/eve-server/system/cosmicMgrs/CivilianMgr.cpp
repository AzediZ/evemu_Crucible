
/**
 * @name CivilianMgr.cpp
 *     Civilian (non-combatant NPC) management system for EVEmu
 *
 * @Author:        Allan
 * @date:          12 Feb 2017
 *
 */


#include "eve-server.h"

#include "EVEServerConfig.h"

#include "system/cosmicMgrs/CivilianMgr.h"

/*  Crucible had no ambient civilian (non-combatant) npc traffic.  convoy and civilian types only appear in missions and
 * deadspace spawns, which their own managers create.  so this manager is intentionally empty, and <cosmic><CiviliansEnabled>
 * only starts it (it does nothing).
 */


CivilianMgr::CivilianMgr()
{
    m_initalized = false;
}

void CivilianMgr::Initialize() {
    m_initalized = true;
    sLog.Blue(" Civilian Manager", "Civilian Manager Initialized (no ambient traffic in Crucible).");
}

void CivilianMgr::Process() {
    // nothing to do.  see above
}

