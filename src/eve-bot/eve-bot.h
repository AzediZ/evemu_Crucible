/*
 * eve-bot: a headless test client for EVEmu.
 *
 * It speaks the same network protocol as the game client, so it can log in,
 * create a character, undock, run GM commands and fly drones against a real
 * server, printing what the server sends back.  Used to test server changes
 * without the game client.
 */

#ifndef __EVE_BOT_H__INCL__
#define __EVE_BOT_H__INCL__

#include "eve-core.h"

#include "log/logsys.h"
#include "log/LogNew.h"
#include "threading/Mutex.h"
#include "utils/Singleton.h"
#include "utils/timer.h"
#include "utils/utils_time.h"

#include "eve-common.h"

#include "EVEVersion.h"
#include "marshal/EVEMarshal.h"
#include "marshal/EVEUnmarshal.h"
#include "network/EVETCPConnection.h"
#include "network/packet_types.h"
#include "packets/Crypto.h"
#include "python/PyPacket.h"
#include "python/PyRep.h"
#include "python/classes/PyDatabase.h"

#endif /* !__EVE_BOT_H__INCL__ */
