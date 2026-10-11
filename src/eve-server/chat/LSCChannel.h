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

#ifndef __LSCCHANNEL_H_INCL__
#define __LSCCHANNEL_H_INCL__

#include "EntityList.h"
#include "EVE_LSC.h"
#include "packets/LSCPkts.h"

class PyRep;
class LSCService;
class LSCChannel;

class LSCChannelChar {
public:
    LSCChannelChar(LSCChannel *chan, uint32 corpID, uint32 charID, std::string charName, uint32 allianceID, uint32 warFactionID, int64 role, uint32 extra, int32 mode) :
      m_parent(chan),
      m_corpID(corpID),
      m_charID(charID),
      m_charName(charName),
      m_allianceID(allianceID),
      m_warFactionID(warFactionID),
      m_role(role),
      m_extra(extra),
      m_mode(mode) { }

    ~LSCChannelChar() { }
    PyRep *Encode() const;

    uint32 GetCorpID() const                            { return m_corpID; }
    uint32 GetAllianceID() const                        { return m_allianceID; }
    void SetMode(int32 mode)                            { m_mode = mode; }

protected:
    LSCChannel *m_parent;
    uint32 m_corpID;
    uint32 m_charID;
    std::string m_charName;
    uint32 m_allianceID;
    uint32 m_warFactionID;
    int64 m_role;
    uint32 m_extra;
    int32 m_mode;
};

/* one access control entry.  accessor is a char, corp or alliance id; 0 is the channel default (None to the client).
 *  untilWhen (filetime, 0 = permanent) entries go back to originalMode when they run out (gags, kicks)
 */
class LSCChannelMod {
public:
    LSCChannelMod(uint32 accessor = 0, int32 mode = LSC::Mode::chUnspecified, int64 untilWhen = 0,
                  int32 originalMode = LSC::Mode::chUnspecified, std::string admin = "", std::string reason = "") :
      m_accessor(accessor),
      m_mode(mode),
      m_untilWhen(untilWhen),
      m_originalMode(originalMode),
      m_admin(admin),
      m_reason(reason) { }

    // one line of the channel's acl rowset (accessor, mode, untilWhen, originalMode, admin, reason)
    PyRep* Encode() const;

    int32 GetMode() const                               { return m_mode; }

    uint32 m_accessor;
    int32 m_mode;
    int64 m_untilWhen;
    int32 m_originalMode;
    std::string m_admin;
    std::string m_reason;
};

class LSCChannel {
public:
    LSCChannel(
        LSCService* svc, int32 channelID, LSC::Type type, uint32 ownerID, const char* displayName, const char* comparisonKey, std::string motd,
        bool memberless, const char* password, bool mailingList, uint32 cspa, bool temporary, bool languageRestriction, int32 groupMessageID, int32 channelMessageID
        );
    ~LSCChannel();

    PyRep *EncodeID();
    // the password is only sent when showPassword (owner/operator/GM).  mode is pClient's access mode
    PyRep *EncodeStaticChannel(Client* pClient, bool showPassword = false, bool subscribed = false);
    PyRep *EncodeDynamicChannel(Client* pClient, bool showPassword = false, bool subscribed = false);
    PyRep *EncodeChannelMods();
    PyRep *EncodeChannelChars();
    PyRep *EncodeEmptyChannelChars();

    const char *GetTypeString();
    bool JoinChannel(Client *pClient);
    void LeaveChannel(Client* pClient);
    bool IsJoined(uint32 charID);
    // true when charID has operator (or creator) mode in this channel's access list
    bool IsOperator(uint32 charID);
    bool HasPassword() const                            { return !m_password.empty(); }

    /* access control (mirrors the client's lscengine.GetAccessInfo, plus the server side extras:
     *  corp ChatManager is operator in his corp/alliance channel, the fleet boss/commander in fleet channels)
     */
    int32 GetAccessMode(Client* pClient);
    bool CanSpeak(Client* pClient)                      { return (GetAccessMode(pClient) >= LSC::Mode::chSpeaker); }
    // sets (or with chUnspecified, removes) an entry, tells the members, kicks the ones who lost access
    void SetAccess(uint32 accessor, int32 mode, int64 untilWhen, const std::string& reason, Client* pAdmin);
    // adds an entry without telling anyone (startup, new channels)
    void LoadMod(const LSCChannelMod& mod)              { m_mods[mod.m_accessor] = mod; }
    bool HasDefaultMod()                                { return (m_mods.find(0) != m_mods.end()); }
    // mode of the entry for accessor (0 = default), chUnspecified when there is none
    int32 GetEntryMode(uint32 accessor);
    // restores entries whose time ran out
    void ExpireMods(Client* pAdmin = nullptr);

