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
    Updates:    Allan
*/

#include "eve-server.h"

#include "../../eve-common/EVE_Character.h"
#include "EVE_Mail.h"



#include "EntityList.h"
#include "account/AccountService.h"
#include "character/CharMgrService.h"
#include "mail/MailMgr.h"
#include "mail/NotificationMgr.h"
#include "packets/CorporationPkts.h"
#include "services/ServiceManager.h"

CharMgrBound::CharMgrBound(EVEServiceManager& mgr, CharMgrService& parent, uint32 ownerID, uint16 contFlag) :
    EVEBoundObject(mgr, parent),
    m_ownerID(ownerID),
    m_containerFlag(contFlag)
{
    this->Add("List", &CharMgrBound::List);
    this->Add("ListStations", static_cast <PyResult (CharMgrBound::*)(PyCallArgs&, PyInt*, PyBool*)> (&CharMgrBound::ListStations));
    this->Add("ListStations", static_cast <PyResult (CharMgrBound::*)(PyCallArgs&, PyInt*, PyInt*)> (&CharMgrBound::ListStations));
    this->Add("ListStationItems", &CharMgrBound::ListStationItems);
    this->Add("ListStationBlueprintItems", &CharMgrBound::ListStationBlueprintItems);
}

PyResult CharMgrBound::List(PyCallArgs& call)
{
    return CharacterDB::List(m_ownerID);
}

PyResult CharMgrBound::ListStationItems(PyCallArgs& call, PyInt* stationID)
{
    // this is the assets window
    return CharacterDB::ListStationItems(m_ownerID, stationID->value());
}

PyResult CharMgrBound::ListStations(PyCallArgs& call, PyInt* blueprintOnly, PyBool* isCorporation)
{
  return ListStations(call, blueprintOnly, new PyInt(isCorporation->value()));
}

PyResult CharMgrBound::ListStations(PyCallArgs& call, PyInt* blueprintOnly, PyInt* isCorporation)
{
    //stations = sm.GetService('invCache').GetInventory(const.containerGlobal).ListStations(blueprintOnly, isCorp)

    call.Dump(CHARACTER__DEBUG);
    std::ostringstream flagIDs;
    flagIDs << flagHangar;
    /** @todo  test m_containerFlag to determine correct flag here and append to flagIDs string? */

    uint32 ownerID(m_ownerID);

    if (isCorporation->value()) {
        Client* pClient = sEntityList.FindClientByCharID(m_ownerID);
        if (pClient == nullptr)
            return nullptr; // make error here
        ownerID = pClient->GetCorporationID();
        //  add corp hangar flags
        flagIDs << "," << flagCorpHangar2 << "," << flagCorpHangar3 << "," << flagCorpHangar4 << ",";
        flagIDs << flagCorpHangar5 << "," << flagCorpHangar6 << "," << flagCorpHangar7;
    }

    return CharacterDB::ListStations(ownerID, flagIDs, isCorporation->value(), blueprintOnly->value());
}

PyResult CharMgrBound::ListStationBlueprintItems(PyCallArgs& call, PyInt* locationID, PyInt* stationID, PyInt* forCorporation)
{
    // this is the BP tab of the S&I window
    call.Dump(CHARACTER__DEBUG);

    /** @todo whats diff between stationID and locationID?
     *  none that i see so far...
     * this could be diff between station and pos   will need to check later (locationID == stationID)
     */
    uint32 ownerID(m_ownerID);

    if (forCorporation->value()) { //forCorp
        Client* pClient = sEntityList.FindClientByCharID(m_ownerID);
        if (pClient == nullptr)
            return nullptr; // make error here
        ownerID = pClient->GetCorporationID();
    }
    return CharacterDB::ListStationBlueprintItems(ownerID, stationID->value(), forCorporation->value());
}

