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
    Author:        Zhur, Aknor Jaden
*/

/** @todo this entire file needs review */

#include "eve-server.h"
#include "Client.h"
#include "chat/LSCDB.h"
#include "chat/LSCService.h"

void LSCDB::GetChannelNames(uint32 charID, std::vector<std::string> & names) {
    DBQueryResult res;

    if (!sDatabase.RunQuery(res,
        "SELECT"
        "    ch.characterName, "
        "    cr.corporationName "
        " FROM chrCharacters AS ch"
        "  LEFT JOIN crpCorporation AS cr USING (corporationID) "
        " WHERE ch.characterID = %u ", charID))
    {
        _log(DATABASE__ERROR, "Error in query: %s", res.error.c_str());
        return;
    }

    DBResultRow row;
    if (!res.GetRow(row)) {
        _log(SERVICE__ERROR, "CharID %u isn't present in the database", charID);
        return;
    }

    names.push_back(row.GetText(0));    // charName
    names.push_back(row.GetText(1));    // corpName
}

int32 LSCDB::GetNextAvailableChannelID()
{
    /** @todo fix this shit */
    DBQueryResult res;
    if ( !sDatabase.RunQuery( res,
        "SELECT"
        "    channelID "
        " FROM channels "
        " WHERE channelID >= %i ORDER BY channelID", LSCService::BASE_CHANNEL_ID ))
    {
        _log(DATABASE__ERROR, "Error in query: %s", res.error.c_str());
        return 0;
    }

    int32 currentChannelID = LSCService::BASE_CHANNEL_ID;

    DBResultRow row;
    while( res.GetRow(row) )
    {
        if ( ++currentChannelID < row.GetInt( 0 ) )
            return currentChannelID;
    }

        // Check to make sure that the next available channelID is not equal to the Maximum channel ID value
    if ( currentChannelID <= LSCService::MAX_CHANNEL_ID )
        return currentChannelID;

    return 0;    // No free channel IDs found (this should never happen as there are way too many IDs to exhaust)
}

void LSCDB::UpdateChannelInfo(LSCChannel *channel) {
    std::string name, motd, key, password("NULL");
    sDatabase.DoEscapeString(name, channel->GetDisplayName());
    sDatabase.DoEscapeString(motd, channel->GetMOTD());
    sDatabase.DoEscapeString(key, channel->GetComparisonKey());
    if (!channel->GetPassword().empty()) {
        std::string pw;
        sDatabase.DoEscapeString(pw, channel->GetPassword());
        password = "'" + pw + "'";
    }

    DBerror err;
    if (!sDatabase.RunQuery(err,
        " INSERT INTO channels"
        "   (channelID, ownerID, displayName, motd, comparisonKey, memberless, password, mailingList, cspa, languageRestriction)"
        " VALUES (%i, %u, '%s', '%s', '%s', %u, %s, %u, %u, %u)"
        " ON DUPLICATE KEY UPDATE"
        "  ownerID=VALUES(ownerID),"
        "  displayName=VALUES(displayName),"
        "  motd=VALUES(motd),"
        "  comparisonKey=VALUES(comparisonKey),"
        "  memberless=VALUES(memberless),"
        "  password=VALUES(password),"
        "  mailingList=VALUES(mailingList),"
        "  cspa=VALUES(cspa),"
        "  languageRestriction=VALUES(languageRestriction)",
        channel->GetChannelID(),
        channel->GetOwnerID(),
        name.c_str(),
        motd.c_str(),
        key.c_str(),
        (channel->GetMemberless() ? 1 : 0),
        password.c_str(),
        (channel->GetMailingList() ? 1 : 0),
        channel->GetCSPA(),
        (channel->GetLanguageRestriction() ? 1 : 0)))
    {
        _log(DATABASE__ERROR, "Error in query: %s", err.c_str());
    }
}

void LSCDB::UpdateSubscription(int32 channelID, Client* pClient) {
    DBerror err;
    sDatabase.RunQuery(err,
        " INSERT IGNORE INTO channelChars "
        " (channelID, corpID, charID, allianceID, role, extra) "
        " VALUES (%i, %i, %i, %i, %lli, 0) ",
        channelID, pClient->GetCorporationID(),  pClient->GetCharacterID(), pClient->GetAllianceID(), pClient->GetAccountRole());
}

