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
    Author:     caytchen
*/

#include "eve-server.h"

#include "../../eve-common/EVE_Character.h"


#include "character/PaperDollService.h"
#include "character/Character.h"
#include "character/CharacterDB.h"
#include "imageserver/ImageServer.h"

// util.KeyVal doll / portrait info from the character creator -> its dict (nullptr if it isn't one)
static PyDict* GetKeyValDict(PyRep* rep)
{
    if ((rep == nullptr) or !rep->IsObject())
        return nullptr;
    PyObject* obj = rep->AsObject();
    if ((obj->arguments() == nullptr) or !obj->arguments()->IsDict())
        return nullptr;
    return obj->arguments()->AsDict();
}

/* the character being recustomized: the session character, or at character selection (forced recustomization,
 *  no session character yet) one of the account's characters
 */
static void CheckRecustomizeOwner(Client* pClient, uint32 charID)
{
    if (pClient->GetCharacterID() != 0) {
        if (charID != pClient->GetCharacterID())
            throw CustomError ("You can only recustomize your own character.");
    } else if (CharacterDB::GetAccountID(charID) != pClient->GetUserID()) {
        throw CustomError ("You can only recustomize your own character.");
    }
}

PaperDollService::PaperDollService() :
    Service("paperDollServer", eAccessLevel_User)
{
    this->Add("GetPaperDollData", &PaperDollService::GetPaperDollData);
    this->Add("GetMyPaperDollData", &PaperDollService::GetMyPaperDollData);
    this->Add("ConvertAndSavePaperDoll", &PaperDollService::ConvertAndSavePaperDoll);
    this->Add("GetPaperDollPortraitDataFor", &PaperDollService::GetPaperDollPortraitDataFor);
    this->Add("UpdateExistingCharacterFull", &PaperDollService::UpdateExistingCharacterFull);
    this->Add("UpdateExistingCharacterLimited", &PaperDollService::UpdateExistingCharacterLimited);
}

//17:35:32 L PaperDollService::Handle_GetPaperDollData(): size=1
PyResult PaperDollService::GetPaperDollData(PyCallArgs &call, PyInt* characterID) {
    call.Dump(PLAYER__CALL_DUMP);
    // this is called when viewing full body of a character.

    return m_db.GetPaperDollAvatarColors(characterID->value());
}

PyResult PaperDollService::ConvertAndSavePaperDoll(PyCallArgs &call) {
    call.Dump(PLAYER__CALL_DUMP);
    return nullptr;
}

PyResult PaperDollService::UpdateExistingCharacterFull(PyCallArgs &call, PyInt* characterID, PyRep* dollInfo, PyRep* portraitInfo, PyBool* dollExists) {
    /*
        sm.RemoteSvc('paperDollServer').UpdateExistingCharacterFull(charID, dollInfo, portraitInfo, dollExists)
     * full recustomization (sculpting) needs a granted paperDollState; the state is used up afterwards
     */
    uint32 charID(characterID->value());
    CheckRecustomizeOwner(call.client, charID);
    switch (CharacterDB::GetPaperDollState(charID)) {
        case Char::PDState::Resculpting:
        case Char::PDState::NoExistingCustomization:
        case Char::PDState::FullRecustomizing:
        case Char::PDState::ForceRecustomize:
            break;
        default:
            throw CustomError ("Your character is not flagged for full recustomization.");
    }

    PyDict* doll(GetKeyValDict(dollInfo));
    PyDict* portrait(GetKeyValDict(portraitInfo));
    if (doll == nullptr)
        throw CustomError ("The character appearance data is invalid.");

    CharacterDB::ClearAvatar(charID, true);
    CharacterAppearance capp;
        capp.Build(charID, doll);
    if (portrait != nullptr) {
        CharacterDB::ClearPortrait(charID);
        CharacterPortrait cpor;
            cpor.Build(charID, portrait);
    }
    CharacterDB::SetPaperDollState(charID, Char::PDState::NoRecustomization);
    // the new portrait image was uploaded (photoUploadSvc) just before this call
    sImageServer.SavePortrait(call.client->GetUserID(), charID);

    _log(CLIENT__MESSAGE, "%s: full recustomization of %u saved.", call.client->GetName(), charID);
    return nullptr;
}

