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
*/

#include "eve-server.h"
#include "EVE_Motd.h"

#include <boost/algorithm/string.hpp>

#include "Client.h"
#include "ConsoleCommands.h"
#include "EVEServerConfig.h"
#include "chat/LSCChannel.h"
#include "chat/LSCDB.h"
#include "chat/LSCService.h"
#include "fleet/FleetService.h"


PyRep *LSCChannelChar::Encode() const {
    ChannelJoinChannelCharsLine line;

    line.allianceID = m_allianceID;
    line.charID = m_charID;
    line.corpID = m_corpID;
    line.role = m_role;
    line.warFactionID = m_warFactionID;
    line.mode = m_mode;

    // extra is a list of charID, charName, charTypeID, unknown bool, unknown PyNone
    util_Row rs;
    rs.header.push_back("ownerID");
    rs.header.push_back("ownerName");
    rs.header.push_back("typeID");
    rs.line = new PyList();
    rs.line->AddItemInt( m_charID );
    rs.line->AddItemString( m_charName.c_str() );
    rs.line->AddItemInt( 1378 );    // fix this ... wtf is this??
    line.extra = rs.Encode();//m_extra;

    return line.Encode();
}

PyRep *LSCChannelMod::Encode() const {
    // header order: accessor, mode, untilWhen, originalMode, admin, reason.  the default entry's accessor is None
    PyList* line = new PyList(6);
        line->SetItem(0, m_accessor ? (PyRep*)new PyInt(m_accessor) : PyStatic.NewNone());
        line->SetItem(1, new PyInt(m_mode));
        line->SetItem(2, m_untilWhen ? (PyRep*)new PyLong(m_untilWhen) : PyStatic.NewNone());
        line->SetItem(3, (m_originalMode != LSC::Mode::chUnspecified) ? (PyRep*)new PyInt(m_originalMode) : PyStatic.NewNone());
        line->SetItem(4, m_admin.empty() ? PyStatic.NewNone() : (PyRep*)new PyWString(m_admin));
        line->SetItem(5, m_reason.empty() ? PyStatic.NewNone() : (PyRep*)new PyWString(m_reason));
    return line;
}

LSCChannel::LSCChannel(LSCService* svc, int32 channelID, LSC::Type type, uint32 ownerID, const char* displayName, const char* comparisonKey, std::string motd,
                       bool memberless, const char* password, bool mailingList, uint32 cspa, bool temporary, bool languageRestriction, int32 groupMessageID, int32 channelMessageID)
: m_service(svc),
  m_ownerID(ownerID),
  m_channelID(channelID),
  m_type(type),
  m_motd(motd),
  m_displayName(displayName==nullptr?"":displayName),
  m_comparisonKey(comparisonKey==nullptr?"":comparisonKey),
  m_memberless(memberless),
  m_password(password==nullptr?"":password),
  m_mailingList(mailingList),
  m_cspa(cspa),
  m_temporary(temporary),
  m_languageRestriction(languageRestriction),
  m_groupMessageID(groupMessageID),
  m_channelMessageID(channelMessageID)
{
    m_mode = LSC::Mode::chConversationalist;    // default mode '3' for enabling all speakers (till i figure out how to correctly set/change later)
    _log(LSC__CHANNELS, "Creating channel %u - \"%s\"", m_channelID, (m_displayName == "") ? ((m_comparisonKey == "") ? "null" : m_comparisonKey.c_str()) : m_displayName.c_str());
}

LSCChannel::~LSCChannel() {
    _log(LSC__CHANNELS, "Destroying channel %u - \"%s\"", m_channelID, (m_displayName == "") ? ((m_comparisonKey == "") ? "null" : m_comparisonKey.c_str()) : m_displayName.c_str());
}

void LSCChannel::GetChannelInfo(int32* channelID, uint32* ownerID, std::string& displayName, std::string& motd, std::string& comparisonKey, bool* memberless, std::string& password, bool* mailingList, uint32* cspa, uint32* temporary)
{
    *channelID = m_channelID;
    *ownerID = m_ownerID;
    displayName = m_displayName;
    motd = m_motd;
    comparisonKey = m_comparisonKey;
    *memberless = m_memberless;
    password = m_password;
    *mailingList = m_mailingList;
    *cspa = m_cspa;
    *temporary = m_temporary;
}