CharMgrService::CharMgrService(EVEServiceManager& mgr) :
    BindableService("charMgr", mgr, eAccessLevel_Character)
{
    this->Add("GetPublicInfo", &CharMgrService::GetPublicInfo);
    this->Add("GetPublicInfo3", &CharMgrService::GetPublicInfo3);
    this->Add("GetPrivateInfo", &CharMgrService::GetPrivateInfo);
    this->Add("GetTopBounties", &CharMgrService::GetTopBounties);
    this->Add("AddToBounty", &CharMgrService::AddToBounty);
    this->Add("GetContactList", &CharMgrService::GetContactList);
    this->Add("GetCloneTypeID", &CharMgrService::GetCloneTypeID);
    this->Add("GetHomeStation", &CharMgrService::GetHomeStation);
    this->Add("GetFactions", &CharMgrService::GetFactions);
    this->Add("SetActivityStatus", &CharMgrService::SetActivityStatus);
    this->Add("GetSettingsInfo", &CharMgrService::GetSettingsInfo);
    this->Add("LogSettings", &CharMgrService::LogSettings);
    this->Add("GetCharacterDescription", &CharMgrService::GetCharacterDescription);
    this->Add("SetCharacterDescription", &CharMgrService::SetCharacterDescription);
    this->Add("GetPaperdollState", &CharMgrService::GetPaperdollState);
    this->Add("GetNote", &CharMgrService::GetNote);
    this->Add("SetNote", &CharMgrService::SetNote);
    this->Add("AddOwnerNote", &CharMgrService::AddOwnerNote);
    this->Add("EditOwnerNote", &CharMgrService::EditOwnerNote);
    this->Add("RemoveOwnerNote", &CharMgrService::RemoveOwnerNote);
    this->Add("GetOwnerNote", &CharMgrService::GetOwnerNote);
    this->Add("GetOwnerNoteLabels", &CharMgrService::GetOwnerNoteLabels);
    this->Add("AddContact", static_cast <PyResult(CharMgrService::*)(PyCallArgs&, PyInt*)> (&CharMgrService::AddContact));
    this->Add("AddContact", static_cast <PyResult(CharMgrService::*)(PyCallArgs&, PyInt*, PyRep*, PyRep*, PyRep*, std::optional<PyRep*>)> (&CharMgrService::AddContact));
    this->Add("EditContact", &CharMgrService::EditContact);
    this->Add("DeleteContacts", &CharMgrService::DeleteContacts);
    this->Add("GetRecentShipKillsAndLosses", &CharMgrService::GetRecentShipKillsAndLosses);
    this->Add("BlockOwners", &CharMgrService::BlockOwners);
    this->Add("UnblockOwners", &CharMgrService::UnblockOwners);
    this->Add("EditContactsRelationshipID", &CharMgrService::EditContactsRelationshipID);
    this->Add("GetImageServerLink", &CharMgrService::GetImageServerLink);
    //these 2 are for labels in PnP window;
    this->Add("GetLabels", &CharMgrService::GetLabels);
    this->Add("CreateLabel", &CharMgrService::CreateLabel);
    this->Add("EditLabel", &CharMgrService::EditLabel);
    this->Add("DeleteLabel", &CharMgrService::DeleteLabel);
    this->Add("AssignLabels", &CharMgrService::AssignLabels);
    this->Add("RemoveLabels", &CharMgrService::RemoveLabels);
}

BoundDispatcher* CharMgrService::BindObject(Client *client, PyRep* bindParameters) {
    _log(CHARACTER__BIND, "CharMgrService bind request:");
    bindParameters->Dump(CHARACTER__BIND, "    ");
    Call_TwoIntegerArgs args;
    //crap
    PyRep* tmp(bindParameters->Clone());
    if (!args.Decode(&tmp)) {
        codelog(SERVICE__ERROR, "%s: Failed to decode arguments.", GetName());
        return nullptr;
    }

    return new CharMgrBound(this->GetServiceManager(), *this, args.arg1, args.arg2);
}

void CharMgrService::BoundReleased(CharMgrBound *bound) {
    // nothing to be done here, this bound service is a singleton
}

PyResult CharMgrService::GetImageServerLink(PyCallArgs& call)
{
    // only called by billboard service for bounties...
    //  serverLink = sm.RemoteSvc('charMgr').GetImageServerLink()
    std::stringstream urlBuilder;
    urlBuilder << "http://" << sConfig.net.imageServer << ":" << sConfig.net.imageServerPort << "/";

    return new PyString(urlBuilder.str());
}

PyResult CharMgrService::GetRecentShipKillsAndLosses(PyCallArgs& call, PyInt* num, std::optional<PyInt*> startIndex)
{   /* cached object - can return db object as DBResultToCRowset*/
    return m_db.GetKillOrLoss(call.client->GetCharacterID());
}

PyResult CharMgrService::GetTopBounties(PyCallArgs& call)
{
    return m_db.GetTopBounties();
}

