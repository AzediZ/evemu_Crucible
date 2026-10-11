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
    Author:        Cometo (basic system idea)
    Updates:    Allan   (mostly complete and working - 27Dec16)
    Major Update:   Allan  process code rewrite/optimized 19Jul19
    Rewrite:    F19 - colonies run the client's shared simulation (ColonySim), lazily
*/

/*
 * COLONY__ERROR
 * COLONY__WARNING
 * COLONY__MESSAGE
 * COLONY__DEBUG
 * COLONY__INFO
 * COLONY__TRACE
 * COLONY__DUMP
 * COLONY__GC_DUMP
 * COLONY__DB_ERROR
 */

#include "eve-server.h"


#include "Client.h"
#include "ship/Ship.h"
#include "inventory/ItemDB.h"
#include "inventory/ItemType.h"
#include "planet/Colony.h"
#include "planet/CustomsOffice.h"
#include "planet/Planet.h"
#include "planet/PlanetDataMgr.h"
// fund xfer and journal logging methods
#include "account/AccountService.h"
#include "system/Container.h"
#include "system/CrimeWatch.h"
#include "system/SystemManager.h"
#include "system/SystemBubble.h"

/* The list of systems without colonies (PlanetBlackListed) is in PlanetMgr.cpp */

namespace {
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

    PyString* FmtVolume(double volume)
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.2f", volume);
        return new PyString(buf);
    }

    PyRep* NewTime(int64 time)
    {
        return (time > 0 ? (PyRep*)new PyLong(time) : PyStatic.NewNone());
    }
}

Colony::Colony(EVEServiceManager& mgr, uint32 ownerID, PlanetSE* pSE)
:m_svcMgr(mgr),
m_pSE(pSE),
m_ownerID(ownerID)
{
    m_colony.ownerID = ownerID;
    m_colony.planetID = pSE->GetID();
    _log(COLONY__DEBUG, "Colony::Colony() c'tor called for %s(%u) owner %u", pSE->GetName(), pSE->GetID(), ownerID);
}

Colony::~Colony()
{
}

void Colony::Init()
{
    m_colony.Init();
    m_colony.ownerID = m_ownerID;
    m_colony.planetID = m_pSE->GetID();
    if (!m_db.LoadColony(m_ownerID, m_pSE->GetID(), m_colony))
        return;
    PrepareLoadedData();
}

void Colony::PrepareLoadedData()
{
    // ColonyData.RestorePinFromRow
    for (auto& cur : m_colony.pins) {
        PI_Pin& pin = cur.second;
        pin.kind = sPIDataMgr.GetPinKind(pin.typeID);
        std::map<uint16, uint32> contents = pin.contents;
        switch (pin.kind) {
            case PI::Pin::Kind::Process: {
                const PI_Schematic* pSchematic = (pin.schematicID ? sPIDataMgr.GetSchematic(pin.schematicID) : nullptr);
                if (pSchematic != nullptr) {
                    ColonySim::SetSchematic(pin, *pSchematic);
                } else {
                    pin.schematicID = 0;
                    pin.cycleTime = 0;
                    pin.demands.clear();
                    pin.products.clear();
                    // a processor without a schematic can't be running
                    if (pin.state > PI::Pin::State::Idle)
                        pin.state = PI::Pin::State::Idle;
                }
            } break;
            case PI::Pin::Kind::ECU: {
                // EcuPin.InstallProgram refuses programs without output
                if ((pin.programType == 0) or (pin.qtyPerCycle == 0) or (pin.cycleTime <= 0)) {
                    int8 state = pin.state;
                    ColonySim::ClearProgram(pin);
                    pin.state = (state > PI::Pin::State::Idle ? (int8)PI::Pin::State::Idle : state);
                }
            } break;
            case PI::Pin::Kind::CommandCenter:
            case PI::Pin::Kind::Spaceport:
            case PI::Pin::Kind::Storage:
                break;
            default: {
                _log(COLONY__ERROR, "Colony::PrepareLoadedData() - pin %u of colony %u has invalid type %u", cur.first, m_colony.ccPinID, pin.typeID);
            } break;
        }
        ColonySim::SetContents(pin, contents);
        for (auto& head : pin.heads)
            head.second.ecuPinID = cur.first;
    }

    ColonySim sim(m_colony, GetPlanetRadius(), GetPlanetTypeID());
    sim.RebuildIndices();
    if (m_colony.currentSimTime == 0)
        sim.RecalculateCurrentSimTime(GetFileTimeNow());
}

