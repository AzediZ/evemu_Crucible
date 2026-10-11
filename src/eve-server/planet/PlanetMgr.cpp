 /**
  * @name PlanetMgr.cpp
  *   Specific Class for managing planet resources
  *         this is based on preliminary work by Comet0
  * @Author:         Allan
  * @date:   30 April 2016
  * @update:  4 November 2016  - began rewrite
  *          16 November 2016  - basic system working
  *          19 July 2019      - all systems working properly
  *          F19               - commands validated and applied with the client's rules, transactional, paid for
  */

/*
 * PLANET__ERROR
 * PLANET__WARNING
 * PLANET__DEBUG
 * PLANET__TRACE
 */


#include "eve-server.h"

#include "Client.h"
#include "EVEServerConfig.h"

#include "account/AccountService.h"
#include "inventory/ItemDB.h"
#include "inventory/ItemType.h"
#include "inventory/InventoryItem.h"
#include "planet/Colony.h"
#include "planet/Planet.h"
#include "planet/PlanetMgr.h"
#include "planet/PlanetDataMgr.h"
#include "system/SystemManager.h"
#include "system/sov/SovereigntyDataMgr.h"


namespace {
    // provisional IDs of pins and routes created in a stream.  real itemIDs and routeIDs are far below this
    const uint32 ProvisionalID = 0xF0000000;

    /* colonies can't be built in these systems (since Tyrannis):  PlanetBlackListed
     *  Amarr, Arnon, Aunia, Auvergne, Balginia, Dodixie, Fricoure, Ichoriya, Irjunen, Isaziwa, Isinokka, Jita, Lustrevik, Motsu,
     *  Oursulaert, Rens, Sankkasen, Umokka
     */
    const char* BlackListedSystems[] = {
        "Amarr", "Arnon", "Aunia", "Auvergne", "Balginia", "Dodixie", "Fricoure", "Ichoriya", "Irjunen",
        "Isaziwa", "Isinokka", "Jita", "Lustrevik", "Motsu", "Oursulaert", "Rens", "Sankkasen", "Umokka"
    };

    // deletes a pin or link item of a colony
    void DeleteColonyItem(uint32 itemID)
    {
        InventoryItemRef iRef = sItemFactory.GetItemRef(itemID);
        if (iRef.get() != nullptr) {
            iRef->Delete();
        } else {
            ItemDB::DeleteItem(itemID);
        }
    }

    double GetNumber(PyRep* rep)
    {
        if (rep == nullptr)
            return 0;
        if (rep->IsFloat())
            return rep->AsFloat()->value();
        return (double)PyRep::IntegerValue(rep);
    }
}

PlanetMgr::PlanetMgr(Client* pClient, PlanetSE* pPlanet, Colony* pColony) :
 m_client(pClient),
 m_colony(pColony),
 m_planet(pPlanet),
 m_charID(pClient->GetCharacterID()),
 m_now(0),
 m_cost(0),
 m_sim(nullptr),
 m_nextPinID(ProvisionalID),
 m_nextRouteID(ProvisionalID),
 m_newCommandCenter(0)
{
}

PlanetMgr::~PlanetMgr()
{
    SafeDelete(m_sim);
}

PyRep* PlanetMgr::GetArg(PyTuple* args, size_t index)
{
    if ((args == nullptr) or (index >= args->size()))
        throw CustomError ("Malformed planet command.");
    return args->GetItem(index);
}

uint32 PlanetMgr::GetPinID(PyRep* id)
{
    if (id->IsInt() or id->IsLong())
        return PyRep::IntegerValueU32(id);
    if (id->IsTuple() and (id->AsTuple()->size() == 2)) {
        std::pair<int64, int64> key(PyRep::IntegerValue(id->AsTuple()->GetItem(0)), PyRep::IntegerValue(id->AsTuple()->GetItem(1)));
        std::map<std::pair<int64, int64>, uint32>::const_iterator itr = m_tempPins.find(key);
        if (itr != m_tempPins.end())
            return itr->second;
    }
    throw UserError("PinDoesNotExist");
}

uint32 PlanetMgr::GetRouteID(PyRep* id)
{
    if (id->IsInt() or id->IsLong())
        return PyRep::IntegerValueU32(id);
    if (id->IsTuple() and (id->AsTuple()->size() == 2)) {
        std::pair<int64, int64> key(PyRep::IntegerValue(id->AsTuple()->GetItem(0)), PyRep::IntegerValue(id->AsTuple()->GetItem(1)));
        std::map<std::pair<int64, int64>, uint32>::const_iterator itr = m_tempRoutes.find(key);
        if (itr != m_tempRoutes.end())
            return itr->second;
    }
    throw UserError("RouteDoesNotExist");
}

