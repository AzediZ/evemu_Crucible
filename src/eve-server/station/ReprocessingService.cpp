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
    Author:     Zhur
    Rewrite:    Allan
*/

/**  @todo  update this entire file...  */
/*
 * # Manufacturing Logging:
 * MANUF__ERROR
 * MANUF__WARNING
 * MANUF__MESSAGE
 * MANUF__INFO
 * MANUF__DEBUG
 * MANUF__TRACE
 * MANUF__DUMP
 */


#include "eve-server.h"

#include "Client.h"
#include "packets/Manufacturing.h"
#include "manufacturing/RamMethods.h"
#include "station/ReprocessingService.h"
#include "station/StationDataMgr.h"
#include "inventory/Inventory.h"
#include "Station.h"
#include "system/SystemManager.h"
#include "services/ServiceManager.h"

ReprocessingService::ReprocessingService(EVEServiceManager& mgr) :
    BindableService("reprocessingSvc", mgr)
{
}

BoundDispatcher* ReprocessingService::BindObject(Client* client, PyRep* bindParameters) {
    if (!bindParameters->IsInt()) {
        _log(SERVICE__ERROR, "%s: Non-integer bind argument '%s'", client->GetName(), bindParameters->TypeString());
        return nullptr;
    }

    uint32 stationID = bindParameters->AsInt()->value();
    if (!sDataMgr.IsStation(stationID)) {
        _log(SERVICE__ERROR, "%s: Expected stationID, but got %u.", client->GetName(), stationID);
        return nullptr;
    }

    return new ReprocessingServiceBound(this->GetServiceManager(), *this, m_db, stationID);
}

void ReprocessingService::BoundReleased (ReprocessingServiceBound* bound) {

}

ReprocessingServiceBound::ReprocessingServiceBound(EVEServiceManager& mgr, ReprocessingService& parent, ReprocessingDB& db, uint32 stationID) :
    EVEBoundObject(mgr, parent),
    m_db(db),
    m_stationCorpID(0),
    m_staEfficiency(0.0f),
    m_tax(0.0f)
{
    this->Add("GetOptionsForItemTypes", &ReprocessingServiceBound::GetOptionsForItemTypes);
    this->Add("GetReprocessingInfo", &ReprocessingServiceBound::GetReprocessingInfo);
    this->Add("GetQuote", &ReprocessingServiceBound::GetQuote);
    this->Add("GetQuotes", &ReprocessingServiceBound::GetQuotes);
    this->Add("Reprocess", &ReprocessingServiceBound::Reprocess);

    m_stationRef = sItemFactory.GetStationRef(stationID);
    if (m_stationRef.get() != nullptr)
        m_stationRef->GetRefineData(m_stationCorpID, m_staEfficiency, m_tax);
}

PyResult ReprocessingServiceBound::GetOptionsForItemTypes(PyCallArgs &call, PyDict* typeIDs) {
    _log(MANUF__INFO, "%s: Calling GetOptionsForItemTypes().", call.client->GetName());
    call.Dump(MANUF__DUMP);

    std::map <uint32, PyRep*> typeIDMap;

    // TODO: rewrite this after the python types are improved
    PyDict::const_iterator dict_2_cur = typeIDs->begin();
    for (size_t dict_2_index(0); dict_2_cur != typeIDs->end(); ++dict_2_cur, ++dict_2_index) {
        if (!dict_2_cur->first->IsInt()) {
            _log(XMLP__DECODE_ERROR, "Decode Call_GetOptionsForItemTypes failed: Key %u in dict dict_2 is not an integer: %s", dict_2_index, dict_2_cur->first->TypeString());
            return nullptr;
        }

        const PyInt* k = dict_2_cur->first->AsInt();
        typeIDMap[k->value()] = dict_2_cur->second->Clone();
    }

    Rsp_GetOptionsForItemTypes      rsp;
    Rsp_GetOptionsForItemTypes_Arg  arg;

    for (auto cur : typeIDMap) {
        arg.isRecyclable = sDataMgr.IsRecyclable(cur.first);
        arg.isRefinable = sDataMgr.IsRefinable(cur.first);
        rsp.typeIDs[cur.first] = arg.Encode();
    }

    return rsp.Encode();
}

