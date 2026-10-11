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
    Author:         Captnoord
    Rewrite:        Allan
    Implementation: AlTahir
*/

#include <boost/algorithm/string/replace.hpp>
#include <algorithm>    // Added to prevent std::find from freaking out
#include "eve-server.h"


#include "EVEServerConfig.h"
#include "contract/ContractProxy.h"
#include "station/Station.h"
#include "inventory/Inventory.h"
#include "system/SolarSystem.h"

#include "contract/ContractUtils.h"
#include "account/AccountService.h"
#include "manufacturing/Blueprint.h"
#include "market/MarketMgr.h"
#include "ship/ShipDB.h"
#include "station/StationDataMgr.h"

/* contractscommon limits */
static const int32 s_numContractsBySkill[6]     = {1, 5, 9, 13, 17, 21};
static const int32 s_numContractsBySkillCorp[6] = {10, 20, 30, 40, 50, 60};
static const int32 s_maxNumContracts            = 500;   // MAX_NUM_CONTRACTS, contracts assigned inside the own corp
static const int32 s_maxNumItems                = 200;   // MAX_NUM_ITEMS
static const double s_courierMaxVolume          = 981250.0;
static const size_t s_maxTitleLength            = 50;
static const size_t s_maxDescLength             = 4000;

ContractProxy::ContractProxy () :
    Service("contractProxy")
{
    this->Add("GetContract", &ContractProxy::GetContract);
    this->Add("CreateContract", &ContractProxy::CreateContract);
    this->Add("DeleteContract", &ContractProxy::DeleteContract);
    this->Add("AcceptContract", &ContractProxy::AcceptContract);
    this->Add("CompleteContract", &ContractProxy::CompleteContract);
    this->Add("GetLoginInfo", &ContractProxy::GetLoginInfo);
    this->Add("SearchContracts", &ContractProxy::SearchContracts);
    this->Add("NumOutstandingContracts", &ContractProxy::NumOutstandingContracts);
    this->Add("CollectMyPageInfo", &ContractProxy::CollectMyPageInfo);
    this->Add("GetItemsInStation", &ContractProxy::GetItemsInStation);
    this->Add("GetItemsInContainer", &ContractProxy::GetItemsInContainer);
    this->Add("SplitStack", &ContractProxy::SplitStack);
    this->Add("GetCourierContractFromItemID", &ContractProxy::GetCourierContractFromItemID);
    this->Add("GetContractListForOwner", &ContractProxy::GetContractListForOwner);
    this->Add("GetMyExpiredContractList", &ContractProxy::GetMyExpiredContractList);
    this->Add("GetMyBids", &ContractProxy::GetMyBids);
    this->Add("GetMyCurrentContractList", &ContractProxy::GetMyCurrentContractList);
    this->Add("DeleteNotification", &ContractProxy::DeleteNotification);
    this->Add("PlaceBid", &ContractProxy::PlaceBid);
    this->Add("FinishAuction", &ContractProxy::FinishAuction);
}

namespace {
    // keyword argument by name, or nullptr when missing
    PyRep* GetKeyword(PyCallArgs& call, const char* name) {
        std::map<std::string, PyRep*>::iterator itr = call.byname.find(name);
        if (itr == call.byname.end())
            return nullptr;
        return itr->second;
    }

    // bool or int argument (None and missing are false)
    bool IsTrue(PyRep* rep) {
        if (rep == nullptr)
            return false;
        if (rep->IsBool())
            return rep->AsBool()->value();
        if (rep->IsInt())
            return (rep->AsInt()->value() != 0);
        if (rep->IsLong())
            return (rep->AsLong()->value() != 0);
        return false;
    }

    bool IsTrue(std::optional<PyRep*>& rep) {
        return (rep.has_value() and IsTrue(rep.value()));
    }

    // integer argument, nullptr/None = 0
    int64 IntArg(PyRep* rep) {
        if ((rep == nullptr) or rep->IsNone())
            return 0;
        return PyRep::IntegerValue(rep);
    }

    // ints of a list, tuple, set (objectEx) or dict keys
    void IntList(PyRep* rep, std::vector<int64>& into) {
        if ((rep == nullptr) or rep->IsNone())
            return;
        if (rep->IsList()) {
            for (auto cur : *rep->AsList())
                if (cur->IsInt() or cur->IsLong())
                    into.push_back(PyRep::IntegerValue(cur));
        } else if (rep->IsTuple()) {
            for (auto cur : *rep->AsTuple())
                if (cur->IsInt() or cur->IsLong())
                    into.push_back(PyRep::IntegerValue(cur));
        } else if (rep->IsDict()) {
            for (auto cur : *rep->AsDict())
                if (cur.first->IsInt() or cur.first->IsLong())
                    into.push_back(PyRep::IntegerValue(cur.first));
        } else if (rep->IsObjectEx()) {
            // __builtin__.set ( (list,) )
            PyRep* header = rep->AsObjectEx()->header();
            if ((header != nullptr) and header->IsTuple() and (header->AsTuple()->size() > 1)) {
                PyRep* args = header->AsTuple()->GetItem(1);
                if ((args != nullptr) and args->IsTuple() and (args->AsTuple()->size() > 0))
                    IntList(args->AsTuple()->GetItem(0), into);
            }
        }
    }

    // "1,234,567.00 ISK"
    std::string FormatISK(int64 amount) {
        std::string digits(std::to_string(amount < 0 ? -amount : amount)), out;
        int count(0);
        for (auto itr = digits.rbegin(); itr != digits.rend(); ++itr) {
            if ((count > 0) and ((count % 3) == 0))
                out.insert(out.begin(), ',');
            out.insert(out.begin(), *itr);
            ++count;
        }
        if (amount < 0)
            out.insert(out.begin(), '-');
        return out + ".00 ISK";
    }

    // is ownerID this client's corp, or a character in it
    bool IsInnerCorp(Client* pClient, uint32 assigneeID) {
        uint32 corpID(pClient->GetCorporationID());
        if (!IsPlayerCorp(corpID) or (assigneeID == 0))
            return false;
        if (assigneeID == corpID)
            return true;
        if (!IsCharacterID(assigneeID))
            return false;
        DBQueryResult res;
        DBResultRow row;
        if (sDatabase.RunQuery(res, "SELECT corporationID FROM chrCharacters WHERE characterID = %u", assigneeID) and res.GetRow(row))
            return (row.GetUInt(0) == corpID);
        return false;
    }

    // office of the client's corp in a station.  throws when there is none
    uint32 GetCorpOffice(Client* pClient, uint32 stationID) {
        uint32 officeID(stDataMgr.GetOfficeIDForCorp(stationID, pClient->GetCorporationID()));
        if (officeID == 0)
            throw CustomError("Your corporation has no office in %s.", stDataMgr.GetStationName(stationID).c_str());
        return officeID;
    }

    // the office's inventory, loaded
    Inventory* GetOfficeInventory(uint32 officeID) {
        InventoryItemRef office = sItemFactory.GetItemRef(officeID);
        if (office.get() == nullptr)
            return nullptr;
        Inventory* pInv = office->GetMyInventory();
        if (pInv != nullptr)
            pInv->LoadContents();
        return pInv;
    }

    bool IsHangarDivision(int32 flag) {
        return ((flag == flagHangar) or ((flag >= flagCorpHangar2) and (flag <= flagCorpHangar7)));
    }

    // a ship or a container the contract wizard can look into
    bool IsContainerType(InventoryItemRef iRef) {
        if (iRef->categoryID() == EVEDB::invCategories::Ship)
            return true;
        switch (iRef->groupID()) {
            case EVEDB::invGroups::Cargo_Container:
            case EVEDB::invGroups::Secure_Cargo_Container:
            case EVEDB::invGroups::Audit_Log_Secure_Container:
            case EVEDB::invGroups::Freight_Container:
                return true;
        }
        return false;
    }
}

void ContractProxy::CheckContractManager(Client* pClient)
{
    if (!IsPlayerCorp(pClient->GetCorporationID()))
        throw UserError("ConNPCNotAllowed");
    if ((pClient->GetCorpRole() & (Corp::Role::ContractManager | Corp::Role::Director)) == 0)
        throw UserError("ConCorpContractRoleMissing");
}

uint16 ContractProxy::GetCorpWalletKey(Client* pClient)
{
    uint16 key(pClient->GetCorpAccountKey());
    if (Corp::AccountTakeRole(key) == 0)
        key = Account::KeyType::Cash;
    if ((pClient->GetCorpRole() & Corp::AccountTakeRole(key)) == 0)
        throw UserError("CrpAccessDenied").AddFormatValue("reason", new PyString("You do not have access to that wallet division."));
    return key;
}

void ContractProxy::CountOutstanding(Client* pClient, OutstandingCounts& into)
{
    // "inner" contracts are private ones assigned to the own player corp or one of its members (contracts.py:2018)
    uint32 charID(pClient->GetCharacterID()), corpID(pClient->GetCorporationID());
    std::string inner = "0";
    if (IsPlayerCorp(corpID))
        inner = "(c.isPrivate <> 0 AND (c.assigneeID = " + std::to_string(corpID) + " OR ch.corporationID = " + std::to_string(corpID) + "))";

    std::string query = "SELECT"
        " IFNULL(SUM(CASE WHEN c.forCorp = 0 AND c.issuerID = {CHAR} THEN 1 ELSE 0 END), 0),"
        " IFNULL(SUM(CASE WHEN c.forCorp = 0 AND c.issuerID = {CHAR} AND NOT {INNER} THEN 1 ELSE 0 END), 0),"
        " IFNULL(SUM(CASE WHEN c.forCorp = 1 AND c.issuerCorpID = {CORP} THEN 1 ELSE 0 END), 0),"
        " IFNULL(SUM(CASE WHEN c.forCorp = 1 AND c.issuerCorpID = {CORP} AND NOT {INNER} THEN 1 ELSE 0 END), 0)"
        " FROM ctrContracts c LEFT JOIN chrCharacters ch ON ch.characterID = c.assigneeID"
        " WHERE c.status = 0 AND (c.issuerID = {CHAR} OR c.issuerCorpID = {CORP})";
    boost::replace_all(query, "{INNER}", inner);
    boost::replace_all(query, "{CHAR}", std::to_string(charID));
    boost::replace_all(query, "{CORP}", std::to_string(corpID));

    into = OutstandingCounts();
    DBQueryResult res;
    DBResultRow row;
    if (!sDatabase.RunQuery(res, query.c_str())) {
        codelog(DATABASE__ERROR, "Error in query: %s", res.error.c_str());
        return;
    }
    if (!res.GetRow(row))
        return;
    into.myCharTotal        = row.GetUInt(0);
    into.nonCorpForMyChar   = row.GetUInt(1);
    into.myCorpTotal        = row.GetUInt(2);
    into.nonCorpForMyCorp   = row.GetUInt(3);
}

std::string ContractProxy::AttentionWhere(Client* pClient, bool forCorp)
{
    // journal.py DoShowContracts status 0
    uint32 me(forCorp ? pClient->GetCorporationID() : pClient->GetCharacterID());
    std::string issuerMe = (forCorp
                         ? "(issuerCorpID = " + std::to_string(me) + " AND forCorp = 1)"
                         : "(issuerID = " + std::to_string(me) + " AND forCorp = 0)");
    std::string acceptorMe = "acceptorID = " + std::to_string(me);
    std::string myBids = "contractId IN (SELECT contractId FROM ctrBids WHERE bidderID = " + std::to_string(me) + ")";
    std::string myOutbid = "(contractId IN (SELECT contractId FROM ctrBids WHERE bidderID = " + std::to_string(me) + " AND refunded = 1)"
                           " AND contractId NOT IN (SELECT contractId FROM ctrBids WHERE bidderID = " + std::to_string(me) + " AND refunded = 0))";
    std::string now = std::to_string(GetFileTimeNow());

    std::string where = " WHERE ("
        // expired, rejected and overdue contracts of mine
        "(" + issuerMe + " AND status = 0 AND dateExpired < " + now + " AND contractType <> 2)"
        " OR (" + issuerMe + " AND status = 6)"
        " OR (" + issuerMe + " AND status = 1 AND (dateAccepted + numDays * " + std::to_string(EvE::Time::Day) + ") < " + now + ")"
        // couriers i'm carrying
        " OR (" + acceptorMe + " AND status = 1)"
        // finished auctions with something to claim (issuer: the isk, bidders: the items)
        " OR (contractType = 2 AND dateExpired < " + now + " AND ((" + issuerMe + " AND status IN (0, 3)) OR (" + myBids + " AND status IN (0, 2))))"
        // running auctions where i was outbid
        " OR (contractType = 2 AND status = 0 AND dateExpired >= " + now + " AND " + myOutbid + ")"
        ") AND contractId NOT IN (SELECT contractID FROM ctrDismissed WHERE ownerID = " + std::to_string(me) + ")";
    return where;
}