PyRep* PlanetMgr::UpdateNetwork(PyList* commandList)
{
    using namespace PI;
    // the colony as of now, and a copy the commands work on
    m_colony->SimulateToNow();
    m_now = GetFileTimeNow();
    m_work = m_colony->GetData();
    m_work.ownerID = m_charID;
    m_work.planetID = m_planet->GetID();
    if (m_work.currentSimTime == 0)
        m_work.currentSimTime = m_now;
    m_sim = new ColonySim(m_work, m_planet->GetRadius(), m_planet->GetTypeID());
    m_sim->RebuildIndices();

    for (size_t i = 0; i < commandList->size(); ++i) {
        PyRep* rep = commandList->GetItem(i);
        if ((rep == nullptr) or !rep->IsTuple() or (rep->AsTuple()->size() < 2) or !rep->AsTuple()->GetItem(1)->IsTuple()) {
            _log(PLANET__ERROR, "PlanetMgr::UpdateNetwork() - malformed command %u", i);
            commandList->Dump(PLANET__WARNING, "      ");
            throw CustomError ("Malformed planet command.");
        }
        int32 command = PyRep::IntegerValue(rep->AsTuple()->GetItem(0));
        PyTuple* args = rep->AsTuple()->GetItem(1)->AsTuple();
        _log(PLANET__TRACE, "PlanetMgr::UpdateNetwork() - %s: command %u, %s(%i)", m_client->GetName(), i, sPlanetDataMgr.GetCommandName(command), command);
        switch (command) {
            case Command::CreatePin:                CreatePin(args);            break;
            case Command::RemovePin:                RemovePin(args);            break;
            case Command::CreateLink:               CreateLink(args);           break;
            case Command::RemoveLink:               RemoveLink(args);           break;
            case Command::SetLinkLevel:             SetLinkLevel(args);         break;
            case Command::CreateRoute:              CreateRoute(args);          break;
            case Command::RemoveRoute:              RemoveRoute(args);          break;
            case Command::SetSchematic:             SetSchematic(args);         break;
            case Command::UpgradeCommandCenter:     UpgradeCommandCenter(args); break;
            case Command::AddExtractorHead:         AddExtractorHead(args);     break;
            case Command::KillExtractorHead:        KillExtractorHead(args);    break;
            case Command::MoveExtractorHead:        MoveExtractorHead(args);    break;
            case Command::InstallProgram:           InstallProgram(args);       break;
            default: {
                // route priorities are not in the Crucible command stream
                _log(PLANET__ERROR, "PlanetMgr::UpdateNetwork() - %s command %i not supported", sPlanetDataMgr.GetCommandName(command), command);
                throw CustomError ("Planet command %i is not supported.", command);
            }
        }
    }

    Commit();
    return m_colony->GetColony();
}

void PlanetMgr::ValidateCommandCenter(uint32 itemID, uint16 typeID)
{
    // the command center is the item from the ship (ClientPlanet.CreateCommandPin)
    InventoryItemRef iRef = sItemFactory.GetItemRef(itemID);
    if ((iRef.get() == nullptr) or (iRef->typeID() != typeID) or (iRef->ownerID() != m_charID)
    or  (m_client->GetShipID() == 0) or (iRef->locationID() != m_client->GetShipID())
    or  ((iRef->flag() != flagCargoHold) and (iRef->flag() != flagCommandCenterHold)))
        throw UserError("CannotBuildCommandPin");

    // colonies are refused in the trade hubs
    if (sConfig.cosmic.PIRestrictTradeHubs) {
        const std::string& systemName = m_planet->SystemMgr()->GetNameStr();
        for (auto cur : BlackListedSystems)
            if (systemName == cur)
                throw UserError("PlanetBlackListed").AddLocationName("planet", m_planet->GetID());
    }

    // ClientColony.PostValidateCreatePin
    uint32 planets = PlanetDB::CountPlanetsForChar(m_charID);
    if ((planets > 0) and (m_client->GetAccountRole() & Acct::Role::GML) == 0) {
        int8 level = m_client->GetChar()->GetSkillLevel(EvESkill::InterplanetaryConsolidation);
        if (level < 1)
            throw UserError("CannotPlaceCommandCenterNotEnoughSkill").AddAmount("maxPlanets", 1);
        if ((uint32)level < planets)
            throw UserError("CannotPlaceCommandCenterNotEnoughSkill").AddAmount("maxPlanets", (int)planets);
    }
    uint32 sovAllianceID = svDataMgr.GetSystemAllianceID(m_planet->SystemMgr()->GetID());
    if ((sovAllianceID != 0) and (sovAllianceID != (uint32)m_client->GetAllianceID()))
        throw UserError("SovPreventsCommandPin");
}