PyResult ReprocessingServiceBound::GetReprocessingInfo(PyCallArgs &call) {
    Client *pClient = call.client;
    Rsp_GetReprocessingInfo rsp;
        rsp.standing = GetStanding(pClient);
        rsp.tax = CalcTax( rsp.standing );
        rsp.yield = m_staEfficiency;
        rsp.combinedyield = CalcReprocessingEfficiency(pClient);
    return rsp.Encode();
}

PyResult ReprocessingServiceBound::GetQuote(PyCallArgs &call, PyInt* itemID) {
    return GetQuote(itemID->value(), call.client);
}

PyResult ReprocessingServiceBound::GetQuotes(PyCallArgs &call, PyList* itemIDs, PyInt* activeShipID) {
    // why shipID here?  processing in cap indy ships?

    std::vector<uint32> items;

    PyList::const_iterator list_2_cur = itemIDs->begin();
    for (size_t list_2_index(0); list_2_cur != itemIDs->end(); ++list_2_cur, ++list_2_index) {
        if (!(*list_2_cur)->IsInt()) {
            _log(XMLP__DECODE_ERROR, "Decode Call_GetQuotes failed: Element %u in list list_2 is not an integer: %s", list_2_index, (*list_2_cur)->TypeString());
            return nullptr;
        }

        const PyInt* t = (*list_2_cur)->AsInt();
        items.push_back(t->value());
    }

    Rsp_GetQuotes rsp;
    for (auto cur : items) {
        PyRep* quote = GetQuote(cur, call.client);
        if (quote != nullptr)
            rsp.quotes[cur] = quote;
    }

    return rsp.Encode();
}