PyResult ContractProxy::SearchContracts(PyCallArgs &call) {
    Client* pClient(call.client);
    auto arg = [&](const char* k) -> PyRep* { return GetKeyword(call, k); };
    auto has = [&](const char* k) -> bool { PyRep* rep(GetKeyword(call, k)); return ((rep != nullptr) and !rep->IsNone()); };

    // We will not proceed, if contractType is not specified
    if (!has("contractType")) {
        codelog(SERVICE__ERROR, "%s: ContractType was not specified. Aborting search", GetName());
        return nullptr;
    }
    int contractType = IntArg(arg("contractType"));

    uint32 charID(pClient->GetCharacterID()), corpID(pClient->GetCorporationID()), allyID(pClient->GetAllianceID());
    std::string now = std::to_string(GetFileTimeNow());

    /**
     * We're using sort of query constructor here - if request have certain value specified, we add it as another AND block.
     *  item filters use the contract's item rows (requested items have no entity), only outstanding unexpired contracts
     */
    std::string from = " FROM ctrContracts cC";
    std::string where = " WHERE cC.status = 0 AND cC.dateExpired > " + now;
    // Type 10 is "All" and "Exclude WTB" (auctions and item exchanges)
    if (contractType == 10) {
        where += " AND cC.contractType IN (1,2)";
    } else {
        where += " AND cC.contractType = " + std::to_string(contractType);
    }

    // availability.  public contracts only in the client's region (ConWrongRegion), private ones assigned to me/my corp/alliance
    int availability = (has("availability") ? IntArg(arg("availability")) : 0);
    switch (availability) {
        case 1:  where += " AND cC.isPrivate <> 0 AND cC.assigneeID = " + std::to_string(charID); break;
        case 2:  where += " AND cC.isPrivate <> 0 AND cC.assigneeID = " + std::to_string(corpID); break;
        case 3:  where += " AND cC.isPrivate <> 0 AND cC.assigneeID = " + std::to_string(allyID ? allyID : (uint32)-1); break;
        default: where += " AND cC.isPrivate = 0 AND cC.startRegionID = " + std::to_string(pClient->GetRegionID()); break;
    }

    // item filters (traded items only)
    std::string itemFilter;
    std::vector<int64> types;
    IntList(arg("itemTypes"), types);
    if (!types.empty()) {
        std::string list;
        for (auto cur : types)
            list += std::to_string(cur) + ",";
        list.pop_back();
        itemFilter += " AND cI.itemTypeID IN (" + list + ")";
    } else if (has("itemTypeName")) {
        std::string name, escaped;
        name = PyRep::StringContent(arg("itemTypeName"));
        // "group:name|meta" forms are resolved by the client into itemTypes
        size_t pos = name.find_first_of(":|");
        if (pos == std::string::npos) {
            sDatabase.DoEscapeString(escaped, name);
            itemFilter += " AND iT.typeName LIKE '%" + escaped + "%'";
        }
    }
    if (has("itemGroupID"))
        itemFilter += " AND iT.groupID = " + std::to_string(IntArg(arg("itemGroupID")));
    if (has("itemCategoryID"))
        itemFilter += " AND iG.categoryID = " + std::to_string(IntArg(arg("itemCategoryID")));
    if (has("searchHint")) {
        int64 hint(IntArg(arg("searchHint")));
        if (hint == 1) {        // SEARCHHINT_BPO
            itemFilter += " AND cI.isCopy = 0";
        } else if (hint == 2) { // SEARCHHINT_BPC
            itemFilter += " AND cI.isCopy = 1";
        }
    }
    if (!itemFilter.empty())
        where += " AND cC.contractId IN (SELECT cI.contractId FROM ctrItems cI"
                 " JOIN invTypes iT ON iT.typeID = cI.itemTypeID JOIN invGroups iG ON iG.groupID = iT.groupID"
                 " WHERE cI.inCrate = 1" + itemFilter + ")";

    if (has("minPrice"))
        where += " AND cC.price >= " + std::to_string(ContractUtils::ReadISK(arg("minPrice")));
    if (has("maxPrice"))
        where += " AND cC.price <= " + std::to_string(ContractUtils::ReadISK(arg("maxPrice")));
    if (has("minReward"))
        where += " AND cC.reward >= " + std::to_string(ContractUtils::ReadISK(arg("minReward")));
    if (has("maxReward"))
        where += " AND cC.reward <= " + std::to_string(ContractUtils::ReadISK(arg("maxReward")));
    if (has("minCollateral"))
        where += " AND cC.collateral >= " + std::to_string(ContractUtils::ReadISK(arg("minCollateral")));
    if (has("maxCollateral"))
        where += " AND cC.collateral <= " + std::to_string(ContractUtils::ReadISK(arg("maxCollateral")));
    if (has("minVolume"))
        where += " AND cC.volume >= " + std::to_string(IntArg(arg("minVolume")));
    if (has("maxVolume"))
        where += " AND cC.volume <= " + std::to_string(IntArg(arg("maxVolume")));
    // contracts that also ask for items
    if (IsTrue(arg("excludeTrade")))
        where += " AND cC.contractId NOT IN (SELECT contractId FROM ctrItems WHERE inCrate = 0)";
    // more than one item
    if (IsTrue(arg("excludeMultiple")))
        where += " AND (SELECT COUNT(*) FROM ctrItems x WHERE x.contractId = cC.contractId AND x.inCrate = 1) = 1";
    // auctions without a buyout
    if (IsTrue(arg("excludeNoBuyout")))
        where += " AND (cC.contractType <> 2 OR cC.collateral > 0)";

    // security classes of the start system (securityClassZeroSec 0, LowSec 1, HighSec 2)
    std::vector<int64> secClasses;
    IntList(arg("securityClasses"), secClasses);
    if (has("securityClasses")) {
        std::string sec;
        for (auto cur : secClasses) {
            if (!sec.empty())
                sec += " OR ";
            switch (cur) {
                case 0: sec += "ms.security <= 0.0"; break;
                case 1: sec += "(ms.security > 0.0 AND ms.security < 0.45)"; break;
                case 2: sec += "ms.security >= 0.45"; break;
            }
        }
        if (sec.empty())
            sec = "0";
        from += " JOIN mapSolarSystems ms ON ms.solarSystemID = cC.startSolarSystemID";
        where += " AND (" + sec + ")";
    }

    // locationID can be a system, constellation or region
    if (has("locationID")) {
        int64 locationId = IntArg(arg("locationID"));
        if (IsSolarSystemID(locationId)) {
            where += " AND cC.startSolarSystemID = " + std::to_string(locationId);
        } else if (IsConstellationID(locationId)) {
            where += " AND cC.startSolarSystemID IN (SELECT solarSystemID FROM mapSolarSystems WHERE constellationID = " + std::to_string(locationId) + ")";
        } else if (IsRegionID(locationId)) {
            where += " AND cC.startRegionID = " + std::to_string(locationId);
        }
    }
    if (has("endLocationID")) {
        int64 locationId = IntArg(arg("endLocationID"));
        if (IsSolarSystemID(locationId)) {
            where += " AND cC.endSolarSystemID = " + std::to_string(locationId);
        } else if (IsConstellationID(locationId)) {
            where += " AND cC.endSolarSystemID IN (SELECT solarSystemID FROM mapSolarSystems WHERE constellationID = " + std::to_string(locationId) + ")";
        } else if (IsRegionID(locationId)) {
            where += " AND cC.endRegionID = " + std::to_string(locationId);
        }
    }
    // the issuer can be either a character or a corporation
    if (has("issuerID")) {
        int64 issuerId = IntArg(arg("issuerID"));
        if (IsCorp(issuerId)) {
            where += " AND cC.issuerCorpID = " + std::to_string(issuerId) + " AND cC.forCorp = 1";
        } else {
            where += " AND cC.issuerID = " + std::to_string(issuerId) + " AND cC.forCorp = 0";
        }
    }

    // contractscommon SORT_*.  sortDir 1 = descending
    std::string order;
    switch (IntArg(arg("sortBy"))) {
        case 1:  order = "cC.price"; break;
        case 2:  order = "cC.dateExpired"; break;
        case 3:  order = "cC.startStationID"; break;
        case 4:  order = "cC.startSolarSystemID"; break;
        case 5:  order = "cC.startRegionID"; break;
        case 7:  order = "cC.contractType"; break;
        case 8:  order = "cC.reward"; break;
        case 9:  order = "cC.collateral"; break;
        case 10: order = "cC.volume"; break;
        case 11: order = "cC.assigneeID"; break;
        default: order = "cC.contractId"; break;
    }
    order += (IntArg(arg("sortDir")) == 1) ? " DESC" : " ASC";
    int64 startNum(std::max((int64)0, IntArg(arg("startNum"))));

    // matches (MAX_CONTRACTS_PER_SEARCH 1000)
    uint32 numFound(0);
    DBQueryResult res;
    DBResultRow row;
    // the type name filter holds '%' (LIKE), so the built queries are passed as an argument, not as the format
    if (sDatabase.RunQuery(res, "%s", ("SELECT COUNT(*)" + from + where).c_str()) and res.GetRow(row))
        numFound = std::min(row.GetUInt(0), (uint32)1000);

    std::vector<int> contractIDs;
    if (!sDatabase.RunQuery(res, "%s", ("SELECT cC.contractId" + from + where + " ORDER BY " + order + " LIMIT " + std::to_string(startNum) + ", 100").c_str())) {
        codelog(DATABASE__ERROR, "Error in query: %s", res.error.c_str());
        return nullptr;
    }
    while (res.GetRow(row))
        contractIDs.push_back(row.GetInt(0));

    PyDict* response = new PyDict;
    PyList* contracts = (contractIDs.empty() ? nullptr : ContractUtils::GetContractEntries(contractIDs));
    response->SetItemString("contracts", contracts ? contracts : new PyList);
    response->SetItemString("numFound", new PyInt(numFound));
    response->SetItemString("searchTime", new PyInt(153));  // Since search time is of no relevance to the client, we simply hard-code it
    response->SetItemString("maxResults", new PyInt(1000));

    return new PyObject("util.KeyVal", response);
}