void PlanetMgr::CheckRequiredSkills(uint16 typeID)
{
    // ClientColony.PostValidateCreatePin
    static const uint16 skillAttrs[3][2] = {
        { AttrRequiredSkill1, AttrRequiredSkill1Level },
        { AttrRequiredSkill2, AttrRequiredSkill2Level },
        { AttrRequiredSkill3, AttrRequiredSkill3Level }
    };
    std::string lackingSkills;
    for (auto cur : skillAttrs) {
        uint16 skillID = (uint16)sPIDataMgr.GetTypeAttributeNoDefault(typeID, cur[0]);
        if (skillID == 0)
            continue;
        uint8 level = (uint8)sPIDataMgr.GetTypeAttributeNoDefault(typeID, cur[1]);
        if (m_client->GetChar()->GetSkillLevel(skillID) >= level)
            continue;
        if (!lackingSkills.empty())
            lackingSkills += ", ";
        Inv::TypeData data = Inv::TypeData();
        sDataMgr.GetType(skillID, data);
        lackingSkills += data.name;
        if (level)
            lackingSkills += " Level " + std::to_string(level);
    }
    if (!lackingSkills.empty()) {
        Inv::TypeData data = Inv::TypeData();
        sDataMgr.GetType(typeID, data);
        throw UserError("CannotPlacePinNotEnoughSkill")
                .AddFormatValue("requiredSkills", new PyString(lackingSkills))
                .AddFormatValue("itemName", new PyString(data.name));
    }
}

void PlanetMgr::CreatePin(PyTuple* args)
{
    // (pinID, typeID, latitude, longitude)
    PyRep* id = GetArg(args, 0);
    uint16 typeID = PyRep::IntegerValue(GetArg(args, 1));
    double latitude = GetNumber(GetArg(args, 2));
    double longitude = GetNumber(GetArg(args, 3));

    uint8 kind = sPIDataMgr.GetPinKind(typeID);
    if (kind == PI::Pin::Kind::Base) {
        // planetary bases are DUST
        _log(PLANET__ERROR, "PlanetMgr::CreatePin() Planet Bases (type %u) not supported.", typeID);
        throw UserError("InvalidPinType");
    }

    if (kind == PI::Pin::Kind::CommandCenter) {
        if (!id->IsInt() and !id->IsLong())
            throw UserError("CannotBuildCommandPin");
        uint32 itemID = PyRep::IntegerValueU32(id);
        m_sim->ValidateCreatePin(m_charID, typeID, latitude, longitude);
        ValidateCommandCenter(itemID, typeID);
        CheckRequiredSkills(typeID);
        if (m_sim->PrimePin(itemID, typeID, m_charID, latitude, longitude) == nullptr)
            throw UserError("InvalidPinType");
        // ClientPlanet.CreateCommandPin:  the colony level is the command center's meta level
        m_sim->SetLevel((uint8)std::min(sPIDataMgr.GetTypeAttribute(typeID, AttrMetaLevel), (double)PI::Sim::MaxCommandUpgradeLevel));
        m_newCommandCenter = itemID;
        return;
    }

    if (!id->IsTuple() or (id->AsTuple()->size() != 2))
        throw UserError("InvalidPinType");
    m_sim->ValidateCreatePin(m_charID, typeID, latitude, longitude);
    CheckRequiredSkills(typeID);

    uint32 pinID = m_nextPinID++;
    if (m_sim->PrimePin(pinID, typeID, m_charID, latitude, longitude) == nullptr)
        throw UserError("InvalidPinType");
    m_tempPins[std::make_pair(PyRep::IntegerValue(id->AsTuple()->GetItem(0)), PyRep::IntegerValue(id->AsTuple()->GetItem(1)))] = pinID;
    // ClientColony:  new pins (not the command center) cost their base price
    m_cost += sPIDataMgr.GetBasePrice(typeID);
}