PyResult ReprocessingServiceBound::Reprocess(PyCallArgs &call, PyList* itemIDs, PyInt* fromLocation, std::optional<PyInt*> ownerID, std::optional<PyInt*> flag, PyBool* unknown, PyList* skipChecks) {
    // TODO: rewrite these once the python types are improved
    std::vector<uint32> items;

    PyList::const_iterator list_2_cur = itemIDs->begin();
    for (size_t list_2_index(0); list_2_cur != itemIDs->end(); ++list_2_cur, ++list_2_index) {
        if (!(*list_2_cur)->IsInt()) {
            _log(XMLP__DECODE_ERROR, "Decode Call_Reprocess failed: Element %u in list list_2 is not an integer: %s", list_2_index, (*list_2_cur)->TypeString());
            return nullptr;
        }

        const PyInt* t = (*list_2_cur)->AsInt();
        items.push_back(t->value());
    }

    std::vector<std::string> skipChecksVector;

    PyList::const_iterator list_3_cur = skipChecks->begin();
    for (uint32 list_3_index(0); list_3_cur != skipChecks->end(); ++list_3_cur, ++list_3_index) {
        if (!(*list_3_cur)->IsString()) {
            _log(XMLP__DECODE_ERROR, "Decode Call_Reprocess failed: Element %u in list list_3 is not a string: %s", list_3_index, (*list_3_cur)->TypeString());
            return nullptr;
        }

        const PyString* t = (*list_3_cur)->AsString();
        skipChecksVector.push_back(t->content());
    }

    if ((m_stationRef.get() == nullptr) or ((uint32)call.client->GetStationID() != m_stationRef->GetID())) {
        _log(MANUF__WARNING, "Character %s tried to reprocess, but isn't docked in this station.", call.client->GetName());
        throw CustomError("You must be docked in this station to use its reprocessing plant.");
    }

    _log(MANUF__INFO, "%s: Calling Reprocess().", call.client->GetName());
    call.Dump(MANUF__DUMP);

    // output goes to the char's item hangar, or to a hangar of the corp's office in this station
    uint32 outOwnerID(ownerID.has_value() ? ownerID.value()->value() : call.client->GetCharacterID());
    EVEItemFlags outFlag(flagHangar);
    uint32 outLocationID(m_stationRef->GetID());
    if (outOwnerID == call.client->GetCharacterID()) {
        outFlag = flagHangar;
    } else if ((outOwnerID == call.client->GetCorporationID()) and IsPlayerCorp(outOwnerID)) {
        if ((call.client->GetCorpRole() & Corp::Role::FactoryManager) != Corp::Role::FactoryManager) {
            _log(MANUF__WARNING, "%s(%u) doesnt have FactoryManager role to access materials for reprocessing.", \
                        call.client->GetName(), call.client->GetCharacterID());
            throw CustomError("You do not have the role 'Factory Manager' which is required to access factory services on behalf of a corporation.");
        }
        if (flag.has_value() and (flag.value()->value() != flagNone))
            outFlag = (EVEItemFlags)flag.value()->value();
        if (!IsHangarFlag(outFlag))
            throw CustomError("Invalid output hangar.");
        outLocationID = stDataMgr.GetOfficeIDForCorp(m_stationRef->GetID(), outOwnerID);
        if (outLocationID == 0)
            throw CustomError("Your corporation does not have an office in this station.");

        sRamMthd.HangarRolesCheck(call.client, m_stationRef->GetID(), outFlag);
    } else {
        _log(MANUF__WARNING, "%s(%u) tried to reprocess for owner %u.", call.client->GetName(), call.client->GetCharacterID(), outOwnerID);
        throw CustomError("Invalid output owner.");
    }

    // validate every item before anything is reprocessed
    std::vector<InventoryItemRef> validItems;
    std::set<uint32> seen;
    for (auto cur : items) {
        if (!seen.insert(cur).second)
            continue;
        InventoryItemRef iRef = sItemFactory.GetItemRef(cur);
        if (iRef.get() == nullptr)
            continue;
        ValidateItem(call.client, iRef, true);
        if (iRef->type().portionSize() > iRef->quantity()) {
            throw UserError ("QuantityLessThanMinimumPortion")
                    .AddTypeName ("typename", iRef->typeID ())
                    .AddAmount ("portion", iRef->type ().portionSize ());
        }
        validItems.push_back(iRef);
    }

    double tax = CalcTax(GetStanding(call.client));
    for (auto iRef : validItems)  {

        // this should never happen, but for sure ...
        if (iRef->type().portionSize() > iRef->quantity()) {
            throw UserError ("QuantityLessThanMinimumPortion")
                    .AddTypeName ("typename", iRef->typeID ())
                    .AddAmount ("portion", iRef->type ().portionSize ());
        }

        float efficiency = CalcReprocessingEfficiency( call.client, iRef );

        // dont hit db for this shit...we kinda have to....dont have this data in static shit.
        std::vector<Recoverable> recoverables;
        if ( !m_db.GetRecoverables( iRef->typeID(), recoverables ) )
            continue;

        std::vector<Recoverable>::iterator itr = recoverables.begin();
        for (; itr != recoverables.end(); ++itr) {
            uint32 full = itr->amountPerBatch * iRef->quantity() / iRef->type().portionSize();
            uint32 quantity = uint32(full * efficiency * (1.0f - tax) );
            if (quantity == 0)
                continue;

            ItemData idata(itr->typeID, outOwnerID, locTemp, flagNone, quantity);
            InventoryItemRef iRef2 = sItemFactory.SpawnItem( idata );
            if (iRef2.get() == nullptr)
                continue;

            iRef2->Move(outLocationID, outFlag, true);
        }

        uint32 qtyLeft = iRef->quantity() % iRef->type().portionSize();
        if (qtyLeft) {
            iRef->SetQuantity(qtyLeft, true);
        } else {
            iRef->Move(iRef->locationID(), flagJunkyardReprocessed, true);
            m_stationRef->RemoveItem(iRef);
            iRef->Delete();
        }
    }

    return nullptr;
}

