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
    Author:     Zhur, Aknor Jaden
    Rewrite:    Allan  (incomplete)
*/

#include "eve-server.h"

#include <boost/algorithm/string.hpp>

#include "ConsoleCommands.h"
#include "admin/CommandDispatcher.h"
#include "admin/SlashService.h"
#include "account/AccountService.h"
#include "mail/MailMgr.h"
#include "chat/LSCService.h"
#include "fleet/FleetService.h"
#include "services/ServiceManager.h"


/** @todo
 * LSC system todo list...
 * to implement...
 *   channel password
 *   private chat invite
 *   *more*
 *
 * to fix...
 *   channelID system
 *   *more*
 *
 * fleet shit...
 *BOSS/FC/WC/SC should be able to moderate text in their respective fleet chat as needed.
 * They should have the power to dictate text color of their type, and silence people who shouldn't be talking..
 *
 */

// Set the base (minimum) and maximum numbers for any user-created chat channel.
const int32 LSCService::BASE_CHANNEL_ID = 2100000000;      //trial accts are spam-restricted to 1m input buffer when channelID < 2100000000
const uint32 LSCService::MAX_CHANNEL_ID = 0xFFFFFFFF;

LSCService::LSCService(EVEServiceManager &mgr, CommandDispatcher* cd) :
    Service("LSC", eAccessLevel_Location),
    m_commandDispatch(cd)
{
    this->m_slash = mgr.Lookup <SlashService>("slash");

    //make sure you edit the header file too
    this->Add("GetChannels", &LSCService::GetChannels);
    this->Add("GetRookieHelpChannel", &LSCService::GetRookieHelpChannel);
    this->Add("JoinChannels", &LSCService::JoinChannels);
    // password join path (lscengine TryOpenChannel)
    this->Add("JoinChannel", static_cast <PyResult (LSCService::*)(PyCallArgs& call, PyInt* channelID)> (&LSCService::JoinChannel));
    this->Add("JoinChannel", static_cast <PyResult (LSCService::*)(PyCallArgs& call, PyTuple* channelID)> (&LSCService::JoinChannel));
    this->Add("LeaveChannels", static_cast <PyResult (LSCService::*)(PyCallArgs& call, PyList* channels, PyBool* usubscribe, PyLong* role)> (&LSCService::LeaveChannels));
    this->Add("LeaveChannels", static_cast <PyResult (LSCService::*)(PyCallArgs& call, PyList* channels, PyInt* usubscribe, PyLong* role)> (&LSCService::LeaveChannels));
    this->Add("LeaveChannel", &LSCService::LeaveChannel);
    this->Add("CreateChannel", &LSCService::CreateChannel);
    this->Add("Configure", &LSCService::Configure);
    this->Add("DestroyChannel", &LSCService::DestroyChannel);
    this->Add("GetMembers", &LSCService::GetMembers);
    this->Add("GetMember", &LSCService::GetMember);
    this->Add("SendMessage", &LSCService::SendMessage);
    this->Add("Invite", &LSCService::Invite);
    this->Add("AccessControl", &LSCService::AccessControl);
    this->Add("RenameChannel", &LSCService::RenameChannel);
    this->Add("SetChannelMOTD", &LSCService::SetChannelMOTD);
    this->Add("SetChannelLanguageRestriction", &LSCService::SetChannelLanguageRestriction);
    this->Add("ForgetChannel", &LSCService::ForgetChannel);
    this->Add("VoiceStatus", &LSCService::VoiceStatus);

    this->Add("GetMyMessages", &LSCService::GetMyMessages);
    this->Add("GetMessageDetails", &LSCService::GetMessageDetails);
    this->Add("Page", &LSCService::Page);
    this->Add("MarkMessagesRead", &LSCService::MarkMessagesRead);
    this->Add("DeleteMessages", &LSCService::DeleteMessages);

    CreateStaticChannels();
    LoadUserChannels();

    // gags outlive restarts
    s_gags.clear();
    LSCDB::LoadGags(s_gags);

    // make startup msg with # of static channels created
}


LSCService::~LSCService() {
    std::map<int32, LSCChannel* >::iterator cur = m_channels.begin();
    for(; cur != m_channels.end(); cur++) {
        SafeDelete(cur->second);
    }
}

void LSCService::Init(CommandDispatcher* cd )
{
    // not used yet
}

/*
LSC__ERROR=1
LSC__WARNING=0
LSC__MESSAGE=0
LSC__INFO=0
LSC__CHANNELS=0
LSC__CALL_DUMP=0
LSC__RSP_DUMP=0
*/

//  there is no reason to hit the db for everyfuckingthing in this class!!

///////////////////////////////////////////////////////////////////////////////
//
// Eve Chat calls
//
///////////////////////////////////////////////////////////////////////////////

const int cspa = 2950; // CONCORD Spam Prevention Act


PyResult LSCService::GetChannels(PyCallArgs &call)
{
    /* the static channels, the caller's corp and alliance channels and the user channels the caller owns or is
     *  subscribed to.  never other people's private conversations
     */
    ChannelInfo info;
    info.lines = new PyList();

    uint32 charID = call.client->GetCharacterID();
    uint32 corpID = call.client->GetCorporationID(), allianceID = call.client->GetAllianceID();
    std::set<int32> subscribed;
    m_db.GetSubscribedChannels(charID, subscribed);

    std::map<int32, LSCChannel*>::iterator cur = m_channels.begin();
    for (; cur != m_channels.end(); cur++) {
        bool isSubscribed(false);
        switch (cur->second->GetType()) {
            case LSC::Type::corp: {
                if (cur->first != corpID)
                    continue;
            } break;
            case LSC::Type::alliance: {
                if ((allianceID == 0) or (cur->first != allianceID))
                    continue;
            } break;
            case LSC::Type::normal: {
                if (cur->second->IsUserChannel()) {
                    if (cur->second->GetTemporary())
                        continue;
                    isSubscribed = (subscribed.find(cur->first) != subscribed.end());
                    if (!isSubscribed and (cur->second->GetOwnerID() != charID))
                        continue;
                }
            } break;
            case LSC::Type::region:
            case LSC::Type::constellation:
            case LSC::Type::solarsystem:
            case LSC::Type::solarsystem2:
            case LSC::Type::fleet:
            case LSC::Type::wing:
            case LSC::Type::squad:
            case LSC::Type::warfaction:
            case LSC::Type::incursion:
            case LSC::Type::custom: {
                continue;
            } break;
            default:
                break;
        }

        info.lines->AddItem(cur->second->EncodeStaticChannel(call.client, IsPrivileged(call.client, cur->second), isSubscribed));
    }

    if (is_log_enabled(LSC__RSP_DUMP))
        info.Dump(LSC__RSP_DUMP);
    return info.Encode();
}


PyResult LSCService::GetRookieHelpChannel(PyCallArgs &call) {
    if (is_log_enabled(LSC__CALL_DUMP)) {
        sLog.Warning("LSCService::Handle_GetRookieHelpChannel()", "size=%lu", call.tuple->size());
        call.Dump(LSC__CALL_DUMP);
    }

    return PyStatic.NewOne();
}

PyResult LSCService::CreateChannel(PyCallArgs& call, PyRep* channelName)
{
    if (is_log_enabled(LSC__CALL_DUMP)) {
        sLog.Warning("LSCService::Handle_CreateChannel()", "size=%lu", call.tuple->size());
        call.Dump(LSC__CALL_DUMP);
    }

    std::string name = PyRep::StringContent(channelName);

    bool create = false, temporary = false, joinExisting = false, noCallThrottling = false, memberless = false;
    if (call.byname.find("create") != call.byname.end())
        create =  call.byname.find("create")->second->AsBool()->value();
    if (call.byname.find("temporary") != call.byname.end())
        temporary = call.byname.find("temporary")->second->AsBool()->value();
    if (call.byname.find("joinExisting") != call.byname.end())
        joinExisting = call.byname.find("joinExisting")->second->AsBool()->value();
    if (call.byname.find("noCallThrottling") != call.byname.end())
        noCallThrottling = call.byname.find("noCallThrottling")->second->AsBool()->value();
    if (call.byname.find("memberless") != call.byname.end())
        memberless = (call.byname.find("memberless")->second->AsInt()->value() ? true : false);

    Client* pClient = call.client;
    ChannelCreateReply reply;

    // create/get channel info
    LSCChannel* channel(nullptr);
    if (joinExisting) {
        channel = GetChannelByName(name);
    } else if (create)  {
        // figure out how to determine owner of this channel.....corp, ally, char.  for now, use charID
        int32 nextID = m_db.GetNextAvailableChannelID();
        // private conversations are never stored, so skip the ids in use here
        while ((nextID != 0) and (m_channels.find(nextID) != m_channels.end()))
            nextID = (nextID < INT32_MAX ? nextID + 1 : 0);
        if (nextID != 0) {
            std::string comStr = name;
            boost::algorithm::to_lower(comStr);
            comStr.erase(std::remove(comStr.begin(), comStr.end(), ' '), comStr.end());
            channel = CreateChannel(nextID, pClient->GetCharacterID(), name.c_str(), "motd", nullptr, comStr.c_str(), LSC::Type::normal, cspa,\
            nextID, nextID, memberless, false, temporary, false);
        }
        if (channel != nullptr) {
            // new channels are public; the owner restricts them in the settings
            LSCChannelMod mod(0, LSC::Mode::chConversationalist);
            channel->LoadMod(mod);
            // save non-temp channel info for new channels
            if (!temporary) {
                m_db.UpdateChannelInfo(channel);
                LSCDB::SaveChannelMod(channel->GetChannelID(), mod);
            }
        }
    } else {
        // make error here for !join and !create  (should never hit)
    }

    // test for channel info
    if (!channel) {
        if (joinExisting) {
            _log(LSC__ERROR, "%s: Channel not found - %s", pClient->GetName(), name.c_str());
            reply.ChannelInfo = new PyInt(LSC::Error::errNoSuchChannel);
        } else if (create)  {
            _log(LSC__ERROR, "%s: Channel not created - %s", pClient->GetName(), name.c_str());
            reply.ChannelInfo = new PyInt(LSC::Error::errUnspecified);
        } else {
            // make error here for !join and !create  (should never hit)
        }
        return reply.Encode();
    }

    // joining an existing channel by name is subject to the same rules as JoinChannels
    if (joinExisting) {
        std::string msg;
        if (!CanAccess(pClient, channel, msg))
            throw UserError("LSCCannotJoin").AddFormatValue("msg", new PyString(msg));
        // the client only handles CHTERR_NOSUCHCHANNEL/ALREADYEXISTS here, so a password failure is a user error
        if (!CheckPassword(pClient, channel, nullptr))
            throw UserError("LSCWrongPassword").AddFormatValue("channelName", new PyString(channel->GetDisplayName()));
    }

    // join channel and send response
    if (!channel->IsJoined(pClient->GetCharacterID()))  {
        if (channel->JoinChannel(pClient)) {
            //if ((channel->GetChannelID() < 0) or (channel->GetChannelID() > maxStaticChannel)) {
                reply.ChannelInfo = channel->EncodeDynamicChannel(pClient, IsPrivileged(pClient, channel), channel->IsUserChannel() and !channel->GetTemporary());
            //} else {
            //    reply.ChannelInfo = channel->EncodeStaticChannel(pClient->GetCharacterID());
            //}
            reply.ChannelChars = channel->EncodeChannelChars();
            reply.ChannelMods = channel->EncodeChannelMods();
        } else {
            reply.ChannelInfo = new PyInt(LSC::Error::errUnspecified);
        }
        if (channel->IsUserChannel() and !channel->GetTemporary())
            m_db.UpdateSubscription(channel->GetChannelID(), pClient);

        return reply.Encode();
    } else {
        _log(LSC__ERROR, "%s: Already joined Channel %i \"%s\".", pClient->GetName(), channel->GetChannelID(), channel->GetDisplayName().c_str());
        reply.ChannelInfo = new PyInt(LSC::Error::errUnspecified);
        return reply.Encode();
    }

    if (is_log_enabled(LSC__RSP_DUMP))
        reply.Dump(LSC__RSP_DUMP);

    /*
     *            ret = sm.RemoteSvc('LSC').CreateChannel(displayName, joinExisting=False, memberless=0, create=True)
     *            if ret:
     *                info, acl, memberList = ret
     *        // on fail,  ChannelInfo is pyint(lsc::type::error), others are null
     *            if info == CHTERR_ALREADYEXISTS:
     *            if info == CHTERR_NOSUCHCHANNEL:
     */
    return nullptr;
}