void Colony::Save()
{
    if (!HasColony())
        return;
    m_db.SaveColony(m_colony, m_pSE->SystemMgr()->GetID(), GetPlanetTypeID());
}

void Colony::SimulateToNow()
{
    if (!HasColony())
        return;

    double profileStartTime(GetTimeUSeconds());
    ColonySim sim(m_colony, GetPlanetRadius(), GetPlanetTypeID());
    sim.RunSimulation(GetFileTimeNow());
    Save();

    if (sConfig.debug.UseProfiling)
        sProfiler.AddTime(Profile::colony, GetTimeUSeconds() - profileStartTime);
    _log(COLONY__INFO, "Colony::SimulateToNow() - colony %u of %u simulated and saved in %.3fus (%u pins, %u links, %u routes)", \
            m_colony.ccPinID, m_ownerID, GetTimeUSeconds() - profileStartTime, m_colony.pins.size(), m_colony.links.size(), m_colony.routes.size());
}

void Colony::Commit(const PI_CCPin& work)
{
    m_colony = work;
    m_colony.ownerID = m_ownerID;
    m_colony.planetID = m_pSE->GetID();
    ColonySim sim(m_colony, GetPlanetRadius(), GetPlanetTypeID());
    sim.RebuildIndices();
    Save();
}

double Colony::GetPlanetRadius()
{
    return m_pSE->GetRadius();
}

uint16 Colony::GetPlanetTypeID()
{
    return m_pSE->GetTypeID();
}

ColonySim::ResourceField Colony::GetResourceField(uint16 typeID)
{
    return m_pSE->GetResourceField(typeID);
}

uint32 Colony::GetNPCOwner()
{
    if (CrimeWatch::SecurityClass(m_pSE->SystemMgr()->GetSystemSecurityRating()) == Crime::SecClass::High)
        return corpCONCORD;
    return corpInterbus;
}

void Colony::AbandonColony(Client* pClient)
{
    if (!HasColony())
        return;
    // the pin and link items go with the colony
    for (auto& cur : m_colony.pins)
        DeleteColonyItem(cur.first);
    for (auto& cur : m_colony.links)
        if (cur.second.linkID)
            DeleteColonyItem(cur.second.linkID);
    m_db.DeleteColony(m_colony.ccPinID, m_pSE->GetID(), m_ownerID);
    _log(COLONY__INFO, "Colony::AbandonColony() - %u abandoned colony %u on %s", m_ownerID, m_colony.ccPinID, m_pSE->GetName());
    m_colony.Init();
    m_colony.ownerID = m_ownerID;
    m_colony.planetID = m_pSE->GetID();
}

/* ----------------------------------------------------------------------------------------------------------------
 *  serialization - the client's ColonyData.RestorePinFromRow / RestoreLinkFromRow / RestoreRouteFromRow read these
 */