void LSCDB::DeleteChannel(int32 channelID)
{
    DBerror err;
    sDatabase.RunQuery(err, "DELETE FROM channelMods WHERE channelID=%i", channelID);
    sDatabase.RunQuery(err, "DELETE FROM channelChars WHERE channelID=%i", channelID);
    sDatabase.RunQuery(err, "DELETE FROM channels WHERE channelID=%i", channelID);
}

void LSCDB::DeleteSubscription(int32 channelID, uint32 charID)
{
    DBerror err;
    sDatabase.RunQuery(err, "DELETE FROM channelChars WHERE channelID=%i AND charID=%u", channelID, charID );
}


// Function: Return true or false result for the check of whether or not the specified
// channel 'displayName' is already being used by a channel.
bool LSCDB::IsChannelNameAvailable(std::string name)
{
    DBQueryResult res;

    // MySQL query channels table for any channel whose displayName matches "name":
    if (!sDatabase.RunQuery(res,
        "SELECT"
        "    displayName "
        " FROM channels "
        " WHERE displayName = upper('%s')", name.c_str()))
    {
        _log(DATABASE__ERROR, "Error in query: %s", res.error.c_str());
        return false;
    }

    DBResultRow row;

    // Return true (this 'displayName' not in use) if there are no rows returned by the query:
    if (!res.GetRow(row))
        return true;

    return false;
}


// Function: Return true or false result for the check of whether or not the specified
// channelID is available to be taken for a new channel's channelID.
bool LSCDB::IsChannelIDAvailable(int32 channelID)
{
    DBQueryResult res;

    if (!sDatabase.RunQuery(res,
        "SELECT"
        "    channelID "
        " FROM channels "
        " WHERE channelID = %i", channelID ))
    {
        _log(DATABASE__ERROR, "Error in query: %s", res.error.c_str());
        return false;
    }

    DBResultRow row;

    // Return true (this channelID not in use) if there are no rows returned by the query:
    if (!(res.GetRow(row)))
        return true;

    return false;
}


// Function: Return true or false result for the check of whether or not the channel
// specified by channelID is already subscribed to by the character specified by charID.
bool LSCDB::IsChannelSubscribedByThisChar(uint32 charID, int32 channelID)
{
    DBQueryResult res;

    if (!sDatabase.RunQuery(res,
        "SELECT"
        "   channelID, "
        "   charID "
        " FROM channelChars "
        " WHERE channelID = %i AND charID = %u", channelID, charID ))
    {
        _log(DATABASE__ERROR, "Error in query: %s", res.error.c_str());
        return false;
    }

    DBResultRow row;

    // Return false (no subscription exists) if there are no rows returned by the query:
    if (!(res.GetRow(row)))
        return false;

    return true;
}

int32 LSCDB::GetChannelID(std::string &name) {
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT channelID FROM channels WHERE displayName RLIKE '%s'", name.c_str())) {
        _log(DATABASE__ERROR, "Error in query: %s", res.error.c_str());
        return 0;
    }

    DBResultRow row;
    if (!res.GetRow(row)) {
        _log(SERVICE__ERROR, "Channel named '%s' isn't present in the database", name.c_str() );
        return 0;
    }

    return row.GetInt(0);
}

// Function: Query 'channels' table for the channel whose 'channelID' matches the ID specified,
// then return all parameters for that channel.
void LSCDB::GetChannelInformation(int32 channelID, std::string & name,
        std::string & motd, uint32 & ownerid, std::string & compkey,
        bool & memberless, std::string & password, bool & maillist,
        uint32 & cspa)
{
    DBQueryResult res;

    if (!sDatabase.RunQuery(res,
        "SELECT"
        "   channelID, "
        "   displayName, "
        "   motd, "
        "   ownerID, "
        "   comparisonKey, "
        "   memberless, "
        "   password, "
        "   mailingList, "
        "   cspa "
        " FROM channels "
        " WHERE channelID = %i", channelID))
    {
        _log(DATABASE__ERROR, "Error in query: %s", res.error.c_str());
        return;
    }

    DBResultRow row;

    if (!(res.GetRow(row)))
    {
            _log(SERVICE__ERROR, "Channel %i isn't present in the database", channelID );
            return;
    }

    name = (row.IsNull(1) ? "" : row.GetText(1));    // empty displayName field in channels table row returns NULL, so fill this string with "" in that case
    motd = (row.IsNull(2) ? "" : row.GetText(2));    // empty motd field in channels table row returns NULL, so fill this string with "" in that case
    ownerid = row.GetUInt(3);
    compkey = (row.IsNull(4) ? "" : row.GetText(4)); // empty comparisonKey field in channels table row returns NULL, so fill this string with "" in that case
    memberless = row.GetBool(5);
    password = (row.IsNull(6) ? "" : row.GetText(6));// empty password field in channels table row returns NULL, so fill this string with "" in that case
    maillist = row.GetBool(7);
    cspa = row.GetUInt(8);
}

