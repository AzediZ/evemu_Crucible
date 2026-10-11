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

/* Secure cargo containers (group 340) and audit log secure containers (ALSC, group 448)
 *  - a general password guards opening and taking out.  ALSCs only ask for it when their config says so
 *  - ALSCs also have a configuration password, which guards locking, unlocking and viewing the log as configured,
 *     and changing the configuration
 *  - ALSCs log what's done with them (invAuditLog), which the client pages through 50 at a time
 *  - the client's PasswordHandlerCallWrapper retries a call after EnterPassword() when it gets SCGeneralPasswordRequired
 *     or SCConfigPasswordRequired, so a wrong password is not an error:  the retry asks again
 * passwords are kept in plain text:  RetrievePassword sends them to the owner, as CCP's did
 */

#ifndef EVEMU_INVENTORY_CONTAINERSECURITY_H_
#define EVEMU_INVENTORY_CONTAINERSECURITY_H_

#include "eve-compat.h"
#include "utils/Singleton.h"
#include "inventory/ItemRef.h"

class Client;
class PyRep;

class ContainerSecurity
: public Singleton<ContainerSecurity>
{
public:
    ContainerSecurity()                                 { /* do nothing here */ }
    ~ContainerSecurity()                                { /* do nothing here */ }

    // secure cargo containers and audit log containers
    static bool IsSecure(InventoryItemRef cRef);
    static bool IsAuditLog(InventoryItemRef cRef);

    bool HasPassword(uint32 contID, uint8 which);
    bool HasAnyPassword(uint32 contID);
    // ALSC config bits (Inv::ALSCConfig), default all
    uint8 GetConfig(uint32 contID);
    // does this match the password?  (no password set = anything matches)
    bool MatchPassword(uint32 contID, uint8 which, PyRep* password);

    /* access checks.  these throw the client's errors */
    // opening, listing, adding and taking.  SCGeneralPasswordRequired
    void CheckOpen(Client* pClient, InventoryItemRef cRef);
    // ALSC config password for an action bit (Inv::ALSCConfig); bit 0 = always when a config password is set.  SCConfigPasswordRequired
    void CheckConfigPassword(Client* pClient, InventoryItemRef cRef, uint8 bit);
    // passwords, configuration, locks:  the owner, or a corp container's Equipment Config / Director.  CantConfigure*
    void CheckManage(Client* pClient, InventoryItemRef cRef);
    // the ALSC log:  the owner, or any member of the owning corp
    void CheckViewLog(Client* pClient, InventoryItemRef cRef);

    /* client operations */
    void SetPassword(Client* pClient, InventoryItemRef cRef, uint8 which, PyRep* oldPassword, PyRep* newPassword);
    void EnterPassword(Client* pClient, InventoryItemRef cRef, uint8 which, PyRep* password);
    void SetConfig(Client* pClient, InventoryItemRef cRef, uint8 config);
    void RetrievePassword(Client* pClient, InventoryItemRef cRef, uint8 which);

    // ALSC log entry (only logs for audit log containers)
    void Log(InventoryItemRef cRef, uint32 actorID, uint8 action, bool ok = true, int32 arg1 = 0, int32 arg2 = 0);
    PyRep* GetLog(uint32 contID, PyRep* fromDate, PyRep* fromLogID, int32 forward);
    bool HasRecentLog(uint32 contID, int64 since);

    // container gone (trashed, destroyed):  drop the cache and entered passwords
    void Forget(uint32 contID);
    // repackaged:  the passwords are gone
    void ClearPasswords(uint32 contID);
    // logged off:  passwords entered by this character are forgotten
    void ForgetCharacter(uint32 charID);

private:
    struct SecData {
        std::string general;
        std::string config;
        uint8 alscConfig;
    };

    SecData& Get(uint32 contID);
    void Save(uint32 contID);
    bool IsExempt(Client* pClient, InventoryItemRef cRef);
    bool HasEntered(uint32 contID, uint32 charID, uint8 which);
    void NotifyChanged(InventoryItemRef cRef, uint8 which);
    // the station or solar system the container is in (through offices, ships and containers)
    uint32 GetInventoryMgrLocation(InventoryItemRef cRef);

    std::map<uint32, SecData> m_cache;                      // contID/data.  lazily loaded; no row = no passwords
    std::map<uint32, std::map<uint32, uint8>> m_entered;    // contID/(charID/password kinds entered)
};

//Singleton
#define sContSec \
    ( ContainerSecurity::get() )

#endif  // EVEMU_INVENTORY_CONTAINERSECURITY_H_
