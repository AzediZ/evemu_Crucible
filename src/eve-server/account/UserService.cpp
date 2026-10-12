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
    Author:        Bloody.Rabbit
*/

#include "eve-server.h"


#include "account/UserService.h"
#include "account/AccountDB.h"
#include "character/CharacterDB.h"
#include "services/ServiceManager.h"

MovementService::MovementService(EVEServiceManager *mgr) :
    Service("movementServer"),
    m_manager(mgr)
{
    this->Add("ResolveNodeID", &MovementService::ResolveNodeID);
}

PyResult MovementService::ResolveNodeID(PyCallArgs& call, PyInt* newWorldSpaceId)
{
    sLog.Yellow( "MovementService", "ResolveNodeID" );
    call.Dump(CHARACTER__DEBUG);

    return new PyInt(this->m_manager->GetNodeID());
}

UserService::UserService() :
    Service("userSvc", eAccessLevel_User)
{
    this->Add("GetRedeemTokens", &UserService::GetRedeemTokens);
    this->Add("ClaimRedeemTokens", &UserService::ClaimRedeemTokens);
    this->Add("ReverseRedeem", &UserService::ReverseRedeem);
    this->Add("GetCreateDate", &UserService::GetCreateDate);
    this->Add("ReportISKSpammer", &UserService::ReportISKSpammer);
    this->Add("ReportBot", &UserService::ReportBot);
    this->Add("ApplyPilotLicence", &UserService::ApplyPilotLicence);
}

PyResult UserService::GetRedeemTokens(PyCallArgs& call)
{
    /* dbutil.CRowset with the header (as in live packet logs):
     *   tokenID, massTokenID, typeID, quantity, label, description, dateTime, expireDateTime, availableDateTime, stationID
     */
    if (!sConfig.account.redeemEnabled)
        return new PyList();
    return AccountDB::GetRedeemTokens(call.client->GetUserID());
}

PyResult UserService::ClaimRedeemTokens(PyCallArgs& call, PyList* tokens, PyInt* charID)
{
    /* sm.RemoteSvc('userSvc').ClaimRedeemTokens(tokens, charID)   tokens = [(tokenID, massTokenID), ...]
     *  the client shows any UserError as a message (redeemsvc.ClaimRedeemTokens), so the receipt is RedeemTokenClaimed
     */
    if (!sConfig.account.redeemEnabled)
        throw UserError ("RedeemDisabled");

    uint32 ownerID(charID->value());
    if ((ownerID == 0) or (CharacterDB::GetAccountID(ownerID) != call.client->GetUserID()))
        throw UserError ("RedeemNoCharacter");

    // items go to the character's station:  the session station in game, the station it is docked in at character selection
    uint32 stationID(0);
    if (ownerID == call.client->GetCharacterID()) {
        if (call.client->IsDocked())
            stationID = call.client->GetStationID();
    } else if (call.client->GetCharacterID() == 0) {
        CharacterData data = CharacterData();
        if (CharacterDB::GetCharacterData(ownerID, data))
            stationID = data.stationID;
    } else {
        throw UserError ("RedeemNoCharacter");
    }
    if ((stationID == 0) or !sDataMgr.IsStation(stationID))
        throw UserError ("RedeemOnlyInStation");

    // check them all before spawning anything
    std::vector<AccountDB::RedeemToken> claim;
    int64 now((int64)GetFileTimeNow());
    for (PyList::const_iterator itr = tokens->begin(); itr != tokens->end(); ++itr) {
        uint32 tokenID(0);
        if ((*itr)->IsTuple() and ((*itr)->AsTuple()->size() > 0)) {
            tokenID = PyRep::IntegerValueU32((*itr)->AsTuple()->GetItem(0));
        } else {
            tokenID = PyRep::IntegerValueU32(*itr);
        }
        AccountDB::RedeemToken token = AccountDB::RedeemToken();
        if (!AccountDB::GetRedeemToken(tokenID, token) or (token.accountID != call.client->GetUserID()) or token.claimed
        or ((token.availableDateTime != 0) and (token.availableDateTime > now)))
            throw UserError ("RedeemTokenNotFound");
        if ((token.expireDateTime != 0) and (token.expireDateTime <= now))
            throw UserError ("RedeemTokenExpired");
        if (sItemFactory.GetType(token.typeID) == nullptr)
            throw UserError ("RedeemTokenNotFound");
        claim.push_back(token);
    }
    if (claim.empty())
        throw UserError ("RedeemNoTokens");

    std::string items;
    sItemFactory.SetUsingClient(call.client);
    for (auto cur : claim) {
        if (!AccountDB::ClaimRedeemToken(cur.tokenID, ownerID))
            continue;
        // a token bound to a station delivers there
        uint32 deliverTo((cur.stationID != 0) and sDataMgr.IsStation(cur.stationID) ? cur.stationID : stationID);
        ItemData data(cur.typeID, ownerID, locTemp, flagNone, cur.quantity);
        InventoryItemRef iRef = sItemFactory.SpawnItem(data);
        if (iRef.get() == nullptr) {
            _log(ITEM__ERROR, "ClaimRedeemTokens: unable to spawn %u x %u for token %u", cur.quantity, cur.typeID, cur.tokenID);
            continue;
        }
        iRef->Move(deliverTo, flagHangar, true);
        items += std::to_string(cur.quantity);
        items += " x ";
        items += sDataMgr.GetTypeName(cur.typeID);
        items += "<br>";
        _log(CLIENT__MESSAGE, "%s: redeemed token %u (%u x %u) to %u in %u", call.client->GetName(), cur.tokenID, cur.quantity, cur.typeID, ownerID, deliverTo);
    }
    sItemFactory.UnsetUsingClient();

    throw UserError ("RedeemTokenClaimed")
            .AddFormatValue ("items", new PyString(items))
            .AddLocationName ("station", stationID)
            .AddOwnerName ("char", ownerID);
}