PyResult LSCService::JoinChannels(PyCallArgs &call, PyList* channelIDs, PyLong* role) {
    if (is_log_enabled(LSC__CALL_DUMP)) {
        sLog.Warning("LSCService::Handle_JoinChannels()", "size=%lu", call.tuple->size());
        call.Dump(LSC__CALL_DUMP);
    }

    std::set<int32> toJoin;

    PyList::const_iterator cur = channelIDs->begin();
    for (; cur != channelIDs->end(); cur++) {
        if ((*cur)->IsInt()) {
            toJoin.insert((*cur)->AsInt()->value());
        } else if ((*cur)->IsTuple()) {
            PyTuple* prt = (*cur)->AsTuple();
            if (prt->items.size() != 1 or !prt->items[0]->IsTuple()) {
                _log(SERVICE__ERROR, "%s: Failed to decode arguments.", GetName());
                continue;
            }
            prt = prt->items[0]->AsTuple();

            if (prt->items.size() != 2 or /* !prt->items[0]->IsString() or unnessecary */ !prt->items[1]->IsInt()) {
                _log(SERVICE__ERROR, "%s: Failed to decode arguments.", GetName());
                continue;
            }
            toJoin.insert(prt->items[1]->AsInt()->value());
        } else {
            codelog(SERVICE__ERROR, "%s: Failed to decode argument ", call.client->GetName());
            return nullptr;
        }
    }

    PyList *ml = new PyList();
    LSCChannel* channel(nullptr);
    uint32 charID = call.client->GetCharacterID();

    std::set<int32>::iterator curs = toJoin.begin();
    for (; curs != toJoin.end(); curs++) {
        int32 channelID = *curs;
        if (channelID == 0)
            continue;
        if (sConfig.chat.EnforceRookieInHelp)
            if ((channelID == 1) or (channelID == 2))
                if (((role->value() & Acct::Role::NEWBIE) != Acct::Role::NEWBIE) or ((role->value() & Acct::Role::EPLAYER) != Acct::Role::EPLAYER))
                    continue;

        channel = GetChannelByID(channelID);
        if (channel == nullptr)
            continue;
        ChannelJoinReply chjr;
            chjr.ChannelID = channel->EncodeID();

        std::string msg;
        if (channel->IsJoined(charID)) {
            //{'FullPath': u'UI/Messages', 'messageID': 256739, 'label': u'LSCChannelIsJoinedBody'}(u'You are already in the channel ({displayName})', None, {u'{displayName}': {'conditionalValues': [], 'variableType': 10, 'propertyName': None, 'args': 0, 'kwargs': {}, 'variableName': 'displayName'}})
            PyDict* args = new PyDict();
                args->SetItemString("displayName", new PyString(channel->GetDisplayName()));
            chjr.JoinRsp = MakeJoinError("LSCChannelIsJoined", args);
            chjr.ok = 0;
        } else if (!CanAccess(call.client, channel, msg)) {
            PyDict* args = new PyDict();
                args->SetItemString("msg", new PyString(msg));
            chjr.JoinRsp = MakeJoinError("LSCCannotJoin", args);
            chjr.ok = 0;
        } else if (!CheckPassword(call.client, channel, nullptr)) {
            /* ask the client for the password: lscengine.GetChannelPasswordAndJoin(title, channelID, displayName) prompts
             * and joins through LSC.JoinChannel(channelID, password=...).  this channel is left out of the reply,
             * since any ok=0 entry pops a message box on top of the prompt.  if the client refuses the call,
             * it gets the LSCWrongPassword message instead
             */
            PyDict* args = new PyDict();
                args->SetItemString("channelName", new PyString(channel->GetDisplayName()));
            call.client->SendServiceCall("LSC", "GetChannelPasswordAndJoin",
                    new_tuple(new PyString(channel->GetDisplayName()), new PyInt(channelID), new PyString(channel->GetDisplayName())),
                    "LSCWrongPassword", args);
            continue;
        } else {
            if (channel->JoinChannel(call.client)) {
                bool subscribe = (channel->IsUserChannel() and !channel->GetTemporary());
                if (subscribe)
                    m_db.UpdateSubscription(channelID, call.client);
                ChannelJoinOK cjok;
                //if ((channelID < 0) or (channelID > maxStaticChannel)) {
                    cjok.ChannelInfo = channel->EncodeDynamicChannel(call.client, IsPrivileged(call.client, channel), subscribe);
                //} else {
                //    cjok.ChannelInfo = channel->EncodeStaticChannel(charID);
                //}
                cjok.ChannelMods = channel->EncodeChannelMods();
                cjok.ChannelChars = channel->EncodeChannelChars();
                chjr.JoinRsp = cjok.Encode();
                chjr.ok = 1;
            } else {
                PyDict* args = new PyDict();
                    args->SetItemString("msg", new PyString(""));
                chjr.JoinRsp = MakeJoinError("LSCCannotJoin", args);
                chjr.ok = 0;
            }
        }
        ml->AddItem(chjr.Encode());
    }

    if (is_log_enabled(LSC__RSP_DUMP))
        ml->Dump(LSC__RSP_DUMP, "   ");
    return ml;
}

PyResult LSCService::SendMessage(PyCallArgs& call, PyRep* channelInfo, PyWString* cMessage)
{
    if (is_log_enabled(LSC__CALL_DUMP)) {
        sLog.Warning("LSCService::Handle_SendMessage()", "size=%lu", call.tuple->size());
        call.Dump(LSC__CALL_DUMP);
    }

    int32 channel_id = 0;
    std::string message = "";

    if ((call.tuple->IsTuple()) and (call.tuple->AsTuple()->items[0]->IsInt())) {
        // Decode All User-created chat channel messages here:
        if (!call.tuple->IsTuple()) {
            _log(LSC__ERROR, "LSCService::Handle_SendMessage failed: tuple0 is the wrong type: %s", call.tuple->TypeString());
            return PyStatic.NewNone();
        }
        PyTuple* tuple0 = call.tuple->AsTuple();

        if (tuple0->size() != 2) {
            _log(LSC__ERROR, "LSCService::Handle_SendMessage failed: tuple0 is the wrong size: expected 2, but got %u", tuple0->size());
            return PyStatic.NewNone();
        }

        channel_id = call.tuple->AsTuple()->items[0]->AsInt()->value();
        message = cMessage->content();
        _log(LSC__INFO, "Handle_SendMessage: call is player channel chat.");
    } else {
        Call_SendMessage args;
        if (!args.Decode(&call.tuple)) {
            codelog(SERVICE__ERROR, "%s: Failed to decode arguments.", GetName());
            return PyStatic.NewNone();
        }
        channel_id = args.channel.id;
        message = args.message;
        _log(LSC__INFO, "Handle_SendMessage: call is system channel chat.");
    }

    std::map<int32, LSCChannel*>::iterator itr = m_channels.find(channel_id);
    if (itr == m_channels.end()) {
        _log(LSC__ERROR, "%s: Couldn't find channel %u", call.client->GetName(), channel_id);
        return PyStatic.NewNone();
    }

    // only members of a channel may speak in it (this covers the '.' slash path too)
    if (!itr->second->IsJoined(call.client->GetCharacterID()))
        throw UserError("LSCCannotSendMessage").AddFormatValue("msg", new PyString("You are not a member of this channel."));

    if (message.empty())
        return PyStatic.NewNone();

    if (message.at(0) == '.') {
        _log(LSC__INFO, "SlashService->SlashCmd() called via LSC Service");
        m_slash->SlashCommand(call.client, message);
        itr->second->SendMessage(call.client, message.c_str(), true);
    } else {
        int64 until(0);
        std::string reason;
        if (IsGagged(call.client->GetCharacterID(), until, reason)) {
            int64 minutes((until - GetFileTimeNow()) / Win32Time_Minute + 1);
            throw CustomError ("You are gagged for another %lli minutes: %s", minutes, reason.c_str());
        }
        // the channel's access list (moderated channels, gags, blocked default)
        if (!itr->second->CanSpeak(call.client))
            throw UserError("LSCCannotSendMessage").AddFormatValue("msg", new PyString("You are not allowed to speak in this channel."));
        itr->second->SendMessage(call.client, message.c_str());
        if (sConfig.world.chatLogs)
            LSCChannel::LogChat(itr->second, call.client, message);
        // console 'hear all'
        if (sConsole.HearAll())
            sLog.Magenta("        Chat", "[%s] %s: %s", itr->second->GetDisplayName().c_str(), call.client->GetName(), message.c_str());
    }

    return PyStatic.NewNone();
}