PyResult CharMgrService::GetLabels(PyCallArgs& call)
{
    return CharacterDB::GetContactLabels("chrLabels", call.client->GetCharacterID());
}

PyResult CharMgrService::GetPaperdollState(PyCallArgs& call)
{
    return new PyInt(Char::PDState::NoRecustomization);
}

PyResult CharMgrService::GetPublicInfo3(PyCallArgs &call, PyInt* characterID)
{
    return m_db.GetCharPublicInfo3(characterID->value());
}

PyResult CharMgrService::GetContactList(PyCallArgs &call)
{
    PyDict* dict = new PyDict();
        dict->SetItemString("addresses", m_db.GetContacts(call.client->GetCharacterID()));
        dict->SetItemString("blocked", m_db.GetBlocked(call.client->GetCharacterID()));
    PyObject* args = new PyObject("util.KeyVal", dict);
    if (is_log_enabled(CLIENT__RSP_DUMP))
        args->Dump(CLIENT__RSP_DUMP, "");
    return args;
}

PyResult CharMgrService::GetPrivateInfo(PyCallArgs& call, PyInt* characterID)
{
    // self.memberinfo = self.charMgr.GetPrivateInfo(self.charID)
    // this is called by corp/editMember
    PyRep* args(m_db.GetCharPrivateInfo(characterID->value()));
    if (is_log_enabled(CLIENT__RSP_DUMP))
        args->Dump(CLIENT__RSP_DUMP, "");
    return args;
}

PyResult CharMgrService::GetPublicInfo(PyCallArgs &call, PyInt* ownerID) {
    //single int arg: char id or corp id
    /*if (IsAgent(args.arg)) {
        //handle agents special right now...
        PyRep *result = m_db.GetAgentPublicInfo(args.arg);
        if (result == nullptr) {
            codelog(CLIENT__ERROR, "%s: Failed to find agent %u", call.client->GetName(), args.arg);
            return nullptr;
        }
        return result;
    }*/

    PyRep *result = m_db.GetCharPublicInfo(ownerID->value());
    if (result == nullptr)
        codelog(CHARACTER__ERROR, "%s: Failed to find char %u", call.client->GetName(), ownerID->value());

    return result;
}

PyResult CharMgrService::AddToBounty(PyCallArgs& call, PyInt* characterID, PyInt* amount) {
    if (call.client->GetCharacterID() == characterID->value()){
        call.client->SendErrorMsg("You cannot put a bounty on yourself.");
        return nullptr;
    }

    if (amount->value() < call.client->GetBalance()) {
        std::string reason = "Placing Bounty on ";
        reason += m_db.GetCharName(characterID->value());
        AccountService::TransferFunds(call.client->GetCharacterID(), corpCONCORD, amount->value(), reason, Journal::EntryType::Bounty, characterID->value());
        m_db.AddBounty(characterID->value(), call.client->GetCharacterID(), amount->value());
        // new system gives target a mail from concord about placement of bounty and char name placing it.
    } else {
        std::map<std::string, PyRep *> res;
        res["amount"] = new PyFloat(amount->value());
        res["balance"] = new PyFloat(call.client->GetBalance());
        throw UserError ("NotEnoughMoney")
                .AddISK ("amount", amount->value())
                .AddISK ("balance", call.client->GetBalance ());
    }

    return PyStatic.NewNone();
}

PyResult CharMgrService::GetCloneTypeID(PyCallArgs& call)
{
	uint32 typeID;
	if (!m_db.GetActiveCloneType(call.client->GetCharacterID(), typeID ) )
	{
		// This should not happen, because a clone is created at char creation.
		// We don't have a clone, so return a basic one. cloneTypeID = 9917 (Clone Grade Delta)
		typeID = 9917;
		sLog.Debug( "CharMgrService", "Returning a basic clone for Char %u of type %u", call.client->GetCharacterID(), typeID );
	}
    return new PyInt(typeID);
}

PyResult CharMgrService::GetHomeStation(PyCallArgs& call)
{
	uint32 stationID = 0;
    if (!CharacterDB::GetCharHomeStation(call.client->GetCharacterID(), stationID) ) {
		sLog.Error( "CharMgrService", "Could't get the home station for Char %u", call.client->GetCharacterID() );
		return PyStatic.NewNone();
	}
    return new PyInt(stationID);
}