void PlanetMgr::RemovePin(PyTuple* args)
{
    // (pinID,)
    PyRep* id = GetArg(args, 0);
    if (!m_sim->HasCommandPin() and (id->IsInt() or id->IsLong()) and (m_sim->GetPin(PyRep::IntegerValueU32(id)) == nullptr))
        return;     // a command center placed and removed again in the same edit session
    uint32 pinID = GetPinID(id);
    m_sim->ValidateRemovePin(m_charID, pinID);
    if (pinID == m_work.ccPinID)
        throw CustomError ("Abandon the colony to remove its command center.");

    // the items of the pin and its links go at commit
    PI_Pin* pPin = m_sim->GetPin(pinID);
    if (pinID < ProvisionalID) {
        m_removedItems.insert(pinID);
    } else {
        // ClientColony.RemoveCreationCost
        m_cost = std::max(0.0, m_cost - sPIDataMgr.GetBasePrice(pPin->typeID));
    }
    std::map<uint32, std::vector<uint32>>::const_iterator itr = m_work.linksByEndpoint.find(pinID);
    if (itr != m_work.linksByEndpoint.end())
        for (auto cur : itr->second) {
            PI_Link* pLink = m_sim->GetLink(pinID, cur);
            if ((pLink != nullptr) and pLink->linkID)
                m_removedItems.insert(pLink->linkID);
        }
    m_sim->RemovePin(pinID);
}

void PlanetMgr::CreateLink(PyTuple* args)
{
    // (endpoint1, endpoint2, level)
    uint32 endpoint1 = GetPinID(GetArg(args, 0));
    uint32 endpoint2 = GetPinID(GetArg(args, 1));
    uint8 level = (uint8)PyRep::IntegerValue(GetArg(args, 2));
    m_sim->ValidateCreateLink(m_charID, endpoint1, endpoint2, PI::Sim::LinkTypeID, level);
    m_sim->PrimeLink(PI::Sim::LinkTypeID, endpoint1, endpoint2, level);
}

void PlanetMgr::RemoveLink(PyTuple* args)
{
    // (endpoint1, endpoint2)
    uint32 endpoint1 = GetPinID(GetArg(args, 0));
    uint32 endpoint2 = GetPinID(GetArg(args, 1));
    m_sim->ValidateRemoveLink(m_charID, endpoint1, endpoint2);
    PI_Link* pLink = m_sim->GetLink(endpoint1, endpoint2);
    if (pLink->linkID)
        m_removedItems.insert(pLink->linkID);
    m_sim->RemoveLink(endpoint1, endpoint2);
}

void PlanetMgr::SetLinkLevel(PyTuple* args)
{
    // (endpoint1, endpoint2, level)
    uint32 endpoint1 = GetPinID(GetArg(args, 0));
    uint32 endpoint2 = GetPinID(GetArg(args, 1));
    int64 level = PyRep::IntegerValue(GetArg(args, 2));
    if (level < 0)
        throw UserError("PinDoesNotExist");
    m_sim->ValidateSetLinkLevel(m_charID, endpoint1, endpoint2, (uint8)std::min(level, (int64)UINT8_MAX));
    m_sim->SetLinkLevel(endpoint1, endpoint2, (uint8)level);
}

void PlanetMgr::CreateRoute(PyTuple* args)
{
    // (routeID, path, typeID, quantity)
    PyRep* id = GetArg(args, 0);
    PyRep* pathRep = GetArg(args, 1);
    uint16 typeID = PyRep::IntegerValue(GetArg(args, 2));
    int64 quantity = (int64)GetNumber(GetArg(args, 3));
    if (!id->IsTuple() or (id->AsTuple()->size() != 2))
        throw UserError("RouteDoesNotExist");
    if (!pathRep->IsList() and !pathRep->IsTuple())
        throw UserError("CreateRouteTooShort");
    std::vector<uint32> path;
    if (pathRep->IsList()) {
        for (size_t i = 0; i < pathRep->AsList()->size(); ++i)
            path.push_back(GetPinID(pathRep->AsList()->GetItem(i)));
    } else {
        for (size_t i = 0; i < pathRep->AsTuple()->size(); ++i)
            path.push_back(GetPinID(pathRep->AsTuple()->GetItem(i)));
    }
    if (quantity < 1)
        throw UserError("CreateRouteWithoutCommodities");
    m_sim->ValidateCreateRoute(m_charID, path, typeID, (uint32)std::min(quantity, (int64)UINT32_MAX));

    uint32 routeID = m_nextRouteID++;
    m_sim->PrimeRoute(routeID, path, typeID, (uint32)quantity);
    m_tempRoutes[std::make_pair(PyRep::IntegerValue(id->AsTuple()->GetItem(0)), PyRep::IntegerValue(id->AsTuple()->GetItem(1)))] = routeID;
    m_sim->OnRouteCreated(routeID);
}