uint32 LSCService::BroadcastSystemMessage(const std::string& msg) {
    uint32 count(0);
    for (auto cur : m_channels) {
        if (cur.second == nullptr)
            continue;
        if ((cur.second->GetType() != LSC::Type::solarsystem) and (cur.second->GetType() != LSC::Type::solarsystem2))
            continue;
        if (cur.second->GetMemberCount() < 1)
            continue;
        cur.second->SendSystemMessage(msg.c_str());
        ++count;
    }
    return count;
}

std::map<uint32, std::pair<int64, std::string>> LSCService::s_gags;

void LSCService::Gag(uint32 charID, int64 until, const std::string& reason, uint32 gmID/*0*/) {
    s_gags[charID] = std::make_pair(until, reason);
    LSCDB::SaveGag(charID, until, reason, gmID);
}

void LSCService::Ungag(uint32 charID) {
    s_gags.erase(charID);
    LSCDB::DeleteGag(charID);
}

bool LSCService::IsGagged(uint32 charID, int64& until, std::string& reason) {
    std::map<uint32, std::pair<int64, std::string>>::iterator itr = s_gags.find(charID);
    if (itr == s_gags.end())
        return false;
    if (itr->second.first <= GetFileTimeNow()) {
        // only runs when a gagged character speaks or is looked up, so the db hit is rare
        s_gags.erase(itr);
        LSCDB::DeleteGag(charID);
        return false;
    }
    until = itr->second.first;
    reason = itr->second.second;
    return true;
}

PyResult LSCService::AccessControl(PyCallArgs& call, PyRep* channelInfo, PyRep* accessor, PyInt* mode, std::optional<PyRep*> untilWhen, std::optional<PyRep*> reason)
{
    /* AccessControl(channelID, accessor|None, mode[, untilWhen, reason])
     *  gag = LISTENER with untilWhen/reason, ungag = CONVERSATIONALIST (or NOTSPECIFIED with a past time),
     *  kick = DISALLOWED with untilWhen/reason; the settings dialog sets allowed/blocked/operator entries and the
     *  default (accessor None)
     */
    LSCChannel* channel = GetChannelByID(DecodeChannelID(channelInfo));
    if (channel == nullptr)
        throw UserError("LSCCannotAccessControl").AddFormatValue("msg", new PyString("That channel does not exist."));
    if (!IsPrivileged(call.client, channel))
        throw UserError("LSCCannotAccessControl").AddFormatValue("msg", new PyString("You are not allowed to change the access list of this channel."));

    uint32 accessorID = (accessor->IsNone() ? 0 : PyRep::IntegerValueU32(accessor));
    int32 newMode = mode->value();
    switch (newMode) {
        case LSC::Mode::chDisallowed:
        case LSC::Mode::chUnspecified:
        case LSC::Mode::chListener:
        case LSC::Mode::chSpeaker:
        case LSC::Mode::chConversationalist:
        case LSC::Mode::chOperator:
        case LSC::Mode::chCreator:
            break;
        default:
            throw UserError("LSCCannotAccessControl").AddFormatValue("msg", new PyString("That is not a valid access mode."));
    }

    if ((accessorID != 0) and (accessorID == channel->GetOwnerID()))
        throw UserError("LSCCannotAccessControl").AddFormatValue("msg", new PyString("The channel owner's access can't be changed."));

    // operators are made (and unmade) by the owner
    bool isCreator = (channel->GetAccessMode(call.client) >= LSC::Mode::chCreator);
    if (!isCreator and ((newMode >= LSC::Mode::chOperator) or (channel->GetEntryMode(accessorID) >= LSC::Mode::chOperator)))
        throw UserError("LSCCannotAccessControl").AddFormatValue("msg", new PyString("Only the channel owner can change operators."));

    int64 until(0);
    if (untilWhen.has_value() and (untilWhen.value() != nullptr) and !untilWhen.value()->IsNone()) {
        until = PyRep::IntegerValue(untilWhen.value());
        // the client offers up to 30 days
        int64 maxUntil = (int64)GetFileTimeNow() + 30 * EvE::Time::Day;
        if (until > maxUntil)
            until = maxUntil;
    }
    std::string reasonStr;
    if (reason.has_value() and (reason.value() != nullptr) and !reason.value()->IsNone())
        reasonStr = PyRep::StringContent(reason.value());

    channel->SetAccess(accessorID, newMode, until, reasonStr, call.client);
    return PyStatic.NewNone();
}

PyResult LSCService::Invite(PyCallArgs& call, PyInt* characterID, PyInt* channelID, std::optional<PyRep*> channelName, std::optional<PyRep*> addAllowed)
{
    // Invite(otherID, channelID, channelName, addAllowed) -> truthy when invited
    LSCChannel* channel = GetChannelByID(channelID->value());
    if (channel == nullptr)
        throw CustomError("That channel does not exist.");
    uint32 charID = call.client->GetCharacterID();
    if (!channel->IsJoined(charID))
        throw CustomError("You must be in the channel to invite someone to it.");

    uint32 targetID = characterID->value();
    if (targetID == charID)
        throw CustomError("You can't invite yourself.");
    if (!IsCharacterID(targetID))
        throw CustomError("You can only invite player characters.");
    if (channel->IsJoined(targetID))
        return PyStatic.NewOne();

    // a public channel anyone may invite to; otherwise it takes an operator
    if (!channel->GetTemporary() and !IsPrivileged(call.client, channel) and (channel->GetEntryMode(0) < LSC::Mode::chConversationalist))
        throw CustomError("You are not allowed to invite people to this channel.");

    Client* pTarget = sEntityList.FindClientByCharID(targetID);
    if (pTarget == nullptr)
        throw CustomError("%s is not online.", sDataMgr.GetOwnerName(targetID).c_str());

    // CSPA (the client wraps Invite in CSPAChargedAction and calls again with approvedCost)
    AccountService::ChargeCSPA(call.client, targetID, call);

    // the client asks to put the invitee on the allowed list when the channel isn't public (operators only, so a kick
    //  can't be lifted thru an invite)
    if (addAllowed.has_value() and (addAllowed.value() != nullptr) and (PyRep::IntegerValue(addAllowed.value()) != 0)
    and IsPrivileged(call.client, channel) and (channel->GetEntryMode(targetID) < LSC::Mode::chConversationalist))
        channel->SetAccess(targetID, LSC::Mode::chConversationalist, 0, "", call.client);

    channel->AddInvite(targetID);

    /* LSC.ChatInvite(invitorID, invitorName, invitorGender, channelID) on the invitee's client: it shows the invitation
     *  (or accepts at once for contacts) and joins.  it returns None when accepted, else a reason (ChtRejected, ChtBlockedBefore,
     *  ChtBlockedNow, ChtAlreadyInvited, ChtAlreadyInChannel, ChtCannotInviteSelf, ChtInviteException).
     *  if the client refuses the call, the invitee gets a notice and can join the channel by name
     */
    std::string inviterName = call.client->GetName();
    std::string channelNameStr = ((channelName.has_value() and (channelName.value() != nullptr) and !channelName.value()->IsNone())
            ? PyRep::StringContent(channelName.value()) : channel->GetDisplayName());
    PyDict* fallback = new PyDict();
        fallback->SetItemString("notify", new PyString(inviterName + " invites you to the chat channel " + channelNameStr + "."));
    int32 gender = ((call.client->GetChar().get() != nullptr) and call.client->GetChar()->gender()) ? 1 : 0;
    PyTuple* args = new PyTuple(4);
        args->SetItem(0, new PyInt(charID));
        args->SetItem(1, new PyWString(inviterName));
        args->SetItem(2, new PyInt(gender));
        args->SetItem(3, channel->EncodeID());
    int64 callID = pTarget->SendServiceCall("LSC", "ChatInvite", args, "CustomNotify", fallback);

    // tell the inviter when the invitation was turned down
    std::string targetName = pTarget->GetName();
    pTarget->OnServerCallResult(callID, [charID, targetName](PyRep* result) {
        if ((result == nullptr) or !(result->IsString() or result->IsWString()))
            return;
        std::string reason = PyRep::StringContent(result);
        Client* pInviter = sEntityList.FindClientByCharID(charID);
        if (pInviter == nullptr)
            return;
        if ((reason == "ChtRejected") or (reason == "ChtBlockedBefore") or (reason == "ChtBlockedNow")) {
            pInviter->SendNotifyMsg("%s rejected your chat invitation.", targetName.c_str());
        } else if (reason == "ChtAlreadyInvited") {
            pInviter->SendNotifyMsg("%s has already been invited.", targetName.c_str());
        } else if (reason == "ChtAlreadyInChannel") {
            pInviter->SendNotifyMsg("%s is already in the channel.", targetName.c_str());
        }
    });
    return PyStatic.NewOne();
}

PyResult LSCService::RenameChannel(PyCallArgs& call, PyRep* channelInfo, PyRep* name)
{
    LSCChannel* channel = GetChannelByID(DecodeChannelID(channelInfo));
    if ((channel == nullptr) or !channel->IsUserChannel() or !IsPrivileged(call.client, channel))
        throw UserError("LSCCannotRename").AddFormatValue("msg", new PyString("You are not allowed to rename this channel."));

    std::string newName = PyRep::StringContent(name);
    if (newName.empty())
        throw UserError("LSCCannotRename").AddFormatValue("msg", new PyString("The channel needs a name."));
    channel->Rename(newName, call.client);
    if (!channel->GetTemporary())
        m_db.UpdateChannelInfo(channel);
    return PyStatic.NewNone();
}

PyResult LSCService::SetChannelMOTD(PyCallArgs& call, PyRep* channelInfo, PyRep* motd)
{
    LSCChannel* channel = GetChannelByID(DecodeChannelID(channelInfo));
    if ((channel == nullptr) or !CanConfigure(call.client, channel))
        throw UserError("LSCCannotSetMOTD").AddFormatValue("msg", new PyString("You are not allowed to change the MOTD of this channel."));

    channel->ChangeMOTD(PyRep::StringContent(motd), call.client);
    if (channel->IsUserChannel() and !channel->GetTemporary())
        m_db.UpdateChannelInfo(channel);
    return PyStatic.NewNone();
}