PyResult ContractProxy::CreateContract(PyCallArgs &call,
    PyInt* contractType, PyRep* isPrivate, std::optional<PyRep*> assigneeID, PyInt* expireTime, PyInt* duration, PyInt* startStationID, std::optional<PyRep*> endStationID,
    PyRep* price, PyRep* reward, PyRep* collateral, PyRep* title, PyRep* description) {
    Client* pClient(call.client);
    int startStationDivision(flagHangar), startSystemId(0), startRegionId(0), endSystemId(0), endRegionId(0);
    uint32 charID(pClient->GetCharacterID()), corpID(pClient->GetCorporationID());
    int32 type(contractType->value());

    /**
     * Since named args (byname) aren't included in packet, we process them separately.
     */
    PyRep* rep = GetKeyword(call, "flag");
    if ((rep != nullptr) and rep->IsInt())
        startStationDivision = rep->AsInt()->value();
    bool forCorp(IsTrue(GetKeyword(call, "forCorp")));

    if (type == Contract::Type::Loan)
        throw CustomError("Loan contracts are not available.");
    if ((type != Contract::Type::ItemExchange)
    and (type != Contract::Type::Auction)
    and (type != Contract::Type::Courier))
        throw CustomError("Invalid contract type.");

    int64 priceISK(ContractUtils::ReadISK(price)), rewardISK(ContractUtils::ReadISK(reward)), collateralISK(ContractUtils::ReadISK(collateral));
    if ((priceISK < 0) or (rewardISK < 0) or (collateralISK < 0))
        throw CustomError("Contract prices, rewards and collateral must be between 0 and 1,000,000,000,000 ISK.");
    // contractscommon.EXPIRE_TIMES
    switch (expireTime->value()) {
        case 1440: case 4320: case 10080: case 20160: break;
        default: throw CustomError("Invalid contract expiration time.");
    }
    if (duration->value() < 0)
        throw CustomError("Invalid contract duration.");

    bool privateContract(IsTrue(isPrivate));
    uint32 assignee(assigneeID.has_value() ? (uint32)IntArg(assigneeID.value()) : 0);
    if (!privateContract)
        assignee = 0;
    if (privateContract) {
        if (assignee == 0)
            throw CustomError("A private contract needs an assignee.");
        // npc characters/corps can't take contracts
        if (!IsCharacterID(assignee) and !IsPlayerCorp(assignee) and !IsAlliance(assignee))
            throw UserError("ConNPCNotAllowed");
    }

    if (sDataMgr.IsStation(startStationID->value())) {
        startSystemId = sDataMgr.GetStationSystem(startStationID->value());
        startRegionId = sDataMgr.GetStationRegion(startStationID->value());
    } else {
        codelog(SERVICE__ERROR, "Specified start Station ID has no Station counterparts in DB");
        throw CustomError("Invalid start station.");
    }
    uint32 startStation(startStationID->value());
    uint32 endStation(0);
    if (endStationID.has_value() and (IntArg(endStationID.value()) != 0)) {
        endStation = IntArg(endStationID.value());
        if (sDataMgr.IsStation(endStation)) {
            endSystemId = sDataMgr.GetStationSystem(endStation);
            endRegionId = sDataMgr.GetStationRegion(endStation);
        } else {
            codelog(SERVICE__ERROR, "Specified end Station ID has no Station counterparts in DB");
            throw CustomError("Invalid destination station.");
        }
    } else {
        endSystemId = startSystemId;
        endRegionId = 0;
    }

    switch (type) {
        case Contract::Type::Courier: {
            if (endStation == 0)
                throw CustomError("A courier contract needs a destination station.");
            if (endStation == startStation)
                throw UserError("ConDestinationSame");
            if (duration->value() < 1)
                throw UserError("ConDurationZero");
        } break;
        case Contract::Type::Auction: {
            // private auctions only for the own corp or alliance (client rule)
            if (privateContract and (assignee != corpID) and ((pClient->GetAllianceID() == 0) or (assignee != (uint32)pClient->GetAllianceID())))
                throw CustomError("Private auctions can only be made available to your corporation or alliance.");
            // collateral is the buyout price
            if ((collateralISK > 0) and (collateralISK < priceISK))
                throw CustomError("The buyout price cannot be lower than the starting bid.");
            if (priceISK < 1)
                throw CustomError("An auction needs a starting bid.");
        } break;
    }

    // corp contracts:  contract managers, items from an office hangar division of the start station, the session's wallet division
    uint32 officeID(0);
    uint16 walletKey(Account::KeyType::Cash);
    uint32 walletID(charID);
    if (forCorp) {
        CheckContractManager(pClient);
        if (!IsHangarDivision(startStationDivision))
            throw CustomError("Invalid corporation hangar division.");
        walletKey = GetCorpWalletKey(pClient);
        walletID = corpID;
    } else {
        startStationDivision = flagHangar;
    }

    // contract limits by Contracting / Corporation Contracting.  contracts inside the own corp have their own limit
    OutstandingCounts counts = OutstandingCounts();
    CountOutstanding(pClient, counts);
    bool inner(privateContract and IsInnerCorp(pClient, assignee));
    uint8 skillLevel(pClient->GetChar()->GetSkillLevel(forCorp ? EvESkill::CorporationContracting : EvESkill::Contracting));
    int32 maxContracts(inner ? s_maxNumContracts : (forCorp ? s_numContractsBySkillCorp[std::min(skillLevel, (uint8)5)] : s_numContractsBySkill[std::min(skillLevel, (uint8)5)]));
    uint32 numContracts(forCorp ? (inner ? counts.myCorpTotal : counts.nonCorpForMyCorp) : (inner ? counts.myCharTotal : counts.nonCorpForMyChar));
    if (numContracts >= (uint32)maxContracts)
        throw UserError(inner ? "ConTooManyContractsMax" : "ConTooManyContracts").AddFormatValue("max", new PyInt(maxContracts));

    /**
     * Validate every item before anything is created.  traded items must be the issuer's own (or the corp's),
     *  in the item hangar (corp: the chosen office division) of the start station, and not the active ship.
     */
    struct TradedItem {
        InventoryItemRef iRef;
        int32 quantity;
    };
    std::vector<TradedItem> tradedItems;
    std::set<uint32> seenIDs;
    rep = GetKeyword(call, "itemList");
    if ((rep != nullptr) and rep->IsList()) {
        PyList* list = rep->AsList();
        if (list->size() > (size_t)s_maxNumItems)
            throw UserError("ConTooManyItems").AddFormatValue("num", new PyInt((int32)list->size())).AddFormatValue("max", new PyInt(s_maxNumItems));
        if (!list->empty()) {
            if (forCorp) {
                officeID = GetCorpOffice(pClient, startStation);
                GetOfficeInventory(officeID);
                int64 takeRole(Corp::HangarTakeRole(startStationDivision));
                if ((takeRole == 0) or ((pClient->GetCorpRolesAtLocation(startStation) & takeRole) == 0))
                    throw UserError("ConNoAccessToCorpHangar");
            } else {
                // make sure this char's hangar in the start station is loaded
                pClient->GetStationHangar(startStation);
            }
        }
        for (size_t index = 0; index < list->size(); ++index) {
            PyRep* entry = list->GetItem(index);
            PyRep* idRep(nullptr);
            PyRep* qtyRep(nullptr);
            if (entry->IsList() and (entry->AsList()->size() >= 2)) {
                idRep = entry->AsList()->GetItem(0);
                qtyRep = entry->AsList()->GetItem(1);
            } else if (entry->IsTuple() and (entry->AsTuple()->size() >= 2)) {
                idRep = entry->AsTuple()->GetItem(0);
                qtyRep = entry->AsTuple()->GetItem(1);
            }
            if ((idRep == nullptr) or (qtyRep == nullptr) or !idRep->IsInt() or !qtyRep->IsInt())
                throw CustomError("Invalid item list.");

            uint32 itemID(idRep->AsInt()->value());
            int32 qty(qtyRep->AsInt()->value());
            if (!seenIDs.insert(itemID).second)
                throw CustomError("The same item cannot be added to a contract twice.");

            InventoryItemRef iRef = sItemFactory.GetItemRef(itemID);
            if (iRef.get() == nullptr)
                throw UserError("ConCannotTradeItemSanity");
            if (itemID == pClient->GetShipID())
                throw UserError("ConCannotTradeCurrentShip");
            if (iRef->groupID() == EVEDB::invGroups::Capsule)
                throw UserError("ConIllegalType");
            if (iRef->ownerID() != walletID) {
                _log(SERVICE__WARNING, "CreateContract - %s(%u) tried to contract %s(%u) owned by %u.", \
                        pClient->GetName(), charID, iRef->name(), itemID, iRef->ownerID());
                throw CustomError("You do not own %s.", iRef->name());
            }
            if (forCorp) {
                if ((iRef->locationID() != officeID) or (iRef->flag() != startStationDivision))
                    throw UserError("ConCannotTradeItemNotInStation").AddFormatValue("example", new PyString(iRef->itemName())).AddLocationName("station", startStation);
            } else if ((iRef->locationID() != startStation) or (iRef->flag() != flagHangar)) {
                throw UserError("ConCannotTradeItemNotInStation").AddFormatValue("example", new PyString(iRef->itemName())).AddLocationName("station", startStation);
            }
            if ((qty < 1) or (qty > iRef->quantity()))
                throw CustomError("Invalid quantity of %s.", iRef->name());
            if (iRef->isSingleton() and (qty != iRef->quantity()))
                throw CustomError("Invalid quantity of %s.", iRef->name());

            tradedItems.push_back({iRef, qty});
        }
    }
    if (type == Contract::Type::Courier) {
        // the package volume (assembled ships and containers with their own volume)
        double volume(0.0);
        for (auto& cur : tradedItems)
            volume += ((cur.iRef->typeID() == itemPlasticWrap) ? cur.iRef->GetAttribute(AttrVolume).get_float() : cur.iRef->GetPackagedVolume()) * cur.quantity;
        if (volume > s_courierMaxVolume)
            throw UserError("ConExceedsMaxVolume").AddFormatValue("max", new PyFloat(s_courierMaxVolume)).AddFormatValue("vol", new PyFloat(volume));
    }

    std::map<int32, int32> requestedItems;
    rep = GetKeyword(call, "requestItemTypeList");
    if ((rep != nullptr) and rep->IsList()) {
        PyList* list = rep->AsList();
        for (size_t index = 0; index < list->size(); ++index) {
            PyRep* entry = list->GetItem(index);
            PyRep* typeRep(nullptr);
            PyRep* qtyRep(nullptr);
            if (entry->IsList() and (entry->AsList()->size() >= 2)) {
                typeRep = entry->AsList()->GetItem(0);
                qtyRep = entry->AsList()->GetItem(1);
            } else if (entry->IsTuple() and (entry->AsTuple()->size() >= 2)) {
                typeRep = entry->AsTuple()->GetItem(0);
                qtyRep = entry->AsTuple()->GetItem(1);
            }
            if ((typeRep == nullptr) or (qtyRep == nullptr) or !typeRep->IsInt() or !qtyRep->IsInt())
                throw CustomError("Invalid requested item list.");
            if ((qtyRep->AsInt()->value() < 1) or (sItemFactory.GetType(typeRep->AsInt()->value()) == nullptr))
                throw CustomError("Invalid requested item.");
            requestedItems[typeRep->AsInt()->value()] += qtyRep->AsInt()->value();
        }
    }
    if (!requestedItems.empty() and (type != Contract::Type::ItemExchange))
        throw CustomError("Only item exchange contracts can ask for items.");

    if (tradedItems.empty() and (type != Contract::Type::ItemExchange))
        throw UserError("ConNoItems");
    if (tradedItems.empty() and requestedItems.empty() and (rewardISK == 0) and (priceISK == 0)) {
        codelog(SERVICE__ERROR, "No traded or requested items was specified. Aborting");
        throw UserError("ConNoItems");
    }

    // broker's fee and deposit (contractscommon.CalcContractFees)
    ContractFees fees = ContractFees();
    if (sConfig.market.ContractFees)
        fees = ContractUtils::CalcFees(priceISK, rewardISK, type, expireTime->value(),
                                       pClient->GetChar()->GetSkillLevel(EvESkill::BrokerRelations),
                                       pClient->GetChar()->GetSkillLevel(EvESkill::Accounting), privateContract);
    // everything is paid after the row exists (the journal references the contractID).  check the wallet first
    MarketMgr::CheckFunds(walletID, walletKey, (double)(rewardISK + fees.brokersFee + fees.deposit));

    std::string titleStr, descStr, rawTitle(PyRep::StringContent(title)), rawDesc(PyRep::StringContent(description));
    if (rawTitle.size() > s_maxTitleLength)
        rawTitle.resize(s_maxTitleLength);
    if (rawDesc.size() > s_maxDescLength)
        rawDesc.resize(s_maxDescLength);
    sDatabase.DoEscapeString(titleStr, rawTitle);
    sDatabase.DoEscapeString(descStr, rawDesc);

    /**
     * Then, we go for actual entries creation.
     * Contract entry
     */
    uint32 contractId = 0;
    DBerror err;
    if (!sDatabase.RunQueryLID(err, contractId,
        "INSERT INTO ctrContracts "
        "(contractType, issuerID, issuerCorpID, forCorp, isPrivate, assigneeID, "
        "dateIssued, dateExpired, expireTimeInMinutes, duration, numDays, "
        "startStationID, startSolarSystemID, startRegionID, endStationID, endSolarSystemID, endRegionID, "
        "price, reward, collateral, title, description, issuerAllianceID, startStationDivision, rewardEscrowed, "
        "issuerWalletKey, brokersFee, deposit) "
        "VALUES "
        "(%u, %u, %u, %u, %u, %u, "
        "%lli, %lli, %u, %u, %u, "
        "%u, %u, %u, %u, %u, %u,"
        "%lli, %lli, %lli, '%s', '%s', %u, %u, %u, "
        "%u, %lli, %lli)",
        type, charID, corpID, forCorp ? 1 : 0, privateContract ? 1 : 0, assignee,
        int64(GetFileTimeNow()), int64(GetRelativeFileTime(0, 0, expireTime->value())), expireTime->value(), duration->value(),
        (type == Contract::Type::Courier ? duration->value() : expireTime->value() / 1440),
        startStation, startSystemId, startRegionId, endStation, endSystemId, endRegionId,
        priceISK, rewardISK, collateralISK, titleStr.c_str(), descStr.c_str(), pClient->GetAllianceID(), startStationDivision,
        (rewardISK > 0 ? 1 : 0), walletKey, fees.brokersFee, fees.deposit))
    {
        codelog(DATABASE__ERROR, "Failed to insert new entity: %s", err.c_str());
        throw CustomError("Failed to create the contract.");
    }

    // the reward and deposit are held by the system from now on, so the issuer can't spend them before the contract ends.
    //  the broker's fee goes to the SCC and is not refunded
    int64 taken(0);
    try {
        if (rewardISK > 0) {
            AccountService::TransferFunds(walletID, ownerSystem, rewardISK, "Contract reward",
                    forCorp ? Journal::EntryType::ContractRewardAddedCorp : Journal::EntryType::ContractRewardAdded, contractId, walletKey, Account::KeyType::Cash, pClient);
            taken += rewardISK;
        }
        if (fees.deposit > 0) {
            AccountService::TransferFunds(walletID, ownerSystem, fees.deposit, "Contract deposit",
                    forCorp ? Journal::EntryType::ContractDepositCorp : Journal::EntryType::ContractDeposit, contractId, walletKey, Account::KeyType::Cash, pClient);
            taken += fees.deposit;
        }
        if (fees.brokersFee > 0)
            AccountService::TransferFunds(walletID, corpSCC, fees.brokersFee, "Contract broker's fee",
                    forCorp ? Journal::EntryType::ContractBrokersFeeCorp : Journal::EntryType::ContractBrokersFee, contractId, walletKey, Account::KeyType::Cash, pClient);
    } catch (...) {
        if (taken > 0)
            AccountService::TransferFunds(ownerSystem, walletID, taken, "Contract creation failed", Journal::EntryType::ContractRewardRefund, contractId, Account::KeyType::Cash, walletKey);
        sDatabase.RunQuery(err, "DELETE FROM ctrContracts WHERE contractId = %u", contractId);
        throw;
    }

    /**
     * Then, we insert the items.  the contracted part of a stack is split off, and the items are held by the system
     *  in the start station (corp items leave the office, which would list them for everyone)
     */
    std::string itemsToInsert;
    float totalVolume = 0.00;
    for (auto& cur : tradedItems) {
        InventoryItemRef iRef(cur.iRef);
        if (cur.quantity < iRef->quantity()) {
            iRef = iRef->Split(cur.quantity, true);
            if (iRef.get() == nullptr) {
                _log(SERVICE__ERROR, "CreateContract - Failed to split %i from %s(%u).", cur.quantity, cur.iRef->name(), cur.iRef->itemID());
                continue;
            }
        }

        int pLevel(0), mLevel(0), isCopy(0), runs(0);
        if (iRef->categoryID() == EVEDB::invCategories::Blueprint) {
            BlueprintRef bRef = BlueprintRef::StaticCast(iRef);
            pLevel = bRef->pLevel();
            mLevel = bRef->mLevel();
            isCopy = bRef->copy() ? 1 : 0;
            runs = bRef->runs();
        }

        itemsToInsert.append("(" + std::to_string(contractId) + ", " +
            std::to_string(iRef->itemID()) + ", " +
            std::to_string(iRef->quantity()) + ", " +
            std::to_string(iRef->typeID()) + ", " +
            "1, 0, " +
            std::to_string(pLevel) + ", " +
            std::to_string(mLevel) + ", " +
            std::to_string(isCopy) + "," +
            std::to_string(runs) + ", " +
            std::to_string(iRef->GetAttribute(AttrDamage).get_int()) + ", " +
            std::to_string(iRef->flag()) + "),");

        // We only calculate volume for courier-type contracts - the other types don't use this value.
        if (type == Contract::Type::Courier)
            totalVolume += ((iRef->typeID() == itemPlasticWrap) ? iRef->GetAttribute(AttrVolume).get_float() : iRef->GetPackagedVolume()) * iRef->quantity();

        // a ship entered into a contract loses its insurance (ConInsuredShip), except corp insurance on a contract that
        //  stays inside the corp/alliance (ConInsuredShipCorp)
        if ((iRef->categoryID() == EVEDB::invCategories::Ship) and iRef->isSingleton()) {
            int8 insurance(ShipDB::GetInsuranceKind(iRef->itemID()));
            if ((insurance == 0) or ((insurance == 1) and !privateContract))
                ShipDB::VoidInsurance(iRef->itemID());
        }

        // the item stays in the station but belongs to the contract (contents of ships/containers keep their owner until accepted)
        iRef->ChangeOwner(ownerSystem, true);
        if (forCorp)
            iRef->Move(startStation, flagHangar, true);
        iRef->SaveItem();
    }

    for (auto cur : requestedItems) {
        itemsToInsert.append("(" + std::to_string(contractId) + ", " +
                             "0, " +
                             std::to_string(cur.second) + ", " +
                             std::to_string(cur.first) + ", " +
                             "0, 0, 0, 0, 0, 0, 0, 0),");
    }

    if (!itemsToInsert.empty()) {
        itemsToInsert.pop_back();
        std::string query = "INSERT INTO ctrItems (contractId, itemID, quantity, itemTypeID, inCrate, parentID, "
                            "productivityLevel, materialLevel, isCopy, licensedProductionRunsRemaining, damage, flagID) "
                            "VALUES " + itemsToInsert;
        uint32 last_insert;
        if (!sDatabase.RunQueryLID(err, last_insert, query.c_str()))
            codelog(DATABASE__ERROR, "Failed to insert contract items: %s", err.c_str());

        // We only insert volume for courier-type contracts - the other types don't use this value.
        if (type == Contract::Type::Courier) {
            if (!sDatabase.RunQuery(err, "UPDATE ctrContracts SET volume = %f WHERE contractId = %u", totalVolume, contractId))
                codelog(DATABASE__ERROR, "Failed to update contract volume: %s", err.c_str());
        }
    }

    if (privateContract)
        ContractUtils::Notify(assignee, "OnContractAssigned", contractId);

    return new PyInt((int) contractId);
}

