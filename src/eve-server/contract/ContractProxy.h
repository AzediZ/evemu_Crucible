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
    Author:        Captnoord
    Updates:    Allan
*/

#ifndef __CONTRACT_PROXY_H__INCL__
#define __CONTRACT_PROXY_H__INCL__

#include "services/Service.h"

class ContractProxy : public Service <ContractProxy>
{
public:
    ContractProxy();

protected:
    // client:  CreateContract(type, avail, assigneeID, expiretime, duration, startStationID, endStationID, price, reward, collateral,
    //  title, description, itemList=, flag=, requestItemTypeList=, forCorp=, confirm=)
    PyResult CreateContract(PyCallArgs& call, PyInt* contractType, PyRep* isPrivate, std::optional<PyRep*> assigneeID, PyInt* expireTime, PyInt* duration, PyInt* startStationID, std::optional<PyRep*> endStationID, PyRep* price, PyRep* reward, PyRep* collateral, PyRep* title, PyRep* description);
    PyResult GetContract(PyCallArgs& call, PyInt* contractID);
    PyResult AcceptContract(PyCallArgs& call, PyInt* contractID, std::optional<PyRep*> forCorp);
    PyResult CompleteContract(PyCallArgs& call, PyInt* contractID, PyInt* completionStatus);
    PyResult DeleteContract(PyCallArgs& call, PyInt* contractID);
    PyResult NumOutstandingContracts(PyCallArgs& call);
    PyResult GetItemsInStation(PyCallArgs& call, PyInt* stationID, std::optional<PyRep*> forCorp);
    PyResult GetItemsInContainer(PyCallArgs& call, PyInt* stationID, PyInt* containerID, PyRep* forCorp, std::optional<PyInt*> flag);
    PyResult SplitStack(PyCallArgs& call, PyInt* stationID, PyInt* itemID, PyInt* qty, PyRep* forCorp, std::optional<PyInt*> flag);
    PyResult GetCourierContractFromItemID(PyCallArgs& call, PyInt* itemID);
    PyResult GetLoginInfo(PyCallArgs& call);
    PyResult SearchContracts(PyCallArgs& call);
    PyResult CollectMyPageInfo(PyCallArgs& call);
    PyResult GetMyExpiredContractList(PyCallArgs& call, std::optional<PyRep*> forCorp);
    PyResult GetMyBids(PyCallArgs& call, std::optional<PyRep*> forCorp);
    PyResult GetMyCurrentContractList(PyCallArgs& call, PyRep* isAccepted, std::optional<PyRep*> forCorp);
    PyResult DeleteNotification(PyCallArgs& call, PyInt* contractID, std::optional<PyRep*> forCorp);
    PyResult PlaceBid(PyCallArgs& call, PyInt* contractID, PyRep* bid, std::optional<PyRep*> forCorp);
    PyResult FinishAuction(PyCallArgs& call, PyInt* contractID, PyRep* isIssuer);
    PyResult GetContractListForOwner(PyCallArgs& call, PyInt* ownerID, PyInt* contractStatus, std::optional <PyInt*> contractType, std::optional <PyBool*> issuedToBy);

private:
    // the client's own (forCorp: its corp's) outstanding contract counts (NumOutstandingContracts)
    struct OutstandingCounts {
        uint32 myCharTotal;
        uint32 nonCorpForMyChar;
        uint32 myCorpTotal;
        uint32 nonCorpForMyCorp;
    };
    static void CountOutstanding(Client* pClient, OutstandingCounts& into);
    // " WHERE ..." of the journal's "requires attention" list
    static std::string AttentionWhere(Client* pClient, bool forCorp);
    // corp contract work needs the Contract Manager role (directors have it)
    static void CheckContractManager(Client* pClient);
    // the corp wallet division of the session, if the member can take from it
    static uint16 GetCorpWalletKey(Client* pClient);
};

#endif /* !__CONTRACT_PROXY_H__INCL__ */