PyResult LSCService::SetChannelLanguageRestriction(PyCallArgs& call, PyRep* channelInfo, PyRep* restricted)
{
    // a GM setting (the client shows it on the GM tab)
    LSCChannel* channel = GetChannelByID(DecodeChannelID(channelInfo));
    if ((channel == nullptr) or !IsGM(call.client))
        throw CustomError("Only a GM can change the language restriction.");

    channel->ChangeLanguageRestriction(PyRep::IntegerValue(restricted) != 0, call.client);
    if (channel->IsUserChannel() and !channel->GetTemporary())
        m_db.UpdateChannelInfo(channel);
    return PyStatic.NewNone();
}

PyResult LSCService::ForgetChannel(PyCallArgs& call, PyRep* channelInfo)
{
    // drops the channel from the caller's channel list
    int32 id = DecodeChannelID(channelInfo);
    m_db.DeleteSubscription(id, call.client->GetCharacterID());
    LSCChannel* channel = GetChannelByID(id);
    if (channel != nullptr)
        DestroyIfAbandoned(channel);
    return PyStatic.NewNone();
}

PyResult LSCService::VoiceStatus(PyCallArgs& call, PyRep* channelInfo, PyInt* status)
{
    // there is no voice server; the members only get the "joined/left/muted voice chat" note.  0 left, 1 joined, 2 muted
    LSCChannel* channel = GetChannelByID(DecodeChannelID(channelInfo));
    if ((channel == nullptr) or !channel->IsJoined(call.client->GetCharacterID()))
        return PyStatic.NewNone();
    channel->Broadcast("VoiceStatus", call.client, new_tuple(new PyInt(status->value())));
    return PyStatic.NewNone();
}

static bool GetNamedArg(PyCallArgs& call, const char* name, PyRep*& into)
{
    std::map<std::string, PyRep*>::iterator itr = call.byname.find(name);
    if (itr == call.byname.end())
        return false;
    into = itr->second;
    return true;
}

PyResult LSCService::Configure(PyCallArgs& call, PyRep* channelInfo)
{
    // Configure(channelID, **{displayName, motd, oldPassword, newPassword, memberless, languageRestriction, creator})
    //  channelID is a tuple for corp/alliance channels (ChatManager setting the MOTD)
    LSCChannel* channel = GetChannelByID(DecodeChannelID(channelInfo));
    if (channel == nullptr)
        throw UserError("LSCCannotSetMOTD").AddFormatValue("msg", new PyString("That channel does not exist."));

    PyRep* value(nullptr);
    if (!CanConfigure(call.client, channel)) {
        // pick the client message matching the requested change
        const char* key = "LSCCannotSetMOTD";
        if (GetNamedArg(call, "newPassword", value)) {
            key = "LSCCannotSetPassword";
        } else if (GetNamedArg(call, "memberless", value)) {
            key = "LSCCannotSetMemberless";
        } else if (GetNamedArg(call, "displayName", value)) {
            key = "LSCCannotRename";
        } else if (GetNamedArg(call, "creator", value)) {
            key = "LSCCannotSetCreator";
        }
        throw UserError(key).AddFormatValue("msg", new PyString("You are not allowed to configure this channel."));
    }

    if (GetNamedArg(call, "displayName", value) and !value->IsNone()) {
        std::string name = PyRep::StringContent(value);
        if (!name.empty() and (name != channel->GetDisplayName())) {
            if (!channel->IsUserChannel())
                throw UserError("LSCCannotRename").AddFormatValue("msg", new PyString("This channel can't be renamed."));
            channel->Rename(name, call.client);
        }
    }

    if (GetNamedArg(call, "motd", value) and !value->IsNone()) {
        std::string motd = PyRep::StringContent(value);
        if (motd != channel->GetMOTD())
            channel->ChangeMOTD(motd, call.client);
    }

    if (GetNamedArg(call, "newPassword", value)) {
        std::string newPassword = (value->IsNone() ? "" : PyRep::StringContent(value));
        PyRep* oldValue(nullptr);
        if (GetNamedArg(call, "oldPassword", oldValue) and !oldValue->IsNone() and !IsGM(call.client)
        and (PyRep::StringContent(oldValue) != channel->GetPassword()))
            throw UserError("LSCCannotSetPassword").AddFormatValue("msg", new PyString("The old password is wrong."));
        if (newPassword != channel->GetPassword())
            channel->ChangePassword(newPassword, call.client);
    }

    if (GetNamedArg(call, "memberless", value) and !value->IsNone()) {
        bool memberless = (PyRep::IntegerValue(value) != 0);
        if (memberless != channel->GetMemberless())
            channel->ChangeMemberless(memberless, call.client);
    }

    if (GetNamedArg(call, "languageRestriction", value) and !value->IsNone()) {
        if (!IsGM(call.client))
            throw CustomError("Only a GM can change the language restriction.");
        bool restricted = (PyRep::IntegerValue(value) != 0);
        if (restricted != channel->GetLanguageRestriction())
            channel->ChangeLanguageRestriction(restricted, call.client);
    }

    if (GetNamedArg(call, "creator", value) and !value->IsNone()) {
        // the owner may hand the channel on; GMs may give it to anyone
        uint32 ownerID = PyRep::IntegerValueU32(value);
        if (!IsGM(call.client) and (channel->GetOwnerID() != (uint32)call.client->GetCharacterID()))
            throw UserError("LSCCannotSetCreator").AddFormatValue("msg", new PyString("Only the channel owner can hand the channel on."));
        if (!IsCharacterID(ownerID) and (ownerID != 1))
            throw UserError("LSCCannotSetCreator").AddFormatValue("msg", new PyString("A channel can only be owned by a character."));
        if (ownerID != channel->GetOwnerID())
            channel->ChangeCreator(ownerID, call.client);
    }

    if (channel->IsUserChannel() and !channel->GetTemporary())
        m_db.UpdateChannelInfo(channel);

    return PyStatic.NewNone();
}


PyResult LSCService::LeaveChannel(PyCallArgs &call, PyRep* channelInfo, PyInt* unsubscribe) {
    if (is_log_enabled(LSC__CALL_DUMP)) {
        sLog.Warning("LSCService::Handle_LeaveChannel()", "size=%lu", call.tuple->size());
        call.Dump(LSC__CALL_DUMP);
    }

    uint32 toLeave = 0;

    if (channelInfo->IsInt()) {
        toLeave = channelInfo->AsInt()->value();
    } else if (channelInfo->IsTuple()) {
        PyTuple* prt = channelInfo->AsTuple();

        if (prt->GetItem(0)->IsInt()) {
            toLeave = prt->GetItem(0)->AsInt()->value();
        } else if (prt->GetItem(0)->IsTuple()) {
            prt = prt->GetItem(0)->AsTuple();

            if (prt->items.size() != 2 or !prt->GetItem(1)->IsInt()) {
                _log(SERVICE__ERROR, "%s: Failed to decode arguments.", GetName());
                return PyStatic.NewNone();
            }

            toLeave = prt->GetItem(1)->AsInt()->value();
        } else {
            _log(SERVICE__ERROR, "%s: Failed to decode arguments.", GetName());
            return PyStatic.NewNone();
        }
    } else {
        _log(SERVICE__ERROR, "%s: Failed to decode arguments.", GetName());
        return PyStatic.NewNone();
    }

    if (unsubscribe->value())
        m_db.DeleteSubscription(toLeave,call.client->GetCharacterID());

    std::map<int32, LSCChannel*>::iterator itr = m_channels.find(toLeave);
    if (itr != m_channels.end()) {
        itr->second->LeaveChannel(call.client);
        DestroyIfAbandoned(itr->second);
    }

    return PyStatic.NewNone();
}

PyResult LSCService::LeaveChannels(PyCallArgs &call, PyList* channels, PyBool* unsubscribe, PyLong* role) {
    return this->LeaveChannels (call, channels, new PyInt (unsubscribe->value ()), role);
}

PyResult LSCService::LeaveChannels(PyCallArgs &call, PyList* channels, PyInt* unsubscribe, PyLong* role) {
    if (is_log_enabled(LSC__CALL_DUMP)) {
        sLog.Warning("LSCService::Handle_LeaveChannels()", "size=%lu", call.tuple->size());
        call.Dump(LSC__CALL_DUMP);
    }

    std::set<uint32> toLeave;
    toLeave.clear();

    PyList::const_iterator cur = channels->begin();
    for (; cur != channels->end(); cur++) {
        if ((*cur)->IsInt()) {
            toLeave.insert((*cur)->AsInt()->value());
        } else if ((*cur)->IsTuple()) {
            PyTuple* prt = (*cur)->AsTuple();

            if (prt->GetItem(0)->IsInt()) {
                toLeave.insert(prt->GetItem(0)->AsInt()->value());
                continue;
            }

            if (!prt->GetItem(0)->IsTuple()) {
                _log(SERVICE__ERROR, "%s: Failed to decode arguments.", GetName());
                continue;
            }
            prt = prt->GetItem(0)->AsTuple();

            if (prt->GetItem(0)->IsTuple())
                prt = prt->GetItem(0)->AsTuple();

            if (prt->size() != 2 or !prt->GetItem(1)->IsInt()) {
                _log(SERVICE__ERROR, "%s: Failed to decode arguments.", GetName());
                continue;
            }

            toLeave.insert(prt->GetItem(1)->AsInt()->value());
        } else {
            _log(SERVICE__ERROR, "%s: Failed to decode arguments.", GetName());
            continue;
        }
    }

    std::set<uint32>::iterator itr = toLeave.begin();
    std::map<int32, LSCChannel*>::iterator itr2;
    for (; itr!=toLeave.end(); itr++) {
        itr2 = m_channels.find(*itr);
        if (itr2 != m_channels.end()) {
            itr2->second->LeaveChannel(call.client);
            if (unsubscribe->value())
                m_db.DeleteSubscription(*itr, call.client->GetCharacterID());
            DestroyIfAbandoned(itr2->second);
        }
    }

    return PyStatic.NewNone();
}