void PlanetMgr::RemoveRoute(PyTuple* args)
{
    // (routeID,)
    uint32 routeID = GetRouteID(GetArg(args, 0));
    m_sim->ValidateRemoveRoute(m_charID, routeID);
    m_sim->RemoveRoute(routeID);
}

void PlanetMgr::SetSchematic(PyTuple* args)
{
    // (pinID, schematicID)
    uint32 pinID = GetPinID(GetArg(args, 0));
    uint16 schematicID = PyRep::IntegerValue(GetArg(args, 1));
    m_sim->ValidateInstallSchematic(m_charID, pinID, schematicID);
    m_sim->InstallSchematicForPin(pinID, schematicID);
}

void PlanetMgr::UpgradeCommandCenter(PyTuple* args)
{
    // (pinID, level)
    int64 level = PyRep::IntegerValue(GetArg(args, 1));
    if ((level < 0) or (level > PI::Sim::MaxCommandUpgradeLevel))
        throw CustomError ("Trying to upgrade past maximum level");
    m_sim->ValidateCommandCenterUpgrade((uint8)level);
    // ClientColony.PostValidateCommandCenterUpgrade
    int8 skillLevel = m_client->GetChar()->GetSkillLevel(EvESkill::CommandCenterUpgrades);
    if (level > skillLevel)
        throw UserError("CantUpgradeCommandCenterSkillRequired")
                .AddTypeName("skillName", EvESkill::CommandCenterUpgrades)
                .AddAmount("currentLevel", (int)skillLevel)
                .AddAmount("requestedLevel", (int)level);
    m_cost += PI::Sim::GetUpgradeCost(m_work.level, (uint8)level);
    m_sim->SetLevel((uint8)level);
}

void PlanetMgr::AddExtractorHead(PyTuple* args)
{
    // (pinID, headID, latitude, longitude)
    uint32 pinID = GetPinID(GetArg(args, 0));
    int64 headID = PyRep::IntegerValue(GetArg(args, 1));
    double latitude = GetNumber(GetArg(args, 2));
    double longitude = GetNumber(GetArg(args, 3));
    if ((headID < 0) or (headID > UINT16_MAX))
        throw UserError("CannotPlaceHeadLimitReached");
    m_sim->ValidateAddExtractorHead(pinID, (uint16)headID, latitude, longitude);
    m_sim->AddExtractorHead(pinID, (uint16)headID, latitude, longitude);
}

void PlanetMgr::KillExtractorHead(PyTuple* args)
{
    // (pinID, headID)
    uint32 pinID = GetPinID(GetArg(args, 0));
    int64 headID = PyRep::IntegerValue(GetArg(args, 1));
    if ((headID < 0) or (headID > UINT16_MAX))
        throw UserError("CannotRemoveHeadNotPresent");
    m_sim->ValidateRemoveExtractorHead(pinID, (uint16)headID);
    m_sim->RemoveExtractorHead(pinID, (uint16)headID);
}

void PlanetMgr::MoveExtractorHead(PyTuple* args)
{
    // (pinID, headID, latitude, longitude)
    uint32 pinID = GetPinID(GetArg(args, 0));
    int64 headID = PyRep::IntegerValue(GetArg(args, 1));
    double latitude = GetNumber(GetArg(args, 2));
    double longitude = GetNumber(GetArg(args, 3));
    if ((headID < 0) or (headID > UINT16_MAX))
        throw UserError("CannotMoveHeadNotPresent");
    m_sim->ValidateMoveExtractorHead(pinID, (uint16)headID, latitude, longitude);
    m_sim->MoveExtractorHead(pinID, (uint16)headID, latitude, longitude);
}

void PlanetMgr::InstallProgram(PyTuple* args)
{
    // (pinID, typeID | None, headRadius)
    uint32 pinID = GetPinID(GetArg(args, 0));
    PyRep* typeRep = GetArg(args, 1);
    uint16 typeID = (typeRep->IsNone() ? 0 : (uint16)PyRep::IntegerValue(typeRep));
    double headRadius = GetNumber(GetArg(args, 2));
    m_sim->ValidateInstallProgram(pinID, typeID, headRadius);
    // ClientColony.PostValidateInstallProgram:  a running program can only be changed after the cooldown
    if (pinID < ProvisionalID)
        ColonySim::CanInstallProgram(*m_sim->GetPin(pinID), m_now);
    if ((typeID != 0) and !m_planet->IsResourceType(typeID))
        throw CustomError ("%s is not found on this planet.", sPIDataMgr.GetProductName(typeID));
    m_sim->InstallProgram(pinID, typeID, headRadius, m_planet->GetResourceField(typeID), m_now);
}

