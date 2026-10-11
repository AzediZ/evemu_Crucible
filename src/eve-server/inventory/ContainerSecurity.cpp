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

#include "../../eve-common/EVE_Mail.h"

#include "Client.h"
#include "EntityList.h"
#include "EVEServerConfig.h"
#include "StaticDataMgr.h"
#include "inventory/ContainerSecurity.h"
#include "inventory/InventoryDB.h"
#include "inventory/ItemFactory.h"
#include "mail/NotificationMgr.h"

bool ContainerSecurity::IsSecure(InventoryItemRef cRef)
{
    if (cRef.get() == nullptr)
        return false;
    return ((cRef->groupID() == EVEDB::invGroups::Secure_Cargo_Container)
         or (cRef->groupID() == EVEDB::invGroups::Audit_Log_Secure_Container));
}

bool ContainerSecurity::IsAuditLog(InventoryItemRef cRef)
{
    if (cRef.get() == nullptr)
        return false;
    return (cRef->groupID() == EVEDB::invGroups::Audit_Log_Secure_Container);
}

ContainerSecurity::SecData& ContainerSecurity::Get(uint32 contID)
{
    std::map<uint32, SecData>::iterator itr = m_cache.find(contID);
    if (itr != m_cache.end())
        return itr->second;

    SecData data = SecData();
    data.alscConfig = Inv::ALSCConfig::Default;
    InventoryDB::GetContainerSecurity(contID, data.general, data.config, data.alscConfig);
    return m_cache.emplace(contID, data).first->second;
}

void ContainerSecurity::Save(uint32 contID)
{
    SecData& data = Get(contID);
    if (data.general.empty() and data.config.empty() and (data.alscConfig == Inv::ALSCConfig::Default)) {
        InventoryDB::DeleteContainerSecurity(contID);
    } else {
        InventoryDB::SaveContainerSecurity(contID, data.general, data.config, data.alscConfig);
    }
}

bool ContainerSecurity::HasPassword(uint32 contID, uint8 which)
{
    SecData& data = Get(contID);
    if (which == Inv::SecPassword::Config)
        return !data.config.empty();
    return !data.general.empty();
}

bool ContainerSecurity::HasAnyPassword(uint32 contID)
{
    SecData& data = Get(contID);
    return (!data.general.empty() or !data.config.empty());
}

uint8 ContainerSecurity::GetConfig(uint32 contID)
{
    return Get(contID).alscConfig;
}

bool ContainerSecurity::MatchPassword(uint32 contID, uint8 which, PyRep* password)
{
    SecData& data = Get(contID);
    const std::string& current(which == Inv::SecPassword::Config ? data.config : data.general);
    if (current.empty())
        return true;
    return (PyRep::StringContent(password) == current);
}

bool ContainerSecurity::HasEntered(uint32 contID, uint32 charID, uint8 which)
{
    std::map<uint32, std::map<uint32, uint8>>::iterator itr = m_entered.find(contID);
    if (itr == m_entered.end())
        return false;
    std::map<uint32, uint8>::iterator cItr = itr->second.find(charID);
    if (cItr == itr->second.end())
        return false;
    return ((cItr->second & which) == which);
}

bool ContainerSecurity::IsExempt(Client* pClient, InventoryItemRef cRef)
{
    // an owner opens their own container in their item hangar or their ship without the password
    if (!sConfig.world.SecureContainerOwnerBypass)
        return false;
    if (cRef->ownerID() != (uint32)pClient->GetCharacterID())
        return false;
    if (sDataMgr.IsStation(cRef->locationID()) and (cRef->flag() == flagHangar))
        return true;
    return (cRef->locationID() == pClient->GetShipID());
}

void ContainerSecurity::CheckOpen(Client* pClient, InventoryItemRef cRef)
{
    if ((pClient == nullptr) or !IsSecure(cRef))
        return;
    if (!HasPassword(cRef->itemID(), Inv::SecPassword::General))
        return;
    // an audit log container asks for it only when it's configured to
    if (IsAuditLog(cRef) and ((GetConfig(cRef->itemID()) & Inv::ALSCConfig::NeedToOpen) == 0))
        return;
    if (IsExempt(pClient, cRef))
        return;
    if (HasEntered(cRef->itemID(), pClient->GetCharacterID(), Inv::SecPassword::General))
        return;
    throw UserError("SCGeneralPasswordRequired");
}

void ContainerSecurity::CheckConfigPassword(Client* pClient, InventoryItemRef cRef, uint8 bit)
{
    if ((pClient == nullptr) or !IsAuditLog(cRef))
        return;
    if (!HasPassword(cRef->itemID(), Inv::SecPassword::Config))
        return;
    if ((bit != 0) and ((GetConfig(cRef->itemID()) & bit) == 0))
        return;
    if (HasEntered(cRef->itemID(), pClient->GetCharacterID(), Inv::SecPassword::Config))
        return;
    throw UserError("SCConfigPasswordRequired");
}