PyResult PaperDollService::UpdateExistingCharacterLimited(PyCallArgs &call, PyInt* characterID, PyRep* dollData, PyRep* portraitInfo, PyBool* dollExists) {
    /* limited recustomization (clothes, hair, makeup, portrait) is free at any time.  the client sends no sculpts, the stored ones are kept
     *  sm.RemoteSvc('paperDollServer').UpdateExistingCharacterLimited(charID, dollData, portraitInfo, dollExists)
     */
    uint32 charID(characterID->value());
    CheckRecustomizeOwner(call.client, charID);

    PyDict* doll(GetKeyValDict(dollData));
    PyDict* portrait(GetKeyValDict(portraitInfo));
    if (doll == nullptr)
        throw CustomError ("The character appearance data is invalid.");

    CharacterDB::ClearAvatar(charID, false);
    CharacterAppearance capp;
        capp.Build(charID, doll);
    if (portrait != nullptr) {
        CharacterDB::ClearPortrait(charID);
        CharacterPortrait cpor;
            cpor.Build(charID, portrait);
    }
    sImageServer.SavePortrait(call.client->GetUserID(), charID);

    _log(CLIENT__MESSAGE, "%s: limited recustomization of %u saved.", call.client->GetName(), charID);
    /*
        sm.RemoteSvc('paperDollServer').UpdateExistingCharacterLimited(charID, dollData, portraitInfo, dollExists)
        */
    /*)
00:46:38 [SvcCall] Service photoUploadSvc::Upload()
00:46:38 W       ImageServer:  ReportNewImage() called.
00:46:38 M    PhotoUploadSvc: Received image from account 3, size: 57621
00:46:39 [SvcCall] Service paperDollServer::UpdateExistingCharacterLimited()
*/
    return nullptr;
}

PyResult PaperDollService::GetPaperDollPortraitDataFor(PyCallArgs &call, PyInt* characterID) {
    //    data = sm.RemoteSvc('paperDollServer').GetPaperDollPortraitDataFor(charID)
    /*
            portraitData = sm.GetService('cc').GetPortraitData(charID)
            if portraitData is not None:
                self.lightingID = portraitData.lightID
                self.lightColorID = portraitData.lightColorID
                self.lightIntensity = portraitData.lightIntensity
                path = self.GetBackgroundPathFromID(portraitData.backgroundID)
                if path in ccConst.backgroundOptions:
                    self.backdropPath = path
                self.poseID = portraitData.portraitPoseNumber
                self.cameraPos = (portraitData.cameraX, portraitData.cameraY, portraitData.cameraZ)
                self.cameraPoi = (portraitData.cameraPoiX, portraitData.cameraPoiY, portraitData.cameraPoiZ)
                self.cameraFov = portraitData.cameraFieldOfView
                params = self.GetControlParametersFromPoseData(portraitData, fromDB=True).values()
                self.characterSvc.SetControlParametersFromList(params, charID)
    */
    return m_db.GetPaperDollPortraitData(characterID->value());
}

PyResult PaperDollService::GetMyPaperDollData(PyCallArgs &call, PyInt* characterID)
{
    call.Dump(PLAYER__CALL_DUMP);

    // at character selection there is no session character yet:  use the (account's) character asked for
    uint32 charID(call.client->GetCharacterID());
    if (charID == 0) {
        CheckRecustomizeOwner(call.client, characterID->value());
        charID = characterID->value();
    }

	PyDict* args = new PyDict;

	args->SetItemString( "colors", m_db.GetPaperDollAvatarColors(charID) );
	args->SetItemString( "modifiers", m_db.GetPaperDollAvatarModifiers(charID) );
	args->SetItemString( "appearance", m_db.GetPaperDollAvatar(charID) );
	args->SetItemString( "sculpts", m_db.GetPaperDollAvatarSculpts(charID) );

    return new PyObject("util.KeyVal", args);
}
