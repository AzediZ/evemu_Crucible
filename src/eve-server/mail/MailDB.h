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
    Author:        caytchen
*/

#ifndef __MAILDB_H_INCL__
#define __MAILDB_H_INCL__

#include "ServiceDB.h"

class PyObject;
class PyString;
class CRowSet;

class MailDB : public ServiceDB
{
public:
    /* eve mail.  one mailMessage row per mail, one mailStatus row (statusMask, labelMask) per character that has it.
     * every per-character query is scoped to that character.  labelIDs are the client's label bits (1 << bit)
     */
    // status rows (messageID, statusMask, labelMask) of all the char's mail
    static PyRep* GetMailStatus(uint32 charID);
    // header rows (messageID, senderID, toCharacterIDs, toListID, toCorpOrAllianceID, title, sentDate) the char has a status row for
    //  minID: only messageID > minID (0 = all)
    static PyRep* GetMailHeaders(uint32 charID, uint32 minID);
    static PyRep* GetMailHeadersByIDs(uint32 charID, const std::vector<int32>& ids);
    // compressed body (PyString), only when the char has the mail.  nullptr otherwise
    static PyString* GetMailBody(uint32 charID, uint32 messageID);

    // set (or clear) statusMask bits
    static void SetStatusBits(uint32 charID, const std::vector<int32>& ids, uint32 mask, bool set);
    static void SetStatusBitsAll(uint32 charID, uint32 mask, bool set, bool skipTrashed);
    // mails having all bits of labelMask
    static void SetStatusBitsByLabel(uint32 charID, int64 labelMask, uint32 mask, bool set, bool skipTrashed);
    static void SetStatusBitsByList(uint32 charID, uint32 listID, uint32 mask, bool set, bool skipTrashed);
    // set (or clear) labelMask bits
    static void SetLabelBits(uint32 charID, const std::vector<int32>& ids, int64 labelMask, bool set);

    // removes the char's status rows (and messages nobody has any more)
    static void DeleteMail(uint32 charID, const std::vector<int32>& ids);
    static void EmptyTrash(uint32 charID);
    static void PurgeOrphanMessages();

    // delivery.  returns messageID, 0 on failure.  0 list/corp ids are stored as NULL
    static uint32 InsertMessage(uint32 senderID, const std::string& toCharacterIDs, uint32 toListID, uint32 toCorpOrAllianceID,
                                const std::string& title, const std::string& compressedBody, int64 sentDate);
    struct StatusRow {
        uint32 charID;
        uint32 statusMask;
        uint32 labelMask;
    };
    static bool InsertStatusRows(uint32 messageID, const std::vector<StatusRow>& rows);
    // the ids of `ids` that are existing characters
    static void GetExistingCharacters(const std::vector<int32>& ids, std::vector<int32>& into);
    static void GetListMemberIDs(uint32 listID, std::vector<uint32>& into);
    static uint32 GetUnreadCount(uint32 charID);

    // labels (mailLabel; bit 4..30, the system labels 1/2/4/8 are client side)
    static PyRep* GetLabels(uint32 charID);
    static uint32 GetLabelCount(uint32 charID);
    static bool LabelNameExists(uint32 charID, const std::string& name, uint32 skipLabelID = 0);
    // returns labelID (1 << bit), 0 when no bit is free
    static uint32 CreateLabel(uint32 charID, const std::string& name, const int32* color);
    // null = unchanged
    static void EditLabel(uint32 charID, uint32 labelID, const std::string* name, const int32* color);
    // also clears the label from the char's mail
    static void DeleteLabel(uint32 charID, uint32 labelID);

    // Mailing list

    PyDict *GetJoinedMailingLists(uint32 characterID);
    uint32 CreateMailingList(uint32 creator, std::string name, int32 defaultAccess,
                           int32 defaultMemberAccess, int32 cost); 

    void JoinMailingList(uint32 characterID, std::string name);
    void LeaveMailingList(uint32 characterID, int32 listID);
    void DeleteMailingList(uint32 characterID, int32 listID);
    // @TODO(groove): KickMembers Figure out converting PyList member info into something usable..
    PyDict *GetMailingListMembers(int32 listID);
    void MailingListSetEntityAccess(int32 entity, int32 access, int32 listID);
    void MailingListClearEntityAccess(int32 entity, int32 listID);
    // @TODO(groove): SetMailingListMembersMuted
    // @TODO(groove): SetMailingListMembersOperator
    // @TODO(groove): SetMailingListMembersClear
    // @TODO(groove): SetMailingListDefaultAccesss
    void SetMailingListDefaultAccess(int32 listID, int32 defaultAccess,
                                     int32 defaultMemberAccess, int32 cost);
    //              : Make generic method for this shit^^^
    //
    // @TODO(groove): MailingListGetInfo
    PyObject *MailingListGetSettings(int32 listID);
    // @TODO(groove): MailingListGetWelcomeMail
    // @TODO(groove): MailingListSaveWelcomeMail
    // @TODO(groove): MailingListSendWelcomeMail
    // @TODO(groove): MailingListClearWelcomeMail
                               
    
    

    // Notifications (chrNotifications)
    // returns notificationID, 0 on failure
    static uint32 StoreNotification(uint32 receiverID, uint16 typeID, uint32 senderID, int64 created, const std::string& yaml);
    // dbutil.CRowset of the char's notifications, newest first.  empty `types` = all types
    static CRowSet* GetNotifications(uint32 charID, const std::vector<uint16>& types, bool unprocessedOnly);
    // ids and types both null = all of the char's notifications
    static void MarkNotificationsProcessed(uint32 charID, const std::vector<int32>* ids, const std::vector<uint16>* types);
    static void DeleteNotifications(uint32 charID, const std::vector<int32>* ids, const std::vector<uint16>* types, bool unprocessedOnly = false);
    static uint32 GetUnprocessedNotificationCount(uint32 charID);
    // members of a corp / alliance whose corpRole|rolesAtAll intersects roleMask (0 = everyone; directors always match)
    static void GetCorpMembersWithRoles(uint32 corpID, int64 roleMask, std::vector<uint32>& into);
    static void GetAllianceMembersWithRoles(uint32 allianceID, int64 roleMask, std::vector<uint32>& into);

protected:
    static int BitFromLabelID(uint32 id);
};

#endif