// Function: Query the 'channelChars' table for all channels subscribed to by the character specified by charID and
// return lists of parameters for all of those channels as well as a total channel count.
void LSCDB::GetChannelSubscriptions(uint32 charID, std::vector<long> & ids, std::vector<std::string> & names,
        std::vector<std::string> & MOTDs, std::vector<unsigned long> & ownerids, std::vector<std::string> & compkeys,
        std::vector<int> & memberless, std::vector<std::string> & passwords, std::vector<int> & maillists,
        std::vector<int> & cspas, int & channelCount)
{
    DBQueryResult res;

    // Cross-reference "channelchars" table with "channels" table using the charID
    // The result is a two column multi-row structure where each row is a channel
    // that the character (charID) is subscribed to where the channel ID is presented
    // in the first column and the display name of that channel in the second column
    if (!sDatabase.RunQuery(res,
        "SELECT"
        "   channelID, "
        "   displayName, "
        "   motd, "
        "   ownerID, "
        "   comparisonKey, "
        "   memberless, "
        "   password, "
        "   mailingList, "
        "   cspa "
        " FROM channels "
        " WHERE channelID = ANY ("
        "   SELECT channelID FROM channelChars WHERE charID = %u )", charID))
    {
        _log(DATABASE__ERROR, "Error in query: %s", res.error.c_str());
        return;
    }

    DBResultRow row;
    int rowCount = 0;

    // Traverse through all rows in the query result and copy the IDs and displayNames to the
    // "ids" and "names" vectors for return to the calling function:
    while(res.GetRow(row))
    {
        ++rowCount;

        ids.push_back(row.GetInt(0));
        names.push_back((row.GetText(1) == NULL ? "" : row.GetText(1)));    // empty displayName field in channels table row returns NULL, so fill this string with "" in that case
        MOTDs.push_back((row.GetText(2) == NULL ? "" : row.GetText(2)));    // empty motd field in channels table row returns NULL, so fill this string with "" in that case
        ownerids.push_back(row.GetUInt(3));
        compkeys.push_back((row.GetText(4) == NULL ? "" : row.GetText(4)));    // empty comparisonKey field in channels table row returns NULL, so fill this string with "" in that case
        memberless.push_back(row.GetUInt(5));
        passwords.push_back((row.GetText(6) == NULL ? "" : row.GetText(6)));    // empty password field in channels table row returns NULL, so fill this string with "" in that case
        maillists.push_back(row.GetUInt(7));
        cspas.push_back(row.GetUInt(8));
    }

    if (rowCount == 0) {
        //_log(SERVICE__ERROR, "CharID %u isn't present in the database", charID);
        return;
    }

    channelCount = rowCount;
}

//TODO  check these next 2 calls to solve error/warning in console...
bool LSCDB::GetChannelInfo(int32 channelID, std::string &name, std::string &motd)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT displayName, motd FROM channels WHERE channelID = %i ", channelID)) {
        _log(DATABASE__ERROR, "Error in query: %s", res.error.c_str());
        return false;
    }

    DBResultRow row;
    if (!res.GetRow(row)) {
        // _log(SERVICE__ERROR, "Couldn't find %u in table channels", channelID);
        return false;
    }

    name = row.GetText(0);
    motd = row.GetText(1);

    return true;
}