PyResult CharMgrService::SetActivityStatus(PyCallArgs& call, PyInt* afk, PyInt* secondsAFK) {
    sLog.Cyan("CharMgrService::SetActivityStatus()", "Player %s(%u) AFK:%s, time:%i.", \
            call.client->GetName(), call.client->GetCharacterID(), (afk->value() ? "true" : "false"), secondsAFK->value());

    if (afk->value()) {
        call.client->SetAFK(true);
    } else {
        call.client->SetAFK(false);
    }

    // call code here to set an AFK watchdog?  config option?
    // returns nothing
    return nullptr;
}

PyResult CharMgrService::GetCharacterDescription(PyCallArgs &call, PyInt* characterID)
{
    sItemFactory.SetUsingClient(call.client);
    CharacterRef c = sItemFactory.GetCharacterRef(characterID->value());
    if (!c ) {
        _log(CHARACTER__ERROR, "GetCharacterDescription failed to load character %u.", characterID->value());
        return nullptr;
    }

    return new PyString(c->description());
}

PyResult CharMgrService::SetCharacterDescription(PyCallArgs &call, PyWString* description)
{
    CharacterRef c = call.client->GetChar();
    if (!c ) {
        _log(CHARACTER__ERROR, "SetCharacterDescription called with no char!");
        return nullptr;
    }
    c->SetDescription(description->content().c_str());

    return nullptr;
}

/** ***********************************************************************
 * @note   these below are partially coded
 */

PyResult CharMgrService::GetSettingsInfo(PyCallArgs& call) {
    /**
     *    def UpdateSettingsStatistics(self):
     *        code, verified = macho.Verify(sm.RemoteSvc('charMgr').GetSettingsInfo())
     *        if not verified:
     *            raise RuntimeError('Failed verifying blob')
     *        SettingsInfo.func_code = marshal.loads(code)
     *        ret = SettingsInfo()
     *        if len(ret) > 0:
     *            sm.RemoteSvc('charMgr').LogSettings(ret)
     */

    // Called in file "carbon/client/script/ui/services/settingsSvc.py"
    // This should return a marshaled python function.
    // It returns a tuple containing a dict that is then sent to
    // charMgr::LogSettings if the tuple has a length greater than zero.
    PyTuple* res = new PyTuple( 2 );
    // This returns an empty tuple
    unsigned char code[] = {
        0x63,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x43,0x00,0x00,
        0x00,0x73,0x0a,0x00,0x00,0x00,0x64,0x01,0x00,0x7d,0x00,0x00,0x7c,0x00,0x00,0x53,
        0x28,0x02,0x00,0x00,0x00,0x4e,0x28,0x00,0x00,0x00,0x00,0x28,0x00,0x00,0x00,0x00,
        0x28,0x01,0x00,0x00,0x00,0x74,0x03,0x00,0x00,0x00,0x74,0x75,0x70,0x28,0x00,0x00,
        0x00,0x00,0x28,0x00,0x00,0x00,0x00,0x73,0x10,0x00,0x00,0x00,0x2e,0x2f,0x6d,0x61,
        0x6b,0x65,0x5a,0x65,0x72,0x6f,0x52,0x65,0x74,0x2e,0x70,0x79,0x74,0x08,0x00,0x00,
        0x00,0x72,0x65,0x74,0x54,0x75,0x70,0x6c,0x65,0x0c,0x00,0x00,0x00,0x73,0x04,0x00,
        0x00,0x00,0x00,0x01,0x06,0x02
    };
    int codeLen = sizeof(code) / sizeof(*code);
    std::string codeString(code, code + codeLen);
    res->items[ 0 ] = new PyString(codeString);

    // error code? 0 = no error
    // if called with any value other than zero the exception output will show 'Verified = False'
    // if called with zero 'Verified = True'
    /*  Just a note
     * due to the client being in placebo mode, the verification code in evecrypto just checks if the signature is 0.
     * when the client is in cryptoapi mode it verifies the signature is signed by CCP's rsa key.
     */

    res->items[ 1 ] = new PyInt( 0 );
    return res;
}