bool LSCChannel::JoinChannel(Client* pClient) {
    m_chars.insert(
        std::make_pair(
            pClient->GetCharacterID(),
            LSCChannelChar( this, pClient->GetCorporationID(), pClient->GetCharacterID(), pClient->GetCharName(), pClient->GetAllianceID(), pClient->GetWarFactionID(), pClient->GetAccountRole(), 0,\
            GetAccessMode(pClient))
        )
    );
    pClient->ChannelJoined( this );

    OnLSC_JoinChannel join;
        join.sender = _MakeSenderInfo(pClient);
        join.member_count = (int32)m_chars.size();
        join.channelID = EncodeID();

    MulticastTarget mct;
    std::map<uint32, LSCChannelChar>::iterator cur;
    cur = m_chars.begin();
    for( ; cur != m_chars.end(); ++cur )
        mct.characters.insert( cur->first );
    PyTuple *answer = join.Encode();
    sEntityList.Multicast( "OnLSC", GetTypeString(), &answer, mct );

    _log(LSC__CHANNELS, "%s Joined Channel %u - %s", pClient->GetName(), m_channelID, m_displayName.c_str());
    return true;
}

void LSCChannel::LeaveChannel(Client *pClient)
{
    if (sConsole.IsShutdown())
        return;

    if (m_chars.empty())
        return;

    uint32 charID = pClient->GetCharacterID();
    if (m_chars.find(charID) == m_chars.end())
        return;

    m_chars.erase(charID);

    MulticastTarget mct;
    std::map<uint32, LSCChannelChar>::iterator cur;
    cur = m_chars.begin();
    for(; cur != m_chars.end(); ++cur)
        mct.characters.insert( cur->first );

    OnLSC_LeaveChannel leave;
    leave.sender = _MakeSenderInfo(pClient);
    leave.member_count = (int32)m_chars.size();
    leave.channelID = EncodeID();

    PyTuple *answer = leave.Encode();
    sEntityList.Multicast("OnLSC", GetTypeString(), &answer, mct);

    _log(LSC__CHANNELS, "%s Left Channel %u - %s", pClient->GetName(), m_channelID, m_displayName.c_str());

    pClient->ChannelLeft(this);
}

void LSCChannel::Evacuate(Client * c) {
    OnLSC_DestroyChannel dc;

    dc.channelID = EncodeID();
    dc.member_count = 0;
    // a null client means the server is destroying the channel
    dc.sender = (c != nullptr ? _MakeSenderInfo(c) : _FakeSenderInfo());

    MulticastTarget mct;

    std::map<uint32, LSCChannelChar>::iterator cur;
    cur = m_chars.begin();
    for(; cur != m_chars.end(); cur++)
        mct.characters.insert(cur->first);

    PyTuple *answer = dc.Encode();
    sEntityList.Multicast("OnLSC", GetTypeString(), &answer, mct);

    // members must not keep a pointer to this channel once it is deleted
    for (auto charID : mct.characters) {
        Client* pClient = sEntityList.FindClientByCharID(charID);
        if (pClient != nullptr)
            pClient->ChannelLeft(this);
    }
    m_chars.clear();
}

namespace {
    /* the client takes a plain string as bytes:  a line past ascii (curly quotes and dashes, a player's accents, a motd.txt
     *  in utf-8) must go as unicode or it reads as "â€™"
     */
    void AsUnicode(PyTuple* answer, const std::string& message)
    {
        if (Motd::IsAscii(message))
            return;
        PyRep* args(answer->GetItem(4));
        if ((args != nullptr) and args->IsTuple() and (args->AsTuple()->size() > 0))
            args->AsTuple()->SetItem(0, new PyWString(message));
    }
}

void LSCChannel::SendMessage(Client * c, const char * message, bool self/*false*/) {
// to send system msgs, senderID should be 1 (system owner)

    MulticastTarget mct;
    if (self) {
        mct.characters.insert(c->GetCharacterID());
    } else {
        std::map<uint32, LSCChannelChar>::iterator itr = m_chars.begin();
        for(; itr != m_chars.end(); ++itr)
            mct.characters.insert( itr->first );
    }

    OnLSC_SendMessage sm;
    sm.sender = _MakeSenderInfo(c);
    sm.channelID = EncodeID();
    sm.message = message;
    sm.member_count = m_chars.size();

    PyTuple *answer = sm.Encode();
    AsUnicode(answer, message);
    sEntityList.Multicast("OnLSC", GetTypeString(), &answer, mct);
}