int32 LSCDB::GetChannelIDFromComparisonKey(std::string compkey)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT channelID FROM channels WHERE comparisonKey RLIKE '%s'", compkey.c_str())) {
        _log(DATABASE__ERROR, "Error in GetChannelIDFromComparisonKey query: %s", res.error.c_str());
        return 0;
    }

    DBResultRow row;
    if (!res.GetRow(row)) {
        _log(SERVICE__ERROR, "Couldn't find %s in table channels", compkey.c_str());
        return 0;
    }

    return row.GetInt(0);
}

std::string LSCDB::GetChannelName(uint32 id, const char * table, const char * column, const char * key) {
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,"SELECT %s FROM %s WHERE %s = %u ", column, table, key, id)) {
        _log(DATABASE__ERROR, "Error in query: %s", res.error.c_str());
        return "";
    }

    DBResultRow row;
    if (!res.GetRow(row)) {
        _log(SERVICE__ERROR, "Couldn't find %s %u in table %s", key, id, table);
        return "";
    }

    return row.GetText(0);
}

//temporarily relocated into ServiceDB until some things get cleaned up...
uint32 LSCDB::StoreMail(uint32 senderID, uint32 recipID, const char * subject, const char * message, int64 sentTime) {
    DBQueryResult res;
    DBerror err;
    DBResultRow row;

    std::string escaped;
    // Escape message header
    sDatabase.DoEscapeString(escaped, subject);

    // Store message header
    uint32 messageID;
    if (!sDatabase.RunQueryLID(err, messageID,
        " INSERT INTO eveMail "
        " (channelID, senderID, subject, created) "
        " VALUES (%u, %u, '%s', %lli) ",
                               recipID, senderID, escaped.c_str(), sentTime ))
    {
        _log(DATABASE__ERROR, "Error in query, message header couldn't be saved: %s", err.c_str());
        return (0);
    }

    _log(SERVICE__MESSAGE, "New messageID: %u", messageID);

    // Escape message content
    sDatabase.DoEscapeString(escaped, message);

    // Store message content
    if (!sDatabase.RunQuery(err,
        " INSERT INTO eveMailDetails "
        " (messageID, mimeTypeID, attachment) VALUES (%u, 1, '%s') ",
                            messageID, escaped.c_str()
    ))
    {
        _log(DATABASE__ERROR, "Error in query, message content couldn't be saved: %s", err.c_str());
        // Delete message header
        if (!sDatabase.RunQuery(err, "DELETE FROM `eveMail` WHERE `messageID` = %u;", messageID))
        {
            _log(DATABASE__ERROR, "Failed to remove invalid header data for messgae id %u: %s", messageID, err.c_str());
        }
        return (0);
    }


    return (messageID);
}


PyObject *LSCDB::GetMailHeaders(uint32 recID) {
    DBQueryResult res;

    if (!sDatabase.RunQuery(res,
        "SELECT channelID, messageID, senderID, subject, created, `read` "
        " FROM eveMail "
        " WHERE channelID=%u", recID))
    {
        _log(DATABASE__ERROR, "Error in query: %s", res.error.c_str());
        return nullptr;
    }

    return DBResultToRowset(res);
}


PyRep *LSCDB::GetMailDetails(uint32 messageID, uint32 readerID) {
    DBQueryResult result;
    DBResultRow row;

    //we need to query out the primary message here... not sure how to properly
    //grab the "main message" though... the text/plain clause is pretty hackish.
    if (!sDatabase.RunQuery(result,
        " SELECT eveMail.messageID, eveMail.senderID, eveMail.subject, " // need messageID as char*
        "   eveMailDetails.attachment, eveMailDetails.mimeTypeID, "
        "   eveMailMimeType.mimeType, eveMailMimeType.`binary`, "
        "   eveMail.created, eveMail.channelID "
        " FROM eveMail "
        " LEFT JOIN eveMailDetails ON eveMailDetails.messageID = eveMail.messageID "
        " LEFT JOIN eveMailMimeType ON eveMailMimeType.mimeTypeID = eveMailDetails.mimeTypeID "
        " WHERE eveMail.messageID=%u AND channelID=%u",
        messageID, readerID
    ))
    {
        _log(DATABASE__ERROR, "Error in query: %s", result.error.c_str());
        return nullptr;
    }

    if (!result.GetRow(row)) {
        codelog(SERVICE__MESSAGE, "No message with messageID %u", messageID);
        return nullptr;
    }

    Rsp_GetEVEMailDetails details;
    details.messageID = row.GetUInt(0);
    details.senderID = row.GetUInt(1);
    details.subject = row.GetText(2);
    details.body = row.GetText(3);
    details.created = row.GetInt64(7);
    details.channelID = row.GetUInt(8);
    details.deleted = 0; // If a message's details are sent, then it isn't deleted. If it's deleted, details cannot be sent
    details.mimeTypeID = row.GetInt(4);
    details.mimeType = row.GetText(5);
    details.binary = row.GetInt(6);

    return details.Encode();
}