PyResult ContractProxy::DeleteContract(PyCallArgs &call, PyInt* contractID) {
    Client* pClient(call.client);
    ContractInfo c = ContractInfo();
    if (!ContractUtils::GetContractInfo(contractID->value(), c))
        throw UserError("ConContractNotFound");

    // only the issuer (a contract manager of the issuing corp) can delete, and only before anyone accepted it
    //  (expired contracts are still outstanding)
    if (c.forCorp) {
        if (c.issuerCorpID != (uint32)pClient->GetCorporationID())
            throw UserError("ConNotYourContract");
        CheckContractManager(pClient);
    } else if (c.issuerID != pClient->GetCharacterID()) {
        throw UserError("ConNotYourContract");
    }
    if ((c.status != Contract::Status::Outstanding) and (c.status != Contract::Status::Rejected))
        throw UserError("ConContractNotOutstanding");
    if ((c.type == Contract::Type::Auction) and ContractUtils::HasBids(c.contractID))
        throw UserError("ConAuctionHasBids");

    // mark it first, so a second call can't return the items or isk again
    if (!ContractUtils::SetStatus(c.contractID, Contract::Status::Deleted))
        return new PyBool(false);

    // items and reward back, the deposit too.  the broker's fee is lost (ConConfirmDeleteContract)
    ContractUtils::ReturnItemsToIssuer(c);
    ContractUtils::RefundReward(c);
    ContractUtils::RefundDeposit(c, false, 0);

    return new PyBool(true);
}

PyResult ContractProxy::GetContract(PyCallArgs &call, PyInt* contractID) {
    ContractInfo c = ContractInfo();
    if (!ContractUtils::GetContractInfo(contractID->value(), c) or !ContractUtils::CanView(call.client, c))
        throw UserError("ConContractNotFound");
    return ContractUtils::GetContractEntry(contractID->value());
}