void LSCChannel::SendSystemMessage(const char * message) {
    MulticastTarget mct;
    for (auto cur : m_chars)
        mct.characters.insert(cur.first);
    if (mct.characters.empty())
        return;

    OnLSC_SendMessage sm;
    sm.sender = _FakeSenderInfo();
    sm.channelID = EncodeID();
    sm.message = message;
    sm.member_count = m_chars.size();

    PyTuple *answer = sm.Encode();
    AsUnicode(answer, message);
    sEntityList.Multicast("OnLSC", GetTypeString(), &answer, mct);
}

void LSCChannel::SendSystemMessageTo(Client* pClient, const std::string& message) {
    if ((pClient == nullptr) or message.empty())
        return;

    OnLSC_SendMessage sm;
    sm.sender = _FakeSenderInfo();
    sm.channelID = EncodeID();
    sm.message = message;
    sm.member_count = m_chars.size();

    PyTuple *answer = sm.Encode();
    AsUnicode(answer, message);
    pClient->SendNotification("OnLSC", GetTypeString(), &answer);
}

bool LSCChannel::IsJoined(uint32 charID) {
    return (m_chars.find(charID) != m_chars.end());
}

OnLSC_SenderInfo *LSCChannel::_MakeSenderInfo(Client *c) {
    OnLSC_SenderInfo *sender = new OnLSC_SenderInfo;
        sender->senderID = c->GetCharacterID();
        sender->senderName = c->GetName();
        sender->senderType = c->GetChar()->typeID();
        sender->corpID = c->GetCorporationID();
        sender->role = c->GetAccountRole();
        sender->corp_role = c->GetCorpRole();
    return sender;
}

OnLSC_SenderInfo *LSCChannel::_FakeSenderInfo() {
    OnLSC_SenderInfo *sender = new OnLSC_SenderInfo();
        sender->senderID = 1;
        sender->senderName = "EVE System";
        sender->senderType = 1;
        sender->corpID = 1;
        sender->role = 1;
        sender->corp_role = 1;
    return sender;
}

PyRep *LSCChannel::EncodeID() {
    switch (m_type) {
        case LSC::Type::global:
        case LSC::Type::corp:
        case LSC::Type::region:
        case LSC::Type::constellation:
        case LSC::Type::solarsystem:
        case LSC::Type::solarsystem2:
        case LSC::Type::fleet:
        case LSC::Type::wing:
        case LSC::Type::squad: {
            LSCChannelMultiDesc desc;
                desc.type = GetTypeString();
                desc.id = m_channelID;
            return desc.Encode();
        }
        case LSC::Type::character:
        case LSC::Type::alliance:
        case LSC::Type::warfaction:
        case LSC::Type::incursion:
        case LSC::Type::normal:
        case LSC::Type::custom: {
            return new PyInt(m_channelID);
        }
    }
    return new PyInt(m_channelID);
}

bool LSCChannel::IsOperator(uint32 charID) {
    std::map<uint32, LSCChannelMod>::iterator itr = m_mods.find(charID);
    if (itr == m_mods.end())
        return false;
    return (itr->second.GetMode() >= LSC::Mode::chOperator);
}

PyRep *LSCChannel::EncodeStaticChannel(Client* pClient, bool showPassword/*false*/, bool subscribed/*false*/) {
    ChannelInfoLine line;
        line.channelID = m_channelID;
        line.ownerID = m_ownerID;
        line.displayName = m_displayName;
        line.motd = m_motd;
        line.comparisonKey = m_comparisonKey;
        line.memberless = m_memberless;
        line.password = (showPassword ? m_password : "");
        line.mailingList = m_mailingList;
        line.cspa = m_cspa;
        line.temporary = m_temporary;
        line.languageRestriction = m_languageRestriction;
        line.mode = (int8)GetAccessMode(pClient);
        // static channels are always listed as subscribed
        line.subscribed = (subscribed or IsStaticChannel(m_channelID));
        line.groupMessageID = m_groupMessageID;
        line.channelMessageID = m_channelMessageID;
        line.estimatedMemberCount = m_chars.size();
    return line.Encode();
}