bool LSCDB::MarkMessageRead(uint32 messageID) {
    DBerror err;

    if (!sDatabase.RunQuery(err,
        " UPDATE eveMail "
        " SET `read` = 1 "
        " WHERE messageID=%u", messageID
    ))
    {
        _log(DATABASE__ERROR, "Error in query: %s", err.c_str());
        return false;
    }

    return true;
}


void LSCDB::SaveGag(uint32 charID, int64 until, const std::string& reason, uint32 gmID) {
    std::string escaped;
    sDatabase.DoEscapeString(escaped, reason.substr(0, 255));

    DBerror err;
    if (!sDatabase.RunQuery(err,
        "REPLACE INTO chrChatGags (characterID, untilTime, reason, gmCharacterID)"
        " VALUES (%u, %lli, '%s', %u)", charID, until, escaped.c_str(), gmID))
    {
        _log(DATABASE__ERROR, "Error in SaveGag query: %s", err.c_str());
    }
}

void LSCDB::DeleteGag(uint32 charID) {
    DBerror err;
    if (!sDatabase.RunQuery(err, "DELETE FROM chrChatGags WHERE characterID = %u", charID))
        _log(DATABASE__ERROR, "Error in DeleteGag query: %s", err.c_str());
}

void LSCDB::LoadGags(std::map<uint32, std::pair<int64, std::string>>& into) {
    DBerror err;
    if (!sDatabase.RunQuery(err, "DELETE FROM chrChatGags WHERE untilTime <= %lli", GetFileTimeNow()))
        _log(DATABASE__ERROR, "Error in LoadGags query: %s", err.c_str());

    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT characterID, untilTime, reason FROM chrChatGags")) {
        _log(DATABASE__ERROR, "Error in LoadGags query: %s", res.error.c_str());
        return;
    }

    DBResultRow row;
    while (res.GetRow(row))
        into[row.GetUInt(0)] = std::make_pair(row.GetInt64(1), std::string(row.GetText(2)));
}

bool LSCDB::DeleteMessage(uint32 messageID, uint32 readerID) {
    DBerror err;
    bool ret = true;

    if (!sDatabase.RunQuery(err,
        " DELETE FROM eveMail "
        " WHERE messageID=%u AND channelID=%u", messageID, readerID
    ))
    {
        _log(DATABASE__ERROR, "Error in query: %s", err.c_str());
        ret = false;
    }
    if (!sDatabase.RunQuery(err,
        " DELETE FROM eveMailDetails "
        " WHERE messageID=%u", messageID
    ))
    {
        _log(DATABASE__ERROR, "Error in query: %s", err.c_str());
        ret = false;
    }

    return ret;
}

/*
 * user channel persistence and access lists
 */
void LSCDB::LoadUserChannels(std::vector<UserChannelRow>& into)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
            "SELECT channelID, ownerID, displayName, motd, comparisonKey, memberless, password, mailingList, cspa, languageRestriction"
            " FROM channels WHERE channelID >= %i", LSCService::BASE_CHANNEL_ID))
    {
        _log(DATABASE__ERROR, "Error in query: %s", res.error.c_str());
        return;
    }

    DBResultRow row;
    while (res.GetRow(row)) {
        UserChannelRow chan;
            chan.channelID = row.GetInt(0);
            chan.ownerID = row.GetUInt(1);
            chan.displayName = (row.IsNull(2) ? "" : row.GetText(2));
            chan.motd = (row.IsNull(3) ? "" : row.GetText(3));
            chan.comparisonKey = (row.IsNull(4) ? "" : row.GetText(4));
            chan.memberless = (row.GetInt(5) != 0);
            chan.password = (row.IsNull(6) ? "" : row.GetText(6));
            chan.mailingList = (row.GetInt(7) != 0);
            chan.cspa = row.GetUInt(8);
            chan.languageRestriction = (row.GetInt(9) != 0);
        into.push_back(chan);
    }
}