PyResult ContractProxy::AcceptContract(PyCallArgs &call, PyInt* contractID, std::optional<PyRep*> forCorp) {
    Client* pClient(call.client);
    bool corpAccept(IsTrue(forCorp));

    ContractInfo c = ContractInfo();
    if (!ContractUtils::GetContractInfo(contractID->value(), c) or !ContractUtils::CanView(pClient, c))
        throw UserError("ConContractNotFound");

    uint32 charID(pClient->GetCharacterID()), corpID(pClient->GetCorporationID());
    int64 timestamp = int64(GetFileTimeNow());

    // the acceptor's wallet:  the character, or the corp's session division (contract managers)
    uint32 acceptorID(charID);
    uint16 acceptorKey(Account::KeyType::Cash);
    if (corpAccept) {
        CheckContractManager(pClient);
        acceptorID = corpID;
        acceptorKey = GetCorpWalletKey(pClient);
    }

    // We can only accept outstanding contracts
    if (c.status != Contract::Status::Outstanding)
        throw UserError("ConContractNotOutstanding");
    if (c.dateExpired <= timestamp)
        throw UserError("ConContractExpired");
    if (c.type == Contract::Type::Auction)
        throw CustomError("Auctions are won by bidding on them.");
    // you can't accept your own contracts, and your corp can't accept your corp's
    if ((!corpAccept and (c.issuerID == charID)) or (corpAccept and c.forCorp and (c.issuerCorpID == corpID)))
        throw UserError("ConContractSameIssuerAndAcceptor");
    if (c.isPrivate
    and (c.assigneeID != charID)
    and (c.assigneeID != corpID)
    and ((pClient->GetAllianceID() == 0) or (c.assigneeID != (uint32)pClient->GetAllianceID())))
        throw UserError("ConNotYourContract");

    // the traded items must still be held by the contract in the start station
    std::vector<int> itemIDs;
    ContractUtils::GetContractItemIDs(c.contractID, &itemIDs);
    std::vector<InventoryItemRef> tradedItems;
    for (auto itemID : itemIDs) {
        InventoryItemRef iRef = sItemFactory.GetItemRef(itemID);
        if ((iRef.get() == nullptr) or (iRef->ownerID() != ownerSystem) or (iRef->locationID() != c.startStationID)) {
            _log(SERVICE__WARNING, "AcceptContract - item %i of contract %i is no longer held by the contract.", itemID, c.contractID);
            throw CustomError("This contract is no longer valid.");
        }
        tradedItems.push_back(iRef);
    }

    // where the acceptor gets things in the start station (corp:  the office's deliveries hangar)
    uint32 deliverOwnerID(acceptorID), deliverLocationID(c.startStationID);
    EVEItemFlags deliverFlag(flagHangar);
    MarketMgr::GetDeliveryLocation(acceptorID, corpAccept, charID, c.startStationID, deliverOwnerID, deliverLocationID, deliverFlag);

    switch (c.type) {
        case Contract::Type::ItemExchange: {
            // requested items must be the acceptor's own, unassembled, in the item hangar of the start station
            //  (corp:  any office division of the start station the member can take from)
            std::map<int, int> requestedItems;
            ContractUtils::GetRequestedItems(c.contractID, &requestedItems);
            Inventory* pHangar(nullptr);
            std::vector<EVEItemFlags> flags;
            if (!requestedItems.empty()) {
                if (corpAccept) {
                    uint32 officeID(GetCorpOffice(pClient, c.startStationID));
                    pHangar = GetOfficeInventory(officeID);
                    int64 roles(pClient->GetCorpRolesAtLocation(c.startStationID));
                    static const EVEItemFlags divisions[7] = {flagHangar, flagCorpHangar2, flagCorpHangar3, flagCorpHangar4, flagCorpHangar5, flagCorpHangar6, flagCorpHangar7};
                    for (auto cur : divisions)
                        if ((roles & Corp::HangarTakeRole(cur)) != 0)
                            flags.push_back(cur);
                } else {
                    pHangar = pClient->GetStationHangar(c.startStationID);
                    flags.push_back(flagHangar);
                }
                if (pHangar == nullptr)
                    throw CustomError("This contract is no longer valid.");
                for (const auto& entry : requestedItems) {
                    uint32 have(0);
                    for (auto flag : flags)
                        have += pHangar->GetTypeQtyForOwner(entry.first, flag, acceptorID);
                    if (have < (uint32)entry.second)
                        throw CustomError("You do not have the required items in %s in %s.", corpAccept ? "your corporation's hangars" : "your hangar", stDataMgr.GetStationName(c.startStationID).c_str());
                }
            }

            // legacy rows (reward not escrowed): the issuer pays the reward now
            if ((c.reward > 0) and !c.rewardEscrowed) {
                if (MarketMgr::GetWalletBalance(ContractUtils::IssuerWallet(c), ContractUtils::IssuerKey(c)) < c.reward)
                    throw CustomError("The issuer of this contract does not have enough ISK to pay the reward.");
            }

            // this throws if the acceptor is short; nothing has changed yet
            if (c.price > 0)
                AccountService::TransferFunds(acceptorID, ContractUtils::IssuerWallet(c), c.price, "Payment for accepted contract",
                        corpAccept ? Journal::EntryType::ContractPriceCorp : Journal::EntryType::ContractPrice, c.contractID,
                        acceptorKey, ContractUtils::IssuerKey(c), pClient);

            // mark it accepted before anything else moves, so a second call fails the status check
            ContractUtils::SetStatus(c.contractID, Contract::Status::Finished, timestamp, timestamp, acceptorID);
            DBerror err;
            sDatabase.RunQuery(err, "UPDATE ctrContracts SET acceptorWalletKey = %u WHERE contractId = %i", acceptorKey, c.contractID);

            if (c.reward > 0) {
                if (c.rewardEscrowed) {
                    AccountService::TransferFunds(ownerSystem, acceptorID, c.reward, "Contract reward", Journal::EntryType::ContractReward, c.contractID, Account::KeyType::Cash, acceptorKey);
                } else {
                    AccountService::TransferFunds(ContractUtils::IssuerWallet(c), acceptorID, c.reward, "Contract reward", Journal::EntryType::ContractReward, c.contractID, ContractUtils::IssuerKey(c), acceptorKey);
                }
            }

            // requested items go to the issuer in the start station (corp:  its office's deliveries hangar)
            uint32 issuerOwnerID(c.issuerID), issuerLocationID(c.startStationID);
            EVEItemFlags issuerFlag(flagHangar);
            MarketMgr::GetDeliveryLocation(ContractUtils::IssuerWallet(c), c.forCorp, c.issuerID, c.startStationID, issuerOwnerID, issuerLocationID, issuerFlag);
            for (const auto& entry : requestedItems) {
                int32 left(entry.second);
                for (auto flag : flags) {
                    if (left < 1)
                        break;
                    uint32 here(pHangar->GetTypeQtyForOwner(entry.first, flag, acceptorID));
                    if (here == 0)
                        continue;
                    int32 take(std::min(left, (int32)here));
                    std::vector<InventoryItemRef> taken;
                    if (!pHangar->TakeTypeQtyForOwner(entry.first, flag, acceptorID, take, taken))
                        _log(SERVICE__ERROR, "AcceptContract - failed to take %i of type %i from %u for contract %i.", take, entry.first, acceptorID, c.contractID);
                    for (auto cur : taken) {
                        ContractUtils::ChangeOwnerRecursive(cur, issuerOwnerID);
                        cur->Move(issuerLocationID, issuerFlag, true);
                    }
                    left -= take;
                }
            }

            // traded items go to the acceptor
            for (auto cur : tradedItems) {
                ContractUtils::ChangeOwnerRecursive(cur, deliverOwnerID);
                if ((cur->locationID() != deliverLocationID) or (cur->flag() != deliverFlag))
                    cur->Move(deliverLocationID, deliverFlag, true);
            }

            // deposit back, sales tax on the contract's value
            ContractUtils::RefundDeposit(c, true, std::max(c.price, c.reward));
            ContractUtils::Notify(ContractUtils::IssuerWallet(c), "OnContractCompleted", c.contractID);
        } break;
        case Contract::Type::Courier: {
            // collateral is held by the system until the contract ends.  this throws if the acceptor is short
            if (c.collateral > 0)
                AccountService::TransferFunds(acceptorID, ownerSystem, c.collateral, "Contract collateral",
                        corpAccept ? Journal::EntryType::ContractCollateralCorp : Journal::EntryType::ContractCollateral, c.contractID,
                        acceptorKey, Account::KeyType::Cash, pClient);

            /**
             * Courier contract acceptance includes following steps:
             * - Create a plastic wrap container
             * - Set capacity and volume attribute to the volume of the contracted items
             * - Move all items inside this container (they stay owned by the system until delivered)
             * - Give that container to the character (or the corp's deliveries hangar)
             * - Update contract entry - move it to In Progress and leave a time-stamp when it was accepted.
             */
            std::string containerName = sDataMgr.GetSystemName(c.startSolarSystemID);
            containerName += " -> ";
            containerName += sDataMgr.GetSystemName(c.endSolarSystemID);
            containerName += " (" + std::to_string(c.volume) + "m3)";

            ItemData itemData(itemPlasticWrap, deliverOwnerID, locTemp, flagNone);
            itemData.name = containerName;
            InventoryItemRef plasticWrap = sItemFactory.SpawnItem(itemData);
            if (plasticWrap.get() == nullptr) {
                if (c.collateral > 0)
                    AccountService::TransferFunds(ownerSystem, acceptorID, c.collateral, "Contract collateral refund", Journal::EntryType::ContractCollateralRefund, c.contractID, Account::KeyType::Cash, acceptorKey);
                throw CustomError("Unable to create the courier package.");
            }
            plasticWrap->SetAttribute(AttrVolume, c.volume);
            plasticWrap->SetAttribute(AttrCapacity, c.volume);
            plasticWrap->SaveItem();

            ContractUtils::SetStatus(c.contractID, Contract::Status::InProgress, timestamp, 0, acceptorID, plasticWrap->itemID());
            DBerror err;
            sDatabase.RunQuery(err, "UPDATE ctrContracts SET acceptorWalletKey = %u WHERE contractId = %i", acceptorKey, c.contractID);

            for (auto cur : tradedItems)
                cur->Move(plasticWrap->itemID(), flagNone, true);

            // And we give the container to the acceptor
            plasticWrap->Move(deliverLocationID, deliverFlag, true);

            ContractUtils::Notify(ContractUtils::IssuerWallet(c), "OnContractAccepted", c.contractID);
        } break;
        default:
            throw CustomError("This type of contract cannot be accepted.");
    }

    // DBResultToCRowset return doesn't work, for some reason (fails at unmarshalling on client's side), so making it a KeyVal dict
    PyDict* ret = new PyDict;
    ret->SetItemString("contractID", new PyInt(c.contractID));
    ret->SetItemString("type", new PyInt(c.type));
    ret->SetItemString("startStationID", new PyInt(c.startStationID));
    ret->SetItemString("endStationID", new PyInt(c.endStationID));
    ret->SetItemString("dateAccepted", new PyLong(timestamp));
    ret->SetItemString("numDays", new PyInt(c.numDays));
    return new PyObject("util.KeyVal", ret);
}


PyResult ContractProxy::CompleteContract(PyCallArgs &call, PyInt* contractID, PyInt* completionStatus) {
    Client* pClient(call.client);
    ContractInfo c = ContractInfo();
    if (!ContractUtils::GetContractInfo(contractID->value(), c))
        throw UserError("ConContractNotFound");

    uint32 charID(pClient->GetCharacterID()), corpID(pClient->GetCorporationID());
    int64 timestamp = int64(GetFileTimeNow());
    // the caller acts for the acceptor / issuer (corp:  contract managers)
    bool isAcceptor((c.acceptorID == charID)
                 or (IsPlayerCorp(c.acceptorID) and (c.acceptorID == corpID) and ((pClient->GetCorpRole() & (Corp::Role::ContractManager | Corp::Role::Director)) != 0)));
    bool isIssuer((!c.forCorp and (c.issuerID == charID))
               or (c.forCorp and (c.issuerCorpID == corpID) and ((pClient->GetCorpRole() & (Corp::Role::ContractManager | Corp::Role::Director)) != 0)));

    switch (completionStatus->value()) {
        case Contract::Status::Finished: {
            // acceptor delivering a courier package
            if ((c.type != Contract::Type::Courier) or (c.status != Contract::Status::InProgress))
                throw UserError("ConContractNotInProgress");
            if (!isAcceptor)
                throw UserError("ConNotYourContract");
            // First, we need to make sure that the container is indeed located in the end station
            if ((uint32)pClient->GetStationID() != c.endStationID)
                throw UserError("ConCrateNotFound").AddFormatValue("location", new PyString(stDataMgr.GetStationName(c.endStationID)));

            // the package:  the acceptor's hangar there (corp:  an office hangar there), or the active ship's hold
            InventoryItemRef crate = sItemFactory.GetItemRef(c.crateID);
            uint32 officeID(IsPlayerCorp(c.acceptorID) ? stDataMgr.GetOfficeIDForCorp(c.endStationID, c.acceptorID) : 0);
            if ((crate.get() == nullptr)
            or ((crate->ownerID() != c.acceptorID) and (crate->ownerID() != charID))
            or ((crate->locationID() != c.endStationID) and (crate->locationID() != pClient->GetShipID())
                and ((officeID == 0) or (crate->locationID() != officeID))))
                throw UserError("ConCrateNotFound").AddFormatValue("location", new PyString(stDataMgr.GetStationName(c.endStationID)));

            // Then, we validate the presence of all expected items
            std::map<int, int> expectedItems;
            ContractUtils::GetContractItemIDsAndQuantities(c.contractID, &expectedItems);
            for (const auto& entry : expectedItems) {
                InventoryItemRef item = sItemFactory.GetItemRef(entry.first);
                if ((item.get() == nullptr) or (item->quantity() != entry.second) or (item->locationID() != c.crateID))
                    throw UserError("ConCourierItemsMissing");
            }

            // finished before the crate goes, so deleting it doesn't fail the contract
            if (!ContractUtils::SetStatus(c.contractID, Contract::Status::Finished, 0, timestamp))
                return new PyBool(false);

            // Once checks have passed, we extract the items into the issuer's hangar in the destination
            //  (corp contracts:  the deliveries hangar of the corp's office there)
            uint32 itemOwnerID(c.issuerID), locationID(c.endStationID);
            EVEItemFlags flag(flagHangar);
            MarketMgr::GetDeliveryLocation(ContractUtils::IssuerWallet(c), c.forCorp, c.issuerID, c.endStationID, itemOwnerID, locationID, flag);
            for (const auto& entry : expectedItems) {
                InventoryItemRef item = sItemFactory.GetItemRef(entry.first);
                ContractUtils::ChangeOwnerRecursive(item, itemOwnerID);
                item->Move(locationID, flag, true);
            }
            crate = sItemFactory.GetItemRef(c.crateID);
            if (crate.get() != nullptr)
                crate->Delete();

            // Then, we pay the reward and return the collateral (both held by the system), and the issuer's deposit
            uint16 acceptorKey(ContractUtils::AcceptorKey(c));
            if (c.reward > 0)
                AccountService::TransferFunds(ownerSystem, c.acceptorID, c.reward, "Contract reward", Journal::EntryType::ContractReward, c.contractID, Account::KeyType::Cash, acceptorKey);
            if (c.collateral > 0)
                AccountService::TransferFunds(ownerSystem, c.acceptorID, c.collateral, "Contract collateral refund", Journal::EntryType::ContractCollateralRefund, c.contractID, Account::KeyType::Cash, acceptorKey);
            ContractUtils::RefundDeposit(c, false, 0);

            ContractUtils::Notify(ContractUtils::IssuerWallet(c), "OnContractCompleted", c.contractID);
        } break;
        case Contract::Status::Failed: {
            if ((c.type != Contract::Type::Courier) or (c.status != Contract::Status::InProgress))
                throw UserError("ConContractNotInProgress");
            // the acceptor can give up at any time; the issuer only when the acceptor is overdue
            bool overdue(timestamp > (c.dateAccepted + (int64)c.numDays * EvE::Time::Day));
            if (!isAcceptor and !(isIssuer and overdue))
                throw UserError("ConNotYourContract");

            ContractUtils::FailCourier(c);
        } break;
        case Contract::Status::Rejected: {
            // assignee of a private contract turning it down.  the items stay locked until the issuer deletes it
            if (c.status != Contract::Status::Outstanding)
                throw UserError("ConContractNotOutstanding");
            if (!c.isPrivate or ((c.assigneeID != charID) and (c.assigneeID != corpID)))
                throw UserError("ConNotYourContract");
            if (c.assigneeID == corpID)
                CheckContractManager(pClient);
            if (!ContractUtils::SetStatus(c.contractID, Contract::Status::Rejected))
                return new PyBool(false);
            ContractUtils::Notify(ContractUtils::IssuerWallet(c), "OnContractRejected", c.contractID);
        } break;
        default:
            // We do not expect anything else
            return new PyBool(false);
    }

    return new PyBool(true);
}