    // OnLSC(channelID, count, method, who, args) to every member.  consumes args
    void Broadcast(const char* method, Client* pWho, PyTuple* args);

    // config changes, each told to the members
    void Rename(const std::string& name, Client* pWho);
    void ChangeMOTD(const std::string& motd, Client* pWho);
    void ChangeLanguageRestriction(bool restricted, Client* pWho);
    // only privileged members get the new password, the others "*" (or "" when cleared)
    void ChangePassword(const std::string& password, Client* pWho);
    void ChangeCreator(uint32 ownerID, Client* pWho);
    void ChangeMemberless(bool memberless, Client* pWho);

    // private (temporary) channels can only be joined by their owner and the characters invited to them
    void AddInvite(uint32 charID)                       { m_invited.insert(charID); }
    bool IsInvited(uint32 charID)                       { return (m_invited.find(charID) != m_invited.end()); }
    // player created channels (not the server's static ones)
    bool IsUserChannel() const;
    // channel ids that are ints to the client (the rest are ((type, id),) tuples)
    bool HasIntID() const;

    void Evacuate(Client * c);
    void SendMessage(Client * c, const char * message, bool self = false);
    // a message from "EVE System" to every member (server broadcasts and notices)
    void SendSystemMessage(const char * message);
    void SendServerMOTD(Client* pClient);

    // world.chatLogs: append a line to <logDir>/chat_YYYY-MM-DD.log
    static void LogChat(LSCChannel* pChan, Client* pClient, const std::string& message);

    void GetChannelInfo(int32 * channelID, uint32 * ownerID, std::string &displayName, std::string &motd, std::string &comparisonKey,
            bool * memberless, std::string &password, bool * mailingList, uint32 * cspa, uint32 * temporary);

    static OnLSC_SenderInfo *_MakeSenderInfo(Client *from);

    void        SetDisplayName(std::string displayName) { m_displayName = displayName; }
    void                SetMOTD(std::string motd)       { m_motd = motd; }
    void                SetMemberless(bool memberless)  { m_memberless = memberless; }
    void              SetPassword(std::string password) { m_password = password; }


    bool                GetMemberless()                 { return m_memberless; }
    bool                GetMailingList()                { return m_mailingList; }
    bool                GetTemporary()                  { return m_temporary; }
    bool                GetLanguageRestriction()        { return m_languageRestriction; }
    int32               GetGrpMsgID()                   { return m_groupMessageID; }
    int32               GetChMsgID()                    { return m_channelMessageID; }
    uint16              GetCSPA()                       { return m_cspa; }
    uint32              GetOwnerID()                    { return m_ownerID; }
    int32               GetChannelID()                  { return m_channelID; }
    uint32              GetMemberCount()                { return (uint32)m_chars.size(); }
    LSC::Type           GetType()                       { return m_type; }
    LSC::Mode           GetMode()                       { return m_mode; }
    std::string         GetDisplayName()                { return m_displayName; }
    std::string         GetMOTD()                       { return m_motd; }
    std::string         GetComparisonKey()              { return m_comparisonKey; }
    std::string         GetPassword()                   { return m_password; }

protected:
    LSCService *const   m_service;    //we do not own this

    LSC::Type           m_type;
    LSC::Mode           m_mode;
    // memberless - true = estimate member count, send estimatedMemberCount in packet.  false = actual memberList.count()   (5m refresh in client)
    // non-npc corp, fleet, and k-space local are always memberless=false
    bool                m_memberless;
    bool                m_mailingList;
    bool                m_temporary;
    bool                m_languageRestriction;
    int32               m_groupMessageID;
    int32               m_channelMessageID;
    uint16              m_cspa;
    uint32              m_ownerID;
    int32               m_channelID;            // ids < 0 are automatic conversationalist mode (or creator) and invite only (per client)
    std::string         m_displayName;
    std::string         m_motd;
    std::string         m_comparisonKey;
    std::string         m_password;

    std::map<uint32, LSCChannelMod> m_mods;     // key = accessor, 0 = default
    std::map<uint32, LSCChannelChar> m_chars;
    std::set<uint32> m_invited;

    OnLSC_SenderInfo *_FakeSenderInfo();
    // saves (or deletes) one entry for channels that outlive a restart
    void PersistMod(uint32 accessor);
    // sends OnLSC to the given characters
    void SendOnLSC(const std::set<uint32>& charIDs, const char* method, Client* pWho, PyTuple* args);
};

#endif