float ReprocessingServiceBound::CalcReprocessingEfficiency(const Client* pClient, InventoryItemRef item) const {
    /* formula is:
        reprocessingEfficiency = 0.375
        *(1 + 0.02 * RefiningSkill)
        *(1 + 0.04 * RefineryEfficiencySkill)
        *(1 + 0.05 * OreProcessingSkill)
    */
    CharacterRef cRef = pClient->GetChar();
    double efficiency =  (0.375f
                        * (1 + (0.02f * cRef->GetSkillLevel(EvESkill::Refining)))             // 2% lvl
                        * (1 + (0.04f * cRef->GetSkillLevel(EvESkill::RefineryEfficiency)))); // 4% lvl

    if (item.get() != nullptr) {
        uint32 specificSkill = item->GetAttribute(AttrReprocessingSkillType).get_int();
        if (specificSkill) {
            efficiency *= (1 + 0.05f * cRef->GetSkillLevel(specificSkill));
        } else {
            efficiency *= (1 + 0.04f * cRef->GetSkillLevel(EvESkill::ScrapmetalProcessing));    // use Scrapmetal Processing as default
        }
    }

    // Zainou 'Beancounter' H40/H50/H60 implants (pre-Odyssey: multiplies the skill part)
    efficiency *= (1 + GetImplantRefiningBonus(cRef) / 100.0);

    efficiency += m_staEfficiency;

    // Crucible yield is capped at 100%
    if (efficiency > 1.0)
        efficiency = 1.0;

    return efficiency;
}

float ReprocessingServiceBound::GetImplantRefiningBonus(CharacterRef cRef)
{
    if (cRef.get() == nullptr)
        return 0.0f;
    Inventory* pInv = cRef->GetMyInventory();
    if (pInv == nullptr)
        return 0.0f;

    // implants are only found here once they are stored as items in the char's inventory (F15)
    float bonus(0.0f);
    std::vector<InventoryItemRef> implants;
    pInv->GetItemsByFlag(flagImplant, implants);
    for (auto cur : implants)
        if (cur->HasAttribute(AttrRefiningYieldMutator))
            bonus += cur->GetAttribute(AttrRefiningYieldMutator).get_float();
    return bonus;
}

bool ReprocessingServiceBound::ValidateItem(Client* pClient, InventoryItemRef iRef, bool throwOnError)
{
    std::string error;
    uint32 stationID(m_stationRef.get() == nullptr ? 0 : m_stationRef->GetID());

    if (iRef->itemID() == pClient->GetShipID()) {
        if (throwOnError)
            throw UserError("CannotReprocessActive");
        return false;
    }

    if (iRef->ownerID() == pClient->GetCharacterID()) {
        // (a) item hangar of this station, or (c) cargo of the active ship docked here
        bool inHangar((iRef->locationID() == stationID) and (iRef->flag() == flagHangar));
        bool inCargo(((uint32)pClient->GetStationID() == stationID) and (iRef->locationID() == pClient->GetShipID()) and (iRef->flag() == flagCargoHold));
        if (!inHangar and !inCargo)
            error = "Items must be in your item hangar or your active ship's cargo hold in this station to be reprocessed.";
    } else if ((iRef->ownerID() == pClient->GetCorporationID()) and IsPlayerCorp(iRef->ownerID())) {
        // (b) a hangar of the corp's office in this station
        if ((pClient->GetCorpRole() & Corp::Role::FactoryManager) != Corp::Role::FactoryManager) {
            _log(MANUF__WARNING, "%s(%u) doesnt have FactoryManager role to access materials for reprocessing.", \
                    pClient->GetName(), pClient->GetCharacterID());
            error = "You do not have the role 'Factory Manager' which is required to access factory services on behalf of a corporation.";
        } else if ((iRef->locationID() != stDataMgr.GetOfficeIDForCorp(stationID, iRef->ownerID())) or !IsHangarFlag(iRef->flag())) {
            error = "Corporation items must be in your corporation's office hangars in this station to be reprocessed.";
        } else if (throwOnError) {
            // this throws "access denied to bom hangar" on error.  probably wrong for this application
            sRamMthd.HangarRolesCheck(pClient, stationID, iRef->flag());
        }
    } else {
        _log(SERVICE__ERROR, "Character %u tried to reprocess item %u of owner %u.", pClient->GetCharacterID(), iRef->itemID(), iRef->ownerID());
        error = "The requested item is not yours.";
    }

    // a ship with jump clones in its Clone Vat Bay can't be reprocessed  (F15)
    if (error.empty() and (iRef->categoryID() == EVEDB::invCategories::Ship) and (CharacterDB::CountJumpClonesAtLocation(iRef->itemID()) > 0)) {
        if (throwOnError)
            throw UserError("ReprocessingFailedShipHasJumpClones")
                    .AddTypeName("what", iRef->typeID());
        error = "Ships with jump clones installed can't be reprocessed.";
    }

    // ships and containers must be empty (contents would be orphaned)
    if (error.empty() and (iRef->GetMyInventory() != nullptr)) {
        iRef->GetMyInventory()->LoadContents();
        if (!iRef->GetMyInventory()->IsEmpty()) {
            error = "Remove all modules, charges, drones and cargo from ";
            error += iRef->name();
            error += " before reprocessing it.";
        }
    }

    if (error.empty())
        return true;
    if (throwOnError)
        throw CustomError("%s", error.c_str());
    _log(MANUF__TRACE, "ReprocessingServiceBound::ValidateItem() - %s(%u): %s", iRef->name(), iRef->itemID(), error.c_str());
    return false;
}