PyResult ContractProxy::PlaceBid(PyCallArgs& call, PyInt* contractID, PyRep* bid, std::optional<PyRep*> forCorp)
{
    Client* pClient(call.client);
    bool corpBid(IsTrue(forCorp));
    uint32 charID(pClient->GetCharacterID()), corpID(pClient->GetCorporationID());

    ContractInfo c = ContractInfo();
    if (!ContractUtils::GetContractInfo(contractID->value(), c) or !ContractUtils::CanView(pClient, c))
        throw UserError("ConContractNotFound");
    if (c.type != Contract::Type::Auction)
        throw CustomError("You can only bid on auctions.");
    if (c.status != Contract::Status::Outstanding)
        throw UserError("ConContractNotOutstanding");
    if (c.dateExpired <= GetFileTimeNow())
        throw UserError("ConContractExpired");

    uint32 bidderID(charID);
    uint16 bidderKey(Account::KeyType::Cash);
    if (corpBid) {
        CheckContractManager(pClient);
        bidderID = corpID;
        bidderKey = GetCorpWalletKey(pClient);
    }
    // no bidding on your own (or your corp's own) auction
    if ((c.issuerID == charID) or (c.forCorp and (c.issuerCorpID == corpID)))
        throw UserError("ConContractSameIssuerAndAcceptor");
    if (c.isPrivate and (c.assigneeID != corpID) and ((pClient->GetAllianceID() == 0) or (c.assigneeID != (uint32)pClient->GetAllianceID())))
        throw UserError("ConNotYourContract");

    // minimum bid:  the starting bid, then 10% of it (at least 1000 isk) over the current bid, capped at the buyout
    ContractBid top = ContractBid();
    bool hasTop(ContractUtils::GetTopBid(c.contractID, top));
    int64 minBid(c.price);
    if (hasTop)
        minBid = std::max(minBid, top.amount + std::max((int64)(0.1 * c.price), (int64)1000));
    if (c.collateral > 0)
        minBid = std::min(minBid, c.collateral);

    int64 amount(ContractUtils::ReadISK(bid));
    if (amount < minBid) {
        std::string bidText(FormatISK(std::max(amount, (int64)0))), minText(FormatISK(minBid));
        throw UserError("ConBidTooLow").AddFormatValue("bid", new PyString(bidText)).AddFormatValue("minBid", new PyString(minText));
    }
    // nobody pays more than the buyout
    if ((c.collateral > 0) and (amount > c.collateral))
        amount = c.collateral;

    // the bid is held by the system.  this throws before anything changed when the wallet is short
    AccountService::TransferFunds(bidderID, ownerSystem, amount, "Contract auction bid",
            corpBid ? Journal::EntryType::ContractAuctionBidCorp : Journal::EntryType::ContractAuctionBid, c.contractID,
            bidderKey, Account::KeyType::Cash, pClient);

    DBerror err;
    if (!sDatabase.RunQuery(err,
        "INSERT INTO ctrBids (contractId, amount, bidderID, bidDateTime, forCorp, bidderCharID) VALUES (%i, %lli, %u, %lli, %u, %u)",
        c.contractID, amount, bidderID, GetFileTimeNow(), corpBid ? 1 : 0, charID))
    {
        codelog(DATABASE__ERROR, "Failed to record bid on contract %i: %s", c.contractID, err.c_str());
        AccountService::TransferFunds(ownerSystem, bidderID, amount, "Contract auction bid refund", Journal::EntryType::ContractAuctionBidRefund, c.contractID, Account::KeyType::Cash, bidderKey);
        throw CustomError("Failed to place the bid.");
    }

    // the previous high bidder gets the bid back
    if (hasTop) {
        sDatabase.RunQuery(err, "UPDATE ctrBids SET refunded = 1 WHERE bidID = %i", top.bidID);
        // corp bids go back to the master wallet (the bid's division isn't kept)
        AccountService::TransferFunds(ownerSystem, top.bidderID, top.amount, "Contract auction bid refund", Journal::EntryType::ContractAuctionBidRefund, c.contractID);
        if (top.bidderID != bidderID)
            ContractUtils::Notify(top.bidderID, "OnContractOutbid", c.contractID);
    }

    // a bid at the buyout price ends the auction now
    if ((c.collateral > 0) and (amount >= c.collateral)) {
        sDatabase.RunQuery(err, "UPDATE ctrContracts SET dateExpired = %lli WHERE contractId = %i", GetFileTimeNow(), c.contractID);
        ContractUtils::Notify(ContractUtils::IssuerWallet(c), "OnContractBuyout", c.contractID);
    }

    return new PyBool(true);
}

PyResult ContractProxy::FinishAuction(PyCallArgs& call, PyInt* contractID, PyRep* isIssuer)
{
    Client* pClient(call.client);
    uint32 charID(pClient->GetCharacterID()), corpID(pClient->GetCorporationID());

    ContractInfo c = ContractInfo();
    if (!ContractUtils::GetContractInfo(contractID->value(), c) or !ContractUtils::CanView(pClient, c))
        throw UserError("ConContractNotFound");
    if (c.type != Contract::Type::Auction)
        throw CustomError("This contract is not an auction.");
    if (c.dateExpired > GetFileTimeNow())
        throw CustomError("This auction has not finished yet.");
    ContractBid top = ContractBid();
    if (!ContractUtils::GetTopBid(c.contractID, top))
        throw CustomError("Nobody has bid on this auction.  You can delete it.");
    if ((c.status != Contract::Status::Outstanding) and (c.status != Contract::Status::FinishedIssuer)
    and (c.status != Contract::Status::FinishedContractor))
        throw UserError("ConAuctionAlreadyClaimed");

    int64 timestamp(GetFileTimeNow());
    DBerror err;
    if (IsTrue(isIssuer)) {
        // the issuer (a contract manager of the issuing corp) claims the isk
        if (c.forCorp) {
            if (c.issuerCorpID != corpID)
                throw UserError("ConNotYourContract");
            CheckContractManager(pClient);
        } else if (c.issuerID != charID) {
            throw UserError("ConNotYourContract");
        }
        if (c.status == Contract::Status::FinishedIssuer)
            throw UserError("ConAuctionAlreadyClaimed");

        int32 newStatus((c.status == Contract::Status::FinishedContractor) ? Contract::Status::Finished : Contract::Status::FinishedIssuer);
        if (!ContractUtils::SetStatus(c.contractID, newStatus, 0, timestamp, top.bidderID))
            return new PyBool(false);
        AccountService::TransferFunds(ownerSystem, ContractUtils::IssuerWallet(c), top.amount, "Contract auction sold", Journal::EntryType::ContractAuctionSold, c.contractID, Account::KeyType::Cash, ContractUtils::IssuerKey(c));
        // deposit back, sales tax on the winning bid
        ContractUtils::RefundDeposit(c, true, top.amount);
        ContractUtils::Notify(top.bidderID, "OnContractCompleted", c.contractID);
    } else {
        // the winner (a contract manager of the winning corp) claims the items
        if (top.forCorp) {
            if (top.bidderID != corpID)
                throw UserError("ConNotYourContract");
            CheckContractManager(pClient);
        } else if (top.bidderID != charID) {
            throw UserError("ConNotYourContract");
        }
        if (c.status == Contract::Status::FinishedContractor)
            throw UserError("ConAuctionAlreadyClaimed");

        int32 newStatus((c.status == Contract::Status::FinishedIssuer) ? Contract::Status::Finished : Contract::Status::FinishedContractor);
        if (!ContractUtils::SetStatus(c.contractID, newStatus, 0, timestamp, top.bidderID))
            return new PyBool(false);
        // the items, in the start station (corp:  its office's deliveries hangar)
        ContractUtils::GiveItemsTo(c, top.bidderID, top.forCorp, top.bidderCharID ? top.bidderCharID : charID, c.startStationID, true);
        ContractUtils::Notify(ContractUtils::IssuerWallet(c), "OnContractCompleted", c.contractID);
    }

    return new PyBool(true);
}

PyResult ContractProxy::GetMyExpiredContractList(PyCallArgs &call, std::optional<PyRep*> forCorp) {
    // the journal's "requires attention" list (KeyVal contracts, items, bids)
    bool corp(IsTrue(forCorp));
    if (corp and (!IsPlayerCorp(call.client->GetCorporationID())
    or ((call.client->GetCorpRole() & (Corp::Role::ContractManager | Corp::Role::Director)) == 0)))
        return ContractUtils::BuildContractList(" WHERE 1 = 0");
    return ContractUtils::BuildContractList(AttentionWhere(call.client, corp));
}

PyResult ContractProxy::GetMyBids(PyCallArgs& call, std::optional<PyRep*> forCorp) {
    // auctions i (my corp) bid on, running or finished in the last week
    bool corp(IsTrue(forCorp));
    uint32 me(corp ? call.client->GetCorporationID() : call.client->GetCharacterID());
    std::string where = " WHERE contractType = 2 AND contractId IN (SELECT contractId FROM ctrBids WHERE bidderID = " + std::to_string(me) + ")"
                        " AND (status = 0 OR (status IN (2, 3, 4) AND dateCompleted > " + std::to_string(GetFileTimeNow() - EvE::Time::Week) + "))";
    return ContractUtils::BuildContractList(where);
}

PyResult ContractProxy::GetMyCurrentContractList(PyCallArgs& call, PyRep* isAccepted, std::optional<PyRep*> forCorp) {
    // in progress contracts i (my corp) issued, or accepted
    bool corp(IsTrue(forCorp));
    uint32 me(corp ? call.client->GetCorporationID() : call.client->GetCharacterID());
    std::string where = " WHERE status = 1 AND ";
    if (IsTrue(isAccepted)) {
        where += "acceptorID = " + std::to_string(me);
    } else if (corp) {
        where += "issuerCorpID = " + std::to_string(me) + " AND forCorp = 1";
    } else {
        where += "issuerID = " + std::to_string(me) + " AND forCorp = 0";
    }
    return ContractUtils::BuildContractList(where);
}

PyResult ContractProxy::DeleteNotification(PyCallArgs& call, PyInt* contractID, std::optional<PyRep*> forCorp) {
    // removes a contract from my "requires attention" list
    uint32 me(IsTrue(forCorp) ? call.client->GetCorporationID() : call.client->GetCharacterID());
    DBerror err;
    sDatabase.RunQuery(err, "INSERT IGNORE INTO ctrDismissed (contractID, ownerID) VALUES (%i, %u)", contractID->value(), me);
    return new PyBool(true);
}

PyResult ContractProxy::NumOutstandingContracts(PyCallArgs &call) {
    // contracts.py GetContractLimits:  KeyVal(myCharTotal, nonCorpForMyChar, myCorpTotal, nonCorpForMyCorp)
    OutstandingCounts counts = OutstandingCounts();
    CountOutstanding(call.client, counts);
    PyDict* dict = new PyDict();
    dict->SetItemString("myCharTotal", new PyInt(counts.myCharTotal));
    dict->SetItemString("nonCorpForMyChar", new PyInt(counts.nonCorpForMyChar));
    dict->SetItemString("myCorpTotal", new PyInt(counts.myCorpTotal));
    dict->SetItemString("nonCorpForMyCorp", new PyInt(counts.nonCorpForMyCorp));
    return new PyObject("util.KeyVal", dict);
}

PyResult ContractProxy::GetItemsInStation(PyCallArgs &call, PyInt* stationID, std::optional<PyRep*> forCorp) {
    // the contract wizard's item list:  my item hangar there, or (forCorp) the office hangars i can take from
    Client* pClient(call.client);
    uint32 station(stationID->value());
    if (!sDataMgr.IsStation(station))
        return nullptr;

    if (!IsTrue(forCorp)) {
        Inventory* pInv(pClient->GetStationHangar(station));
        if (pInv == nullptr)
            return nullptr;
        return pInv->List(flagHangar, pClient->GetCharacterID());
    }

    CheckContractManager(pClient);
    DBRowDescriptor* header = sDataMgr.CreateHeader();
    CRowSet* rowset = new CRowSet(&header);
    uint32 officeID(stDataMgr.GetOfficeIDForCorp(station, pClient->GetCorporationID()));
    Inventory* pInv((officeID != 0) ? GetOfficeInventory(officeID) : nullptr);
    if (pInv != nullptr) {
        int64 roles(pClient->GetCorpRolesAtLocation(station));
        std::vector<InventoryItemRef> items;
        pInv->GetInventoryVec(items);
        for (auto cur : items) {
            if ((cur->ownerID() != (uint32)pClient->GetCorporationID()) or !IsHangarDivision(cur->flag()))
                continue;
            if ((roles & Corp::HangarTakeRole(cur->flag())) == 0)
                continue;
            cur->GetItemRow(rowset->NewRow());
        }
    }
    return rowset;
}