void Colony::SerializePins(PyList* list)
{
    for (auto& cur : m_colony.pins) {
        const PI_Pin& pin = cur.second;
        // BasePin.Serialize
        PyDict* dict = new PyDict();
        dict->SetItemString("id", new PyInt(cur.first));
        dict->SetItemString("typeID", new PyInt(pin.typeID));
        dict->SetItemString("ownerID", new PyInt(pin.ownerID));
        dict->SetItemString("latitude", new PyFloat(pin.latitude));
        dict->SetItemString("longitude", new PyFloat(pin.longitude));
        dict->SetItemString("lastRunTime", NewTime(pin.lastRunTime));
        dict->SetItemString("state", new PyInt(pin.state));
        PyDict* contents = new PyDict();
        for (auto cont : pin.contents)
            contents->SetItem(new PyInt(cont.first), new PyInt(cont.second));
        dict->SetItemString("contents", contents);

        switch (pin.kind) {
            case PI::Pin::Kind::Process: {
                // ProcessPin.Serialize
                dict->SetItemString("schematicID", (pin.schematicID ? (PyRep*)new PyInt(pin.schematicID) : PyStatic.NewNone()));
                dict->SetItemString("hasReceivedInputs", new PyBool(pin.hasReceivedInputs));
                dict->SetItemString("receivedInputsLastCycle", new PyBool(pin.receivedInputsLastCycle));
            } break;
            case PI::Pin::Kind::CommandCenter:
            case PI::Pin::Kind::Spaceport: {
                // SpaceportPin.Serialize
                dict->SetItemString("lastLaunchTime", NewTime(pin.lastLaunchTime));
            } break;
            case PI::Pin::Kind::ECU: {
                // EcuPin.Serialize
                bool program = (pin.programType != 0);
                dict->SetItemString("cycleTime", (pin.cycleTime > 0 ? (PyRep*)new PyLong(pin.cycleTime) : PyStatic.NewNone()));
                dict->SetItemString("programType", (program ? (PyRep*)new PyInt(pin.programType) : PyStatic.NewNone()));
                dict->SetItemString("qtyPerCycle", new PyInt(pin.qtyPerCycle));
                dict->SetItemString("expiryTime", (program ? NewTime(pin.expiryTime) : PyStatic.NewNone()));
                dict->SetItemString("installTime", (program ? NewTime(pin.installTime) : PyStatic.NewNone()));
                dict->SetItemString("headRadius", (pin.headRadius > 0 ? (PyRep*)new PyFloat(pin.headRadius) : PyStatic.NewNone()));
                PyList* heads = new PyList();
                for (auto head : pin.heads) {
                    PyTuple* tuple = new PyTuple(3);
                        tuple->SetItem(0, new PyInt(head.first));
                        tuple->SetItem(1, new PyFloat(head.second.latitude));
                        tuple->SetItem(2, new PyFloat(head.second.longitude));
                    heads->AddItem(tuple);
                }
                dict->SetItemString("heads", heads);
            } break;
        }
        list->AddItem(new PyObject("util.KeyVal", dict));
    }
}

void Colony::SerializeLinks(PyList* list)
{
    // BaseLink.Serialize
    for (auto& cur : m_colony.links) {
        PyDict* dict = new PyDict();
            dict->SetItemString("typeID", new PyInt(cur.second.typeID));
            dict->SetItemString("endpoint1", new PyInt(cur.second.endpoint1));
            dict->SetItemString("endpoint2", new PyInt(cur.second.endpoint2));
            dict->SetItemString("level", new PyInt(cur.second.level));
        list->AddItem(new PyObject("util.KeyVal", dict));
    }
}

void Colony::SerializeRoutes(PyList* list)
{
    // BaseRoute.Serialize, in ascending routeID (routesByDestination order on the client)
    for (auto& cur : m_colony.routes) {
        PyList* path = new PyList();
        for (auto id : cur.second.path)
            path->AddItem(new PyInt(id));
        PyDict* dict = new PyDict();
            dict->SetItemString("routeID", new PyInt(cur.first));
            dict->SetItemString("charID", new PyInt(m_ownerID));
            dict->SetItemString("path", path);
            dict->SetItemString("commodityTypeID", new PyInt(cur.second.commodityTypeID));
            dict->SetItemString("commodityQuantity", new PyInt(cur.second.commodityQuantity));
        list->AddItem(new PyObject("util.KeyVal", dict));
    }
}

void Colony::SerializeInto(PyDict* args)
{
    PyList* pins = new PyList();
    PyList* links = new PyList();
    PyList* routes = new PyList();
    SerializePins(pins);
    SerializeLinks(links);
    SerializeRoutes(routes);
    args->SetItemString("ownerID", new PyInt(m_ownerID));
    args->SetItemString("level", new PyInt(m_colony.level));
    args->SetItemString("pins", pins);
    args->SetItemString("links", links);
    args->SetItemString("routes", routes);
    args->SetItemString("currentSimTime", NewTime(m_colony.currentSimTime));
}