PyResult LSCService::DestroyChannel(PyCallArgs& call, PyInt* channelID)
{
    if (is_log_enabled(LSC__CALL_DUMP)) {
        sLog.Warning("LSCService::Handle_DestroyChannel()", "size=%lu", call.tuple->size());
        call.Dump(LSC__CALL_DUMP);
    }

    std::map<int32, LSCChannel*>::iterator itr = m_channels.find(channelID->value());
    if (itr == m_channels.end()) {
        _log(LSC__ERROR, "%s: Couldn't find channel %u", call.client->GetName(), channelID->value());
        return PyStatic.NewNone();
    }

    // only user-created channels can be destroyed, by their owner or a GM (the client refuses system channels itself)
    LSCChannel* pChan = itr->second;
    if ((pChan->GetType() != LSC::Type::normal) or (pChan->GetChannelID() <= 0) or IsStaticChannel(pChan->GetChannelID())
    or (!IsGM(call.client) and (pChan->GetOwnerID() != call.client->GetCharacterID())))
        throw UserError("LSCCannotDestroy").AddFormatValue("msg", new PyString("You are not allowed to destroy this channel."));

    // Now, remove the channel from the database:
    m_db.DeleteChannel(itr->second->GetChannelID());

    // Finally, remove the channel from the server dynamic objects:
    itr->second->Evacuate(call.client);
    SafeDelete(itr->second);
    m_channels.erase(itr);

    return PyStatic.NewNone();
}


PyResult LSCService::GetMembers(PyCallArgs &call, PyRep* channelInfo) {
    uint32 channelID;
    if (channelInfo->IsInt()) {
        channelID = channelInfo->AsInt()->value();
    } else if (channelInfo->IsTuple()) {
        PyTuple* prt = channelInfo->AsTuple();

        if (prt->GetItem(0)->IsInt()) {
            channelID = prt->GetItem(0)->AsInt()->value();
        } else if (prt->GetItem(0)->IsTuple()) {
            prt = prt->GetItem(0)->AsTuple();

            if (prt->items.size() != 2 or !prt->GetItem(1)->IsInt())
            {
                _log(SERVICE__ERROR, "%s: Failed to decode arguments.", GetName());
                return nullptr;
            }

            channelID = prt->GetItem(1)->AsInt()->value();
        } else {
            _log(SERVICE__ERROR, "%s: Failed to decode arguments.", GetName());
            return nullptr;
        }
    } else {
        _log(SERVICE__ERROR, "%s: Failed to decode arguments.", GetName());
        return nullptr;
    }

    std::map<int32, LSCChannel*>::iterator itr = m_channels.find(channelID);
    if (itr == m_channels.end())
        return nullptr;
    // member lists are for members (the client only asks for joined channels)
    if (!itr->second->IsJoined(call.client->GetCharacterID()) and !IsGM(call.client))
        return nullptr;
    return itr->second->EncodeChannelChars();
}


PyResult LSCService::GetMember(PyCallArgs &call) {
    return nullptr;
}

PyResult LSCService::JoinChannel(PyCallArgs& call, PyTuple* channelID)
{
    int32 id = DecodeChannelID(channelID);
    if (id == 0)
        throw UserError("LSCCannotJoin").AddFormatValue("msg", new PyString("That channel does not exist."));
    return JoinChannel(call, new PyInt(id));
}

PyResult LSCService::JoinChannel(PyCallArgs& call, PyInt* channelID)
{
    /* lscengine.TryOpenChannel:  tmp = sm.RemoteSvc('LSC').JoinChannel(channelID, password=password)
     *   returns (info, acl, memberList) on success, None for a wrong password (the client asks again),
     *   UserError for anything else
     */
    if (is_log_enabled(LSC__CALL_DUMP)) {
        sLog.Warning("LSCService::Handle_JoinChannel()", "size=%lu", call.tuple->size());
        call.Dump(LSC__CALL_DUMP);
    }

    LSCChannel* channel = GetChannelByID(channelID->value());
    if (channel == nullptr)
        throw UserError("LSCCannotJoin").AddFormatValue("msg", new PyString("That channel does not exist."));

    std::string msg;
    if (!CanAccess(call.client, channel, msg))
        throw UserError("LSCCannotJoin").AddFormatValue("msg", new PyString(msg));

    std::string password;
    std::map<std::string, PyRep*>::iterator itr = call.byname.find("password");
    if (itr != call.byname.end())
        password = PyRep::StringContent(itr->second);
    if (!CheckPassword(call.client, channel, &password))
        return PyStatic.NewNone();

    uint32 charID = call.client->GetCharacterID();
    if (!channel->IsJoined(charID))
        if (!channel->JoinChannel(call.client))
            throw UserError("LSCCannotJoin").AddFormatValue("msg", new PyString(""));
    bool subscribe = (channel->IsUserChannel() and !channel->GetTemporary());
    if (subscribe)
        m_db.UpdateSubscription(channel->GetChannelID(), call.client);

    ChannelJoinOK cjok;
        cjok.ChannelInfo = channel->EncodeDynamicChannel(call.client, IsPrivileged(call.client, channel), subscribe);
        cjok.ChannelMods = channel->EncodeChannelMods();
        cjok.ChannelChars = channel->EncodeChannelChars();
    return cjok.Encode();
}

bool LSCService::IsGM(Client* pClient)
{
    if (pClient == nullptr)
        return false;
    return ((pClient->GetAccountRole() & (Acct::Role::CHTADMINISTRATOR | Acct::Role::GMH)) != 0);
}

bool LSCService::CanAccess(Client* pClient, LSCChannel* pChan, std::string& msg)
{
    if ((pClient == nullptr) or (pChan == nullptr))
        return false;
    if (IsGM(pClient))
        return true;

    // private conversations are invite only; other int channels follow their access list (kicks, blocked default)
    if (pChan->HasIntID()) {
        uint32 charID = pClient->GetCharacterID();
        if (pChan->GetTemporary() and (pChan->GetOwnerID() != charID) and !pChan->IsInvited(charID)) {
            msg = "This is a private conversation.";
            return false;
        }
        if (pChan->GetAccessMode(pClient) < LSC::Mode::chListener) {
            msg = "You are not allowed to join this channel.";
            return false;
        }
    }

    if (!sConfig.chat.EnforceChannelAccess)
        return true;

    int32 channelID = pChan->GetChannelID();
    Character* pChar = pClient->GetChar().get();
    switch (pChan->GetType()) {
        case LSC::Type::corp: {
            if ((int32)pClient->GetCorporationID() == channelID)
                return true;
            msg = "You are not a member of this corporation.";
        } break;
        case LSC::Type::alliance: {
            if ((pClient->GetAllianceID() != 0) and (pClient->GetAllianceID() == channelID))
                return true;
            msg = "You are not a member of this alliance.";
        } break;
        case LSC::Type::solarsystem:
        case LSC::Type::solarsystem2: {
            if ((int32)pClient->GetSystemID() == channelID)
                return true;
            msg = "You are not in this solar system.";
        } break;
        case LSC::Type::constellation: {
            if ((int32)pClient->GetConstellationID() == channelID)
                return true;
            msg = "You are not in this constellation.";
        } break;
        case LSC::Type::region: {
            if ((int32)pClient->GetRegionID() == channelID)
                return true;
            msg = "You are not in this region.";
        } break;
        case LSC::Type::fleet: {
            if ((pChar != nullptr) and (pChar->fleetID() == channelID))
                return true;
            msg = "You are not a member of this fleet.";
        } break;
        case LSC::Type::wing: {
            // fleetSvc.CanIJoinChannel: boss/FC any wing, everyone else their own wing
            if ((pChar != nullptr) and (pChar->fleetID() != 0) and sFltSvc.IsWingInFleet(channelID, pChar->fleetID()))
                if (sFltSvc.IsBoss(pChar) or (pChar->fleetRole() == Fleet::Role::FleetLeader) or (pChar->wingID() == channelID))
                    return true;
            msg = "You are not a member of this wing.";
        } break;
        case LSC::Type::squad: {
            // boss/FC any squad, a WC the squads of his wing, everyone else their own squad
            if ((pChar != nullptr) and (pChar->fleetID() != 0) and sFltSvc.IsSquadInFleet(channelID, pChar->fleetID())) {
                if (sFltSvc.IsBoss(pChar) or (pChar->fleetRole() == Fleet::Role::FleetLeader) or (pChar->squadID() == channelID))
                    return true;
                if (pChar->fleetRole() == Fleet::Role::WingLeader) {
                    SquadData sData = SquadData();
                    sFltSvc.GetSquadData(channelID, sData);
                    if ((int32)sData.wingID == pChar->wingID())
                        return true;
                }
            }
            msg = "You are not a member of this squad.";
        } break;
        default:
            // global, help and user channels are open (subject to password)
            return true;
    }

    return false;
}

bool LSCService::IsPrivileged(Client* pClient, LSCChannel* pChan)
{
    if ((pClient == nullptr) or (pChan == nullptr))
        return false;
    if (IsGM(pClient))
        return true;
    uint32 charID = pClient->GetCharacterID();
    return ((pChan->GetOwnerID() == charID) or (pChan->GetAccessMode(pClient) >= LSC::Mode::chOperator));
}

bool LSCService::CheckPassword(Client* pClient, LSCChannel* pChan, const std::string* password)
{
    if (!pChan->HasPassword())
        return true;
    if (IsPrivileged(pClient, pChan))
        return true;
    return ((password != nullptr) and (*password == pChan->GetPassword()));
}

bool LSCService::CanConfigure(Client* pClient, LSCChannel* pChan)
{
    if ((pClient == nullptr) or (pChan == nullptr))
        return false;
    if (IsGM(pClient))
        return true;

    int32 channelID = pChan->GetChannelID();
    switch (pChan->GetType()) {
        // lscengine: corp/alliance channel settings need the ChatManager corp role
        case LSC::Type::corp:
            return (((int32)pClient->GetCorporationID() == channelID) and ((pClient->GetCorpRole() & Corp::Role::ChatManager) == Corp::Role::ChatManager));
        case LSC::Type::alliance:
            return ((pClient->GetAllianceID() != 0) and (pClient->GetAllianceID() == channelID)
                and ((pClient->GetCorpRole() & Corp::Role::ChatManager) == Corp::Role::ChatManager));
        // the fleet MOTD belongs to the boss
        case LSC::Type::fleet: {
            Character* pChar = pClient->GetChar().get();
            return ((pChar != nullptr) and (pChar->fleetID() == channelID) and sFltSvc.IsBoss(pChar));
        }
        case LSC::Type::normal:
        case LSC::Type::custom:
        case LSC::Type::character: {
            // static (server) channels are owned by the system
            if (IsStaticChannel(channelID))
                return false;
            return IsPrivileged(pClient, pChan);
        }
        default:
            break;
    }

    return false;
}

