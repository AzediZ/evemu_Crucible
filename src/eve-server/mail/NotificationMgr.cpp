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

#include "eve-server.h"

#include <cmath>

#include "EVE_Mail.h"
#include "Client.h"
#include "EntityList.h"
#include "mail/MailDB.h"
#include "mail/NotificationMgr.h"

namespace {
namespace G = Notify::Groups;

// notification type -> group, copied from the client's notificationUtil.groupTypes.  type 24 is unused.
// types not listed here (eg. the internal corp broadcast codes 125-129) are never stored.
const std::map<uint16, uint8> s_typeGroups = {
    { 1, G::Old}, { 2, G::Corp}, { 3, G::Corp}, { 4, G::Bills}, { 5, G::War}, { 6, G::War},
    { 7, G::War}, { 8, G::War}, { 9, G::Bills}, {10, G::Bills}, {11, G::Bills}, {12, G::Bills},
    {13, G::Bills}, {14, G::Misc}, {15, G::Misc}, {16, G::Corp}, {17, G::Corp}, {18, G::Corp},
    {19, G::Corp}, {20, G::Corp}, {21, G::Corp}, {22, G::Corp}, {23, G::Corp}, {25, G::Corp},
    {26, G::Corp}, {27, G::War}, {28, G::War}, {29, G::War}, {30, G::War}, {31, G::War},
    {32, G::Misc}, {33, G::Misc}, {34, G::Misc}, {35, G::Misc}, {36, G::Misc}, {37, G::Sov},
    {38, G::Sov}, {39, G::Sov}, {40, G::Sov}, {41, G::Sov}, {42, G::Sov}, {43, G::Sov},
    {44, G::Sov}, {45, G::Structures}, {46, G::Sov}, {47, G::Sov}, {48, G::Sov}, {49, G::Structures},
    {50, G::Bills}, {51, G::Misc}, {52, G::Misc}, {53, G::Misc}, {54, G::Misc}, {55, G::Misc},
    {56, G::Misc}, {57, G::Misc}, {58, G::War}, {59, G::War}, {60, G::War}, {61, G::War},
    {62, G::War}, {63, G::War}, {64, G::War}, {65, G::War}, {66, G::Agents}, {67, G::Misc},
    {68, G::Misc}, {69, G::Agents}, {70, G::Agents}, {71, G::Agents}, {72, G::Agents}, {73, G::Agents},
    {74, G::Agents}, {75, G::Structures}, {76, G::Structures}, {77, G::Structures}, {78, G::Structures}, {79, G::Structures},
    {80, G::Structures}, {81, G::War}, {82, G::War}, {83, G::War}, {84, G::War}, {85, G::Corp},
    {86, G::Sov}, {87, G::Sov}, {88, G::Sov}, {89, G::Contacts}, {90, G::Contacts}, {91, G::Misc},
    {92, G::Corp}, {93, G::Structures}, {94, G::Structures}, {95, G::Structures}, {96, G::War}, {97, G::War},
};
}

int8 NotificationMgr::GetGroup(uint16 typeID)
{
    std::map<uint16, uint8>::const_iterator itr = s_typeGroups.find(typeID);
    if (itr == s_typeGroups.end())
        return -1;
    return itr->second;
}

void NotificationMgr::GetGroupTypes(uint8 groupID, std::vector<uint16>& into)
{
    for (auto cur : s_typeGroups)
        if (cur.second == groupID)
            into.push_back(cur.first);
}

uint32 NotificationMgr::Send(uint32 toID, uint16 typeID, uint32 senderID, PyDict* data)
{
    if (data == nullptr)
        data = new PyDict();

    if ((GetGroup(typeID) < 0) or (!IsCharacterID(toID))) {
        _log(MAIL__WARNING, "NotificationMgr::Send() - dropping type %u to %u (not a client notification or not a character)", typeID, toID);
        PyDecRef(data);
        return 0;
    }

    int64 created = (int64)GetFileTimeNow();
    uint32 notifyID = MailDB::StoreNotification(toID, typeID, senderID, created, ToYaml(data));
    if (notifyID == 0) {
        PyDecRef(data);
        return 0;
    }

    Client* pClient = sEntityList.FindClientByCharID(toID);
    if (pClient == nullptr) {
        PyDecRef(data);
        return notifyID;
    }

    // OnNotificationReceived(notificationID, typeID, senderID, created, data={})
    OnNotify onn;
        onn.notifyID = notifyID;
        onn.typeID = typeID;
        onn.senderID = senderID;
        onn.created = created;
        onn.data = data;    // onn releases it
    PyTuple* tuple = onn.Encode();
    pClient->SendNotification("OnNotificationReceived", "clientID", &tuple);

    return notifyID;
}