PyRep* Colony::GetColony()
{
    // BaseColony.Serialize
    PyDict* args = new PyDict();
    SerializeInto(args);
    PyObject* res = new PyObject("util.KeyVal", args);

    if (is_log_enabled(COLONY__GC_DUMP)) {
        _log(COLONY__GC_DUMP, "Colony::GetColony() Dump");
        res->Dump(COLONY__GC_DUMP, "    ");
    }
    return res;
}

/* ----------------------------------------------------------------------------------------------------------------
 *  colony actions outside the edit mode
 */

PyRep* Colony::GetProgramResultInfo(PyRep* pinID, uint16 typeID, PyList* heads, double headRadius)
{
    // qtyToDistribute, cycleTime, numCycles = remoteHandler.GetProgramResultInfo(pinID, typeID, pin.heads, headRadius)
    //  the pin may be one placed in the current edit session (a temporary (1, n) id):  ecus all share the default ecu attributes
    if ((headRadius < PI::Sim::RadiusDrillAreaMin) or (headRadius > PI::Sim::RadiusDrillAreaMax))
        throw CustomError ("Cannot install a program with a completely bonkers radius");
    if (!m_pSE->IsResourceType(typeID))
        throw CustomError ("%s is not found on this planet.", sPIDataMgr.GetProductName(typeID));

    uint32 ecuPinID = 0;
    uint16 ecuTypeID = 0;
    if (pinID->IsInt() or pinID->IsLong()) {
        ecuPinID = PyRep::IntegerValueU32(pinID);
        std::map<uint32, PI_Pin>::const_iterator itr = m_colony.pins.find(ecuPinID);
        if (itr != m_colony.pins.end())
            ecuTypeID = itr->second.typeID;
    }

    std::vector<ColonySim::Head> points;
    for (size_t i = 0; i < heads->size(); ++i) {
        PyRep* rep = heads->GetItem(i);
        if ((rep == nullptr) or !rep->IsTuple() or (rep->AsTuple()->size() < 3))
            continue;
        PyTuple* tuple = rep->AsTuple();
        ColonySim::Head head = ColonySim::Head();
            head.id = PyRep::IntegerValue(tuple->GetItem(0));
            head.latitude = PyRep::FloatValue(tuple->GetItem(1));
            head.longitude = PyRep::FloatValue(tuple->GetItem(2));
        points.push_back(head);
        if (points.size() >= PI::Sim::EcuMaxHeads)
            break;
    }

    uint32 maxValue = 0, numCycles = 0;
    int64 cycleTime = 0;
    ColonySim sim(m_colony, GetPlanetRadius(), GetPlanetTypeID());
    sim.CreateProgram(GetResourceField(typeID), ecuPinID, ecuTypeID, typeID, points, headRadius, maxValue, cycleTime, numCycles);

    _log(PLANET__TRACE, "Colony::GetProgramResultInfo() - %s: %u heads, radius %.4f -> qtyPerCycle %u, cycleTime %lli, numCycles %u", \
            sPIDataMgr.GetProductName(typeID), points.size(), headRadius, maxValue, cycleTime, numCycles);

    PyTuple* res = new PyTuple(3);
        res->SetItem(0, new PyInt(maxValue));
        res->SetItem(1, new PyLong(cycleTime));
        res->SetItem(2, new PyInt(numCycles));
    return res;
}

PyRep* Colony::TransferCommodities(Client* pClient, const std::vector<uint32>& path, std::map< uint16, uint32 >& items)
{
    // simTime, sourceRunTime = UserTransferCommodities(path, commodities)
    //  the client runs its simulation to simTime, does the same expedited transfer and takes the source's run time from us
    if (!HasColony())
        throw UserError("CannotManagePlanetWithoutCommandCenter");
    SimulateToNow();

    int64 runTime = GetFileTimeNow();
    ColonySim sim(m_colony, GetPlanetRadius(), GetPlanetTypeID());
    double minBandwidth = sim.ValidateExpeditedTransfer(m_ownerID, path, items, runTime);
    sim.ExecuteExpeditedTransfer(path.front(), path.back(), items, minBandwidth, runTime);
    m_colony.currentSimTime = runTime;
    Save();

    PyTuple* res = new PyTuple(2);
        res->SetItem(0, new PyLong(runTime));
        res->SetItem(1, NewTime(sim.GetPin(path.front())->lastRunTime));
    return res;
}