PyRep* LSCService::MakeJoinError(const char* key, PyDict* args)
{
    ChannelJoinNotOK cjnok;
        cjnok.Error = key;
        cjnok.rspDict = (args != nullptr ? args : new PyDict());
    return cjnok.Encode();
}

int32 LSCService::DecodeChannelID(PyRep* rep)
{
    // channelID is either an int (user channels) or ((type, id),) / (type, id) for system channels
    if (rep == nullptr)
        return 0;
    if (rep->IsInt())
        return rep->AsInt()->value();
    if (!rep->IsTuple() or (rep->AsTuple()->size() < 1))
        return 0;
    PyTuple* prt = rep->AsTuple();
    if ((prt->size() == 1) and prt->GetItem(0)->IsTuple())
        prt = prt->GetItem(0)->AsTuple();
    if ((prt->size() == 2) and prt->GetItem(1)->IsInt())
        return prt->GetItem(1)->AsInt()->value();
    if ((prt->size() == 1) and prt->GetItem(0)->IsInt())
        return prt->GetItem(0)->AsInt()->value();
    return 0;
}


void LSCService::CharacterLogin(Client* pClient)
{
    CreateSystemChannel(pClient->GetCorporationID());
    CreateSystemChannel(pClient->GetAllianceID());
}

/*
 * channel lifetime
 */
void LSCService::LoadUserChannels()
{
    // player channels and stored access lists outlive restarts
    if (sConfig.chat.UserChannelCleanup)
        LSCDB::DeleteOrphanUserChannels();

    std::vector<UserChannelRow> rows;
    LSCDB::LoadUserChannels(rows);
    for (auto& cur : rows) {
        if (m_channels.find(cur.channelID) != m_channels.end())
            continue;
        CreateChannel(cur.channelID, cur.ownerID, cur.displayName.c_str(), cur.motd, (cur.password.empty() ? nullptr : cur.password.c_str()),
                      cur.comparisonKey.c_str(), LSC::Type::normal, cur.cspa, cur.channelID, cur.channelID, cur.memberless, cur.mailingList,
                      false, cur.languageRestriction);
    }

    std::vector<std::pair<int32, LSCChannelMod>> mods;
    LSCDB::LoadChannelMods(mods);
    for (auto& cur : mods) {
        LSCChannel* pChan = GetChannelByID(cur.first);
        if (pChan != nullptr) {
            pChan->LoadMod(cur.second);
        } else {
            // alliance channels are made at login
            m_storedMods[cur.first].push_back(cur.second);
        }
    }

    // channels from before access lists existed are public
    for (auto& cur : rows) {
        LSCChannel* pChan = GetChannelByID(cur.channelID);
        if ((pChan == nullptr) or pChan->HasDefaultMod())
            continue;
        LSCChannelMod mod(0, LSC::Mode::chConversationalist);
        pChan->LoadMod(mod);
        LSCDB::SaveChannelMod(cur.channelID, mod);
    }

    _log(LSC__CHANNELS, "Loaded %u user channels and %u access entries", (uint32)rows.size(), (uint32)mods.size());
}

bool LSCService::DestroyIfAbandoned(LSCChannel* pChan)
{
    // private conversations go when the last member leaves; other user channels (chat.UserChannelCleanup) when nobody is subscribed
    if ((pChan == nullptr) or (pChan->GetMemberCount() > 0))
        return false;

    int32 channelID = pChan->GetChannelID();
    if (!pChan->GetTemporary()) {
        if (!pChan->IsUserChannel() or !sConfig.chat.UserChannelCleanup)
            return false;
        if (m_db.GetSubscriberCount(channelID) > 0)
            return false;
    }

    _log(LSC__CHANNELS, "Destroying abandoned channel %i - %s", channelID, pChan->GetDisplayName().c_str());
    m_db.DeleteChannel(channelID);
    m_channels.erase(channelID);
    SafeDelete(pChan);
    return true;
}

void LSCService::CharacterLogout(Client* pClient)
{
    // copy, since leaving changes the client's set
    std::set<LSCChannel*> channels = pClient->GetChannels();
    for (auto cur : channels) {
        cur->LeaveChannel(pClient);
        DestroyIfAbandoned(cur);
    }
}

void LSCService::EnforceMembership(Client* pClient)
{
    // after a session change: leave the local/constellation/region/corp/alliance/fleet channels the character no longer belongs to
    if ((pClient == nullptr) or (pClient->GetChar().get() == nullptr))
        return;

    std::set<LSCChannel*> channels = pClient->GetChannels();
    for (auto cur : channels) {
        switch (cur->GetType()) {
            case LSC::Type::solarsystem:
            case LSC::Type::solarsystem2:
            case LSC::Type::constellation:
            case LSC::Type::region:
            case LSC::Type::corp:
            case LSC::Type::alliance:
            case LSC::Type::fleet:
            case LSC::Type::wing:
            case LSC::Type::squad: {
                std::string msg;
                if (CanAccess(pClient, cur, msg))
                    continue;
                _log(LSC__CHANNELS, "%s no longer belongs in channel %i (%s)", pClient->GetName(), cur->GetChannelID(), msg.c_str());
                cur->LeaveChannel(pClient);
                DestroyIfAbandoned(cur);
            } break;
            default:
                break;
        }
    }
}

void LSCService::SystemUnload(uint32 systemID, uint32 constID, uint32 regionID)
{
    std::map<int32, LSCChannel*>::iterator itr = m_channels.find(systemID);
    if (itr != m_channels.end()) {
        SafeDelete(itr->second);
        m_channels.erase(itr);
    }
    /** @todo  find a way to track usages of region and const channels to delete when no longer used */
    //  is this needed?
}

LSCChannel* LSCService::CreateChannel(int32 channelID, uint32 ownerID, const char *name, std::string motd, const char *password, const char *compkey,
                                      LSC::Type type/*LSC::Type::normal*/, uint32 cspa/*0*/, int32 groupMessageID/*0*/, int32 channelMessageID/*0*/, bool memberless/*false*/,
                                      bool maillist/*false*/, bool temporary/*false*/, bool languageRestriction/*false*/) {
    // test for invalid channelID (alliance not implemented yet)
    if (channelID == 0)
        return nullptr;

    LSCChannel* channel = new LSCChannel(this, channelID, type, ownerID, name, compkey, motd, memberless, password, maillist, cspa, temporary, languageRestriction, groupMessageID, channelMessageID);
    return (m_channels[channelID] = channel);
}

LSCChannel* LSCService::GetChannelByID(int32 channelID)
{
    std::map<int32, LSCChannel*>::iterator itr = m_channels.find(channelID);
    if (itr != m_channels.end())
        return itr->second;
    return nullptr;
}

LSCChannel* LSCService::GetChannelByName(std::string channelName)
{
    // joinExisting by name: public (normal) channels only, never someone's private conversation
    for (auto cur : m_channels)
        if ((cur.second->GetType() == LSC::Type::normal) and !cur.second->GetTemporary() and (cur.second->GetDisplayName() == channelName))
            return cur.second;
    return nullptr;
}

void LSCService::CreateSystemChannel(int32 channelID)
{
    if (channelID == 0)
        return;
    if (m_channels.find(channelID) != m_channels.end())
        return;

    LSC::Type type = LSC::Type::normal;
    std::string name= "", motd = "";
    int32 messageID = -1, grpMsgID = 0;
    uint32 ownerID = channelID;

    /** @todo  i know this isnt used much, but wtf was i thinking hitting db for names, when they are in static data?? */
    if (IsRegionID(channelID)) {
        type = LSC::Type::region;
        name = "Region";
        motd = m_db.GetRegionName(channelID);
        grpMsgID = 1;
    } else if (IsConstellationID(channelID)) {
        type = LSC::Type::constellation;
        name = "Constellation";
        motd = m_db.GetConstellationName(channelID);
        grpMsgID = 2;
    } else if (IsKSpaceID(channelID)) {
        type = LSC::Type::solarsystem2;
        name = "Local";
        motd = m_db.GetSolarSystemName(channelID);
        grpMsgID = 3;
    } else if (IsWSpaceID(channelID)) {
        type = LSC::Type::solarsystem;
        name = "System";
        motd = m_db.GetSolarSystemName(channelID);
        grpMsgID = 3;
    } else if (IsNPCCorp(channelID)) {
        type = LSC::Type::corp;
        name = "Corp";
        motd = m_db.GetCorporationName(channelID);
        grpMsgID = 263235;
    } else if (IsPlayerCorp(channelID)) {
        type = LSC::Type::corp;
        name = "Corp";
        motd = m_db.GetCorporationName(channelID);
        grpMsgID = 263235;
        messageID = 0;
    } else if (IsAlliance(channelID)) {
        type = LSC::Type::alliance;
        name = "Alliance";
        motd = m_db.GetAllianceName(channelID);
        grpMsgID = 5;
        messageID = 0;
    } else if (IsFleetID(channelID)) {
        type = LSC::Type::fleet;
        name = sFltSvc.GetFleetName(channelID);
        motd = sFltSvc.GetFleetDescription(channelID);
        messageID = 0;
        ownerID = sFltSvc.GetFleetLeaderID(channelID);
    } else if (IsWingID(channelID)) {
        type = LSC::Type::wing;
        name = sFltSvc.GetWingName(channelID);
        motd = name;
        motd += "<br>";
        motd += sFltSvc.GetFleetDescription(channelID);
        messageID = 0;
        //ownerID = sFltSvc.GetWingLeaderID(channelID);
        ownerID = sFltSvc.GetFleetLeaderID(channelID);
    } else if (IsSquadID(channelID)) {
        type = LSC::Type::squad;
        name = sFltSvc.GetSquadName(channelID);
        motd = name;
        motd += "<br>";
        motd += sFltSvc.GetFleetDescription(channelID);
        messageID = 0;
        //ownerID = sFltSvc.GetSquadLeaderID(channelID);
        ownerID = sFltSvc.GetFleetLeaderID(channelID);
    } else {
        // not sure what to do here....should never hit
    }
    std::string comStr = motd;
    boost::algorithm::to_lower(comStr);
    comStr.erase(std::remove(comStr.begin(), comStr.end(), ' '), comStr.end());
    LSCChannel* pChan = CreateChannel(channelID, ownerID, name.c_str(), motd, nullptr, comStr.c_str(), type, cspa, grpMsgID, messageID);

    // the client checks int channels (alliance) against their access list: members may talk
    if ((pChan != nullptr) and pChan->HasIntID()) {
        std::map<int32, std::vector<LSCChannelMod>>::iterator itr = m_storedMods.find(channelID);
        if (itr != m_storedMods.end()) {
            for (auto& cur : itr->second)
                pChan->LoadMod(cur);
            m_storedMods.erase(itr);
        }
        if (!pChan->HasDefaultMod())
            pChan->LoadMod(LSCChannelMod(0, LSC::Mode::chConversationalist));
    }
}

