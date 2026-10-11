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

#ifndef __NOTIFICATIONMGR_H__INCL__
#define __NOTIFICATIONMGR_H__INCL__

#include "eve-compat.h"

class PyRep;
class PyDict;

/* server side of the mail window's notifications tab.
 *  notifications are stored per character (chrNotifications) until the player deletes them,
 *  and pushed live with OnNotificationReceived when the receiver is online.
 *  the client reads them back thru notificationMgr (NotificationMgrService).
 *
 *  `data` keys must match the client's formatter for the type (notificationUtil.py), and
 *  values should be ints, floats, strings, None, lists or dicts of those.
 */
class NotificationMgr {
public:
    // store one notification for toID and push OnNotificationReceived if online.  returns notificationID (0 = failed / not stored)
    // consumes `data` (may be nullptr -> empty dict)
    static uint32 Send(uint32 toID, uint16 typeID, uint32 senderID, PyDict* data);
    // same notification to many characters (data cloned per receiver).  consumes `data`
    static void   Send(const std::vector<uint32>& toIDs, uint16 typeID, uint32 senderID, PyDict* data);
    // all members (online+offline) of a player corp whose corpRole|rolesAtAll intersects roleMask (0 = everyone).
    // directors always match a role mask.  consumes `data`
    static void   SendToCorp(uint32 corpID, int64 roleMask, uint16 typeID, uint32 senderID, PyDict* data);
    // all members of all corps in an alliance (roleMask as above).  consumes `data`
    static void   SendToAlliance(uint32 allianceID, int64 roleMask, uint16 typeID, uint32 senderID, PyDict* data);

    // group id (Notify::Groups) for a type, -1 when the type is not a client notification (eg. 125-129)
    static int8   GetGroup(uint16 typeID);
    static void   GetGroupTypes(uint8 groupID, std::vector<uint16>& into);

    // YAML (flow style) for the `data` column.  the client loads it with yaml.CSafeLoader
    static std::string ToYaml(PyRep* rep);

private:
    static void   AppendYaml(std::string& into, PyRep* rep);
    static void   AppendYamlString(std::string& into, const std::string& str);
};

#endif  // __NOTIFICATIONMGR_H__INCL__