void NotificationMgr::Send(const std::vector<uint32>& toIDs, uint16 typeID, uint32 senderID, PyDict* data)
{
    if (data == nullptr)
        data = new PyDict();

    std::set<uint32> sent;
    for (auto cur : toIDs) {
        if (!sent.insert(cur).second)
            continue;
        Send(cur, typeID, senderID, data->Clone()->AsDict());
    }

    PyDecRef(data);
}

void NotificationMgr::SendToCorp(uint32 corpID, int64 roleMask, uint16 typeID, uint32 senderID, PyDict* data)
{
    if (!IsPlayerCorp(corpID)) {
        PySafeDecRef(data);
        return;
    }

    std::vector<uint32> toIDs;
    MailDB::GetCorpMembersWithRoles(corpID, roleMask, toIDs);
    Send(toIDs, typeID, senderID, data);
}

void NotificationMgr::SendToAlliance(uint32 allianceID, int64 roleMask, uint16 typeID, uint32 senderID, PyDict* data)
{
    if (!IsAlliance(allianceID)) {
        PySafeDecRef(data);
        return;
    }

    std::vector<uint32> toIDs;
    MailDB::GetAllianceMembersWithRoles(allianceID, roleMask, toIDs);
    Send(toIDs, typeID, senderID, data);
}

std::string NotificationMgr::ToYaml(PyRep* rep)
{
    std::string yaml;
    if (rep == nullptr) {
        yaml = "{}";
        return yaml;
    }
    AppendYaml(yaml, rep);
    return yaml;
}

void NotificationMgr::AppendYamlString(std::string& into, const std::string& str)
{
    // double-quoted scalar.  utf-8 passes thru untouched
    into += '"';
    for (unsigned char c : str) {
        switch (c) {
            case '\\': into += "\\\\"; break;
            case '"':  into += "\\\""; break;
            case '\n': into += "\\n";  break;
            case '\r': into += "\\r";  break;
            case '\t': into += "\\t";  break;
            default: {
                if ((c < 0x20) or (c == 0x7f)) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\x%02x", c);
                    into += buf;
                } else {
                    into += (char)c;
                }
            }
        }
    }
    into += '"';
}

void NotificationMgr::AppendYaml(std::string& into, PyRep* rep)
{
    if ((rep == nullptr) or rep->IsNone()) {
        into += "null";
    } else if (rep->IsBool()) {
        into += (rep->AsBool()->value() ? "true" : "false");
    } else if (rep->IsInt()) {
        into += std::to_string(rep->AsInt()->value());
    } else if (rep->IsLong()) {
        into += std::to_string(rep->AsLong()->value());
    } else if (rep->IsFloat()) {
        double val = rep->AsFloat()->value();
        if (std::isnan(val)) {
            into += ".nan";
        } else if (std::isinf(val)) {
            into += (val > 0 ? ".inf" : "-.inf");
        } else {
            // the yaml float resolver needs a '.' in the number
            char buf[40];
            snprintf(buf, sizeof(buf), "%.17g", val);
            std::string num(buf);
            size_t ePos = num.find_first_of("eE");
            if (num.find('.') == std::string::npos) {
                if (ePos == std::string::npos) {
                    num += ".0";
                } else {
                    num.insert(ePos, ".0");
                }
            }
            into += num;
        }
    } else if (rep->IsString()) {
        AppendYamlString(into, rep->AsString()->content());
    } else if (rep->IsWString()) {
        AppendYamlString(into, rep->AsWString()->content());
    } else if (rep->IsToken()) {
        AppendYamlString(into, rep->AsToken()->content());
    } else if (rep->IsBuffer()) {
        const Buffer& buf = rep->AsBuffer()->content();
        AppendYamlString(into, std::string(buf.begin<char>(), buf.end<char>()));
    } else if (rep->IsDict()) {
        into += '{';
        bool first = true;
        for (PyDict::const_iterator itr = rep->AsDict()->begin(); itr != rep->AsDict()->end(); ++itr) {
            if (!first)
                into += ", ";
            first = false;
            AppendYaml(into, itr->first);
            into += ": ";
            AppendYaml(into, itr->second);
        }
        into += '}';
    } else if (rep->IsList()) {
        into += '[';
        PyList* list = rep->AsList();
        for (size_t i = 0; i < list->size(); ++i) {
            if (i)
                into += ", ";
            AppendYaml(into, list->GetItem(i));
        }
        into += ']';
    } else if (rep->IsTuple()) {
        into += '[';
        PyTuple* tuple = rep->AsTuple();
        for (size_t i = 0; i < tuple->size(); ++i) {
            if (i)
                into += ", ";
            AppendYaml(into, tuple->GetItem(i));
        }
        into += ']';
    } else if (rep->IsObject() and rep->AsObject()->arguments()->IsDict()) {
        // eg. util.KeyVal.  the safe loader can't build python objects, so send its attributes
        AppendYaml(into, rep->AsObject()->arguments());
    } else {
        _log(MAIL__WARNING, "NotificationMgr::ToYaml() - unsupported type %s, sending null", rep->TypeString());
        into += "null";
    }
}