void LSCService::SetSystemChannelMOTD(int32 channelID, const std::string& motd)
{
    LSCChannel* pChan(GetChannelByID(channelID));
    if (pChan != nullptr)
        pChan->SetMOTD(motd);
}

void LSCService::DeleteSystemChannel(int32 channelID)
{
    std::map<int32, LSCChannel*>::iterator itr = m_channels.find(channelID);
    if (itr == m_channels.end())
        return;

    itr->second->Evacuate(nullptr);
    SafeDelete(itr->second);
    m_channels.erase(itr);
}

void LSCService::CreateStaticChannels() {
    // hardcode creating server static channels during server startup
    // these are all set to memberless to avoid constant updates
    std::ostringstream str;

    // world.globalChat = false: no server wide public channels, only the GM command channel
    if (sConfig.world.globalChat) {

// basic template
    //CreateChannel(channelID, ownerID, "name", "motd", "password" or nullptr, "*title*", LSC::Type::normal, cspa, 0, 0, true);

//Help
// Incursion Rookie Help MOTD
    str.str("");
    str << "<br><color=0xff007fff><b>Welcome to the EVEmu <u>EVE Online: Crucible</u> Emulator</b></color><br><br>";
    str << "<color=0xff00ff00><b>Topic:</b></color>  ";
    str << "<color=0xffffffff>EVE-Online related rookie help.</color><br><br>";
    str << "<color=0xff00ff00><b>Rules:</b></color>  ";
    str << "<color=0xffffffff>No WTB, WTS, WTT, PC, advertising, recruiting, scamming, offering private help in any form or begging in this channel.<br>";
    str << "No CAPS or text-decoration.</color><br><br>";
    str << "<color=0xff00ff00><b>Language:</b></color>  ";
    str << "<color=0xffffffff>This channel is ENGLISH ONLY.</color><br><br>";
    str << "<color=0xff00ff00><b>How to contact a GM:</b> </color>  ";
    str << "<color=0xffffffff>Right now, our petition service isnt operational, so to contact a GM, post on the forums.</color><br><br>";
    //str << "<color=0xffffffff>File a petition (F12 - Petitions - New Petition)</color><br><br>";
    str << "<color=0xff00ff00><b>ISK Advertising:</b></color>  ";
    str << "<color=0xffff0000>Contrary to what they spam, EVEmu does not authorize any person or site to sell ISK for RL cash.</color><br><br>";
    str << "<color=0xff00ff00><b>Please Note:</b></color><br>";
    str << "<color=0xffffffff>There are no third party applications (.exe) that will magically give you any type of ship you wish or hack your wallet. Please report characters advertising these types of links IMMEDIATELY via petition and DO NOT download and try them. <br><br>";
    // str << "Sister of EVE storyline starts in <url=showinfo:5//30005001><u>Arnon</url></b></u> <b>with <url=showinfo:1378//3019356><u>Sister Alitura</url></b></u>. <br><br></color>";
    CreateChannel(1, 1, "Rookie Help", str.str().c_str(), nullptr, "help", LSC::Type::normal, cspa, 263238, -1, true);
// Incursion Help MOTD
    str.str("");
    str << "<br><color=0xff007fff><b>Welcome to the EVEmu <u>EVE Online: Crucible</u> Emulator</color><br><br>";
    str << "<color=0xff00ff00><b>Topic:</b></color>  ";
    str << "<color=0xffffffff>EVE-Online related help.</color><br><br>";
    str << "<color=0xff00ff00><b>Rules:</b></color>  ";
    str << "<color=0xffffffff>No WTB, WTS, WTT, PC, advertising, recruiting, scamming, offering private help in any form or begging in this channel.<br>";
    str << "No CAPS or text-decoration.</color><br><br>";
    str << "<color=0xff00ff00><b>Language:</b></color>  ";
    str << "<color=0xffffffff>This channel is ENGLISH ONLY.</color><br><br>";
    str << "<color=0xff00ff00><b>How to contact a GM:</b> </color>  ";
    str << "<color=0xffffffff>Right now, our petition service isnt operational, so to contact a GM, post on the forums.</color><br><br>";
    //str << "<color=0xffffffff>File a petition (F12 - Petitions - New Petition)</color><br><br>";
    str << "<color=0xff00ff00><b>ISK Advertising:</b></color>  ";
    str << "<color=0xffff0000>Contrary to what they spam, EVEmu does not authorize any person or site to sell ISK for RL cash.</color><br><br>";
    CreateChannel(2, 1, "Help", str.str().c_str(), nullptr, "help", LSC::Type::normal, cspa, 263238, -1/*263262*/, true);
//Empires/Factions
    CreateChannel(10, 1, "Caldari", "Caldari Faction", nullptr, "caldari", LSC::Type::normal, cspa, 263329, -1/*263268*/, true);
    CreateChannel(11, 1, "Amarr", "Amarr Faction", nullptr, "amarr", LSC::Type::normal, cspa, 263329, -1/*263246*/, true);
    CreateChannel(12, 1, "Minmatar", "Minmatar Faction", nullptr, "minmatar", LSC::Type::normal, cspa, 263329, -1/*263281*/, true);
    CreateChannel(13, 1, "Gallente", "Gallente Faction", nullptr, "gallente", LSC::Type::normal, cspa, 263329, -1/*263271*/, true);
    CreateChannel(14, 1, "Jove", "Jove Faction", nullptr, "jove", LSC::Type::normal, cspa, 263329, -1/*263258*/, true);
// Incursion recruitment MOTD...
    str.str("");
    str << "<br><color=0xffffffff>Welcome to the recruitment channel.<br><br>";
    str << "This channel is intended for those players looking to find a new corporation, as well as those looking to enlist new players.<br>";
    str << "Other activities, such as non-recruitment discussion and scamming, are not permitted in this channel.";
//Corporate
    CreateChannel(20, 9, "Recruitment", str.str().c_str(), nullptr, "recruitment", LSC::Type::normal, cspa, 263235, -1/*263286*/, true);
    str.str("");
    str << "<br><color=0xffffffff>Welcome to this</color> <color=0xff00ffff>Corporate CEO</color><color=0xffffffff> channel.<br><br>";
    str << "This channel is intended for corp CEOs to discuss business as they see fit.</color>";
    CreateChannel(21, 9, "CEO", str.str().c_str(), nullptr, "ceo", LSC::Type::normal, cspa, 263235, -1/*263287*/, true);
//Alliance
    /*
    str.str("");
    str << "<color=0xffffffff>Welcome to this</color> <color=0xff00ffff>Alliance</color><color=0xffffffff>  channel.  This channel is intended for corp CEOs to discuss business as they see fit.</color>";
    CreateChannel(31, 1, "Smacktalk", str.str().c_str(), nullptr, "smacktalk", LSC::Type::normal, cspa, 263330, -1, true);
    CreateChannel(32, 1, "Rumour Mill", str.str().c_str(), nullptr, "rumourmill", LSC::Type::normal, cspa, 263330, -1, true);
    CreateChannel(33, 1, "Freelancer", str.str().c_str(), nullptr, "freelancer", LSC::Type::normal, cspa, 263330, -1, true);
    */
//Trade
    str.str("");
    str << "<br><color=0xffffffff>Welcome to this</color> <color=0xff00ffff>Trade</color><color=0xffffffff> channel.<br><br>";
    str << "This channel is intended for those players looking to trade the various items as referenced in the channel title.</color>";
    CreateChannel(40, 1, "Other", str.str().c_str(), nullptr, "other", LSC::Type::normal, cspa, 263240, -1/*263277*/, true);
    CreateChannel(41, 1, "Ships", str.str().c_str(), nullptr, "ships", LSC::Type::normal, cspa, 263240, -1/*263245*/, true);
    CreateChannel(42, 1, "Blueprints", str.str().c_str(), nullptr, "blueprints", LSC::Type::normal, cspa, 263240, -1/*263292*/, true);
    CreateChannel(43, 1, "Modules and Munitions", str.str().c_str(), nullptr, "modulesandmunitions", LSC::Type::normal, cspa, 263240, -1/*263254*/, true);
    CreateChannel(44, 1, "Minerals and Manufacturing", str.str().c_str(), nullptr, "mineralsandmanufacturing", LSC::Type::normal, cspa, 263240, -1/*263275*/, true);
//Science and Industry
    str.str("");
    str << "<br><color=0xffffffff>Welcome to this</color> <color=0xff00ffff>Science and Industry</color><color=0xffffffff> channel.<br><br>";
    str << "This channel is intended for those players looking to discuss the various items as referenced in the channel title.</color>";
    CreateChannel(50, 1, "Boosters", str.str().c_str(), nullptr, "boosters", LSC::Type::normal, cspa, 263331, -1/*263365*/, true);
    CreateChannel(51, 1, "Invention", str.str().c_str(), nullptr, "invention", LSC::Type::normal, cspa, 263331, -1/*263366*/, true);
    CreateChannel(52, 1, "Manufacturing", str.str().c_str(), nullptr, "manufacturing", LSC::Type::normal, cspa, 263331, -1/*263367*/, true);
    CreateChannel(53, 1, "Mining", str.str().c_str(), nullptr, "mining", LSC::Type::normal, cspa, 263331, -1/*263368*/, true);
    CreateChannel(54, 1, "Planetary Interaction", str.str().c_str(), nullptr, "planetaryinteraction", LSC::Type::normal, cspa, 263331, -1/*263369*/, true);
    CreateChannel(55, 1, "Research", str.str().c_str(), nullptr, "research", LSC::Type::normal, cspa, 263331, -1/*263370*/, true);
//Groups/Content
    str.str("");
    str << "<br><color=0xffffffff>Welcome to this</color> <color=0xff00ffff>Content</color><color=0xffffffff> channel.<br><br>";
    str << "This channel is intended for those players looking to discuss the various items as referenced in the channel title.</color>";
    CreateChannel(60, 1, "Incursions", str.str().c_str(), nullptr, "incursions", LSC::Type::normal, cspa, 263328, -1/*263289*/, true);
    CreateChannel(61, 1, "Ratting", str.str().c_str(), nullptr, "ratting", LSC::Type::normal, cspa, 263328, -1/*263338*/, true);
    CreateChannel(62, 1, "Scanning", str.str().c_str(), nullptr, "scanning", LSC::Type::normal, cspa, 263328, -1/*263339*/, true);
    CreateChannel(63, 1, "Wormholes", str.str().c_str(), nullptr, "wormholes", LSC::Type::normal, cspa, 263328, -1/*263340*/, true);
//Misc
    str.str("");
    str << "<br><color=0xffffffff>Welcome to the <url=https://evemu.dev>Free Wrecks</url> channel.<br>";
    str << "Here you can offer your abandoned mission wrecks to any willing freelance salvager in New Eden.</color><br><br>";
    str << "<b><u><color=0xff00ffff>Salvagers</color></u><br><color=0xff00ff00>Alphas</color></b>";
    str << "<color=0xffffffff> - Basic salvage fits by race: </color><br>";
    str << "<url=fitting:16236:31083;2:25861;4:8135;1:31370;1:1319;2:4435;1:5973;1:24348;4::>Amarr</url><color=0xffffffff> - </color>";
    str << "<url=fitting:16238:25861;4:1319;2:31370;1:31083;2:4435;2:5973;1:24348;4::>Caldari</url><color=0xffffffff> - </color>";
    str << "<url=fitting:16240:31083;2:25861;4:8135;1:31370;1:1319;2:4435;1:5973;1:24348;4::>Gallente</url><color=0xffffffff> - </color>";
    str << "<url=fitting:16242:25861;4:1319;2:31370;1:31083;2:6001;1:4435;2:24348;4::>Minmatar</url><color=0xffffffff> </color><br><br>";
    /*  this suggests t2 shit which isnt available yet
    str << "<b><color=0xff00ff00>Omegas</color></b><color=0xffffffff> - You should be able to use a </color><url=showinfo:2998>Noctis</url>";
    str << "<color=0xffffffff>, </color><url=showinfo:30836>Salvager II's</url><color=0xffffffff> &amp; </color><url=showinfo:4250>Tractor Beam II's</url><br>";
    */
    str << "<b><u><color=0xff00ffff>Mission Runners</color></b></u><br><color=0xffffffff>";
    str << "Please state where your bookmark will be traded, contracted or if you're hoping to fleet with the salvager looking for work. <br>";
    str << "If you are contracting or trading bookmarks dont forget to</color> <color=0xff007fff>Abandon all wrecks and containers</color><color=0xffffffff>.</color><br><br>";
    str << "<b><u><color=0xff00ff00>Useful Links.</color></u><br>";
    str << "<loc><url=http://evemaps.dotlan.net/>DOTLAN</url></loc></b><color=0xffffffff> - A database of everything you need to know about New Eden; maps, corporations, navigations and much more.</color><color=0xff0000ff>NOTE:</color><color=0xffffffff> While this site is specific for Tranquility, the maps and navigation are the same here on EVEmu.</color><br>";
    str << "<loc><url=http://o.smium.org>Osmium</url></loc><color=0xffffffff> - A site where pilots post their ship fittings to help players get the most out of their ship class.</color><br>";
    str << "<loc><url=http://eve-survival.org/wikka.php?wakka=MissionReports>EVE Survival</url></loc><color=0xffffffff> - A database of missions within New Eden. Here you can find information about gaining the upper hand on those sneaky NPCs and how to perfectly run the mission in question.</color><br>";
    str << "<loc><url=http://www.fuzzwork.co.uk/>Fuzz Work</url></loc><color=0xffffffff> - A brilliant site that has many awesome calculators for LP stores, Blueprints, Invention, Ore and much more!</color><br>";
    CreateChannel(100, 2, "Free Wrecks", str.str().c_str(), nullptr, "freewrecks", LSC::Type::normal, cspa, 0, 0, true); //256739 <-- this was messageID from error about "channel already joined"
    //CreateChannel(101, 1, "", "motd", nullptr, "*title*", LSC::Type::normal, cspa, 0, 0, true);
    }


//GM Command channel
    str.str("");
    str << "<br><color=0xffffffff>Welcome to the</color> <color=0xff00ffff>GM Command</color><color=0xffffffff> channel.<br><br>";
    str << "This channel is intended for using dot commands.</color>";
    CreateChannel(2900000000, 1, "Command", str.str().c_str(), nullptr, "command", LSC::Type::custom, cspa, 0, 0, true);

    // the client checks int channels against their access list: everyone may talk in the public channels
    for (auto cur : m_channels)
        if (cur.second->HasIntID() and !cur.second->HasDefaultMod())
            cur.second->LoadMod(LSCChannelMod(0, LSC::Mode::chConversationalist));
}