PyRep *LSCChannel::EncodeDynamicChannel(Client* pClient, bool showPassword/*false*/, bool subscribed/*false*/) {
    ChannelJoinChannelInfo info;
        info.channelID = m_channelID;
        info.comparisonKey = m_comparisonKey;
        info.cspa = m_cspa;
        info.displayName = m_displayName;
        info.mailingList = m_mailingList;
        info.memberless = m_memberless;
        info.motd = m_motd;
        info.ownerID = m_ownerID;
        info.password = (showPassword ? m_password : "");
        info.subscribed = (subscribed or IsStaticChannel(m_channelID));
        info.temporary = m_temporary;
        info.mode = (int8)GetAccessMode(pClient);
        info.languageRestriction = m_languageRestriction;
        info.groupMessageID = m_groupMessageID;
        info.channelMessageID = m_channelMessageID;
        info.estimatedMemberCount = m_chars.size();
    return info.Encode();
}

PyRep *LSCChannel::EncodeChannelMods()
{
    ChannelJoinChannelMods info;
    info.lines = new PyList();
    std::map<uint32, LSCChannelMod>::iterator itr = m_mods.begin();
    for(; itr != m_mods.end(); itr++)
        info.lines->AddItem( itr->second.Encode() );
    return info.Encode();
}

PyRep *LSCChannel::EncodeChannelChars() {
    /*
        [PyObjectData Name: util.Rowset]
          [PyDict 3 kvp]
            [PyString "header"]
            [PyList 6 items]
              [PyString "charID"]
              [PyString "corpID"]
              [PyString "allianceID"]
              [PyString "warFactionID"]
              [PyString "role"]
              [PyString "extra"]
            [PyString "RowClass"]
            [PyToken util.Row]
            [PyString "lines"]
            [PyList 84 items]
            */
    ChannelJoinChannelChars info;
    info.lines = new PyList();
    std::map<uint32, LSCChannelChar>::iterator itr = m_chars.begin();
    for(; itr != m_chars.end(); itr++)
        info.lines->AddItem( itr->second.Encode() );
    return info.Encode();
}

PyRep *LSCChannel::EncodeEmptyChannelChars() {
    ChannelJoinChannelChars info;
    info.lines = new PyList();
    return info.Encode();
}

const char *LSCChannel::GetTypeString() {
    switch(m_type) {
        case LSC::Type::normal:         return "normal";
        case LSC::Type::custom:         return "private";
        case LSC::Type::corp:           return "corpid";
        case LSC::Type::solarsystem2:   return "solarsystemid2";
        case LSC::Type::region:         return "regionid";
        case LSC::Type::constellation:  return "constellationid";
        case LSC::Type::global:         return "global";
        case LSC::Type::alliance:       return "allianceid";
        case LSC::Type::fleet:          return "fleetid";
        case LSC::Type::squad:          return "squadid";
        case LSC::Type::wing:           return "wingid";
        case LSC::Type::warfaction:     return "warfactionid";
        case LSC::Type::character:      return "charid";
        case LSC::Type::solarsystem:    return "solarsystemid";
        case LSC::Type::incursion:      return "incursion";
        default:                        return "unknown";
    }
}

/*
 * access control
 */
bool LSCChannel::IsUserChannel() const
{
    return (m_channelID >= LSCService::BASE_CHANNEL_ID);
}

int32 LSCChannel::GetEntryMode(uint32 accessor)
{
    std::map<uint32, LSCChannelMod>::iterator itr = m_mods.find(accessor);
    if (itr == m_mods.end())
        return LSC::Mode::chUnspecified;
    return itr->second.m_mode;
}

bool LSCChannel::HasIntID() const
{
    switch (m_type) {
        case LSC::Type::character:
        case LSC::Type::alliance:
        case LSC::Type::warfaction:
        case LSC::Type::incursion:
        case LSC::Type::normal:
        case LSC::Type::custom:
            return true;
        default:
            return false;
    }
}