// this takes in the value returned from the function we return in GetSettingsInfo
// based on the captured data, this is used to gather info about game settings the players use
PyResult CharMgrService::LogSettings(PyCallArgs& call, PyRep* settingsInfoRet) {
    /*
     *    [PyTuple 1 items]
     *      [PyTuple 2 items]
     *        [PyInt 0]
     *        [PySubStream 734 bytes]
     *          [PyTuple 4 items]
     *            [PyInt 1]
     *            [PyString "LogSettings"]
     *            [PyTuple 1 items]
     *              [PyDict 36 kvp]
     *                [PyString "locale"]
     *                [PyString "en_US"]
     *                [PyString "shadowquality"]
     *                [PyInt 1]
     *                [PyString "video_vendorid"]
     *                [PyIntegerVar 4318]
     *                [PyString "dronemodelsenabled"]
     *                [PyInt 1]
     *                [PyString "loadstationenv2"]
     *                [PyInt 1]
     *                [PyString "video_deviceid"]
     *                [PyIntegerVar 3619]
     *                [PyString "camerashakesenabled"]
     *                [PyInt 1]
     *                [PyString "interiorshaderquality"]
     *                [PyInt 0]
     *                [PyString "presentation_interval"]
     *                [PyInt 1]
     *                [PyString "windowed_resolution"]
     *                [PyString "1440x900"]
     *                [PyString "explosioneffectssenabled"]
     *                [PyInt 1]
     *                [PyString "uiscalingfullscreen"]
     *                [PyFloat 1]
     *                [PyString "lod"]
     *                [PyInt 1]
     *                [PyString "audioenabled"]
     *                [PyInt 1]
     *                [PyString "voiceenabled"]
     *                [PyInt 1]
     *                [PyString "autodepth_stencilformat"]
     *                [PyInt 75]
     *                [PyString "hdrenabled"]
     *                [PyInt 1]
     *                [PyString "backbuffer_format"]
     *                [PyInt 22]
     *                [PyString "effectssenabled"]
     *                [PyInt 1]
     *                [PyString "breakpadUpload"]
     *                [PyInt 1]
     *                [PyString "windowed"]
     *                [PyBool True]
     *                [PyString "transgaming"]
     *                [PyBool False]
     *                [PyString "missilesenabled"]
     *                [PyInt 1]
     *                [PyString "sunocclusion"]
     *                [PyInt 1]
     *                [PyString "optionalupgrade"]
     *                [PyInt 0]
     *                [PyString "uiscalingwindowed"]
     *                [PyFloat 1]
     *                [PyString "textureqality"]
     *                [PyInt 0]
     *                [PyString "interiorgraphicsquality"]
     *                [PyInt 0]
     *                [PyString "antialiasing"]
     *                [PyInt 2]
     *                [PyString "defaultDockingView"]
     *                [PyString "hangar"]
     *                [PyString "turretsenabled"]
     *                [PyInt 1]
     *                [PyString "advancedcamera"]
     *                [PyInt 0]
     *                [PyString "postprocessing"]
     *                [PyInt 1]
     *                [PyString "fixedwindow"]
     *                [PyBool False]
     *                [PyString "shaderquality"]
     *                [PyInt 3]
     *                [PyString "fullscreen_resolution"]
     *                [PyString "1440x900"]
     *            [PyDict 1 kvp]
     *              [PyString "machoVersion"]
     *              [PyInt 1]
     */
    sLog.Warning( "CharMgrService::Handle_LogSettings()", "size= %lli", call.tuple->size());
    call.Dump(CHARACTER__TRACE);
    return nullptr;
}

//17:09:10 L CharMgrService::Handle_GetNote(): size= 1
PyResult CharMgrService::GetNote(PyCallArgs& call, PyInt* itemID)
{
    uint32 ownerID = call.client->GetCharacterID();

	PyString *str = m_db.GetNote(ownerID, itemID->value());
    if (!str)
        str = new PyString("");

    return str;
}

PyResult CharMgrService::SetNote(PyCallArgs &call, PyInt* itemID, PyString* note)
{
    m_db.SetNote(call.client->GetCharacterID(), itemID->value(), note->content().c_str());

    return PyStatic.NewNone();
}

/*  notepad.  every note is one chrOwnerNote row; the label prefix tells the kind:
 *   N: note (label = title), S: client setting (eg. 'S:Folders', split in 6000 char parts), B: bookmark note, I: item note
 */
static bool IsValidNoteLabel(const std::string& label)
{
    if ((label.size() < 2) or (label[1] != ':'))
        return false;
    return ((label[0] == 'N') or (label[0] == 'S') or (label[0] == 'B') or (label[0] == 'I'));
}

