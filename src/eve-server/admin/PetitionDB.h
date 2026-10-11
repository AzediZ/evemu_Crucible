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
    Petition storage (petQueues, petCategories, petPetitions, petMessages, petLog)
*/

#ifndef __PETITION_DB_H_INCL__
#define __PETITION_DB_H_INCL__

#include "eve-server.h"

namespace Petition {
    // petLog event ids (served by petitioner.GetEvents)
    namespace Event {
        enum {
            Created         = 1,
            Claimed         = 2,
            Unclaimed       = 3,
            Escalated       = 4,
            Closed          = 5,
            Cancelled       = 6,
            Deleted         = 7,
            PetitionerMsg   = 8,
            GMMessage       = 9,
            GMComment       = 10,
            Rated           = 11
        };
    }
}

struct PetitionRow {
    uint32 petitionID;
    uint32 petitionerID;
    uint32 userID;
    uint32 categoryID;
    uint8 queueID;
    std::string subject;
    std::string petition;
    int64 createDate;
    int64 touchDate;
    uint32 claimedBy;           // 0 = unclaimed
    bool closed;
    bool deleted;
    bool updated;               // client:  0 = "Updated" (petitioner wrote last), 1 = "Waiting" (GM answered)
    int64 closedDate;           // 0 = open
    int8 responseTimeRating;    // -1 = not rated
    int8 helpfulnessRating;
    int8 attitudeRating;
    std::string ratingComment;
    int64 ratingDateTime;       // 0 = not rated
};

struct PetitionMessage {
    uint32 messageID;
    uint32 petitionID;
    uint32 senderID;            // 0 = none
    std::string text;
    int64 sentDate;
    bool comment;
};

struct PetitionLogRow {
    int64 logDateTime;
    uint32 characterID;         // 0 = none
    std::string userName;
    uint8 eventID;
};

struct PetitionCategory {
    uint32 categoryID;
    uint32 parentID;            // 0 = top level
    std::string displayName;
    std::string description;
    uint8 queueID;
    std::string languageID;
};

struct PetitionQueue {
    uint8 queueID;
    std::string queueName;
    int16 escalatesTo;          // -1 = none
};

class PetitionDB
{
public:
    static void GetCategories(std::vector<PetitionCategory>& into);
    static bool GetCategory(uint32 categoryID, PetitionCategory& into);
    static void GetQueues(std::map<uint8, PetitionQueue>& into);

    // strings are raw (escaped here).  returns the new petitionID, 0 on failure
    static uint32 Create(uint32 petitionerID, uint32 userID, uint32 categoryID, uint8 queueID, const std::string& subject,
                         const std::string& petition, const std::string& chatLog, const std::string& combatLog);
    static bool GetPetition(uint32 petitionID, PetitionRow& into);
    // not deleted
    static void GetByPetitioner(uint32 charID, std::vector<PetitionRow>& into);
    // claimed by this GM and still open
    static void GetByClaimer(uint32 charID, std::vector<PetitionRow>& into);
    // open petitions of a queue
    static void GetQueue(uint8 queueID, std::vector<PetitionRow>& into);
    // open petitions of a character
    static uint32 CountOpen(uint32 charID);

    // atomic:  only an unclaimed, open petition.  returns true when this call claimed it
    static bool Claim(uint32 petitionID, uint32 gmID);
    static void Unclaim(uint32 petitionID);
    static void SetQueue(uint32 petitionID, uint8 queueID);
    static void Close(uint32 petitionID);
    static void Delete(uint32 petitionID);
    static void SetUpdated(uint32 petitionID, bool updated);
    static void Touch(uint32 petitionID);
    static void SetRating(uint32 petitionID, uint8 responseTime, uint8 helpfulness, uint8 attitude, const std::string& comment, int64 when);

    // returns the new messageID, 0 on failure.  isRead set for messages the petitioner never needs to be told about
    static uint32 AddMessage(uint32 petitionID, uint32 senderID, const std::string& text, bool comment, bool isRead);
    static void GetMessages(uint32 petitionID, bool includeComments, std::vector<PetitionMessage>& into);
    // GM answers on this character's petitions that it has not read yet
    static void GetUnread(uint32 charID, std::vector<PetitionMessage>& into);
    // only messages on this character's own petitions
    static void MarkRead(uint32 messageID, uint32 charID);

    static void AddLog(uint32 petitionID, uint32 charID, const std::string& userName, uint8 eventID);
    static void GetLog(uint32 petitionID, std::vector<PetitionLogRow>& into);

    static std::string GetAccountName(uint32 accountID);
    // 0 when the character doesn't exist
    static uint32 GetCharacterAccount(uint32 charID);
};

#endif  // __PETITION_DB_H_INCL__