void ContainerSecurity::CheckManage(Client* pClient, InventoryItemRef cRef)
{
    uint32 ownerID(cRef->ownerID());
    if (ownerID == (uint32)pClient->GetCharacterID())
        return;
    if (IsPlayerCorp(ownerID) and (ownerID == pClient->GetCorporationID())) {
        if ((pClient->GetCorpRole() & (Corp::Role::EquipmentConfig | Corp::Role::Director)) == 0)
            throw UserError("CantConfigureCorpRoleLacking");
        return;
    }
    throw UserError("CantConfigureNotOwner");
}

void ContainerSecurity::CheckViewLog(Client* pClient, InventoryItemRef cRef)
{
    uint32 ownerID(cRef->ownerID());
    if (ownerID == (uint32)pClient->GetCharacterID())
        return;
    if (IsPlayerCorp(ownerID) and (ownerID == pClient->GetCorporationID()))
        return;
    throw UserError("CantConfigureNotOwner");
}

void ContainerSecurity::SetPassword(Client* pClient, InventoryItemRef cRef, uint8 which, PyRep* oldPassword, PyRep* newPassword)
{
    // container.SetPassword(which, old|None, new).  the client checked 3..16 letters and the confirmation
    if ((which != Inv::SecPassword::General) and (which != Inv::SecPassword::Config))
        throw UserError("CantConfigureThat");
    if (!IsSecure(cRef) or ((which == Inv::SecPassword::Config) and !IsAuditLog(cRef)))
        throw UserError("CantConfigureThat");
    CheckManage(pClient, cRef);

    std::string newStr(PyRep::StringContent(newPassword));
    if (newStr.size() > 16)
        throw UserError("CantSetPasswordTooLong").AddAmount("count", 16);
    if (newStr.size() < 3)
        throw UserError("MinThreeLetters");

    uint32 contID(cRef->itemID());
    SecData& data = Get(contID);
    std::string& current(which == Inv::SecPassword::Config ? data.config : data.general);
    if (!current.empty()) {
        if (PyRep::StringContent(oldPassword) != current) {
            Log(cRef, pClient->GetCharacterID(), Inv::ALSCAction::SetPassword, false, which);
            throw CustomError("The old password you entered is incorrect.");
        }
        if (newStr == current)
            throw UserError("CantChangePasswordToSame");
    }

    current = newStr;
    Save(contID);

    // everyone else has to enter the new one
    uint32 charID(pClient->GetCharacterID());
    std::map<uint32, std::map<uint32, uint8>>::iterator itr = m_entered.find(contID);
    if (itr != m_entered.end())
        for (auto& cur : itr->second)
            if (cur.first != charID)
                cur.second &= ~which;
    m_entered[contID][charID] |= which;

    Log(cRef, charID, Inv::ALSCAction::SetPassword, true, which);
    NotifyChanged(cRef, which);
}

void ContainerSecurity::EnterPassword(Client* pClient, InventoryItemRef cRef, uint8 which, PyRep* password)
{
    // a wrong password isn't an error:  the client retries the call it was asked for, which asks again
    if (!IsSecure(cRef))
        return;
    uint32 contID(cRef->itemID());
    bool ok(MatchPassword(contID, which, password));
    if (ok)
        m_entered[contID][pClient->GetCharacterID()] |= which;
    Log(cRef, pClient->GetCharacterID(), Inv::ALSCAction::EnterPassword, ok, which);
}

void ContainerSecurity::SetConfig(Client* pClient, InventoryItemRef cRef, uint8 config)
{
    if (!IsAuditLog(cRef))
        throw UserError("CantConfigureThat");
    CheckManage(pClient, cRef);
    CheckConfigPassword(pClient, cRef, 0);

    SecData& data = Get(cRef->itemID());
    data.alscConfig = (config & Inv::ALSCConfig::Default);
    Save(cRef->itemID());
    Log(cRef, pClient->GetCharacterID(), Inv::ALSCAction::Configure, true);
}