PyResult ContractProxy::GetItemsInContainer(PyCallArgs& call, PyInt* stationID, PyInt* containerID, PyRep* forCorp, std::optional<PyInt*> flag) {
    // contents of a ship or container about to be entered into a contract (contracts.py ConNonEmptyContainer2)
    Client* pClient(call.client);
    uint32 station(stationID->value());
    InventoryItemRef iRef = sItemFactory.GetItemRef(containerID->value());
    if ((iRef.get() == nullptr) or !IsContainerType(iRef))
        return nullptr;

    if (IsTrue(forCorp)) {
        CheckContractManager(pClient);
        uint32 officeID(stDataMgr.GetOfficeIDForCorp(station, pClient->GetCorporationID()));
        if ((officeID == 0) or (iRef->ownerID() != (uint32)pClient->GetCorporationID()) or (iRef->locationID() != officeID))
            return nullptr;
        if ((pClient->GetCorpRolesAtLocation(station) & Corp::HangarTakeRole(iRef->flag())) == 0)
            throw UserError("ConNoAccessToCorpHangar");
    } else if ((iRef->ownerID() != pClient->GetCharacterID()) or (iRef->locationID() != station) or (iRef->flag() != flagHangar)) {
        return nullptr;
    }

    Inventory* pInv(iRef->GetMyInventory());
    if (pInv == nullptr)
        return nullptr;
    pInv->LoadContents();
    DBRowDescriptor* header = sDataMgr.CreateHeader();
    CRowSet* rowset = new CRowSet(&header);
    std::vector<InventoryItemRef> items;
    pInv->GetInventoryVec(items);
    for (auto cur : items)
        cur->GetItemRow(rowset->NewRow());
    return rowset;
}

PyResult ContractProxy::SplitStack(PyCallArgs& call, PyInt* stationID, PyInt* itemID, PyInt* qty, PyRep* forCorp, std::optional<PyInt*> flag) {
    // the contract wizard splitting a stack in the start station
    Client* pClient(call.client);
    uint32 station(stationID->value());
    InventoryItemRef iRef = sItemFactory.GetItemRef(itemID->value());
    if (iRef.get() == nullptr)
        throw UserError("ConCannotTradeItemSanity");

    if (IsTrue(forCorp)) {
        CheckContractManager(pClient);
        uint32 officeID(stDataMgr.GetOfficeIDForCorp(station, pClient->GetCorporationID()));
        if ((officeID == 0) or (iRef->ownerID() != (uint32)pClient->GetCorporationID()) or (iRef->locationID() != officeID))
            throw UserError("ConCannotTradeItemSanity");
        if ((pClient->GetCorpRolesAtLocation(station) & Corp::HangarTakeRole(iRef->flag())) == 0)
            throw UserError("ConNoAccessToCorpHangar");
    } else if ((iRef->ownerID() != pClient->GetCharacterID()) or (iRef->locationID() != station) or (iRef->flag() != flagHangar)) {
        throw UserError("ConCannotTradeItemSanity");
    }

    if (iRef->isSingleton() or (qty->value() < 1) or (qty->value() >= iRef->quantity()))
        throw UserError("ConSplitStackIllegalQty");
    if (iRef->Split(qty->value(), true).get() == nullptr)
        throw UserError("ConSplitStackIllegalQty");
    return new PyBool(true);
}

PyResult ContractProxy::GetCourierContractFromItemID(PyCallArgs& call, PyInt* itemID) {
    // the courier contract of a package i (my corp) carry or issued.  None -> ConContractNotFoundForCrate
    uint32 charID(call.client->GetCharacterID()), corpID(call.client->GetCorporationID());
    DBQueryResult res;
    DBResultRow row;
    if (!sDatabase.RunQuery(res,
        "SELECT contractId FROM ctrContracts WHERE crateID = %i AND status = %u"
        " AND (acceptorID IN (%u, %u) OR issuerID = %u OR (forCorp = 1 AND issuerCorpID = %u)) LIMIT 1",
        itemID->value(), Contract::Status::InProgress, charID, corpID, charID, corpID))
    {
        codelog(DATABASE__ERROR, "Error in query: %s", res.error.c_str());
        return PyStatic.NewNone();
    }
    if (!res.GetRow(row))
        return PyStatic.NewNone();
    PyDict* dict = new PyDict();
    dict->SetItemString("contractID", new PyInt(row.GetInt(0)));
    return new PyObject("util.KeyVal", dict);
}

PyResult ContractProxy::CollectMyPageInfo(PyCallArgs &call) {
    Client* pClient(call.client);
    uint32 charID(pClient->GetCharacterID()), corpID(pClient->GetCorporationID());
    OutstandingCounts counts = OutstandingCounts();
    CountOutstanding(pClient, counts);

    std::string mainQuery = "SELECT "
                            "IFNULL(SUM(CASE WHEN (cC.acceptorID = {CHAR_ID} AND cC.status = 1) THEN 1 ELSE 0 END), 0) AS numInProgress, "
                            "IFNULL(SUM(CASE WHEN (cC.acceptorID = {CORP_ID} AND cC.status = 1) THEN 1 ELSE 0 END), 0) AS numInProgressCorp, "
                            "IFNULL(SUM(CASE WHEN (cC.contractType = 2 AND cC.status = 0 AND cC.contractId IN (SELECT contractId FROM ctrBids WHERE bidderID = {CHAR_ID})) THEN 1 ELSE 0 END), 0) AS numBiddingOn, "
                            "IFNULL(SUM(CASE WHEN (cC.contractType = 2 AND cC.status = 0 AND cC.contractId IN (SELECT contractId FROM ctrBids WHERE bidderID = {CORP_ID})) THEN 1 ELSE 0 END), 0) AS numBiddingOnCorp "
                            "FROM ctrContracts cC "
                            "WHERE cC.acceptorID IN ({CHAR_ID},{CORP_ID}) OR cC.contractType = 2";
    std::string outstandingContractsQuery = "SELECT issuerID, issuerCorpID, assigneeID, contractType "
                                            "FROM ctrContracts "
                                            "WHERE assigneeID IN ({CHAR_ID},{CORP_ID}) AND status = 0 AND dateExpired > {NOW}";
    // Since we already use boost - why not use some of it's libraries for string juggling? :P
    boost::replace_all(mainQuery, "{CHAR_ID}", std::to_string(charID));
    boost::replace_all(mainQuery, "{CORP_ID}", std::to_string(corpID));
    boost::replace_all(outstandingContractsQuery, "{CHAR_ID}", std::to_string(charID));
    boost::replace_all(outstandingContractsQuery, "{CORP_ID}", std::to_string(corpID));
    boost::replace_all(outstandingContractsQuery, "{NOW}", std::to_string(GetFileTimeNow()));

    // First, we gather outstanding contracts - they will later be packed into response dict
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, outstandingContractsQuery.c_str()))
    {
        codelog(DATABASE__ERROR, "Error in mainQuery: %s", res.error.c_str());
        return nullptr;
    }
    DBResultRow row;
    PyList* oustandingContractsList = new PyList;
    while (res.GetRow(row)) {
        PyList* list = new PyList(4);
        list->SetItem(0, new PyInt(row.GetInt(0)));
        list->SetItem(1, new PyInt(row.GetInt(1)));
        list->SetItem(2, new PyInt(row.GetInt(2)));
        list->SetItem(3, new PyInt(row.GetInt(3)));

        oustandingContractsList->AddItem(list);
    }

    // Then, we go for main message body data
    if (!sDatabase.RunQuery(res, mainQuery.c_str()))
    {
        codelog(DATABASE__ERROR, "Error in mainQuery: %s", res.error.c_str());
        PyDecRef(oustandingContractsList);
        return nullptr;
    }
    int32 inProgress(0), inProgressCorp(0), biddingOn(0), biddingOnCorp(0);
    if (res.GetRow(row)) {
        inProgress = row.GetInt(0);
        inProgressCorp = row.GetInt(1);
        biddingOn = row.GetInt(2);
        biddingOnCorp = row.GetInt(3);
    }

    // requires attention, as the journal lists them
    int32 attention(0), attentionCorp(0);
    if (sDatabase.RunQuery(res, ("SELECT COUNT(*) FROM ctrContracts" + AttentionWhere(pClient, false)).c_str()) and res.GetRow(row))
        attention = row.GetInt(0);
    if (IsPlayerCorp(corpID) and ((pClient->GetCorpRole() & (Corp::Role::ContractManager | Corp::Role::Director)) != 0))
        if (sDatabase.RunQuery(res, ("SELECT COUNT(*) FROM ctrContracts" + AttentionWhere(pClient, true)).c_str()) and res.GetRow(row))
            attentionCorp = row.GetInt(0);

    // Because of outstandingContracts list in this response, we can't return DBResultToCRowset directly - so, we'll have to compose the dict manually.
    PyDict* vals = new PyDict;
    vals->SetItemString("numOutstandingContracts", new PyInt(counts.myCharTotal));
    vals->SetItemString("numOutstandingContractsNonCorp", new PyInt(counts.nonCorpForMyChar));
    vals->SetItemString("numOutstandingContractsForCorp", new PyInt(counts.nonCorpForMyCorp));
    vals->SetItemString("numInProgress", new PyInt(inProgress));
    vals->SetItemString("numInProgressCorp", new PyInt(inProgressCorp));
    vals->SetItemString("outstandingContracts", oustandingContractsList);
    vals->SetItemString("numRequiresAttention", new PyInt(attention));
    vals->SetItemString("numRequiresAttentionCorp", new PyInt(attentionCorp));
    vals->SetItemString("numBiddingOn", new PyInt(biddingOn));
    vals->SetItemString("numBiddingOnCorp", new PyInt(biddingOnCorp));

    return new PyObject("util.KeyVal", vals);
}