// chrOwnerNote.note is a text column
static const size_t maxOwnerNoteSize = 65000;

PyResult CharMgrService::AddOwnerNote(PyCallArgs& call, PyRep* label, PyRep* note) {
    // AddOwnerNote(label, note) -> noteID.  either arg may arrive as String or WString
    std::string labelStr = PyRep::StringContent(label);
    std::string noteStr = PyRep::StringContent(note);
    if (!IsValidNoteLabel(labelStr) or (labelStr.size() > 255))
        throw CustomError("Invalid note label.");
    if (noteStr.size() > maxOwnerNoteSize)
        throw CustomError("This note is too long.");

    uint32 noteID = m_db.AddOwnerNote(call.client->GetCharacterID(), labelStr, noteStr);
    if (noteID == 0)
        throw CustomError("The note could not be saved.");
    return new PyInt(noteID);
}

PyResult CharMgrService::EditOwnerNote(PyCallArgs& call, PyInt* noteID, PyRep* label, std::optional<PyRep*> note) {
    // EditOwnerNote(noteID, label[, note]).  renaming a note sends no text
    std::string labelStr = PyRep::StringContent(label);
    if (!IsValidNoteLabel(labelStr) or (labelStr.size() > 255))
        throw CustomError("Invalid note label.");

    std::string noteStr;
    bool hasNote = (note.has_value() and (note.value() != nullptr) and !note.value()->IsNone());
    if (hasNote) {
        noteStr = PyRep::StringContent(note.value());
        if (noteStr.size() > maxOwnerNoteSize)
            throw CustomError("This note is too long.");
    }

    m_db.EditOwnerNote(call.client->GetCharacterID(), noteID->value(), &labelStr, hasNote ? &noteStr : nullptr);
    return PyStatic.NewNone();
}

PyResult CharMgrService::RemoveOwnerNote(PyCallArgs& call, PyInt* noteID) {
    m_db.RemoveOwnerNote(call.client->GetCharacterID(), noteID->value());
    return PyStatic.NewNone();
}

PyResult CharMgrService::GetOwnerNote(PyCallArgs &call, PyInt* noteID)
{
    // rowset (noteID, label, note) of the caller's note; the client reads [0].note and [0].label
    return m_db.GetOwnerNote(call.client->GetCharacterID(), noteID->value());
}

PyResult CharMgrService::GetOwnerNoteLabels(PyCallArgs &call)
{
    // rowset (noteID, label) of all the caller's notes
    return m_db.GetOwnerNoteLabels(call.client->GetCharacterID());
}

/*  contacts (address book).
 *   standings are the five client levels (contact*Standing: -10, -5, 0, 5, 10).
 *   agents are added with AddContact(agentID) only; they have no standing or watchlist.
 *   the blocked list is separate (chrBlocked); blocking doesn't touch the contact.
 */
static bool IsTrue(PyRep* rep)
{
    if ((rep == nullptr) or rep->IsNone())
        return false;
    if (rep->IsBool())
        return rep->AsBool()->value();
    if (rep->IsInt() or rep->IsLong())
        return (PyRep::IntegerValue(rep) != 0);
    if (rep->IsFloat())
        return (rep->AsFloat()->value() != 0);
    return true;
}

static float ClampStanding(PyRep* rep)
{
    double value(0);
    if (rep->IsFloat()) {
        value = rep->AsFloat()->value();
    } else if (rep->IsInt() or rep->IsLong()) {
        value = (double)PyRep::IntegerValue(rep);
    }
    // nearest of the five levels
    static const float levels[] = { -10.0f, -5.0f, 0.0f, 5.0f, 10.0f };
    float best(0.0f);
    double dist(100.0);
    for (float cur : levels) {
        if (std::fabs(value - cur) < dist) {
            dist = std::fabs(value - cur);
            best = cur;
        }
    }
    return best;
}

PyResult CharMgrService::AddContact(PyCallArgs& call, PyInt* contactID)
{
    // the address book adds agents with only their id
    if (!IsAgent(contactID->value()))
        throw CustomError("Only agents can be added without a standing.");
    m_db.AddContact(call.client->GetCharacterID(), contactID->value(), 0.0f, false);
    return PyStatic.NewNone();
}

