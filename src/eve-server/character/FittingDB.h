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

/*
 * saved ship fittings for characters (charFittingMgr) and corporations (corpFittingMgr).
 *  stored in chrFittings (header) and chrFittingItems (typeID, flagID, quantity per fitting)
 */

#ifndef EVEMU_CHARACTER_FITTINGDB_H_
#define EVEMU_CHARACTER_FITTINGDB_H_

#include "ServiceDB.h"

// client appConst
namespace Fitting {
    const uint16 maxCharFittings = 100;
    const uint16 maxCorpFittings = 200;
    const uint16 maxLengthFittingName = 50;
    const uint16 maxLengthFittingDescription = 500;
}

struct FittingData {
    std::string name;
    std::string description;
    uint32 shipTypeID;
    std::vector<std::tuple<uint32, uint16, uint32>> items;     // typeID, flagID, quantity
};

class Client;
class PyCallArgs;

class FittingDB
{
public:
    // dict fittingID -> KeyVal(fittingID, ownerID, name, description, shipTypeID, fitData [(typeID, flagID, qty)])
    static PyDict* GetFittings(uint32 ownerID);
    static uint32 CountFittings(uint32 ownerID);
    static bool FittingExists(uint32 ownerID, uint32 fittingID);
    // returns the new fittingID (0 on failure)
    static uint32 SaveFitting(uint32 ownerID, const FittingData& data);
    static bool DeleteFitting(uint32 ownerID, uint32 fittingID);
    static bool UpdateNameAndDescription(uint32 ownerID, uint32 fittingID, const std::string& name, const std::string& description);

    // decodes a client util.KeyVal fitting and validates it like the client's fittingSvc.VerifyFitting (throws the client's errors)
    static void Decode(PyRep* rep, FittingData& into);
    // validates and trims a name/description pair (throws the client's errors)
    static void CheckNameAndDescription(std::string& name, std::string& description);
    /* the shared service checks: owner is the caller (char) or the caller's corp (corp);
     *  corp writes need the Fitting Manager role (directors have every role)
     */
    static void CheckOwner(Client* pClient, uint32 ownerID, bool corp, bool write);

    // the charFittingMgr / corpFittingMgr calls (same contract, corp = corpFittingMgr)
    static PyRep* HandleGetFittings(Client* pClient, uint32 ownerID, bool corp);
    static PyRep* HandleSaveFitting(Client* pClient, uint32 ownerID, bool corp, PyRep* fitting);
    static PyRep* HandleSaveManyFittings(Client* pClient, uint32 ownerID, bool corp, PyDict* fittings);
    static void HandleDeleteFitting(Client* pClient, uint32 ownerID, bool corp, uint32 fittingID);
    static void HandleUpdateNameAndDescription(Client* pClient, uint32 ownerID, bool corp, uint32 fittingID, PyRep* name, PyRep* description);
};

#endif  // EVEMU_CHARACTER_FITTINGDB_H_