int32 LSCChannel::GetAccessMode(Client* pClient)
{
    if (pClient == nullptr)
        return LSC::Mode::chUnspecified;
    if ((pClient->GetAccountRole() & (Acct::Role::CHTADMINISTRATOR | Acct::Role::GMH)) != 0)
        return LSC::Mode::chCreator;

    ExpireMods();

    uint32 charID = pClient->GetCharacterID();
    uint32 corpID = pClient->GetCorporationID();
    uint32 allianceID = pClient->GetAllianceID();
    int32 mode(LSC::Mode::chUnspecified);

    std::map<uint32, LSCChannelMod>::iterator itr = m_mods.find(charID);
    if (HasIntID()) {
        // lscengine.GetAccessInfo: owner, then the char's entry, then the most restrictive corp/alliance entry, then the default
        if (m_ownerID == charID)
            return LSC::Mode::chCreator;
        if (itr != m_mods.end()) {
            mode = itr->second.m_mode;
        } else {
            bool found(false);
            std::map<uint32, LSCChannelMod>::iterator oItr = m_mods.find(corpID);
            if (oItr != m_mods.end()) {
                mode = oItr->second.m_mode;
                found = true;
            }
            if (allianceID != 0) {
                oItr = m_mods.find(allianceID);
                if ((oItr != m_mods.end()) and (!found or (oItr->second.m_mode < mode))) {
                    mode = oItr->second.m_mode;
                    found = true;
                }
            }
            if (!found) {
                oItr = m_mods.find(0);
                if (oItr != m_mods.end())
                    mode = oItr->second.m_mode;
            }
        }
    } else {
        // system channels (local, corp, fleet...) are open to everyone that may be in them; gags/kicks still count
        mode = ((itr != m_mods.end()) ? itr->second.m_mode : (int32)LSC::Mode::chConversationalist);
    }

    // corp ChatManager runs his corp and alliance channels
    if ((pClient->GetCorpRole() & Corp::Role::ChatManager) == Corp::Role::ChatManager) {
        if (((m_type == LSC::Type::corp) and ((int32)corpID == m_channelID))
        or ((m_type == LSC::Type::alliance) and (allianceID != 0) and ((int32)allianceID == m_channelID)))
            mode = std::max(mode, (int32)LSC::Mode::chOperator);
    }
    // and the fleet boss/commander the fleet channels
    if ((m_type == LSC::Type::fleet) or (m_type == LSC::Type::wing) or (m_type == LSC::Type::squad)) {
        Character* pChar = pClient->GetChar().get();
        if ((pChar != nullptr) and (pChar->fleetID() != 0) and (sFltSvc.IsBoss(pChar) or (pChar->fleetRole() == Fleet::Role::FleetLeader)))
            mode = std::max(mode, (int32)LSC::Mode::chOperator);
    }

    return mode;
}

void LSCChannel::PersistMod(uint32 accessor)
{
    // private conversations and channels the server makes on demand (local, corp, fleet...) are not kept
    if (m_temporary or !HasIntID())
        return;

    std::map<uint32, LSCChannelMod>::iterator itr = m_mods.find(accessor);
    if (itr == m_mods.end()) {
        LSCDB::DeleteChannelMod(m_channelID, accessor);
    } else {
        LSCDB::SaveChannelMod(m_channelID, itr->second);
    }
}

void LSCChannel::ExpireMods(Client* pAdmin/*nullptr*/)
{
    int64 now = (int64)GetFileTimeNow();
    std::vector<uint32> expired;
    for (auto& cur : m_mods)
        if ((cur.second.m_untilWhen != 0) and (cur.second.m_untilWhen <= now))
            expired.push_back(cur.first);

    for (auto accessor : expired) {
        std::map<uint32, LSCChannelMod>::iterator itr = m_mods.find(accessor);
        int32 newMode = itr->second.m_originalMode;
        if (newMode == LSC::Mode::chUnspecified) {
            m_mods.erase(itr);
        } else {
            itr->second.m_mode = newMode;
            itr->second.m_untilWhen = 0;
            itr->second.m_originalMode = LSC::Mode::chUnspecified;
            itr->second.m_admin.clear();
            itr->second.m_reason.clear();
        }
        PersistMod(accessor);

        // AccessControl(accessor, mode, untilWhen, originalMode, reason, admin)
        PyTuple* args = new PyTuple(6);
            args->SetItem(0, accessor ? (PyRep*)new PyInt(accessor) : PyStatic.NewNone());
            args->SetItem(1, new PyInt(newMode));
            args->SetItem(2, PyStatic.NewNone());
            args->SetItem(3, PyStatic.NewNone());
            args->SetItem(4, PyStatic.NewNone());
            args->SetItem(5, PyStatic.NewNone());
        Broadcast("AccessControl", pAdmin, args);

        std::map<uint32, LSCChannelChar>::iterator cItr = m_chars.find(accessor);
        if (cItr != m_chars.end())
            cItr->second.SetMode(newMode == LSC::Mode::chUnspecified ? (int32)LSC::Mode::chConversationalist : newMode);
    }
}