PyRep *ReprocessingServiceBound::GetQuote(uint32 itemID, Client* pClient) {
    InventoryItemRef iRef = sItemFactory.GetItemRef( itemID );
    if (iRef.get() == nullptr)
        return nullptr;    // No action as GetQuote is also called for reprocessed items (probably for check)

    // owner, location, contents (no quote for items that can't be reprocessed here)
    if (!ValidateItem(pClient, iRef, false))
        return nullptr;

    if (iRef->quantity() < iRef->type().portionSize()) {
        throw UserError ("QuantityLessThanMinimumPortion")
                .AddTypeName ("typename", iRef->typeID ())
                .AddAmount ("portion", iRef->type ().portionSize ());
    }

    std::vector<Recoverable> recoverables;
    if (!m_db.GetRecoverables( iRef->typeID(), recoverables))
        return nullptr;

    Rsp_GetQuote quote;
    quote.lines = new PyList();
    quote.leftOvers = iRef->quantity() % iRef->type().portionSize();
    quote.quantityToProcess = iRef->quantity() - quote.leftOvers;
    quote.playerStanding = GetStanding(pClient);

    double tax = CalcTax( quote.playerStanding );
    double efficiency = CalcReprocessingEfficiency(pClient, iRef);

    for (auto cur :recoverables) {
        uint32 ratio = cur.amountPerBatch * quote.quantityToProcess / iRef->type().portionSize();
        Rsp_GetQuote_Recoverables_Line line;
            line.typeID			= cur.typeID;
            line.client			= uint32(efficiency * (1.0f - tax)   * ratio);
            line.station		= uint32(efficiency * tax           * ratio);
            line.unrecoverable	= ratio - line.client - line.station;
        quote.lines->AddItem( line.Encode() );
    }

    return quote.Encode();
}

/** @todo  should this be moved to standings code?   yes!! */
float ReprocessingServiceBound::GetStanding(const Client* pClient) const
{
    float standing = StandingDB::GetStanding(m_stationCorpID, pClient->GetCharacterID());
    if (standing < 0.0f) {
        standing += ((10.0f + standing) * 0.04f * pClient->GetChar()->GetSkillLevel(EvESkill::Diplomacy));
    } else {
        standing += ((10.0f - standing) * 0.04f * pClient->GetChar()->GetSkillLevel(EvESkill::Connections));
    }

    return EvE::max(standing, StandingDB::GetStanding(m_stationCorpID, pClient->GetCorporationID()));
}

// this should be moved to eve math or eve calc's or w/e
float ReprocessingServiceBound::CalcTax(float standing) const {
    //EvEMath::Refine::StationTaxesForReprocessing(standing);
    float tax = m_tax - 0.75f / 100.0f * standing;
    if (tax < 0.0f)
        tax = 0.0f;
    return tax;
}