PyRep* Colony::LaunchCommodities(Client* pClient, uint32 pinID, std::map< uint16, uint32 >& items)
{
    // lastLaunchTime = UserLaunchCommodities(commandPinID, commoditiesToLaunch)
    //  launches from the command center to a container in orbit, with the command center's export tax at the default rate
    if (!HasColony())
        throw UserError("CannotLaunchWithoutColony");
    if (items.empty())
        throw UserError("PleaseSelectCommoditiesToLaunch");
    SimulateToNow();

    ColonySim sim(m_colony, GetPlanetRadius(), GetPlanetTypeID());
    PI_Pin* pPin = sim.GetPin(pinID);
    if ((pPin == nullptr) or !ColonySim::IsCommandCenter(*pPin))
        throw UserError("CanOnlyLaunchFromCommandCenters");
    int64 now = GetFileTimeNow();
    if ((pPin->lastLaunchTime != 0) and (pPin->lastLaunchTime + PI::Sim::LaunchCycleTime > now))
        throw UserError("CannotLaunchCommandPinNotReady");
    // SpaceportPin.CanLaunch
    for (auto cur : items)
        if (pPin->contents.find(cur.first) == pPin->contents.end())
            throw UserError("CannotLaunchCommoditiesNotFound");

    // what will actually go (SpaceportPin.FetchLaunchCommodities), checked before anything is changed
    std::map<uint16, uint32> commodsToLaunch;
    for (auto cur : items) {
        uint32 qty = std::min(cur.second, pPin->contents[cur.first]);
        if (qty > 0)
            commodsToLaunch[cur.first] = qty;
    }
    if (commodsToLaunch.empty())
        throw UserError("CannotLaunchCommoditiesNotFound");
    double cost = sPIDataMgr.GetExportTax(pPin->typeID, commodsToLaunch, 0.05);
    if ((cost > 0) and (pClient->GetBalance() < cost))
        throw UserError("NotEnoughMoney")
                .AddISK("balance", pClient->GetBalance())
                .AddISK("amount", cost);
    double volume = sPIDataMgr.GetCommodityVolume(commodsToLaunch);
    if (volume > sPIDataMgr.GetCapacity(EVEDB::invTypes::PlanetaryLaunchContainer))
        throw CustomError ("That is more than a launch container can hold (%.2f m3).", sPIDataMgr.GetCapacity(EVEDB::invTypes::PlanetaryLaunchContainer));

    // the container goes into orbit around the planet.  launches decay after five days (Container)
    SystemManager* pSysMgr(m_pSE->SystemMgr());
    GPoint location(m_pSE->GetPosition());
    location.MakeRandomPointOnSphere(m_pSE->GetRadius() + 2000000);   //2000km orbit for launch can
    ItemData canData(EVEDB::invTypes::PlanetaryLaunchContainer, m_ownerID, pSysMgr->GetID(), flagNone, "PI Commodities Container", location);
    CargoContainerRef contRef = sItemFactory.SpawnCargoContainer(canData);
    if (contRef.get() == nullptr)
        throw CustomError ("Unable to spawn item of type %u.", EVEDB::invTypes::PlanetaryLaunchContainer);

    FactionData data = FactionData();
        data.allianceID = pClient->GetAllianceID();
        data.corporationID = pClient->GetCorporationID();
        data.factionID = pClient->GetWarFactionID();
        data.ownerID = m_ownerID;
    ContainerSE* cSE = new ContainerSE(contRef, m_svcMgr, pSysMgr, data);
    contRef->SetMySE(cSE);      // item-to-entity internal interface
    pSysMgr->AddEntity(cSE);

    for (auto cur : commodsToLaunch) {
        uint32 qty = ColonySim::RemoveCommodity(*pPin, cur.first, cur.second);
        if (qty < 1)
            continue;
        ItemData iData(cur.first, m_ownerID, locTemp, flagNone, qty);
        InventoryItemRef iRef = sItemFactory.SpawnItem(iData);
        if (iRef.get() == nullptr)
            continue;
        iRef->Move(contRef->itemID(), flagNone);
        iRef->SaveItem();
    }
    contRef->SaveItem();
    pPin->lastLaunchTime = now;
    m_colony.currentSimTime = now;
    m_db.SaveLaunch(contRef->itemID(), m_ownerID, pSysMgr->GetID(), m_pSE->GetID(), location);
    Save();

    if (cost > 0) {
        std::string reason = "DESC:  Launching PI items from ";
        reason += m_pSE->GetName();
        AccountService::TransferFunds(m_ownerID, GetNPCOwner(), cost, reason.c_str(), Journal::EntryType::PlanetaryExportTax, m_pSE->GetID());
    }

    _log(COLONY__TRACE, "Colony::LaunchCommodities() - %u launched %u types from command center %u to %s (%u)", \
            m_ownerID, commodsToLaunch.size(), pinID, contRef->name(), contRef->itemID());
    return new PyLong(pPin->lastLaunchTime);
}