PyResult CharMgrService::AddContact(PyCallArgs& call, PyInt* contactID, PyRep* relationshipID, PyRep* inWatchlist, PyRep* notify, std::optional<PyRep*> message)
{
    SaveContact(call.client, contactID->value(), relationshipID, inWatchlist, notify, message.has_value() ? message.value() : nullptr, false);
    return PyStatic.NewNone();
}

PyResult CharMgrService::EditContact(PyCallArgs& call, PyInt* contactID, PyRep* relationshipID, PyRep* inWatchlist, PyRep* notify, std::optional<PyRep*> message)
{
    SaveContact(call.client, contactID->value(), relationshipID, inWatchlist, notify, message.has_value() ? message.value() : nullptr, true);
    return PyStatic.NewNone();
}

void CharMgrService::SaveContact(Client* pClient, uint32 contactID, PyRep* relationshipID, PyRep* inWatchlist, PyRep* notify, PyRep* message, bool isEdit)
{
    // CSPA charges for the notification (ContactCostNotApproved) are not implemented
    uint32 charID = pClient->GetCharacterID();
    if (contactID == charID)
        throw CustomError("You can't add yourself as a contact.");
    bool isChar = IsCharacterID(contactID);
    if (!isChar and !IsAgent(contactID) and !IsCorp(contactID) and !IsAlliance(contactID) and !IsFaction(contactID))
        throw CustomError("That can't be added as a contact.");

    float standing = ClampStanding(relationshipID);
    // only characters can be watched
    bool watch = (isChar and IsTrue(inWatchlist));

    bool wasWatched = (isChar and CharacterDB::IsInWatchlist(charID, contactID));
    if (isEdit and m_db.IsContact(charID, contactID)) {
        m_db.UpdateContact(charID, contactID, standing, watch ? 1 : 0);
    } else {
        m_db.AddContact(charID, contactID, standing, watch);
    }

    // with mutual watchlists, a contact watching me sees my state only while I watch them
    if ((watch != wasWatched) and sConfig.chat.ContactStatusMutual and CharacterDB::IsInWatchlist(contactID, charID)) {
        Client* pContact = sEntityList.FindClientByCharID(contactID);
        if (pContact != nullptr) {
            PyTuple* tuple = new PyTuple(1);
                tuple->SetItem(0, new PyInt(charID));
            pContact->SendNotification(watch ? "OnContactLoggedOn" : "OnContactLoggedOff", "charid", &tuple);
        }
    }

    if (!IsTrue(notify) or !isChar)
        return;
    // the notification is optional, and never reaches someone who blocked me
    if (CharacterDB::IsBlocked(contactID, charID))
        return;

    PyDict* data = new PyDict();
        data->SetItemString("level", new PyFloat(standing));
    if ((message != nullptr) and !message->IsNone()) {
        std::string text = PyRep::StringContent(message);
        if (!text.empty())
            data->SetItemString("message", new PyWString(text));
    }
    NotificationMgr::Send(contactID, isEdit ? Notify::Types::ContactEdit : Notify::Types::ContactAdd, charID, data);
}

PyResult CharMgrService::DeleteContacts(PyCallArgs& call, PyList* contactIDs)
{
    // DeleteContacts([contactIDs])
    for (PyList::const_iterator itr = contactIDs->begin(); itr != contactIDs->end(); ++itr)
        m_db.RemoveContact(PyRep::IntegerValueU32(*itr), call.client->GetCharacterID());
    return PyStatic.NewNone();
}

PyResult CharMgrService::BlockOwners(PyCallArgs& call, PyList* ownerIDs)
{
    // blocking is for players: self and npc ids are ignored
    uint32 charID = call.client->GetCharacterID();
    for (PyList::const_iterator itr = ownerIDs->begin(); itr != ownerIDs->end(); ++itr) {
        uint32 ownerID = PyRep::IntegerValueU32(*itr);
        if ((ownerID == charID) or !(IsCharacterID(ownerID) or IsPlayerCorp(ownerID) or IsAlliance(ownerID)))
            continue;
        CharacterDB::Block(charID, ownerID);
    }
    return PyStatic.NewNone();
}

PyResult CharMgrService::UnblockOwners(PyCallArgs& call, PyList* ownerIDs)
{
    for (PyList::const_iterator itr = ownerIDs->begin(); itr != ownerIDs->end(); ++itr)
        CharacterDB::Unblock(call.client->GetCharacterID(), PyRep::IntegerValueU32(*itr));
    return PyStatic.NewNone();
}