PyResult ContractProxy::GetContractListForOwner(PyCallArgs &call, PyInt* ownerID, PyInt* contractStatus, std::optional <PyInt*> contractType, std::optional <PyBool*> issuedToBy) {
    sLog.White( "ContractProxy::Handle_GetContractListForOwner()", "size=%lu", call.tuple->size());
    call.Dump(SERVICE__CALL_DUMP);

    return ContractUtils::GetContractListForOwner(ownerID, contractStatus, contractType, issuedToBy);
    /*
     *  client call....
      [PyTuple 2 items]
        [PyInt 0]
        [PySubStream 73 bytes]
          [PyTuple 4 items]
            [PyInt 1]
            [PyString "GetContractListForOwner"]
            [PyTuple 4 items]
              [PyInt 649670823]
              [PyInt 0] (Contract status, 0 for outstanding, 1 for in progress, 4 for Finished)
              [PyInt] (Contract type. 1 for exchange, 2 for auction, 3 for courier, None for all)
              [PyBool] (false for Issued By, true for issued To, None for both)
            [PyDict 3 kvp]
              [PyString "num"]
              [PyInt 100]
              [PyString "machoVersion"]
              [PyInt 1]
              [PyString "startContractID"]
              [PyNone]

        server reply...
      [PySubStream 713 bytes]
        [PyObjectData Name: util.KeyVal]
          [PyDict 3 kvp]
            [PyString "contracts"]
            [PyObjectEx Type2]
              [PyTuple 2 items]
                [PyTuple 1 items]
                  [PyToken dbutil.CRowset]
                [PyDict 1 kvp]
                  [PyString "header"]
                  [PyObjectEx Normal]
                    [PyTuple 2 items]
                      [PyToken blue.DBRowDescriptor]
                      [PyTuple 1 items]
                        [PyTuple 30 items]
                          [PyTuple 2 items]
                            [PyString "contractID"]
                            [PyInt 3]
                          [PyTuple 2 items]
                            [PyString "type"]
                            [PyInt 17]
                          [PyTuple 2 items]
                            [PyString "issuerID"]
                            [PyInt 3]
                          [PyTuple 2 items]
                            [PyString "issuerCorpID"]
                            [PyInt 3]
                          [PyTuple 2 items]
                            [PyString "forCorp"]
                            [PyInt 11]
                          [PyTuple 2 items]
                            [PyString "availability"]
                            [PyInt 3]
                          [PyTuple 2 items]
                            [PyString "assigneeID"]
                            [PyInt 3]
                          [PyTuple 2 items]
                            [PyString "acceptorID"]
                            [PyInt 3]
                          [PyTuple 2 items]
                            [PyString "dateIssued"]
                            [PyInt 64]
                          [PyTuple 2 items]
                            [PyString "dateExpired"]
                            [PyInt 64]
                          [PyTuple 2 items]
                            [PyString "dateAccepted"]
                            [PyInt 64]
                          [PyTuple 2 items]
                            [PyString "numDays"]
                            [PyInt 3]
                          [PyTuple 2 items]
                            [PyString "dateCompleted"]
                            [PyInt 64]
                          [PyTuple 2 items]
                            [PyString "startStationID"]
                            [PyInt 3]
                          [PyTuple 2 items]
                            [PyString "startSolarSystemID"]
                            [PyInt 3]
                          [PyTuple 2 items]
                            [PyString "startRegionID"]
                            [PyInt 3]
                          [PyTuple 2 items]
                            [PyString "endStationID"]
                            [PyInt 3]
                          [PyTuple 2 items]
                            [PyString "endSolarSystemID"]
                            [PyInt 3]
                          [PyTuple 2 items]
                            [PyString "endRegionID"]
                            [PyInt 3]
                          [PyTuple 2 items]
                            [PyString "price"]
                            [PyInt 6]
                          [PyTuple 2 items]
                            [PyString "reward"]
                            [PyInt 6]
                          [PyTuple 2 items]
                            [PyString "collateral"]
                            [PyInt 6]
                          [PyTuple 2 items]
                            [PyString "title"]
                            [PyInt 130]
                          [PyTuple 2 items]
                            [PyString "status"]
                            [PyInt 17]
                          [PyTuple 2 items]
                            [PyString "volume"]
                            [PyInt 5]
                          [PyTuple 2 items]
                            [PyString "issuerAllianceID"]
                            [PyInt 3]
                          [PyTuple 2 items]
                            [PyString "issuerWalletKey"]
                            [PyInt 3]
                          [PyTuple 2 items]
                            [PyString "acceptorWalletKey"]
                            [PyInt 3]
                          [PyTuple 2 items]
                            [PyString "crateID"]
                            [PyInt 20]
                          [PyTuple 2 items]
                            [PyString "contractID"]
                            [PyInt 3]
              [PyPackedRow 146 bytes]
                ["contractID" => <41239648> [I4]]
                ["type" => <1> [UI1]]
                ["issuerID" => <649670823> [I4]]
                ["issuerCorpID" => <98038978> [I4]]
                ["forCorp" => <0> [Bool]]
                ["availability" => <1> [I4]]
                ["assigneeID" => <1661059544> [I4]]
                ["acceptorID" => <0> [I4]]
                ["dateIssued" => <129494707760000000> [FileTime]]
                ["dateExpired" => <129495571760000000> [FileTime]]
                ["dateAccepted" => <129494707760000000> [FileTime]]
                ["numDays" => <0> [I4]]
                ["dateCompleted" => <129494707760000000> [FileTime]]
                ["startStationID" => <60006433> [I4]]
                ["startSolarSystemID" => <30000135> [I4]]
                ["startRegionID" => <10000002> [I4]]
                ["endStationID" => <60006433> [I4]]
                ["endSolarSystemID" => <0> [I4]]
                ["endRegionID" => <0> [I4]]
                ["price" => <150000000> [CY]]
                ["reward" => <0> [CY]]
                ["collateral" => <0> [CY]]
                ["title" => <quafe!> [WStr]]
                ["status" => <0> [UI1]]
                ["volume" => <6> [R8]]
                ["issuerAllianceID" => <0> [I4]]
                ["issuerWalletKey" => <0> [I4]]
                ["acceptorWalletKey" => <0> [I4]]
                ["crateID" => <1002309425092> [I8]]
                ["contractID" => <41239648> [I4]]
            [PyString "items"]
            [PyDict 1 kvp]
              [PyInt 41239648]
              [PyList 1 items]
                [PyObjectData Name: util.Row]
                  [PyDict 2 kvp]
                    [PyString "header"]
                    [PyList 3 items]
                      [PyString "itemTypeID"]
                      [PyString "quantity"]
                      [PyString "inCrate"]
                    [PyString "line"]
                    [PyList 3 items]
                      [PyInt 3898]
                      [PyInt 6]
                      [PyBool True]
            [PyString "bids"]
            [PyDict 0 kvp]
     */
    return nullptr;
}

PyResult ContractProxy::GetLoginInfo(PyCallArgs &call)
{
    // contracts.py NeocomBlink:  assignedToMe (contractID, issuerID), needsAttention (contractID, forCorp),
    //  inProgress couriers i carry (contractID, startStationID, endStationID, expires)
    Client* pClient(call.client);
    uint32 charID(pClient->GetCharacterID()), corpID(pClient->GetCorporationID());
    std::string now(std::to_string(GetFileTimeNow()));
    DBQueryResult res;
    DBResultRow row;

    /* create needsAttention row descriptor */
    DBRowDescriptor *needsAttentionHeader = new DBRowDescriptor();
        needsAttentionHeader->AddColumn( "contractID",   DBTYPE_I4);
        needsAttentionHeader->AddColumn( "forCorp",      DBTYPE_I4);
    CRowSet *needsAttention_rowset = new CRowSet( &needsAttentionHeader );
    if (sDatabase.RunQuery(res, ("SELECT contractId FROM ctrContracts" + AttentionWhere(pClient, false)).c_str())) {
        while (res.GetRow(row)) {
            PyPackedRow* into = needsAttention_rowset->NewRow();
            into->SetField((uint32)0, new PyInt(row.GetInt(0)));
            into->SetField(1, new PyInt(0));
        }
    }
    if (IsPlayerCorp(corpID) and ((pClient->GetCorpRole() & (Corp::Role::ContractManager | Corp::Role::Director)) != 0)) {
        if (sDatabase.RunQuery(res, ("SELECT contractId FROM ctrContracts" + AttentionWhere(pClient, true)).c_str())) {
            while (res.GetRow(row)) {
                PyPackedRow* into = needsAttention_rowset->NewRow();
                into->SetField((uint32)0, new PyInt(row.GetInt(0)));
                into->SetField(1, new PyInt(1));
            }
        }
    }

    /* create inProgress row descriptor */
    DBRowDescriptor *inProgressHeader = new DBRowDescriptor();
        inProgressHeader->AddColumn( "contractID",      DBTYPE_I4 );
        inProgressHeader->AddColumn( "startStationID",  DBTYPE_I4 );
        inProgressHeader->AddColumn( "endStationID",    DBTYPE_I4 );
        inProgressHeader->AddColumn( "expires",         DBTYPE_FILETIME );
    CRowSet *inProgress_rowset = new CRowSet( &inProgressHeader );
    if (sDatabase.RunQuery(res,
        "SELECT contractId, startStationID, endStationID, dateAccepted + numDays * %lli FROM ctrContracts"
        " WHERE acceptorID = %u AND status = %u AND contractType = %u",
        EvE::Time::Day, charID, Contract::Status::InProgress, Contract::Type::Courier))
    {
        while (res.GetRow(row)) {
            PyPackedRow* into = inProgress_rowset->NewRow();
            into->SetField((uint32)0, new PyInt(row.GetInt(0)));
            into->SetField(1, new PyInt(row.GetInt(1)));
            into->SetField(2, new PyInt(row.GetInt(2)));
            into->SetField(3, new PyLong(row.GetInt64(3)));
        }
    }

    /* create assignedToMe row descriptor */
    DBRowDescriptor *assignedToMeHeader = new DBRowDescriptor();
        assignedToMeHeader->AddColumn( "contractID",    DBTYPE_I4);
        assignedToMeHeader->AddColumn( "issuerID",      DBTYPE_I4);
    CRowSet *assignedToMe_rowset = new CRowSet( &assignedToMeHeader );
    if (sDatabase.RunQuery(res,
        "SELECT contractId, issuerID, issuerCorpID, forCorp FROM ctrContracts"
        " WHERE status = 0 AND isPrivate <> 0 AND dateExpired > %s AND assigneeID IN (%u, %u)",
        now.c_str(), charID, corpID))
    {
        while (res.GetRow(row)) {
            PyPackedRow* into = assignedToMe_rowset->NewRow();
            into->SetField((uint32)0, new PyInt(row.GetInt(0)));
            // contracts.py checks the issuer against the ignore list
            into->SetField(1, new PyInt(row.GetBool(3) ? row.GetInt(2) : row.GetInt(1)));
        }
    }

    PyDict* args = new PyDict;
        args->SetItemString( "needsAttention",          needsAttention_rowset );
        args->SetItemString( "inProgress",              inProgress_rowset );
        args->SetItemString( "assignedToMe",            assignedToMe_rowset );

    return new PyObject( "util.KeyVal", args );
}

     /**
     * NOTE: Contract statuses:
     * 0 - Outstanding
     * 1 - In Progress
     * 2 - Items not yet claimed
     * 3 - Unclaimed by seller
     * 4,5 - Finished
     * 6 - Rejected
     * 7 - Failed
     * 8 - Deleted
     * 9 - Reversal
     */

/* Description
 * GetLoginInfo sends back a util.KeyVal PyClass. This class contains 3 entries, those entries are
 * "needsAttention", "inProgress" and "assignedToMe".
 *
 * ------------------
 * assignedToMe
 * ------------------
 * assignedToMe is a pretty obvious packet, it contains rows of userid's and contractid's
 * that are belonging to the player.
 *
 * ------------------
 * inProgress
 * ------------------
 * inProgress is also pretty obvious, it contains rows of contractID, startStationID, endStationID and expires
 * that marks what contract is in progress.
 *
 * ------------------
 * needsAttention
 * ------------------
 * needsAttention contains contracts that need attention, its rather a way to make the journal blink. The
 * contracts off course are highlighted in the journal.
 *
      [PySubStream 273 bytes]
        [PyObjectData Name: util.KeyVal]
          [PyDict 3 kvp]
            [PyString "inProgress"]
            [PyObjectEx Type2]
              [PyTuple 2 items]
                [PyTuple 1 items]
                  [PyToken dbutil.CRowset]
                [PyDict 1 kvp]
                  [PyString "header"]
                  [PyObjectEx Normal]
                    [PyTuple 2 items]
                      [PyToken blue.DBRowDescriptor]
                      [PyTuple 1 items]
                        [PyTuple 4 items]
                          [PyTuple 2 items]
                            [PyString "contractID"]
                            [PyInt 3]
                          [PyTuple 2 items]
                            [PyString "startStationID"]
                            [PyInt 3]
                          [PyTuple 2 items]
                            [PyString "endStationID"]
                            [PyInt 3]
                          [PyTuple 2 items]
                            [PyString "expires"]
                            [PyInt 64]
            [PyString "needsAttention"]
            [PyObjectEx Type2]
              [PyTuple 2 items]
                [PyTuple 1 items]
                  [PyToken dbutil.CRowset]
                [PyDict 1 kvp]
                  [PyString "header"]
                  [PyObjectEx Normal]
                    [PyTuple 2 items]
                      [PyToken blue.DBRowDescriptor]
                      [PyTuple 1 items]
                        [PyTuple 2 items]
                          [PyTuple 2 items]
                            [PyString "contractID"]
                            [PyInt 3]
                          [PyTuple 2 items]
                            [PyString ""]
                            [PyInt 3]
            [PyString "assignedToMe"]
            [PyObjectEx Type2]
              [PyTuple 2 items]
                [PyTuple 1 items]
                  [PyToken dbutil.CRowset]
                [PyDict 1 kvp]
                  [PyString "header"]
                  [PyObjectEx Normal]
                    [PyTuple 2 items]
                      [PyToken blue.DBRowDescriptor]
                      [PyTuple 1 items]
                        [PyTuple 2 items]
                          [PyTuple 2 items]
                            [PyString "contractID"]
                            [PyInt 3]
                          [PyTuple 2 items]
                            [PyString "issuerID"]
                            [PyInt 3]
*/