PyResult UserService::ReverseRedeem(PyCallArgs& call, PyInt* itemID)
{
    //sm.RemoteSvc('userSvc').ReverseRedeem(item.itemID)
    InventoryItemRef iRef(sItemFactory.GetItemRef(itemID->value()));
    if ((iRef.get() == nullptr) or (iRef->ownerID() != call.client->GetCharacterID()))
        throw UserError ("ReverseRedeemingYouDoNotOwnItem");
    if ((iRef->flag() != flagHangar) or !sDataMgr.IsStation(iRef->locationID()))
        throw UserError ("ReverseRedeemingOnlyInStation");

    // only the configured types (none by default:  Crucible had no general reverse redeeming)
    bool allowed(false);
    std::stringstream types(sConfig.account.reverseRedeemTypes);
    std::string type;
    while (std::getline(types, type, ','))
        if (!type.empty() and (strtoul(type.c_str(), nullptr, 10) == iRef->typeID()))
            allowed = true;
    // packaged items only (an assembled ship or container holds more than its type)
    if (!allowed or iRef->isSingleton())
        throw UserError ("ReverseRedeemingIllegalType");

    uint32 typeID(iRef->typeID()), qty(iRef->quantity()), stationID(iRef->locationID());
    if (AccountDB::CreateRedeemToken(call.client->GetUserID(), typeID, qty, iRef->type().name(), "", stationID) == 0)
        throw UserError ("ReverseRedeemingFatalError");
    iRef->Delete();

    throw UserError ("ReverseRedeemingCompleted")
            .AddAmount ("quantity", (int)qty)
            .AddTypeName ("type", typeID)
            .AddLocationName ("station", stationID);
}

PyResult UserService::GetCreateDate(PyCallArgs& call)
{
    return new PyLong(call.client->GetChar()->createDateTime());
}