void LSCService::SendServerMOTD(Client* pClient)
{
    std::map<int32, LSCChannel*>::iterator itr = m_channels.find(pClient->GetSystemID());
    if (itr != m_channels.end())
        itr->second->SendServerMOTD(pClient);
}

///////////////////////////////////////////////////////////////////////////////
//
// EveMail calls:
//
///////////////////////////////////////////////////////////////////////////////

PyResult LSCService::GetMyMessages(PyCallArgs &call) {
    return(m_db.GetMailHeaders(call.client->GetCharacterID()));
}


PyResult LSCService::GetMessageDetails(PyCallArgs &call, PyInt* readerID, PyInt* messageID) {
    //TODO: verify ability to read this message...

    return(m_db.GetMailDetails(messageID->value(), readerID->value()));
}


PyResult LSCService::Page(PyCallArgs &call, PyList* recipientIDs, PyRep* subject, PyRep* body) {
    std::vector <int32> recipients;

    PyList::const_iterator list_2_cur = recipientIDs->begin();
    for (size_t list_2_index(0); list_2_cur != recipientIDs->end(); ++list_2_cur, ++list_2_index) {
        if (!(*list_2_cur)->IsInt()) {
            _log(XMLP__DECODE_ERROR, "Decode Call_Page failed: Element %u in list list_2 is not an integer: %s", list_2_index, (*list_2_cur)->TypeString());
            return nullptr;
        }

        const PyInt* t = (*list_2_cur)->AsInt();
        recipients.push_back(t->value());
    }

    std::string subjectStr = PyRep::StringContent(subject);
    std::string bodyStr = PyRep::StringContent(body);

    _log(SERVICE__MESSAGE, "%s: Received evemail msg with subject '%s': %s", call.client->GetName(), subjectStr.c_str(), bodyStr.c_str());

    SendMail(call.client->GetCharacterID(), recipients, subjectStr, bodyStr);

    return nullptr;
}


//stuck here to be close to related functionality
void LSCService::SendMail(uint32 sender, const std::vector<int32> &recipients, const std::string &subject, const std::string &content) {
    // server generated mail goes to the eve mail window (mailMgr).  the pre-Apocrypha eveMail/OnMessage
    //  path is not read by the Crucible client
    MailMgr::SendMail(sender, recipients, 0, 0, subject, content);
}


//stuck here to be close to related functionality
//theres a lot of duplicated crap in here...
//this could be replaced by the SendNewEveMail if it weren't in the Client
void Client::SelfEveMail(const char* subject, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    char* str = nullptr;
    vasprintf(&str, fmt, args);
    assert(str);

    va_end(args);

    this->m_lsc->SendMail(GetCharacterID(), GetCharacterID(), subject, str);
    SafeFree(str);
}


PyResult LSCService::MarkMessagesRead(PyCallArgs &call, PyList* messageIDs) {
    std::vector <int32> messages;

    PyList::const_iterator list_2_cur = messageIDs->begin();
    for (size_t list_2_index(0); list_2_cur != messageIDs->end(); ++list_2_cur, ++list_2_index) {
        if (!(*list_2_cur)->IsInt()) {
            _log(XMLP__DECODE_ERROR, "Decode Call_Page failed: Element %u in list list_2 is not an integer: %s", list_2_index, (*list_2_cur)->TypeString());
            return nullptr;
        }

        const PyInt* t = (*list_2_cur)->AsInt();
        messages.push_back(t->value());
    }

    std::vector<int32>::iterator cur, end;
    cur = messages.begin();
    end = messages.end();
    for(; cur != end; cur++) {
        m_db.MarkMessageRead(*cur);
    }
    return nullptr;
}


PyResult LSCService::DeleteMessages(PyCallArgs &call, PyInt* channelID, PyList* messageIDs) {
    std::vector<int32> messages;

    PyList::const_iterator list_2_cur = messageIDs->begin();
    for (size_t list_2_index(0); list_2_cur != messageIDs->end(); ++list_2_cur, ++list_2_index) {
        if (!(*list_2_cur)->IsInt()) {
            _log(XMLP__DECODE_ERROR, "Decode Call_DeleteMessages failed: Element %u in list list_2 is not an integer: %s", list_2_index, (*list_2_cur)->TypeString());
            return nullptr;
        }

        const PyInt* t = (*list_2_cur)->AsInt();
        messages.push_back(t->value());
    }

    if (channelID->value() != (int32)call.client->GetCharacterID()) {
        _log(SERVICE__ERROR, "%s (%d) tried to delete messages in channel %u. Denied.", call.client->GetName(), call.client->GetCharacterID(), channelID->value());
        return nullptr;
    }

    std::vector<int32>::iterator cur, end;
    cur = messages.begin();
    end = messages.end();
    for(; cur != end; cur++) {
        m_db.DeleteMessage(*cur, channelID->value());
    }

    return nullptr;
}

PyResult LSCService::ExecuteCommand(Client *from, const char *msg) {
    return(m_commandDispatch->Execute(from, msg));
}