void LSCChannel::SetAccess(uint32 accessor, int32 mode, int64 untilWhen, const std::string& reason, Client* pAdmin)
{
    ExpireMods(pAdmin);

    // a timed entry remembers what to go back to; a second gag keeps the first one's original
    int32 originalMode(LSC::Mode::chUnspecified);
    std::map<uint32, LSCChannelMod>::iterator itr = m_mods.find(accessor);
    if (itr != m_mods.end())
        originalMode = (itr->second.m_untilWhen ? itr->second.m_originalMode : itr->second.m_mode);

    // ungag/unkick (NOTSPECIFIED with a past time) ends a timed entry: back to the mode it replaced
    if ((mode == LSC::Mode::chUnspecified) and (itr != m_mods.end()) and (itr->second.m_untilWhen != 0)) {
        mode = itr->second.m_originalMode;
        untilWhen = 0;
    }

    std::string admin;
    if (pAdmin != nullptr)
        admin = pAdmin->GetName();

    if (mode == LSC::Mode::chUnspecified) {
        if (accessor == 0) {
            // a blocked default stays an entry, so it isn't taken for a channel from before access lists (made public at load)
            m_mods[0] = LSCChannelMod(0, LSC::Mode::chUnspecified);
        } else if (itr != m_mods.end()) {
            m_mods.erase(itr);
        }
        untilWhen = 0;
        originalMode = LSC::Mode::chUnspecified;
    } else {
        if (untilWhen == 0)
            originalMode = LSC::Mode::chUnspecified;
        m_mods[accessor] = LSCChannelMod(accessor, mode, untilWhen, originalMode, (untilWhen ? admin : ""), (untilWhen ? reason : ""));
    }
    PersistMod(accessor);

    // AccessControl(accessor, mode, untilWhen, originalMode, reason, admin)
    PyTuple* args = new PyTuple(6);
        args->SetItem(0, accessor ? (PyRep*)new PyInt(accessor) : PyStatic.NewNone());
        args->SetItem(1, new PyInt(mode));
        args->SetItem(2, untilWhen ? (PyRep*)new PyLong(untilWhen) : PyStatic.NewNone());
        args->SetItem(3, (originalMode != LSC::Mode::chUnspecified) ? (PyRep*)new PyInt(originalMode) : PyStatic.NewNone());
        args->SetItem(4, reason.empty() ? PyStatic.NewNone() : (PyRep*)new PyWString(reason));
        args->SetItem(5, admin.empty() ? PyStatic.NewNone() : (PyRep*)new PyWString(admin));
    Broadcast("AccessControl", pAdmin, args);

    // members that can't listen any more are put out.  a changed default only affects who may join
    std::vector<Client*> kicked;
    for (auto& cur : m_chars) {
        if ((accessor != 0) and (cur.first != accessor) and (cur.second.GetCorpID() != accessor) and (cur.second.GetAllianceID() != accessor))
            continue;
        Client* pClient = sEntityList.FindClientByCharID(cur.first);
        if (pClient == nullptr)
            continue;
        int32 newMode = GetAccessMode(pClient);
        cur.second.SetMode(newMode);
        if ((accessor != 0) and (newMode < LSC::Mode::chListener))
            kicked.push_back(pClient);
    }
    for (auto pClient : kicked) {
        // KickUser(charID, untilWhen, reason)
        PyTuple* kArgs = new PyTuple(3);
            kArgs->SetItem(0, new PyInt(pClient->GetCharacterID()));
            kArgs->SetItem(1, untilWhen ? (PyRep*)new PyLong(untilWhen) : PyStatic.NewNone());
            kArgs->SetItem(2, reason.empty() ? PyStatic.NewNone() : (PyRep*)new PyWString(reason));
        Broadcast("KickUser", pAdmin, kArgs);
        LeaveChannel(pClient);
    }
}

