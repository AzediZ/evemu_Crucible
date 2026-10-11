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

#ifndef __MAILMGR_H__INCL__
#define __MAILMGR_H__INCL__

#include "eve-common.h"

/* eve mail delivery, shared by mailMgr, mailing lists and every server-generated mail.
 *  stores the message, one mailStatus row per recipient (with the same status/label masks the client
 *  computes in mailSvc.OnMailSent) and pushes OnMailSent to online recipients.
 */
class MailMgr {
public:
    // full delivery.  validates nothing about permissions (callers do); returns messageID or 0.
    //  recipients: toCharacterIDs (characters only), + members of toCorpOrAllianceID (corp or alliance),
    //  + members of toListID (or *overrideRecipients when given), + the sender when it is a character.
    //  extraStatus is OR'd into every recipient's statusMask (eg. mailStatusMaskAutomated).
    //  pushes OnMailSent to every online recipient except `noNotifyID` (the sending client, which adds it locally)
    static uint32 SendMail(uint32 senderID, const std::vector<int32>& toCharacterIDs, int32 toListID /*0=none*/,
                           int32 toCorpOrAllianceID /*0=none*/, const std::string& title, const std::string& body,
                           uint32 extraStatus = 0, uint32 noNotifyID = 0, const std::vector<uint32>* overrideRecipients = nullptr);
    // convenience for server mails (NPC corp/agent/system sender -> one character)
    static uint32 SendMail(uint32 senderID, uint32 toCharID, const std::string& title, const std::string& body);

    // client OnMailSent label/status logic for one recipient
    static void ComputeMasks(uint32 charID, uint32 senderID, const std::vector<int32>& toChars, int32 toListID,
                             int32 toCorpOrAllianceID, uint32& statusMask, uint32& labelMask);

    // unread, not trashed mail of a character (char select "new mail")
    static uint32 GetUnreadCount(uint32 charID);

    // cut a utf-8 string to at most maxChars code points
    static void TruncateUtf8(std::string& str, size_t maxChars);
};

#endif  // __MAILMGR_H__INCL__