PyResult CharMgrService::EditContactsRelationshipID(PyCallArgs& call, PyList* contactIDs, PyRep* relationshipID)
{
    float standing = ClampStanding(relationshipID);
    for (PyList::const_iterator itr = contactIDs->begin(); itr != contactIDs->end(); ++itr)
        m_db.UpdateContact(call.client->GetCharacterID(), PyRep::IntegerValueU32(*itr), standing);
    return PyStatic.NewNone();
}

/*  contact labels.  labelIDs are bit values, so they can be longs */
static const char* s_contactLabels = "chrLabels";
static const char* s_contacts = "chrContacts";

PyResult CharMgrService::CreateLabel(PyCallArgs& call, PyRep* name, std::optional<PyRep*> color)
{
    uint32 charID = call.client->GetCharacterID();
    std::string labelName = PyRep::StringContent(name);
    if (labelName.empty())
        throw UserError("MailLabelMustProvideName");
    MailMgr::TruncateUtf8(labelName, mailMaxLabelSize);
    if (CharacterDB::ContactLabelNameExists(s_contactLabels, charID, labelName))
        throw UserError("MailLabelNameExists");

    int32 colorVal(0);
    bool hasColor = (color.has_value() and (color.value() != nullptr) and !color.value()->IsNone());
    if (hasColor)
        colorVal = (int32)PyRep::IntegerValue(color.value());

    int64 labelID = CharacterDB::CreateContactLabel(s_contactLabels, charID, labelName, hasColor ? &colorVal : nullptr);
    if (labelID == 0)
        throw CustomError("You have too many labels.");
    return new PyLong(labelID);
}

PyResult CharMgrService::EditLabel(PyCallArgs& call, PyRep* labelID, PyRep* name, std::optional<PyRep*> color)
{
    uint32 charID = call.client->GetCharacterID();
    int64 id = PyRep::IntegerValue(labelID);

    std::string labelName;
    bool hasName = !name->IsNone();
    if (hasName) {
        labelName = PyRep::StringContent(name);
        if (labelName.empty())
            throw UserError("MailLabelMustProvideName");
        MailMgr::TruncateUtf8(labelName, mailMaxLabelSize);
        if (CharacterDB::ContactLabelNameExists(s_contactLabels, charID, labelName, id))
            throw UserError("MailLabelNameExists");
    }
    int32 colorVal(0);
    bool hasColor = (color.has_value() and (color.value() != nullptr) and !color.value()->IsNone());
    if (hasColor)
        colorVal = (int32)PyRep::IntegerValue(color.value());

    CharacterDB::EditContactLabel(s_contactLabels, charID, id, hasName ? &labelName : nullptr, hasColor ? &colorVal : nullptr);
    return PyStatic.NewNone();
}

PyResult CharMgrService::DeleteLabel(PyCallArgs& call, PyRep* labelID)
{
    CharacterDB::DeleteContactLabel(s_contactLabels, s_contacts, call.client->GetCharacterID(), PyRep::IntegerValue(labelID));
    return PyStatic.NewNone();
}

static void GetContactIDs(PyList* list, std::vector<int32>& into)
{
    for (PyList::const_iterator itr = list->begin(); itr != list->end(); ++itr)
        if ((*itr)->IsInt() or (*itr)->IsLong())
            into.push_back(PyRep::IntegerValue(*itr));
}

PyResult CharMgrService::AssignLabels(PyCallArgs& call, PyList* contactIDs, PyRep* labelID)
{
    std::vector<int32> ids;
    GetContactIDs(contactIDs, ids);
    CharacterDB::SetContactLabelBits(s_contacts, call.client->GetCharacterID(), ids, PyRep::IntegerValue(labelID), true);
    return PyStatic.NewNone();
}

PyResult CharMgrService::RemoveLabels(PyCallArgs& call, PyList* contactIDs, PyRep* labelID)
{
    std::vector<int32> ids;
    GetContactIDs(contactIDs, ids);
    CharacterDB::SetContactLabelBits(s_contacts, call.client->GetCharacterID(), ids, PyRep::IntegerValue(labelID), false);
    return PyStatic.NewNone();
}

PyResult CharMgrService::GetFactions(PyCallArgs& call)
{
    sLog.Warning( "CharMgrService::Handle_GetFactions()", "size= %lli", call.tuple->size());
    call.Dump(CHARACTER__TRACE);
    return nullptr;
}