void PlanetMgr::Commit()
{
    // CannotBuildNotEnoughCash  (ClientColony.PostValidateCreatePin, checked by the client against the sum of the session's costs)
    if ((m_cost > 0) and (m_client->GetBalance() < m_cost))
        throw UserError("CannotBuildNotEnoughCash").AddISK("cost", m_cost);

    if (m_work.ccPinID == 0) {
        // nothing was built (a command center placed and removed again)
        return;
    }

    // the command center is the item from the ship.  a stack is split, one goes down to the planet
    if (m_newCommandCenter) {
        ValidateCommandCenter(m_newCommandCenter, m_work.pins[m_newCommandCenter].typeID);
        InventoryItemRef iRef = sItemFactory.GetItemRef(m_newCommandCenter);
        if (iRef->quantity() > 1) {
            ItemData data(iRef->typeID(), m_charID, locTemp, flagNone, iRef->quantity() - 1);
            InventoryItemRef iRef2 = sItemFactory.SpawnItem(data);
            if (iRef2.get() != nullptr)
                iRef2->Move(iRef->locationID(), iRef->flag(), true);
            iRef->SetQuantity(1, true);
        }
        iRef->Move(m_planet->GetID(), flagPlanetSurface, true);
        iRef->ChangeSingleton(true);
    }

    // new pins become items
    std::map<uint32, uint32> pinIDs, routeIDs;
    for (auto& cur : m_work.pins) {
        if (cur.first < ProvisionalID)
            continue;
        ItemData data(cur.second.typeID, m_charID, locTemp, flagNone, 1);
        InventoryItemRef iRef = sItemFactory.SpawnItem(data);
        if (iRef.get() == nullptr)
            throw CustomError ("Unable to build %s.", sPIDataMgr.GetProductName(cur.second.typeID));
        iRef->Move(m_planet->GetID(), flagPlanetSurface, true);
        iRef->ChangeSingleton(true);
        pinIDs[cur.first] = iRef->itemID();
    }
    m_sim->RemapIDs(pinIDs, routeIDs);

    // new routes get their IDs from piRoutes
    for (auto& cur : m_work.routes) {
        if (cur.first < ProvisionalID)
            continue;
        uint32 routeID = m_db.InsertRoute(m_work.ccPinID, cur.second);
        if (routeID == 0)
            throw CustomError ("Unable to save the route.");
        routeIDs[cur.first] = routeID;
    }
    pinIDs.clear();
    m_sim->RemapIDs(pinIDs, routeIDs);

    // new links become items
    for (auto& cur : m_work.links) {
        if (cur.second.linkID)
            continue;
        ItemData data(cur.second.typeID, m_charID, locTemp, flagNone, 1);
        InventoryItemRef iRef = sItemFactory.SpawnItem(data);
        if (iRef.get() == nullptr)
            throw CustomError ("Unable to build the link.");
        iRef->Move(m_planet->GetID(), flagPlanetSurface, true);
        iRef->ChangeSingleton(true);
        cur.second.linkID = iRef->itemID();
    }

    for (auto cur : m_removedItems)
        DeleteColonyItem(cur);

    m_work.currentSimTime = m_now;
    m_colony->Commit(m_work);

    // every planet with a colony has a customs office
    if (m_newCommandCenter and (m_planet->GetCustomsOffice() == nullptr))
        m_planet->CreateCustomsOffice();

    if (m_cost > 0) {
        std::string reason = "DESC:  Planetary construction on ";
        reason += m_planet->GetName();
        AccountService::TransferFunds(m_charID, m_colony->GetNPCOwner(), m_cost, reason.c_str(), Journal::EntryType::PlanetaryConstruction, m_planet->GetID());
    }

    _log(PLANET__TRACE, "PlanetMgr::Commit() - %s updated colony %u on %s:  %u pins, %u links, %u routes, cost %.2f", \
            m_client->GetName(), m_work.ccPinID, m_planet->GetName(), m_work.pins.size(), m_work.links.size(), m_work.routes.size(), m_cost);
}