void Colony::PlanetXfer(Client* pClient, CustomsSE* pCO, uint32 spaceportID, std::map< uint32, uint32 > importItems, std::map< uint16, uint32 > exportItems, double taxRate)
{
    // customsOfficeInventory.ImportExportWithPlanet(spaceportPinID, {itemID: qty}, {typeID: qty}, taxRate)
    //  import is from the office to the planet.  export is from the planet to the office.
    if (!HasColony())
        throw UserError("CannotImportEndpointNotFound");
    if (pCO->IsReinforced())
        throw CustomError ("The customs office is reinforced.  Nothing can be moved through it until it comes out of reinforcement.");
    // the client offers the office within transfer range (menusvc checkTransferDist)
    ShipSE* pShip(pClient->GetShipSE());
    if (!pClient->IsInSpace() or (pShip == nullptr) or (pShip->SystemMgr() != pCO->SystemMgr()))
        throw CustomError ("You are too far away from the customs office.");
    double maxDistance = std::max(2500.0, (double)pCO->GetSelf()->GetAttribute(AttrMaxOperationalDistance).get_float());
    double distance = GVector(pShip->GetPosition(), pCO->GetPosition()).length() - pShip->GetRadius() - pCO->GetRadius();
    if (distance > maxDistance)
        throw CustomError ("You are too far away from the customs office.");
    SimulateToNow();

    ColonySim sim(m_colony, GetPlanetRadius(), GetPlanetTypeID());
    PI_Pin* pPin = sim.GetPin(spaceportID);
    // BaseColony.GetImportEndpoints:  launchpads only
    if ((pPin == nullptr) or (pPin->kind != PI::Pin::Kind::Spaceport) or (pPin->ownerID != m_ownerID))
        throw UserError("CannotImportEndpointNotFound");

    double rate = pCO->GetTaxRate(pClient);
    if (rate < 0)
        throw UserError("PortStandingCheckFail").AddOwnerName("corpName", pCO->GetOwnerID());
    if (std::fabs(rate - taxRate) > 1e-5)
        throw UserError("TaxChanged");

    // imports:  the caller's own items from this office's hangar
    std::vector<std::pair<InventoryItemRef, uint32>> imports;
    std::map<uint16, uint32> importedByType;
    double importVolume = 0;
    for (auto cur : importItems) {
        if (cur.second < 1)
            continue;
        InventoryItemRef iRef = sItemFactory.GetItemRef(cur.first);
        if ((iRef.get() == nullptr) or (iRef->ownerID() != m_ownerID) or (iRef->locationID() != pCO->GetID()) or (iRef->flag() != flagHangar))
            throw CustomError ("You can only import your own items from this customs office.");
        if (cur.second > iRef->quantity())
            throw CustomError ("You do not have %u %s in this customs office.", cur.second, iRef->name());
        imports.push_back(std::make_pair(iRef, cur.second));
        importedByType[iRef->typeID()] += cur.second;
        importVolume += iRef->type().volume() * cur.second;
    }
    if ((importVolume > 0) and (pPin->capacityUsed + importVolume > ColonySim::GetCapacity(*pPin) + 1e-5))
        throw UserError("NotEnoughSpace")
                .AddFormatValue("volume", FmtVolume(importVolume))
                .AddFormatValue("available", FmtVolume(ColonySim::GetCapacity(*pPin) - pPin->capacityUsed));

    // exports:  what the launchpad holds
    double exportVolume = 0;
    for (auto cur : exportItems) {
        std::map<uint16, uint32>::const_iterator itr = pPin->contents.find(cur.first);
        if (itr == pPin->contents.end())
            throw UserError("CannotExportCommodityNotFound").AddTypeName("typeName", cur.first);
        if (cur.second > itr->second)
            throw UserError("CannotExportCommodityNotEnough")
                    .AddAmount("desired", (int)cur.second)
                    .AddAmount("contained", (int)itr->second)
                    .AddTypeName("typeName", cur.first);
        exportVolume += sPIDataMgr.GetVolume(cur.first) * cur.second;
    }
    // the office's room is per character
    if (exportVolume > 0) {
        double available = pCO->GetAvailableSpace(m_ownerID) + importVolume;
        if (exportVolume - available > 1e-5)
            throw UserError("NotEnoughSpace")
                    .AddFormatValue("volume", FmtVolume(exportVolume))
                    .AddFormatValue("available", FmtVolume(available));
    }

    // SpaceportPin.GetImportTax / GetExportTax, paid to the office's owner
    double importTax = sPIDataMgr.GetImportTax(pPin->typeID, importedByType, rate);
    double exportTax = sPIDataMgr.GetExportTax(pPin->typeID, exportItems, rate);
    if ((importTax + exportTax > 0) and (pClient->GetBalance() < importTax + exportTax))
        throw UserError("NotEnoughMoney")
                .AddISK("balance", pClient->GetBalance())
                .AddISK("amount", importTax + exportTax);

    for (auto cur : imports) {
        uint16 typeID = cur.first->typeID();
        if (cur.second < cur.first->quantity()) {
            // split the stack:  only the imported part goes to the planet
            cur.first->SetQuantity(cur.first->quantity() - cur.second, true);
        } else {
            cur.first->ToVirtual(spaceportID);
        }
        ColonySim::AddCommodity(*pPin, typeID, cur.second);
    }

    for (auto cur : exportItems) {
        uint32 qty = ColonySim::RemoveCommodity(*pPin, cur.first, cur.second);
        if (qty < 1)
            continue;
        pCO->AddCommodity(m_ownerID, cur.first, qty);
    }

    std::string reason;
    if (importTax > 0) {
        reason = "DESC:  Importing items to ";
        reason += m_pSE->GetName();
        AccountService::TransferFunds(m_ownerID, pCO->GetOwnerID(), importTax, reason.c_str(), Journal::EntryType::PlanetaryImportTax, m_pSE->GetID());
    }
    if (exportTax > 0) {
        reason = "DESC:  Exporting items from ";
        reason += m_pSE->GetName();
        AccountService::TransferFunds(m_ownerID, pCO->GetOwnerID(), exportTax, reason.c_str(), Journal::EntryType::PlanetaryExportTax, m_pSE->GetID());
    }

    // imports move on along the launchpad's routes
    int64 now = GetFileTimeNow();
    if (!importedByType.empty())
        sim.StimulateIdlePin(spaceportID);
    pPin = sim.GetPin(spaceportID);
    pPin->lastLaunchTime = now;
    m_colony.currentSimTime = now;
    Save();

    _log(COLONY__TRACE, "Colony::PlanetXfer() - %u: %u stacks imported to and %u types exported from launchpad %u through %u (tax %.2f + %.2f)", \
            m_ownerID, imports.size(), exportItems.size(), spaceportID, pCO->GetID(), importTax, exportTax);

    // the client's colony is stale now
    PyTuple* tuple = new PyTuple(2);
        tuple->SetItem(0, new PyInt(m_pSE->GetID()));
        tuple->SetItem(1, PyStatic.NewFalse());
    pClient->SendNotification("OnMajorPlanetStateUpdate", "clientID", &tuple);
}