PyResult UserService::ReportISKSpammer(PyCallArgs& call, PyInt* characterID, PyRep* channel, PyList* spamEntries)
{
    // sm.RemoteSvc('userSvc').ReportISKSpammer(charID, channelDisplayName, spamEntries)  entries are '[time] who > text'
    if (characterID->value() == call.client->GetCharacterID())
        throw UserError ("ReportISKSpammerCannotReportYourself");

    std::string entries;
    for (PyList::const_iterator itr = spamEntries->begin(); itr != spamEntries->end(); ++itr) {
        if (!(*itr)->IsString() and !(*itr)->IsWString())
            continue;
        if (!entries.empty())
            entries += "\n";
        entries += PyRep::StringContent(*itr);
    }
    std::string chan(((channel == nullptr) or channel->IsNone()) ? "" : (channel->IsInt() ? std::to_string(channel->AsInt()->value())
                    : PyRep::StringContent(channel)));
    AccountDB::AddUserReport(call.client->GetCharacterID(), characterID->value(), 2, chan, entries);
    _log(CLIENT__MESSAGE, "%s reported %u as an ISK spammer in %s.", call.client->GetName(), characterID->value(), chan.c_str());
    return nullptr;
}

PyResult UserService::ReportBot(PyCallArgs& call, PyInt* itemID)
{
    // the client shows BotReported itself
    if (itemID->value() == call.client->GetCharacterID())
        throw UserError ("ReportBotCannotReportYourself");
    AccountDB::AddUserReport(call.client->GetCharacterID(), itemID->value(), 1, "", "");
    _log(CLIENT__MESSAGE, "%s reported %u as a bot.", call.client->GetName(), itemID->value());
    return nullptr;
}

PyResult UserService::ApplyPilotLicence(PyCallArgs& call, PyInt* itemID)
{
    /* sm.RemoteSvc('userSvc').ApplyPilotLicence(itemID, justQuery=True)
     *  the query answers 28DaysConfirmApplyServer {info}; the client asks and calls again with justQuery=False.
     *  one PLEX adds 30 days of game time to the account (only shown at character selection and the character sheet)
     */
    bool justQuery(true);
    std::map<std::string, PyRep*>::const_iterator itr = call.byname.find("justQuery");
    if (itr != call.byname.end())
        justQuery = (PyRep::IntegerValue(itr->second) != 0);

    InventoryItemRef iRef(sItemFactory.GetItemRef(itemID->value()));
    if ((iRef.get() == nullptr) or (iRef->ownerID() != call.client->GetCharacterID()))
        throw UserError ("28DaysYouDoNotOwnItem");
    if ((iRef->flag() != flagHangar) or !sDataMgr.IsStation(iRef->locationID()))
        throw UserError ("28DaysNotInStation");
    if (iRef->typeID() != EVEDB::invTypes::PilotLicence)
        throw CustomError ("Only a %s can add game time to your account.", sDataMgr.GetTypeName(EVEDB::invTypes::PilotLicence));

    uint32 accountID(call.client->GetUserID());
    int64 now((int64)GetFileTimeNow());
    int64 paidUntil(AccountDB::GetPaidUntil(accountID));
    int64 newPaidUntil(std::max(now, paidUntil) + (EvE::Time::Day * 30));

    std::string info;
    if (justQuery) {
        info = "You are about to use one ";
        info += sDataMgr.GetTypeName(EVEDB::invTypes::PilotLicence);
        info += " to add 30 days of game time to your account.<br><br>";
        if (paidUntil > now) {
            info += "Your game time currently runs until ";
            info += Win32TimeToString(paidUntil);
            info += ".<br>";
        }
        info += "It will then run until ";
        info += Win32TimeToString(newPaidUntil);
        info += ".<br><br>Do you want to continue?";
        throw UserError ("28DaysConfirmApplyServer")
                .AddFormatValue ("info", new PyString(info));
    }

    // use up one licence of the stack
    if (iRef->quantity() > 1) {
        iRef->AlterQuantity(-1, true);
    } else {
        iRef->Delete();
    }
    AccountDB::SetPaidUntil(accountID, newPaidUntil);
    _log(CLIENT__MESSAGE, "%s applied a pilot licence; account %u is paid until %s.", call.client->GetName(), accountID, Win32TimeToString(newPaidUntil).c_str());

    // the receipt
    info = "30 days of game time have been added to your account.<br>It now runs until ";
    info += Win32TimeToString(newPaidUntil);
    info += ".";
    throw UserError ("28DaysApplyCompleted")
            .AddFormatValue ("info", new PyString(info));
}