void LSCDB::LoadChannelMods(std::vector<std::pair<int32, LSCChannelMod>>& into)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
            "SELECT channelID, accessor, mode, untilWhen, originalMode, admin, reason FROM channelMods"))
    {
        _log(DATABASE__ERROR, "Error in query: %s", res.error.c_str());
        return;
    }

    DBResultRow row;
    while (res.GetRow(row)) {
        LSCChannelMod mod(row.GetUInt(1), row.GetInt(2), (row.IsNull(3) ? 0 : row.GetInt64(3)),
                          (row.IsNull(4) ? (int32)LSC::Mode::chUnspecified : row.GetInt(4)),
                          (row.IsNull(5) ? "" : row.GetText(5)), (row.IsNull(6) ? "" : row.GetText(6)));
        into.push_back(std::make_pair(row.GetInt(0), mod));
    }
}

void LSCDB::SaveChannelMod(int32 channelID, const LSCChannelMod& mod)
{
    std::string admin, reason;
    sDatabase.DoEscapeString(admin, mod.m_admin);
    sDatabase.DoEscapeString(reason, mod.m_reason);
    std::string untilWhen("NULL"), originalMode("NULL");
    if (mod.m_untilWhen != 0)
        untilWhen = std::to_string(mod.m_untilWhen);
    if (mod.m_originalMode != LSC::Mode::chUnspecified)
        originalMode = std::to_string(mod.m_originalMode);

    DBerror err;
    if (!sDatabase.RunQuery(err,
            "INSERT INTO channelMods (channelID, accessor, mode, untilWhen, originalMode, admin, reason)"
            " VALUES (%i, %u, %i, %s, %s, '%s', '%s')"
            " ON DUPLICATE KEY UPDATE mode = VALUES(mode), untilWhen = VALUES(untilWhen), originalMode = VALUES(originalMode),"
            " admin = VALUES(admin), reason = VALUES(reason)",
            channelID, mod.m_accessor, mod.m_mode, untilWhen.c_str(), originalMode.c_str(), admin.c_str(), reason.c_str()))
        _log(DATABASE__ERROR, "Error in query: %s", err.c_str());
}

void LSCDB::DeleteChannelMod(int32 channelID, uint32 accessor)
{
    DBerror err;
    sDatabase.RunQuery(err, "DELETE FROM channelMods WHERE channelID = %i AND accessor = %u", channelID, accessor);
}

void LSCDB::GetSubscribedChannels(uint32 charID, std::set<int32>& into)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT channelID FROM channelChars WHERE charID = %u", charID)) {
        _log(DATABASE__ERROR, "Error in query: %s", res.error.c_str());
        return;
    }
    DBResultRow row;
    while (res.GetRow(row))
        into.insert(row.GetInt(0));
}

uint32 LSCDB::GetSubscriberCount(int32 channelID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT COUNT(*) FROM channelChars WHERE channelID = %i", channelID))
        return 1;   // keep the channel when in doubt
    DBResultRow row;
    if (!res.GetRow(row))
        return 1;
    return row.GetUInt(0);
}

void LSCDB::DeleteOrphanUserChannels()
{
    // user channels nobody is subscribed to any more
    DBerror err;
    if (!sDatabase.RunQuery(err,
            "DELETE c FROM channels AS c"
            " LEFT JOIN channelChars AS s ON s.channelID = c.channelID"
            " WHERE c.channelID >= %i AND s.channelID IS NULL", LSCService::BASE_CHANNEL_ID))
        _log(DATABASE__ERROR, "Error in query: %s", err.c_str());
    if (!sDatabase.RunQuery(err,
            "DELETE m FROM channelMods AS m"
            " LEFT JOIN channels AS c ON c.channelID = m.channelID"
            " WHERE m.channelID >= %i AND c.channelID IS NULL", LSCService::BASE_CHANNEL_ID))
        _log(DATABASE__ERROR, "Error in query: %s", err.c_str());
}
