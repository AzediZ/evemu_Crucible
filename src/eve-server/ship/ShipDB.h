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


#ifndef __SHIPDB_H_INCL__
#define __SHIPDB_H_INCL__

#include "ServiceDB.h"

class PyRep;
class PyTuple;
class PyResult;

class ShipDB
: public ServiceDB
{
public:
    // insurance functions  -allan  21Jul14
    struct InsuranceRow {
        bool isCorp;
        uint16 typeID;
        uint32 shipID;
        uint32 ownerID;         // character, or the corp for corp insurance
        float fraction;         // 0.5 .. 1.0
        int64 startDate;
        int64 endDate;
        std::string shipName;
    };
    PyRep *GetInsuranceByShipID(uint32 shipID);
    PyRep *GetInsuranceByOwnerID(uint32 ownerID);
    static bool InsertInsurance(uint32 shipID, uint16 typeID, const std::string& name, uint32 ownerID, bool isCorpItem, float fraction, double payOut, uint8 numWeeks = 12);
    static void DeleteInsuranceByShipID(uint32 shipID);
    static bool GetInsurance(uint32 shipID, InsuranceRow& into);
    // contracts that ended before 'now' and weren't announced yet
    static void GetExpiredInsurance(int64 now, std::vector<InsuranceRow>& into);
    static void SetExpiryNotified(uint32 shipID);
    // expired rows are kept a while to report "expired" when the ship dies
    static void DeleteInsuranceEndedBefore(int64 time);
    // -1 not insured, 0 personal insurance, 1 corp insurance
    static int8 GetInsuranceKind(uint32 shipID);
    // ends a ship's insurance (contract, trade, owner change...)
    static void VoidInsurance(uint32 shipID);
    // a ship changed owner:  personal insurance ends (corp insurance is checked at the loss)
    static void OnShipOwnerChanged(uint32 shipID, uint32 newOwnerID);

    bool IsShipInsured(uint32 shipID);

    // linking save/load methods  -allan 05Jan19
    static void LoadWeaponGroups(uint32 shipID, DBQueryResult& res);
    // this will delete existing regardless of save state
    static void SaveWeaponGroups(uint32 shipID, std::multimap<uint32, uint32>& data);
    static void ClearWeaponGroups(uint32 shipID);
};

#endif