void ContainerSecurity::RetrievePassword(Client* pClient, InventoryItemRef cRef, uint8 which)
{
    // the password comes as a notification (notificationTypeContainerPasswordMsg)
    if (!IsSecure(cRef) or ((which == Inv::SecPassword::Config) and !IsAuditLog(cRef)))
        throw UserError("CantConfigureThat");
    CheckManage(pClient, cRef);

    SecData& data = Get(cRef->itemID());
    const std::string& current(which == Inv::SecPassword::Config ? data.config : data.general);

    uint32 locationID(GetInventoryMgrLocation(cRef));
    uint32 stationID(0), systemID(0);
    if (sDataMgr.IsStation(locationID)) {
        stationID = locationID;
        systemID = sDataMgr.GetStationSystem(locationID);
    } else if (sDataMgr.IsSolarSystem(locationID)) {
        systemID = locationID;
    }

    PyDict* dict = new PyDict();
        dict->SetItemString("charID", new PyInt(pClient->GetCharacterID()));
        dict->SetItemString("typeID", new PyInt(cRef->typeID()));
        dict->SetItemString("stationID", (stationID == 0 ? PyStatic.NewNone() : new PyInt(stationID)));
        dict->SetItemString("solarSystemID", new PyInt(systemID));
        dict->SetItemString("password", (current.empty() ? PyStatic.NewNone() : new PyString(current)));
        dict->SetItemString("passwordType", new PyString(which == Inv::SecPassword::Config ? "config" : "general"));
    NotificationMgr::Send(pClient->GetCharacterID(), Notify::Types::ContainerPassword, cRef->ownerID(), dict);
}

void ContainerSecurity::NotifyChanged(InventoryItemRef cRef, uint8 which)
{
    // the client closes the container if it has it open (invCache.OnPasswordChanged)
    PyTuple* tuple = new PyTuple(2);
        tuple->SetItem(0, new PyInt(which));
        tuple->SetItem(1, cRef->GetItemRow());
    if (IsCharacterID(cRef->ownerID())) {
        Client* pClient(sEntityList.FindClientByCharID(cRef->ownerID()));
        if (pClient != nullptr) {
            pClient->SendNotification("OnPasswordChanged", "clientID", &tuple);
        } else {
            PyDecRef(tuple);
        }
    } else if (IsPlayerCorp(cRef->ownerID())) {
        sEntityList.CorpNotify(cRef->ownerID(), Notify::Types::ItemUpdateStation, "OnPasswordChanged", "clientID", tuple);
    } else {
        PyDecRef(tuple);
    }
}

uint32 ContainerSecurity::GetInventoryMgrLocation(InventoryItemRef cRef)
{
    uint32 locationID(cRef->locationID());
    for (uint8 i = 0; i < 4; ++i) {
        if (sDataMgr.IsStation(locationID) or sDataMgr.IsSolarSystem(locationID))
            return locationID;
        if (!IsOfficeID(locationID) and !IsPlayerItem(locationID))
            return locationID;
        InventoryItemRef iRef = sItemFactory.GetItemRefFromID(locationID, false);
        if (iRef.get() == nullptr)
            iRef = sItemFactory.GetItemRef(locationID);
        if (iRef.get() == nullptr)
            return locationID;
        locationID = iRef->locationID();
    }
    return locationID;
}

void ContainerSecurity::Log(InventoryItemRef cRef, uint32 actorID, uint8 action, bool ok, int32 arg1, int32 arg2)
{
    if (!IsAuditLog(cRef))
        return;
    InventoryDB::AddAuditLog(cRef->itemID(), actorID, action, ok, GetInventoryMgrLocation(cRef), cRef->locationID(),
                             cRef->flag(), arg1, arg2);
}

PyRep* ContainerSecurity::GetLog(uint32 contID, PyRep* fromDate, PyRep* fromLogID, int32 forward)
{
    int64 date((fromDate == nullptr) or fromDate->IsNone() ? 0 : PyRep::IntegerValue(fromDate));
    int32 logID((fromLogID == nullptr) or fromLogID->IsNone() ? 0 : PyRep::IntegerValue(fromLogID));
    return InventoryDB::GetAuditLog(contID, date, logID, (forward == 1));
}

bool ContainerSecurity::HasRecentLog(uint32 contID, int64 since)
{
    return InventoryDB::HasAuditLogSince(contID, since);
}

void ContainerSecurity::Forget(uint32 contID)
{
    m_cache.erase(contID);
    m_entered.erase(contID);
}

void ContainerSecurity::ClearPasswords(uint32 contID)
{
    SecData& data = Get(contID);
    data.general.clear();
    data.config.clear();
    data.alscConfig = Inv::ALSCConfig::Default;
    InventoryDB::DeleteContainerSecurity(contID);
    m_entered.erase(contID);
}

void ContainerSecurity::ForgetCharacter(uint32 charID)
{
    std::map<uint32, std::map<uint32, uint8>>::iterator itr = m_entered.begin();
    while (itr != m_entered.end()) {
        itr->second.erase(charID);
        if (itr->second.empty()) {
            itr = m_entered.erase(itr);
        } else {
            ++itr;
        }
    }
}