void LSCChannel::SendOnLSC(const std::set<uint32>& charIDs, const char* method, Client* pWho, PyTuple* args)
{
    if (charIDs.empty()) {
        PySafeDecRef(args);
        return;
    }

    NotifyOnLSC notify;
        notify.channelID = EncodeID();
        notify.member_count = (int32)m_chars.size();
        notify.method = method;
        notify.sender = (pWho != nullptr ? _MakeSenderInfo(pWho) : _FakeSenderInfo());
        notify.arguments = (args != nullptr ? args : new PyTuple(0));

    MulticastTarget mct;
    for (auto cur : charIDs)
        mct.characters.insert(cur);
    // notify releases its arguments (Encode gave answer its own reference)
    PyTuple* answer = notify.Encode();
    sEntityList.Multicast("OnLSC", GetTypeString(), &answer, mct);
}

void LSCChannel::Broadcast(const char* method, Client* pWho, PyTuple* args)
{
    std::set<uint32> charIDs;
    for (auto& cur : m_chars)
        charIDs.insert(cur.first);
    SendOnLSC(charIDs, method, pWho, args);
}

void LSCChannel::Rename(const std::string& name, Client* pWho)
{
    m_displayName = name;
    m_comparisonKey = name;
    boost::algorithm::to_lower(m_comparisonKey);
    m_comparisonKey.erase(std::remove(m_comparisonKey.begin(), m_comparisonKey.end(), ' '), m_comparisonKey.end());
    Broadcast("RenameChannel", pWho, new_tuple(new PyWString(name)));
}

void LSCChannel::ChangeMOTD(const std::string& motd, Client* pWho)
{
    m_motd = motd;
    Broadcast("SetChannelMOTD", pWho, new_tuple(new PyWString(motd)));
}

void LSCChannel::ChangeLanguageRestriction(bool restricted, Client* pWho)
{
    m_languageRestriction = restricted;
    Broadcast("SetChannelLanguageRestriction", pWho, new_tuple(new PyBool(restricted)));
}

void LSCChannel::ChangePassword(const std::string& password, Client* pWho)
{
    m_password = password;

    std::set<uint32> privileged, others;
    for (auto& cur : m_chars) {
        Client* pClient = sEntityList.FindClientByCharID(cur.first);
        if ((pClient != nullptr) and (GetAccessMode(pClient) >= LSC::Mode::chOperator)) {
            privileged.insert(cur.first);
        } else {
            others.insert(cur.first);
        }
    }
    SendOnLSC(privileged, "SetChannelPassword", pWho, new_tuple(new PyWString(password)));
    SendOnLSC(others, "SetChannelPassword", pWho, new_tuple(new PyWString(std::string(password.empty() ? "" : "*"))));
}

void LSCChannel::ChangeCreator(uint32 ownerID, Client* pWho)
{
    m_ownerID = ownerID;
    Broadcast("SetChannelCreator", pWho, new_tuple(new PyInt(ownerID)));
}

void LSCChannel::ChangeMemberless(bool memberless, Client* pWho)
{
    m_memberless = memberless;
    // the client rebuilds its member list from the rowset when the channel goes back to immediate mode
    Broadcast("SetChannelMemberless", pWho, new_tuple(new PyBool(memberless), EncodeChannelChars()));
}

void LSCChannel::LogChat(LSCChannel* pChan, Client* pClient, const std::string& message)
{
    time_t now = time(nullptr);
    tm t;
#ifdef _WIN32
    localtime_s(&t, &now);
#else
    localtime_r(&now, &t);
#endif
    char date[16], clock[16];
    strftime(date, sizeof(date), "%Y-%m-%d", &t);
    strftime(clock, sizeof(clock), "%H:%M:%S", &t);

    std::string path = sConfig.files.logDir;
    if (!path.empty() and (path.back() != '/') and (path.back() != '\\'))
        path += "/";
    path += "chat_";
    path += date;
    path += ".log";

    FILE* file = fopen(path.c_str(), "a");
    if (file == nullptr) {
        _log(LSC__ERROR, "Could not open chat log %s", path.c_str());
        return;
    }

    std::string channel = pChan->GetDisplayName();
    if (channel.empty() or !pChan->HasIntID()) {
        channel = pChan->GetTypeString();
        channel += ":";
        channel += std::to_string(pChan->GetChannelID());
    }
    fprintf(file, "[%s] %s %s> %s\n", clock, channel.c_str(), pClient->GetName(), message.c_str());
    fclose(file);
}
