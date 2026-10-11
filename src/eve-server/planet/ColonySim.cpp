/**
 * @name ColonySim.cpp
 *   the client's shared planetary interaction code ported to the server.  see ColonySim.h
 *   every function names its python source.  the branch order is kept, so the server and the client agree.
 *
 * @Author:         Allan (original PI code), F19 port
 * @date:   October 2026
 */

/*
 * COLONY__ERROR
 * COLONY__WARNING
 * COLONY__DEBUG
 * COLONY__TRACE
 */

#include <algorithm>
#include <tuple>

#include "eve-server.h"

#include "planet/ColonySim.h"
#include "planet/PlanetDataMgr.h"


namespace {
    const int64 SEC = EvE::Time::Second;
    const int64 HOUR = EvE::Time::Hour;
    const double PI2 = 2.0 * M_PI;

    struct CommandCenterInfo {
        int32 powerOutput;
        int32 cpuOutput;
        int64 upgradeCost;
    };
    // planetCommon.commandCenterInfoPerLevel
    const CommandCenterInfo commandCenterInfoPerLevel[PI::Sim::MaxCommandUpgradeLevel + 1] = {
        {  6000,  1675,       0 },
        {  9000,  7057,  580000 },
        { 12000, 12136, 1510000 },
        { 15000, 17215, 2710000 },
        { 17000, 21315, 4210000 },
        { 19000, 25415, 6310000 }
    };

    // python's floor division for longs
    int64 FloorDiv(int64 a, int64 b)
    {
        int64 q = a / b;
        if (((a % b) != 0) and ((a < 0) != (b < 0)))
            --q;
        return q;
    }

    uint32 GetQty(const std::map<uint16, uint32>& contents, uint16 typeID)
    {
        std::map<uint16, uint32>::const_iterator itr = contents.find(typeID);
        return (itr == contents.end() ? 0 : itr->second);
    }
}

int32 PI::Sim::GetCPUOutput(uint8 level)
{
    return commandCenterInfoPerLevel[std::min(level, MaxCommandUpgradeLevel)].cpuOutput;
}

int32 PI::Sim::GetPowerOutput(uint8 level)
{
    return commandCenterInfoPerLevel[std::min(level, MaxCommandUpgradeLevel)].powerOutput;
}

int64 PI::Sim::GetUpgradeCost(uint8 currentLevel, uint8 desiredLevel)
{
    return commandCenterInfoPerLevel[std::min(desiredLevel, MaxCommandUpgradeLevel)].upgradeCost
         - commandCenterInfoPerLevel[std::min(currentLevel, MaxCommandUpgradeLevel)].upgradeCost;
}

double PI::Sim::GetProgramLengthFromHeadRadius(double headRadius)
{
    return (headRadius - RadiusDrillAreaMin) / RadiusDrillAreaDiff * 335 + 1;
}

double PI::Sim::GetCycleTimeFromProgramLength(double programLength)
{
    return 0.25 * std::pow(2.0, std::max(0.0, std::floor(std::log(programLength / 25.0) / std::log(2.0)) + 1));
}

double PI::Sim::GetBandwidth(double commodityVolume, int64 cycleTime)
{
    return commodityVolume * HOUR / double(cycleTime);
}

int64 PI::Sim::GetExpeditedTransferTime(double linkBandwidth, const std::map<uint16, uint32>& commodities)
{
    double commodityVolume = sPIDataMgr.GetCommodityVolume(commodities);
    return (int64)std::ceil(std::max(double(5 * EvE::Time::Minute), commodityVolume / linkBandwidth * HOUR));
}

double PI::Sim::GetAngleBetween(double lat1, double long1, double lat2, double long2)
{
    // SurfacePoint(theta=longitude, phi=latitude):  x = sin(phi)cos(theta), z = sin(phi)sin(theta), y = cos(phi)
    double x1 = std::sin(lat1) * std::cos(long1), z1 = std::sin(lat1) * std::sin(long1), y1 = std::cos(lat1);
    double x2 = std::sin(lat2) * std::cos(long2), z2 = std::sin(lat2) * std::sin(long2), y2 = std::cos(lat2);
    double dotProduct = x1 * x2 + y1 * y2 + z1 * z2;
    if (dotProduct > 1.0)
        dotProduct = 1.0;
    if (dotProduct < -1.0)
        dotProduct = -1.0;
    return std::acos(dotProduct);
}


ColonySim::ColonySim(PI_CCPin& colony, double planetRadius, uint16 planetTypeID)
: m_colony(colony),
m_planetRadius(planetRadius),
m_planetTypeID(planetTypeID)
{
    if (m_planetRadius <= 0)
        m_planetRadius = 1.0;
}

PI_Pin* ColonySim::GetPin(uint32 pinID)
{
    std::map<uint32, PI_Pin>::iterator itr = m_colony.pins.find(pinID);
    return (itr == m_colony.pins.end() ? nullptr : &itr->second);
}

PI_Link* ColonySim::GetLink(uint32 endpoint1, uint32 endpoint2)
{
    if (endpoint1 > endpoint2)
        std::swap(endpoint1, endpoint2);
    std::map<std::pair<uint32, uint32>, PI_Link>::iterator itr = m_colony.links.find(std::make_pair(endpoint1, endpoint2));
    return (itr == m_colony.links.end() ? nullptr : &itr->second);
}

PI_Route* ColonySim::GetRoute(uint32 routeID)
{
    std::map<uint32, PI_Route>::iterator itr = m_colony.routes.find(routeID);
    return (itr == m_colony.routes.end() ? nullptr : &itr->second);
}

/* ----------------------------------------------------------------------------------------------------------------
 *  pin classes
 */

bool ColonySim::IsStorage(const PI_Pin& pin)
{
    // StoragePin, SpaceportPin, CommandPin
    return (pin.kind == PI::Pin::Kind::Storage) or (pin.kind == PI::Pin::Kind::Spaceport) or (pin.kind == PI::Pin::Kind::CommandCenter);
}

bool ColonySim::IsSpaceport(const PI_Pin& pin)
{
    return (pin.kind == PI::Pin::Kind::Spaceport) or (pin.kind == PI::Pin::Kind::CommandCenter);
}

bool ColonySim::IsCommandCenter(const PI_Pin& pin)
{
    return (pin.kind == PI::Pin::Kind::CommandCenter);
}

bool ColonySim::IsConsumer(const PI_Pin& pin)
{
    return (pin.kind == PI::Pin::Kind::Process);
}

bool ColonySim::IsProcessor(const PI_Pin& pin)
{
    return (pin.kind == PI::Pin::Kind::Process);
}

bool ColonySim::IsProducer(const PI_Pin& pin)
{
    return (pin.kind == PI::Pin::Kind::Process) or (pin.kind == PI::Pin::Kind::ECU);
}

bool ColonySim::IsExtractor(const PI_Pin& pin)
{
    return (pin.kind == PI::Pin::Kind::ECU);
}

bool ColonySim::IsActive(const PI_Pin& pin)
{
    // EcuPin.IsActive / BasePin.IsActive
    if (pin.kind == PI::Pin::Kind::ECU)
        return (pin.programType != 0) and (pin.state > PI::Pin::State::Idle);
    return (pin.state > PI::Pin::State::Idle);
}

bool ColonySim::HasEnoughInputs(const PI_Pin& pin)
{
    // ProcessPin.HasEnoughInputs
    for (auto cur : pin.demands)
        if (cur.second > GetQty(pin.contents, cur.first))
            return false;
    return true;
}

bool ColonySim::CanActivate(const PI_Pin& pin)
{
    switch (pin.kind) {
        case PI::Pin::Kind::CommandCenter:
        case PI::Pin::Kind::Spaceport:
        case PI::Pin::Kind::Storage: {
            // StoragePin.CanActivate
            return false;
        }
        case PI::Pin::Kind::Process: {
            // ProcessPin.CanActivate
            if (pin.state < PI::Pin::State::Idle)
                return false;
            if (pin.schematicID == 0)
                return false;
            if (IsActive(pin))
                return true;
            if (pin.hasReceivedInputs or pin.receivedInputsLastCycle)
                return true;
            return HasEnoughInputs(pin);
        }
        case PI::Pin::Kind::ECU: {
            // EcuPin.CanActivate
            if (pin.state < PI::Pin::State::Active)
                return false;
            return (pin.programType != 0);
        }
    }
    // BasePin.CanActivate
    return (pin.state >= PI::Pin::State::Idle);
}

int64 ColonySim::GetCycleTime(const PI_Pin& pin)
{
    switch (pin.kind) {
        case PI::Pin::Kind::CommandCenter:
        case PI::Pin::Kind::Spaceport:
        case PI::Pin::Kind::Storage:
            return 0;       // StoragePin.GetCycleTime
        case PI::Pin::Kind::Process:
        case PI::Pin::Kind::ECU:
            // ProcessPin/EcuPin.GetCycleTime.  None until a schematic or program sets it.
            return (pin.cycleTime > 0 ? pin.cycleTime : -1);
    }
    return 60 * SEC;        // BasePin.GetCycleTime
}

bool ColonySim::GetNextRunTime(const PI_Pin& pin, int64& nextRunTime)
{
    // ProcessPin.GetNextRunTime
    if ((pin.kind == PI::Pin::Kind::Process) and !IsActive(pin) and HasEnoughInputs(pin))
        return false;
    // BasePin.GetNextRunTime
    if (pin.lastRunTime == 0)
        return false;
    int64 cycleTime = GetCycleTime(pin);
    if (cycleTime < 0)
        return false;   // the client logs GetCycleTime unexpectedly gave us None
    nextRunTime = pin.lastRunTime + cycleTime;
    return true;
}

bool ColonySim::CanRun(const PI_Pin& pin, int64 runTime)
{
    if (IsStorage(pin))
        return false;   // StoragePin.CanRun
    if (pin.kind == PI::Pin::Kind::Process) {
        // ProcessPin.CanRun
        if (!IsActive(pin) and !CanActivate(pin))
            return false;
    } else if (!CanActivate(pin)) {
        // BasePin.CanRun
        return false;
    }
    int64 nextRunTime = 0;
    if (!GetNextRunTime(pin, nextRunTime) or (nextRunTime <= runTime))
        return true;
    return false;
}

bool ColonySim::HasCapacity(const PI_Pin& pin)
{
    return IsStorage(pin);  // StoragePin.GetCapacity, BasePin.GetCapacity is None
}

double ColonySim::GetCapacity(const PI_Pin& pin)
{
    return sPIDataMgr.GetCapacity(pin.typeID);
}

double ColonySim::GetPinFreeSpace(const PI_Pin& pin)
{
    // StoragePin.GetFreeSpace
    double usedSpace = 0;
    for (auto cur : pin.contents)
        usedSpace += sPIDataMgr.GetVolume(cur.first) * cur.second;
    return GetCapacity(pin) - usedSpace;
}

double ColonySim::GetInputBufferState(const PI_Pin& pin)
{
    // ProcessPin.GetInputBufferState
    if (pin.demands.empty())
        return 1.0;
    double productsRatio = 0;
    for (auto cur : pin.demands)
        productsRatio += double(GetQty(pin.contents, cur.first)) / cur.second;
    return 1 - productsRatio / pin.demands.size();
}

int64 ColonySim::CanAccept(const PI_Pin& pin, uint16 typeID, int64 quantity)
{
    if (pin.kind == PI::Pin::Kind::ECU)
        return 0;   // EcuPin.CanAccept

    if (pin.kind == PI::Pin::Kind::Process) {
        // ProcessPin.CanAccept
        std::map<uint16, uint32>::const_iterator itr = pin.demands.find(typeID);
        if (itr == pin.demands.end())
            return 0;
        if (quantity < 0)
            quantity = itr->second;
        int64 remainingSpace = int64(itr->second) - GetQty(pin.contents, typeID);
        if (remainingSpace < quantity)
            return remainingSpace;
        return quantity;
    }

    // BasePin.CanAccept
    if (pin.state < PI::Pin::State::Idle)
        return 0;
    if (HasCapacity(pin)) {
        double volume = sPIDataMgr.GetVolume(typeID);
        double newVolume = volume * quantity;
        double capacityRemaining = std::max(0.0, GetCapacity(pin) - pin.capacityUsed);
        if ((newVolume > capacityRemaining) or (quantity == -1)) {
            if (volume <= 0)
                return (quantity < 0 ? 0 : quantity);
            return (int64)(capacityRemaining / volume);
        }
        return quantity;
    }
    return std::max((int64)0, quantity);
}

uint32 ColonySim::AddCommodity(PI_Pin& pin, uint16 typeID, int64 quantity)
{
    if (pin.kind == PI::Pin::Kind::ECU)
        return 0;   // EcuPin.AddCommodity

    // BasePin._AddCommodity
    int64 quantityToAdd = CanAccept(pin, typeID, quantity);
    if (quantityToAdd < 1)
        return 0;
    if (HasCapacity(pin))
        pin.capacityUsed += quantityToAdd * sPIDataMgr.GetVolume(typeID);
    pin.contents[typeID] += (uint32)quantityToAdd;

    // ProcessPin.AddCommodity
    if (pin.kind == PI::Pin::Kind::Process)
        pin.hasReceivedInputs = true;
    return (uint32)quantityToAdd;
}

uint32 ColonySim::RemoveCommodity(PI_Pin& pin, uint16 typeID, int64 quantity)
{
    // BasePin._RemoveCommodity
    std::map<uint16, uint32>::iterator itr = pin.contents.find(typeID);
    if (itr == pin.contents.end())
        return 0;
    if (quantity < 0)
        quantity = 0;
    uint32 qtyRemoved = 0;
    if (itr->second <= quantity) {
        qtyRemoved = itr->second;
        pin.contents.erase(itr);
    } else {
        qtyRemoved = (uint32)quantity;
        itr->second -= qtyRemoved;
    }
    if (HasCapacity(pin))
        pin.capacityUsed = std::max(0.0, pin.capacityUsed - sPIDataMgr.GetVolume(typeID) * qtyRemoved);
    return qtyRemoved;
}

void ColonySim::SetContents(PI_Pin& pin, const std::map<uint16, uint32>& contents)
{
    // BasePin.SetContents
    pin.contents = contents;
    pin.capacityUsed = 0;
    for (auto cur : pin.contents)
        pin.capacityUsed += cur.second * sPIDataMgr.GetVolume(cur.first);
}

void ColonySim::Run(PI_Pin& pin, int64 runTime, std::map<uint16, uint32>& products)
{
    products.clear();
    switch (pin.kind) {
        case PI::Pin::Kind::Process: {
            // ProcessPin.Run
            if (IsActive(pin))
                products = pin.products;
            if (HasEnoughInputs(pin)) {
                for (auto cur : pin.demands)
                    RemoveCommodity(pin, cur.first, cur.second);
                pin.state = PI::Pin::State::Active;
            } else {
                pin.state = PI::Pin::State::Idle;
            }
            pin.receivedInputsLastCycle = pin.hasReceivedInputs;
            pin.hasReceivedInputs = false;
            pin.lastRunTime = runTime;
        } break;
        case PI::Pin::Kind::ECU: {
            // EcuPin.Run
            pin.lastRunTime = runTime;
            if (pin.programType == 0)
                return;
            if (IsActive(pin)) {
                products[pin.programType] = GetProgramOutput(pin, runTime, pin.qtyPerCycle, pin.cycleTime, pin.installTime);
                if (pin.expiryTime <= runTime)
                    pin.state = PI::Pin::State::Idle;
            }
        } break;
        default: {
            // BasePin.Run
            pin.lastRunTime = runTime;
            pin.state = std::min(pin.state, (int8)PI::Pin::State::Idle);
        } break;
    }
}

void ColonySim::GetProducts(const PI_Pin& pin, std::map<uint16, uint32>& products)
{
    products.clear();
    if (pin.kind == PI::Pin::Kind::Process) {
        products = pin.products;
    } else if ((pin.kind == PI::Pin::Kind::ECU) and (pin.programType != 0)) {
        products[pin.programType] = pin.qtyPerCycle;
    }
}

void ColonySim::GetProductMaxOutput(const PI_Pin& pin, std::map<uint16, double>& products)
{
    products.clear();
    if (pin.kind == PI::Pin::Kind::Process) {
        for (auto cur : pin.products)
            products[cur.first] = cur.second;
    } else if ((pin.kind == PI::Pin::Kind::ECU) and (pin.programType != 0)) {
        products[pin.programType] = GetMaxOutput(pin, pin.qtyPerCycle, pin.cycleTime);
    }
}

uint32 ColonySim::GetProgramOutput(const PI_Pin& pin, int64 currentTime, int64 baseValue, int64 cycleTime, int64 startTime)
{
    // EcuPin.GetProgramOutput.  python 2 integer division for the longs
    if (cycleTime <= 0)
        return 0;
    double decayFactor = sPIDataMgr.GetTypeAttribute(pin.typeID, AttrECUDecayFactor);
    double noiseFactor = sPIDataMgr.GetTypeAttribute(pin.typeID, AttrECUNoiseFactor);
    int64 timeDiff = currentTime - startTime;
    int64 cycleNum = std::max(FloorDiv(timeDiff + SEC, cycleTime) - 1, (int64)0);
    double barWidth = FloorDiv(cycleTime, SEC) / 900.0;
    double t = (cycleNum + 0.5) * barWidth;
    double decayValue = baseValue / (1 + t * decayFactor);
    double f1 = 1.0 / 12;
    double f2 = 1.0 / 5;
    double f3 = 1.0 / 2;
    double phaseShift = std::pow((double)baseValue, 0.7);
    double sinA = std::cos(phaseShift + t * f1);
    double sinB = std::cos(phaseShift / 2 + t * f2);
    double sinC = std::cos(t * f3);
    double sinStuff = (sinA + sinB + sinC) / 3;
    sinStuff = std::max(0.0, sinStuff);
    double barHeight = decayValue * (1 + noiseFactor * sinStuff);
    double value = barWidth * barHeight;
    return (value > 0 ? (uint32)value : 0);
}

double ColonySim::GetMaxOutput(const PI_Pin& pin, int64 baseOutput, int64 cycleTime)
{
    // EcuPin.GetMaxOutput:  int(scalar * baseOutput) * cycleTime / SEC / 900.0
    double scalar = sPIDataMgr.GetTypeAttribute(pin.typeID, AttrECUNoiseFactor) + 1;
    int64 base = (int64)(scalar * baseOutput);
    return FloorDiv(base * cycleTime, SEC) / 900.0;
}

bool ColonySim::CanInstallProgram(const PI_Pin& pin, int64 runTime)
{
    // EcuPin.CanInstallProgram
    if (pin.programType == 0)
        return true;
    if (pin.expiryTime <= runTime)
        return true;
    int64 nextEditTime = pin.installTime + PI::Sim::ProgramInstallationCooldown;
    if (nextEditTime <= runTime)
        return true;
    throw UserError("CantInstallProgramNeedsCooldown")
            .AddTypeName("ecu", pin.typeID)
            .AddTimeShort("timeDiff", nextEditTime - runTime);
}

void ColonySim::EcuInstallProgram(PI_Pin& pin, uint16 programType, int64 cycleTime, int64 expiryTime, uint32 qtyPerCycle, double headRadius, int64 runTime)
{
    // EcuPin.InstallProgram
    if (qtyPerCycle <= 0) {
        _log(COLONY__WARNING, "EcuPin.InstallProgram - owner %u ECU pin error, zero qtyPerCycle.  program not installed", pin.ownerID);
        return;
    }
    pin.programType = programType;
    pin.qtyPerCycle = qtyPerCycle;
    pin.expiryTime = expiryTime;
    pin.cycleTime = cycleTime;
    pin.state = PI::Pin::State::Active;
    pin.lastRunTime = runTime;
    pin.installTime = pin.lastRunTime;
    pin.headRadius = headRadius;
}

void ColonySim::ClearProgram(PI_Pin& pin)
{
    // EcuPin.ClearProgram
    pin.programType = 0;
    pin.qtyPerCycle = 0;
    pin.expiryTime = 0;
    pin.installTime = 0;
    pin.state = PI::Pin::State::Idle;
}

void ColonySim::SetSchematic(PI_Pin& pin, const PI_Schematic& schematic)
{
    // ProcessPin.SetSchematic
    pin.demands = schematic.inputs;
    pin.products = schematic.outputs;
    pin.schematicID = schematic.schematicID;
    pin.cycleTime = schematic.cycleTime * SEC;
    std::map<uint16, uint32> newContents;
    for (auto cur : pin.contents) {
        std::map<uint16, uint32>::const_iterator itr = pin.demands.find(cur.first);
        if (itr != pin.demands.end())
            newContents[cur.first] = (cur.second < itr->second ? cur.second : itr->second);
    }
    pin.contents = newContents;
}

void ColonySim::SetLatitude(PI_Pin& pin, double latitude)
{
    // BasePin.SetLatitude
    pin.latitude = std::min(M_PI, std::max(-M_PI, latitude));
}

void ColonySim::SetLongitude(PI_Pin& pin, double longitude)
{
    // BasePin.SetLongitude:  abs(longitude % 2pi), python's modulo has the sign of the divisor
    double value = std::fmod(longitude, PI2);
    if (value < 0)
        value += PI2;
    pin.longitude = std::fabs(value);
}

/* ----------------------------------------------------------------------------------------------------------------
 *  ColonyData
 */

void ColonySim::RebuildIndices()
{
    m_colony.megaRoutesBySource.clear();
    m_colony.routesBySource.clear();
    m_colony.routesByDestination.clear();
    m_colony.linksByEndpoint.clear();
    for (auto& cur : m_colony.links) {
        cur.second.routesTransiting.clear();
        m_colony.linksByEndpoint[cur.second.endpoint1].push_back(cur.second.endpoint2);
        m_colony.linksByEndpoint[cur.second.endpoint2].push_back(cur.second.endpoint1);
    }
    // ColonyData.PrimeRoute, in ascending routeID (the order the routes are restored on the client)
    std::vector<uint32> badRoutes;
    for (auto& cur : m_colony.routes) {
        PI_Route& route = cur.second;
        if (route.path.size() < 2) {
            badRoutes.push_back(cur.first);
            continue;
        }
        bool ok = true;
        for (size_t i = 1; i < route.path.size(); ++i)
            if (GetLink(route.path[i - 1], route.path[i]) == nullptr)
                ok = false;
        if (!ok) {
            _log(COLONY__ERROR, "ColonySim::RebuildIndices() - route %u of colony %u crosses a missing link.  route removed.", cur.first, m_colony.ccPinID);
            badRoutes.push_back(cur.first);
            continue;
        }
        uint32 sourceID = route.path.front(), destID = route.path.back();
        m_colony.megaRoutesBySource[sourceID][std::make_pair(destID, route.commodityTypeID)] += route.commodityQuantity;
        m_colony.routesBySource[sourceID].insert(cur.first);
        m_colony.routesByDestination[destID].push_back(cur.first);
        for (size_t i = 1; i < route.path.size(); ++i)
            GetLink(route.path[i - 1], route.path[i])->routesTransiting.push_back(cur.first);
    }
    for (auto cur : badRoutes)
        m_colony.routes.erase(cur);
}

PI_Pin* ColonySim::PrimePin(uint32 pinID, uint16 typeID, uint32 ownerID, double latitude, double longitude)
{
    // ColonyData.PrimePin
    PI_Pin pin = PI_Pin();
    pin.kind = sPIDataMgr.GetPinKind(typeID);
    if (pin.kind == PI::Pin::Kind::None) {
        _log(COLONY__ERROR, "ColonySim::PrimePin() - Unable to prime pin of invalid type %u", typeID);
        return nullptr;
    }
    // BasePin.Startup
    pin.typeID = typeID;
    pin.ownerID = ownerID;
    pin.state = PI::Pin::State::Idle;
    SetLatitude(pin, latitude);
    SetLongitude(pin, longitude);
    m_colony.pins[pinID] = pin;
    if (pin.kind == PI::Pin::Kind::CommandCenter)
        m_colony.ccPinID = pinID;
    return &m_colony.pins[pinID];
}

void ColonySim::RemovePin(uint32 pinID)
{
    // ColonyData.RemovePin
    if (GetPin(pinID) == nullptr) {
        _log(COLONY__WARNING, "ColonySim::RemovePin() - unable to remove pin %u as it was not found", pinID);
        return;
    }
    std::vector<uint32> linksToRemove = m_colony.linksByEndpoint[pinID];
    for (auto cur : linksToRemove)
        RemoveLink(pinID, cur);
    if (m_colony.ccPinID == pinID)
        m_colony.ccPinID = 0;
    m_colony.pins.erase(pinID);
    m_colony.linksByEndpoint.erase(pinID);
    OnPinRemoved(pinID);
}

PI_Link* ColonySim::PrimeLink(uint16 linkTypeID, uint32 endpoint1, uint32 endpoint2, uint8 level/*0*/, uint32 linkID/*0*/)
{
    // ColonyData.PrimeLink
    if (endpoint1 > endpoint2)
        std::swap(endpoint1, endpoint2);
    PI_Link link = PI_Link();
        link.typeID = linkTypeID;
        link.linkID = linkID;
        link.level = level;
        link.endpoint1 = endpoint1;
        link.endpoint2 = endpoint2;
    std::pair<uint32, uint32> key(endpoint1, endpoint2);
    m_colony.links[key] = link;
    m_colony.linksByEndpoint[endpoint1].push_back(endpoint2);
    m_colony.linksByEndpoint[endpoint2].push_back(endpoint1);
    return &m_colony.links[key];
}

void ColonySim::RemoveLink(uint32 endpoint1, uint32 endpoint2)
{
    // ColonyData.RemoveLink
    if (endpoint1 > endpoint2)
        std::swap(endpoint1, endpoint2);
    std::map<std::pair<uint32, uint32>, PI_Link>::iterator itr = m_colony.links.find(std::make_pair(endpoint1, endpoint2));
    if (itr != m_colony.links.end()) {
        std::vector<uint32> routes = itr->second.routesTransiting;
        for (auto cur : routes)
            RemoveRoute(cur);
        m_colony.links.erase(std::make_pair(endpoint1, endpoint2));
    }
    std::vector<uint32>& ep1 = m_colony.linksByEndpoint[endpoint1];
    ep1.erase(std::remove(ep1.begin(), ep1.end(), endpoint2), ep1.end());
    std::vector<uint32>& ep2 = m_colony.linksByEndpoint[endpoint2];
    ep2.erase(std::remove(ep2.begin(), ep2.end(), endpoint1), ep2.end());
}

void ColonySim::PrimeRoute(uint32 routeID, const std::vector<uint32>& path, uint16 typeID, uint32 quantity)
{
    // ColonyData.PrimeRoute
    for (size_t i = 1; i < path.size(); ++i)
        if (GetLink(path[i - 1], path[i]) == nullptr)
            throw CustomError ("Unable to find link between pins %u and %u", path[i - 1], path[i]);

    PI_Route route = PI_Route();
        route.routeID = routeID;
        route.commodityTypeID = typeID;
        route.commodityQuantity = quantity;
        route.path = path;
    uint32 sourceID = path.front(), destID = path.back();
    m_colony.megaRoutesBySource[sourceID][std::make_pair(destID, typeID)] += quantity;
    m_colony.routesBySource[sourceID].insert(routeID);
    std::vector<uint32>& byDest = m_colony.routesByDestination[destID];
    if (std::find(byDest.begin(), byDest.end(), routeID) == byDest.end())
        byDest.push_back(routeID);
    m_colony.routes[routeID] = route;
    for (size_t i = 1; i < path.size(); ++i)
        GetLink(path[i - 1], path[i])->routesTransiting.push_back(routeID);
}

void ColonySim::RemoveRoute(uint32 routeID)
{
    // ColonyData.RemoveRoute
    std::map<uint32, PI_Route>::iterator itr = m_colony.routes.find(routeID);
    if (itr == m_colony.routes.end()) {
        _log(COLONY__WARNING, "ColonySim::RemoveRoute() - Unable to remove route %u: route not found", routeID);
        return;
    }
    PI_Route route = itr->second;
    m_colony.routes.erase(itr);
    uint32 sourceID = route.path.front(), destID = route.path.back();
    std::map<uint32, std::map<std::pair<uint32, uint16>, uint32>>::iterator mItr = m_colony.megaRoutesBySource.find(sourceID);
    if (mItr != m_colony.megaRoutesBySource.end()) {
        std::map<std::pair<uint32, uint16>, uint32>::iterator kItr = mItr->second.find(std::make_pair(destID, route.commodityTypeID));
        if (kItr != mItr->second.end()) {
            kItr->second -= std::min(kItr->second, route.commodityQuantity);
            if (kItr->second == 0)
                mItr->second.erase(kItr);
        }
        if (mItr->second.empty())
            m_colony.megaRoutesBySource.erase(mItr);
    }
    std::map<uint32, std::set<uint32>>::iterator sItr = m_colony.routesBySource.find(sourceID);
    if (sItr != m_colony.routesBySource.end()) {
        sItr->second.erase(routeID);
        if (sItr->second.empty())
            m_colony.routesBySource.erase(sItr);
    }
    std::map<uint32, std::vector<uint32>>::iterator dItr = m_colony.routesByDestination.find(destID);
    if (dItr != m_colony.routesByDestination.end()) {
        dItr->second.erase(std::remove(dItr->second.begin(), dItr->second.end(), routeID), dItr->second.end());
        if (dItr->second.empty())
            m_colony.routesByDestination.erase(dItr);
    }
    for (size_t i = 1; i < route.path.size(); ++i) {
        PI_Link* pLink = GetLink(route.path[i - 1], route.path[i]);
        if (pLink != nullptr)
            pLink->routesTransiting.erase(std::remove(pLink->routesTransiting.begin(), pLink->routesTransiting.end(), routeID), pLink->routesTransiting.end());
    }
}

void ColonySim::SetLinkLevel(uint32 endpoint1, uint32 endpoint2, uint8 newLevel)
{
    PI_Link* pLink = GetLink(endpoint1, endpoint2);
    if (pLink != nullptr)
        pLink->level = newLevel;
}

void ColonySim::AddExtractorHead(uint32 pinID, uint16 headID, double latitude, double longitude)
{
    // ColonyData.AddExtractorHead -> EcuPin.AddHead
    PI_Pin* pPin = GetPin(pinID);
    if ((pPin == nullptr) or (pPin->heads.size() >= PI::Sim::EcuMaxHeads))
        return;
    PI_Heads head = PI_Heads();
        head.ecuPinID = pinID;
        head.latitude = latitude;
        head.longitude = longitude;
        head.typeID = (pPin->programType ? sPIDataMgr.GetHeadType(pPin->typeID, pPin->programType) : 0);
    pPin->heads[headID] = head;
}

void ColonySim::RemoveExtractorHead(uint32 pinID, uint16 headID)
{
    // ColonyData.RemoveExtractorHead -> EcuPin.RemoveHead
    PI_Pin* pPin = GetPin(pinID);
    if (pPin != nullptr)
        pPin->heads.erase(headID);
}

void ColonySim::MoveExtractorHead(uint32 pinID, uint16 headID, double latitude, double longitude)
{
    // ColonyData.MoveExtractorHead -> EcuPin.SetHeadPosition
    RemoveExtractorHead(pinID, headID);
    AddExtractorHead(pinID, headID, latitude, longitude);
}

void ColonySim::InstallProgram(uint32 pinID, uint16 typeID, double headRadius, const ResourceField& field, int64 runTime)
{
    // ColonyData.InstallProgram
    PI_Pin* pPin = GetPin(pinID);
    if (pPin == nullptr)
        return;
    if (typeID == 0) {
        ClearProgram(*pPin);
        return;
    }
    std::vector<Head> heads;
    for (auto cur : pPin->heads) {
        Head head = Head();
            head.id = cur.first;
            head.latitude = cur.second.latitude;
            head.longitude = cur.second.longitude;
        heads.push_back(head);
    }
    uint32 maxValue = 0, numCycles = 0;
    int64 cycleTime = 0;
    CreateProgram(field, pinID, pPin->typeID, typeID, heads, headRadius, maxValue, cycleTime, numCycles);
    int64 endTime = runTime + cycleTime * numCycles;
    EcuInstallProgram(*pPin, typeID, cycleTime, endTime, maxValue, headRadius, runTime);
    // the heads now extract this program's resource
    uint16 headType = (pPin->programType ? sPIDataMgr.GetHeadType(pPin->typeID, pPin->programType) : 0);
    for (auto& cur : pPin->heads)
        cur.second.typeID = headType;
}

void ColonySim::GetObsoleteRoutesForPin(uint32 pinID, std::vector<uint32>& routeIDs)
{
    // ColonyData.GetObsoleteRoutesForPin
    PI_Pin* pPin = GetPin(pinID);
    if (pPin == nullptr)
        return;
    if (IsProducer(*pPin)) {
        std::map<uint16, uint32> products;
        GetProducts(*pPin, products);
        std::map<uint32, std::set<uint32>>::const_iterator itr = m_colony.routesBySource.find(pinID);
        if (itr != m_colony.routesBySource.end())
            for (auto cur : itr->second) {
                PI_Route* pRoute = GetRoute(cur);
                if ((pRoute != nullptr) and (products.find(pRoute->commodityTypeID) == products.end()))
                    routeIDs.push_back(cur);
            }
    }
    if (IsConsumer(*pPin)) {
        std::map<uint32, std::vector<uint32>>::const_iterator itr = m_colony.routesByDestination.find(pinID);
        if (itr != m_colony.routesByDestination.end())
            for (auto cur : itr->second) {
                PI_Route* pRoute = GetRoute(cur);
                if ((pRoute != nullptr) and (pPin->demands.find(pRoute->commodityTypeID) == pPin->demands.end()))
                    routeIDs.push_back(cur);
            }
    }
}

double ColonySim::GetPinCpuUsage(const PI_Pin& pin, int32 numHeads/*-1*/)
{
    // BasePin/EcuPin.GetCpuUsage
    double usage = sPIDataMgr.GetTypeAttribute(pin.typeID, AttrCpuLoad);
    if (pin.kind == PI::Pin::Kind::ECU) {
        if (numHeads < 0)
            numHeads = pin.heads.size();
        usage += sPIDataMgr.GetTypeAttribute(pin.typeID, AttrECUExtractorHeadCPU) * numHeads;
    }
    return usage;
}

double ColonySim::GetPinPowerUsage(const PI_Pin& pin, int32 numHeads/*-1*/)
{
    // BasePin/EcuPin.GetPowerUsage
    double usage = sPIDataMgr.GetTypeAttribute(pin.typeID, AttrPowerLoad);
    if (pin.kind == PI::Pin::Kind::ECU) {
        if (numHeads < 0)
            numHeads = pin.heads.size();
        usage += sPIDataMgr.GetTypeAttribute(pin.typeID, AttrECUExtractorHeadPower) * numHeads;
    }
    return usage;
}

double ColonySim::GetPinCpuOutput(const PI_Pin& pin)
{
    // CommandPin.GetCpuOutput uses the colony level
    if (pin.kind == PI::Pin::Kind::CommandCenter)
        return PI::Sim::GetCPUOutput(m_colony.level);
    return sPIDataMgr.GetTypeAttribute(pin.typeID, AttrCpuOutput);
}

double ColonySim::GetPinPowerOutput(const PI_Pin& pin)
{
    if (pin.kind == PI::Pin::Kind::CommandCenter)
        return PI::Sim::GetPowerOutput(m_colony.level);
    return sPIDataMgr.GetTypeAttribute(pin.typeID, AttrPowerOutput);
}

double ColonySim::GetLinkDistance(const PI_Link& link)
{
    // BaseLink.GetDistance
    PI_Pin* pPin1 = GetPin(link.endpoint1);
    PI_Pin* pPin2 = GetPin(link.endpoint2);
    if ((pPin1 == nullptr) or (pPin2 == nullptr))
        return 0;
    return m_planetRadius * PI::Sim::GetAngleBetween(pPin1->latitude, pPin1->longitude, pPin2->latitude, pPin2->longitude);
}

int64 ColonySim::GetLinkCpuUsage(uint16 typeID, double length, uint8 level)
{
    // planetCommon.GetCpuUsageForLink
    int64 baseCpuUsage = (int64)sPIDataMgr.GetTypeAttributeNoDefault(typeID, AttrCpuLoad);
    double cpuUsagePerKm = sPIDataMgr.GetTypeAttributeNoDefault(typeID, AttrCPULoadPerKm);
    double cpuUsageLevelModifier = sPIDataMgr.GetTypeAttributeNoDefault(typeID, AttrCPULoadLevelModifier);
    return baseCpuUsage + (int64)std::ceil(cpuUsagePerKm * length / 1000.0 * std::pow(level + 1.0, cpuUsageLevelModifier));
}

int64 ColonySim::GetLinkPowerUsage(uint16 typeID, double length, uint8 level)
{
    // planetCommon.GetPowerUsageForLink
    int64 basePowerUsage = (int64)sPIDataMgr.GetTypeAttributeNoDefault(typeID, AttrPowerLoad);
    double powerUsagePerKm = sPIDataMgr.GetTypeAttributeNoDefault(typeID, AttrPowerLoadPerKm);
    double powerUsageLevelModifier = sPIDataMgr.GetTypeAttributeNoDefault(typeID, AttrPowerLoadLevelModifier);
    return basePowerUsage + (int64)std::ceil(powerUsagePerKm * length / 1000.0 * std::pow(level + 1.0, powerUsageLevelModifier));
}

void ColonySim::GetResourceUsage(double& cpuUsage, double& cpuSupply, double& powerUsage, double& powerSupply)
{
    // ColonyData.RecalculatePowerAndCpuStatistics
    cpuUsage = cpuSupply = powerUsage = powerSupply = 0;
    for (auto& cur : m_colony.pins) {
        cpuUsage += GetPinCpuUsage(cur.second);
        cpuSupply += GetPinCpuOutput(cur.second);
        powerUsage += GetPinPowerUsage(cur.second);
        powerSupply += GetPinPowerOutput(cur.second);
    }
    for (auto& cur : m_colony.links) {
        double length = GetLinkDistance(cur.second);
        cpuUsage += GetLinkCpuUsage(cur.second.typeID, length, cur.second.level);
        powerUsage += GetLinkPowerUsage(cur.second.typeID, length, cur.second.level);
    }
}

double ColonySim::GetLinkTotalBandwidth(const PI_Link& link)
{
    // BaseLink.GetBandwidthForLevel
    return sPIDataMgr.GetTypeAttribute(link.typeID, AttrLogisticalCapacity) * std::pow(2.0, link.level);
}

double ColonySim::GetRouteBandwidthUsage(const PI_Route& route)
{
    // BaseRoute.GetBandwidthUsage / GetRouteCycleTime
    double bwth = sPIDataMgr.GetVolume(route.commodityTypeID) * route.commodityQuantity;
    int64 cycleTime = 0;
    PI_Pin* pSource = GetPin(route.path.front());
    if (pSource != nullptr) {
        if (IsProducer(*pSource)) {
            cycleTime = GetCycleTime(*pSource);
        } else if (IsStorage(*pSource)) {
            PI_Pin* pDest = GetPin(route.path.back());
            if ((pDest != nullptr) and IsConsumer(*pDest))
                cycleTime = GetCycleTime(*pDest);
        }
    }
    if (cycleTime <= 0)
        return bwth;
    return PI::Sim::GetBandwidth(bwth, cycleTime);
}

double ColonySim::GetLinkBandwidthUsage(const PI_Link& link)
{
    double usage = 0;
    for (auto cur : link.routesTransiting) {
        PI_Route* pRoute = GetRoute(cur);
        if (pRoute != nullptr)
            usage += GetRouteBandwidthUsage(*pRoute);
    }
    return usage;
}

void ColonySim::RemapIDs(const std::map<uint32, uint32>& pinIDs, const std::map<uint32, uint32>& routeIDs)
{
    auto mapPin = [&pinIDs](uint32 id) { std::map<uint32, uint32>::const_iterator itr = pinIDs.find(id); return (itr == pinIDs.end() ? id : itr->second); };
    auto mapRoute = [&routeIDs](uint32 id) { std::map<uint32, uint32>::const_iterator itr = routeIDs.find(id); return (itr == routeIDs.end() ? id : itr->second); };

    std::map<uint32, PI_Pin> pins;
    for (auto& cur : m_colony.pins) {
        uint32 pinID = mapPin(cur.first);
        for (auto& head : cur.second.heads)
            head.second.ecuPinID = pinID;
        pins[pinID] = cur.second;
    }
    m_colony.pins.swap(pins);
    m_colony.ccPinID = mapPin(m_colony.ccPinID);

    std::map<std::pair<uint32, uint32>, PI_Link> links;
    for (auto& cur : m_colony.links) {
        PI_Link link = cur.second;
        link.endpoint1 = mapPin(cur.second.endpoint1);
        link.endpoint2 = mapPin(cur.second.endpoint2);
        if (link.endpoint1 > link.endpoint2)
            std::swap(link.endpoint1, link.endpoint2);
        links[std::make_pair(link.endpoint1, link.endpoint2)] = link;
    }
    m_colony.links.swap(links);

    std::map<uint32, PI_Route> routes;
    for (auto& cur : m_colony.routes) {
        PI_Route route = cur.second;
        route.routeID = mapRoute(cur.first);
        for (auto& id : route.path)
            id = mapPin(id);
        routes[route.routeID] = route;
    }
    m_colony.routes.swap(routes);

    RebuildIndices();
}

/* ----------------------------------------------------------------------------------------------------------------
 *  BaseColony - simulation
 */

void ColonySim::RecalculateCurrentSimTime(int64 now)
{
    // BaseColony.RecalculateCurrentSimTime
    m_colony.currentSimTime = 0;
    for (auto& cur : m_colony.pins) {
        if (IsStorage(cur.second) or (cur.second.lastRunTime == 0))
            continue;
        if ((m_colony.currentSimTime == 0) or (cur.second.lastRunTime < m_colony.currentSimTime))
            m_colony.currentSimTime = cur.second.lastRunTime;
    }
    if (m_colony.currentSimTime == 0)
        m_colony.currentSimTime = now;
}

void ColonySim::AddTimer(uint32 pinID, int64 runTime)
{
    // BaseColony.AddTimer
    m_simQueue.insert(std::make_pair(runTime, pinID));
    m_scheduled[pinID] = runTime;
}

void ColonySim::SchedulePin(uint32 pinID)
{
    // BaseColony.SchedulePin
    PI_Pin* pPin = GetPin(pinID);
    if (pPin == nullptr)
        return;
    int64 nextRunTime = 0;
    bool hasNextRunTime = GetNextRunTime(*pPin, nextRunTime);
    std::map<uint32, int64>::iterator itr = m_scheduled.find(pinID);
    if (itr != m_scheduled.end()) {
        if (!hasNextRunTime or (nextRunTime < itr->second)) {
            m_simQueue.erase(std::make_pair(itr->second, pinID));
            m_scheduled.erase(itr);
        } else {
            return;
        }
    }
    if (!hasNextRunTime or (nextRunTime < m_colony.currentSimTime)) {
        AddTimer(pinID, m_colony.currentSimTime);
    } else {
        AddTimer(pinID, nextRunTime);
    }
}

void ColonySim::OnPinRemoved(uint32 pinID)
{
    // BaseColony.OnPinRemoved
    std::map<uint32, int64>::iterator itr = m_scheduled.find(pinID);
    if (itr != m_scheduled.end()) {
        m_simQueue.erase(std::make_pair(itr->second, pinID));
        m_scheduled.erase(itr);
    }
}

void ColonySim::PrimeSimulation(int64 simEndTime)
{
    // BaseColony.PrimeSimulation
    m_simQueue.clear();
    m_scheduled.clear();
    for (auto& cur : m_colony.pins)
        if (CanRun(cur.second, simEndTime))
            SchedulePin(cur.first);
}

int64 ColonySim::RunSimulation(int64 runSimUntil)
{
    // BaseColony.RunSimulation
    if (m_colony.currentSimTime == 0)
        RecalculateCurrentSimTime(runSimUntil);
    int64 simEndTime = runSimUntil;
    PrimeSimulation(simEndTime);
    // the python loop has no limit.  a pin that runs without moving its run time would never end, so cap the evaluations
    uint32 evaluations = 0;
    while (!m_simQueue.empty()) {
        std::pair<int64, uint32> next = *m_simQueue.begin();
        m_simQueue.erase(m_simQueue.begin());
        m_scheduled.erase(next.second);
        if (next.first > simEndTime)
            break;
        m_colony.currentSimTime = next.first;
        PI_Pin* pPin = GetPin(next.second);
        if (pPin == nullptr) {
            _log(COLONY__ERROR, "ColonySim::RunSimulation() - Unable to find scheduled pin %u", next.second);
            continue;
        }
        if (!CanRun(*pPin, m_colony.currentSimTime))
            continue;
        EvaluatePin(next.second);
        if (++evaluations > 1000000) {
            _log(COLONY__ERROR, "ColonySim::RunSimulation() - colony %u of %u ran away at %lli.  simulation stopped.", m_colony.ccPinID, m_colony.ownerID, m_colony.currentSimTime);
            break;
        }
    }
    m_colony.currentSimTime = simEndTime;
    m_simQueue.clear();
    m_scheduled.clear();
    return simEndTime;
}

void ColonySim::EvaluatePin(uint32 pinID)
{
    // BaseColony.EvaluatePin
    PI_Pin* pPin = GetPin(pinID);
    if (!CanActivate(*pPin) and !IsActive(*pPin))
        return;
    std::map<uint16, uint32> commods;
    Run(*pPin, m_colony.currentSimTime, commods);
    if (IsConsumer(*pPin))
        RouteCommodityInput(pinID);
    if (IsActive(*pPin) or CanActivate(*pPin))
        SchedulePin(pinID);
    if (commods.empty())
        return;
    RouteCommodityOutput(pinID, commods);
}

void ColonySim::StimulateIdlePin(uint32 pinID)
{
    // BaseColony.StimulateIdlePin
    PI_Pin* pPin = GetPin(pinID);
    if (pPin == nullptr)
        throw CustomError ("Cannot stimulate a None pin");
    if (IsStorage(*pPin)) {
        std::map<uint16, uint32> contents = pPin->contents;
        RouteCommodityOutput(pinID, contents);
    }
    if (IsConsumer(*pPin))
        pPin->hasReceivedInputs = true;
    if (!IsActive(*pPin) and CanActivate(*pPin))
        SchedulePin(pinID);
}

void ColonySim::RouteCommodityOutput(uint32 sourcePinID, std::map<uint16, uint32>& commodities)
{
    // BaseColony.RouteCommodityOutput with ColonyData.GetSortedRoutesForPin
    PI_Pin* pSource = GetPin(sourcePinID);
    if (pSource == nullptr)
        return;

    // (sort key, destID, typeID, qty).  processors by input buffer state, storage by free space, both lowest first
    typedef std::tuple<double, uint32, uint16, uint32> SortedRoute;
    std::vector<SortedRoute> processorRoutes, storageRoutes;
    std::map<uint32, std::map<std::pair<uint32, uint16>, uint32>>::const_iterator mItr = m_colony.megaRoutesBySource.find(sourcePinID);
    if (mItr != m_colony.megaRoutesBySource.end())
        for (auto cur : mItr->second) {
            if (cur.second == 0)
                continue;
            if (commodities.find(cur.first.second) == commodities.end())
                continue;
            PI_Pin* pDest = GetPin(cur.first.first);
            if (pDest == nullptr)
                continue;
            if (IsProcessor(*pDest)) {
                processorRoutes.push_back(SortedRoute(GetInputBufferState(*pDest), cur.first.first, cur.first.second, cur.second));
            } else {
                storageRoutes.push_back(SortedRoute(GetPinFreeSpace(*pDest), cur.first.first, cur.first.second, cur.second));
            }
        }
    std::sort(processorRoutes.begin(), processorRoutes.end());
    std::sort(storageRoutes.begin(), storageRoutes.end());

    std::map<uint32, std::map<uint16, uint32>> pinsReceivingCommodities;
    bool done = false;
    for (uint8 isStorageRoutes = 0; isStorageRoutes < 2; ++isStorageRoutes) {
        if (done)
            break;
        std::vector<SortedRoute>& listOfRoutes = (isStorageRoutes ? storageRoutes : processorRoutes);
        for (size_t i = 0; i < listOfRoutes.size(); ++i) {
            uint32 destID = std::get<1>(listOfRoutes[i]);
            uint16 commodityTypeID = std::get<2>(listOfRoutes[i]);
            uint32 qty = std::get<3>(listOfRoutes[i]);
            bool hasMaxAmount = false;
            double maxAmount = 0;
            if (isStorageRoutes) {
                // remaining routes, this one included
                size_t routesLeft = listOfRoutes.size() - i;
                hasMaxAmount = true;
                maxAmount = std::ceil(double(GetQty(commodities, commodityTypeID)) / routesLeft);
            }
            std::pair<uint16, uint32> moved = TransferCommodities(sourcePinID, destID, commodityTypeID, qty, commodities, hasMaxAmount, maxAmount);
            std::map<uint16, uint32>::iterator cItr = commodities.find(moved.first);
            if (cItr != commodities.end()) {
                if (cItr->second <= moved.second) {
                    commodities.erase(cItr);
                } else {
                    cItr->second -= moved.second;
                }
            }
            if (moved.second > 0)
                pinsReceivingCommodities[destID][moved.first] += moved.second;
            if (commodities.empty()) {
                done = true;
                break;
            }
        }
    }

    bool sourceIsStorage = IsStorage(*GetPin(sourcePinID));
    for (auto& cur : pinsReceivingCommodities) {
        PI_Pin* pReceiving = GetPin(cur.first);
        if (pReceiving == nullptr)
            continue;
        if (IsConsumer(*pReceiving))
            SchedulePin(cur.first);
        if (!sourceIsStorage and IsStorage(*pReceiving)) {
            std::map<uint16, uint32> commodsAdded = cur.second;
            RouteCommodityOutput(cur.first, commodsAdded);
        }
    }
}

void ColonySim::RouteCommodityInput(uint32 destinationPinID)
{
    // BaseColony.RouteCommodityInput
    std::map<uint32, std::vector<uint32>>::const_iterator itr = m_colony.routesByDestination.find(destinationPinID);
    if (itr == m_colony.routesByDestination.end())
        return;
    std::vector<uint32> routesToEvaluate = itr->second;
    for (auto routeID : routesToEvaluate) {
        PI_Route* pRoute = GetRoute(routeID);
        if (pRoute == nullptr)
            continue;
        PI_Pin* pSource = GetPin(pRoute->path.front());
        if (pSource == nullptr) {
            _log(COLONY__WARNING, "ColonySim::RouteCommodityInput() - Route %u has nonexistent source pin %u", routeID, pRoute->path.front());
            continue;
        }
        if (!IsStorage(*pSource))
            continue;
        std::map<uint16, uint32> storedCommods = pSource->contents;
        if (storedCommods.empty())
            continue;
        ExecuteRoute(routeID, storedCommods);
    }
}

std::pair<uint16, uint32> ColonySim::ExecuteRoute(uint32 routeID, std::map<uint16, uint32>& commodities)
{
    // BaseColony.ExecuteRoute
    PI_Route* pRoute = GetRoute(routeID);
    if (pRoute == nullptr) {
        _log(COLONY__ERROR, "ColonySim::ExecuteRoute() - Cannot find route %u", routeID);
        return std::make_pair(0, 0);
    }
    return TransferCommodities(pRoute->path.front(), pRoute->path.back(), pRoute->commodityTypeID, pRoute->commodityQuantity, commodities);
}

std::pair<uint16, uint32> ColonySim::TransferCommodities(uint32 sourcePinID, uint32 destPinID, uint16 typeID, uint32 qty,
                                                          std::map<uint16, uint32>& commodities, bool hasMaxAmount/*false*/, double maxAmount/*0*/)
{
    // BaseColony.TransferCommodities
    PI_Pin* pSource = GetPin(sourcePinID);
    if (pSource == nullptr)
        throw CustomError ("Unable to find pin %u", sourcePinID);
    std::map<uint16, uint32>::const_iterator itr = commodities.find(typeID);
    if (itr == commodities.end())
        return std::make_pair(0, 0);
    double amtToMove = std::min(itr->second, qty);
    if (hasMaxAmount)
        amtToMove = std::min(maxAmount, amtToMove);
    if (amtToMove <= 0)
        return std::make_pair(0, 0);
    PI_Pin* pDest = GetPin(destPinID);
    if (pDest == nullptr)
        throw CustomError ("Unable to find pin %u", destPinID);
    uint32 amtMoved = AddCommodity(*pDest, typeID, (int64)amtToMove);
    if (IsStorage(*pSource))
        RemoveCommodity(*pSource, typeID, amtMoved);
    return std::make_pair(typeID, amtMoved);
}

void ColonySim::ExecuteExpeditedTransfer(uint32 sourceID, uint32 destinationID, const std::map<uint16, uint32>& commodities, double minBandwidth, int64 runTime)
{
    // BaseColony.ExecuteExpeditedTransfer
    PI_Pin* pSource = GetPin(sourceID);
    if (pSource == nullptr)
        throw CustomError ("Unable to find pin %u", sourceID);
    if (!IsStorage(*pSource))
        throw CustomError ("Expedited transfer attempted from non-storage pin %u", sourceID);
    std::map<uint16, uint32> commodsToPush;
    for (auto cur : commodities) {
        std::map<uint16, uint32>::const_iterator itr = pSource->contents.find(cur.first);
        if (itr == pSource->contents.end())
            continue;
        uint32 qty = std::min(itr->second, cur.second);
        if (qty >= 1)
            commodsToPush[cur.first] = qty;
    }
    if (commodsToPush.empty())
        return;
    PI_Pin* pDest = GetPin(destinationID);
    if (pDest == nullptr)
        throw CustomError ("Unable to find pin %u", destinationID);
    std::map<uint16, uint32> commodsMoved;
    for (auto cur : commodsToPush)
        commodsMoved[cur.first] = AddCommodity(*pDest, cur.first, cur.second);
    for (auto cur : commodsMoved)
        RemoveCommodity(*pSource, cur.first, cur.second);
    // StoragePin.ExecuteTransfer
    pSource->lastRunTime = runTime + PI::Sim::GetExpeditedTransferTime(minBandwidth, commodsMoved);
    StimulateIdlePin(destinationID);
}

void ColonySim::InstallSchematicForPin(uint32 pinID, uint16 schematicID)
{
    // BaseColony.InstallSchematicForPin -> ProcessPin.InstallSchematic
    PI_Pin* pPin = GetPin(pinID);
    if (pPin == nullptr)
        throw CustomError ("Unable to install schematic - cannot find pin");
    const PI_Schematic* pSchematic = sPIDataMgr.GetSchematic(schematicID);
    if (pSchematic == nullptr)
        throw CustomError ("Schematic with schematicID %u does not exist", schematicID);
    if (!sPIDataMgr.IsSchematicAllowed(schematicID, pPin->typeID))
        throw UserError("CannotAssignSchematicToPinType");
    SetSchematic(*pPin, *pSchematic);
    pPin->state = PI::Pin::State::Idle;
    StimulateIdlePin(pinID);
    // OnSchematicInstalled:  routes the new schematic can't use go
    std::vector<uint32> obsoleteRoutes;
    GetObsoleteRoutesForPin(pinID, obsoleteRoutes);
    for (auto cur : obsoleteRoutes)
        RemoveRoute(cur);
}

void ColonySim::OnRouteCreated(uint32 routeID)
{
    // BaseColony.OnRouteCreated
    PI_Route* pRoute = GetRoute(routeID);
    if (pRoute == nullptr)
        throw CustomError ("Unable to find recently-created route");
    uint32 sourceID = pRoute->path.front(), destID = pRoute->path.back();
    PI_Pin* pSource = GetPin(sourceID);
    if (IsStorage(*pSource)) {
        std::map<uint16, uint32> contents = pSource->contents;
        ExecuteRoute(routeID, contents);
    } else {
        StimulateIdlePin(sourceID);
    }
    PI_Pin* pDest = GetPin(destID);
    if (!IsStorage(*pDest) and !IsActive(*pDest) and CanActivate(*pDest))
        SchedulePin(destID);
}

/* ----------------------------------------------------------------------------------------------------------------
 *  BaseColony - validation
 */

void ColonySim::ValidateCreatePin(uint32 charID, uint16 typeID, double latitude, double longitude)
{
    // BaseColony.ValidateCreatePin
    uint16 groupID = sPIDataMgr.GetGroupID(typeID);
    if (groupID == EVEDB::invGroups::Command_Centers) {
        if (HasCommandPin())
            throw UserError("CannotBuildMultipleCommandPins");
    } else if (!HasCommandPin()) {
        throw UserError("CannotManagePlanetWithoutCommandCenter");
    }
    uint8 kind = sPIDataMgr.GetPinKind(typeID);
    if ((kind == PI::Pin::Kind::None) or (kind == PI::Pin::Kind::Base))
        throw UserError("InvalidPinType");
    if (sPIDataMgr.HasTypeAttribute(typeID, AttrPlanetRestriction)) {
        uint16 planetTypeRestriction = (uint16)sPIDataMgr.GetTypeAttribute(typeID, AttrPlanetRestriction);
        if ((planetTypeRestriction != 0) and (planetTypeRestriction != m_planetTypeID))
            throw UserError("CannotCreatePinWrongPlanetType");
    }
    double cpuUsage = sPIDataMgr.GetTypeAttribute(typeID, AttrCpuLoad);
    double cpuOutput = sPIDataMgr.GetTypeAttribute(typeID, AttrCpuOutput);
    double powerUsage = sPIDataMgr.GetTypeAttribute(typeID, AttrPowerLoad);
    double powerOutput = sPIDataMgr.GetTypeAttribute(typeID, AttrPowerOutput);
    double colonyCpuUsage(0), colonyCpuSupply(0), colonyPowerUsage(0), colonyPowerSupply(0);
    GetResourceUsage(colonyCpuUsage, colonyCpuSupply, colonyPowerUsage, colonyPowerSupply);
    if ((cpuOutput <= 0) and (cpuUsage + colonyCpuUsage > colonyCpuSupply))
        throw UserError("CannotAddToColonyCPUUsageExceeded").AddTypeName("typeName", typeID);
    if ((powerOutput <= 0) and (powerUsage + colonyPowerUsage > colonyPowerSupply))
        throw UserError("CannotAddToColonyPowerUsageExceeded").AddTypeName("typeName", typeID);
}

void ColonySim::ValidateRemovePin(uint32 charID, uint32 pinID)
{
    // BaseColony.ValidateRemovePin
    if (GetPin(pinID) == nullptr)
        throw UserError("PinDoesNotExist");
}

void ColonySim::ValidateCreateLink(uint32 charID, uint32 endpoint1, uint32 endpoint2, uint16 linkTypeID, uint8 level)
{
    // BaseColony.ValidateCreateLink
    if (endpoint1 > endpoint2)
        std::swap(endpoint1, endpoint2);
    if (!HasCommandPin())
        throw UserError("CannotManagePlanetWithoutCommandCenter");
    if (GetLink(endpoint1, endpoint2) != nullptr)
        throw UserError("LinkAlreadyExists");
    PI_Pin* pPin1 = GetPin(endpoint1);
    PI_Pin* pPin2 = GetPin(endpoint2);
    if ((pPin1 == nullptr) or (pPin2 == nullptr))
        throw UserError("PinDoesNotExist");
    if ((pPin1->ownerID != charID) or (pPin2->ownerID != charID))
        throw UserError("CanOnlyManageOwnPins");
    if (endpoint1 == endpoint2)
        throw UserError("CannotLinkPinsToThemselves");
    double linkLength = m_planetRadius * PI::Sim::GetAngleBetween(pPin1->latitude, pPin1->longitude, pPin2->latitude, pPin2->longitude);
    int64 addlCpuUsage = GetLinkCpuUsage(linkTypeID, linkLength, level);
    int64 addlPowerUsage = GetLinkPowerUsage(linkTypeID, linkLength, level);
    double colonyCpuUsage(0), colonyCpuSupply(0), colonyPowerUsage(0), colonyPowerSupply(0);
    GetResourceUsage(colonyCpuUsage, colonyCpuSupply, colonyPowerUsage, colonyPowerSupply);
    if (addlCpuUsage + colonyCpuUsage > colonyCpuSupply)
        throw UserError("CannotAddToColonyCPUUsageExceeded").AddTypeName("typeName", linkTypeID);
    if (addlPowerUsage + colonyPowerUsage > colonyPowerSupply)
        throw UserError("CannotAddToColonyPowerUsageExceeded").AddTypeName("typeName", linkTypeID);
}

void ColonySim::ValidateRemoveLink(uint32 charID, uint32 endpoint1, uint32 endpoint2)
{
    // BaseColony.ValidateRemoveLink
    if (GetLink(endpoint1, endpoint2) == nullptr)
        throw UserError("LinkDoesNotExist");
}

void ColonySim::ValidateCreateRoute(uint32 charID, const std::vector<uint32>& path, uint16 typeID, uint32 quantity)
{
    // BaseColony.ValidateCreateRoute
    if ((typeID == 0) or (quantity < 1))
        throw UserError("CreateRouteWithoutCommodities");
    if (path.size() < 2)
        throw UserError("CreateRouteTooShort");
    if (path.size() - 2 > PI::Sim::MaxWaypoints)
        throw UserError("CannotRouteTooManyWaypoints");
    PI_Pin* pSource = GetPin(path.front());
    PI_Pin* pDest = GetPin(path.back());
    if ((pSource == nullptr) or (pDest == nullptr))
        throw UserError("RouteFailedValidationPinDoesNotExist");
    if (IsStorage(*pSource) and !IsConsumer(*pDest))
        throw UserError("CannotRouteStorageMustGoToConsumer");
    if (!IsConsumer(*pDest) and !IsStorage(*pDest))
        throw UserError("CannotRouteDestinationWillNotAccept");
    if (IsProducer(*pSource)) {
        std::map<uint16, double> totalCommodsUsed;
        std::map<uint32, std::set<uint32>>::const_iterator itr = m_colony.routesBySource.find(path.front());
        if (itr != m_colony.routesBySource.end())
            for (auto cur : itr->second) {
                PI_Route* pRoute = GetRoute(cur);
                if (pRoute != nullptr)
                    totalCommodsUsed[pRoute->commodityTypeID] += pRoute->commodityQuantity;
            }
        std::map<uint16, double> sourceProducts;
        GetProductMaxOutput(*pSource, sourceProducts);
        if (sourceProducts.find(typeID) == sourceProducts.end())
            throw UserError("CreateRouteCommodityNotProduced").AddTypeName("typeName", typeID);
        totalCommodsUsed[typeID] += quantity;
        if (totalCommodsUsed[typeID] > sourceProducts[typeID])
            throw UserError("CreateRouteCommodityProductionTooSmall").AddTypeName("typeName", typeID);
    }
    if (IsConsumer(*pDest))
        if (pDest->demands.find(typeID) == pDest->demands.end())
            throw UserError("CreateRouteDestinationCannotAcceptCommodity").AddTypeName("typeName", typeID);

    double additionalBandwidth = sPIDataMgr.GetVolume(typeID) * quantity;
    int64 pinCycleTime = 0;
    if (IsProducer(*pSource)) {
        pinCycleTime = GetCycleTime(*pSource);
    } else if (IsStorage(*pSource) and IsConsumer(*pDest)) {
        pinCycleTime = GetCycleTime(*pDest);
    } else {
        pinCycleTime = std::max(GetCycleTime(*pSource), GetCycleTime(*pDest));
    }
    if (pinCycleTime > 0)
        additionalBandwidth = PI::Sim::GetBandwidth(additionalBandwidth, pinCycleTime);

    uint32 prevID = 0;
    bool first = true;
    for (auto pinID : path) {
        PI_Pin* pPin = GetPin(pinID);
        if (pPin == nullptr)
            throw UserError("RouteFailedValidationPinDoesNotExist");
        if (pPin->ownerID != charID)
            throw UserError("RouteFailedValidationPinNotYours");
        if (first) {
            first = false;
            prevID = pinID;
            continue;
        }
        PI_Link* pLink = GetLink(prevID, pinID);
        if (pLink == nullptr)
            throw UserError("RouteFailedValidationLinkDoesNotExist");
        // BaseLink.CanRouteBandwidth
        if (GetLinkBandwidthUsage(*pLink) + additionalBandwidth > GetLinkTotalBandwidth(*pLink))
            throw UserError("RouteFailedValidationCannotRouteCommodities");
        prevID = pinID;
    }
}

void ColonySim::ValidateRemoveRoute(uint32 charID, uint32 routeID)
{
    // BaseColony.ValidateRemoveRoute
    if (GetRoute(routeID) == nullptr)
        throw UserError("RouteDoesNotExist");
}

void ColonySim::ValidateInstallSchematic(uint32 charID, uint32 pinID, uint16 schematicID)
{
    // BaseColony.ValidateInstallSchematic
    PI_Pin* pPin = GetPin(pinID);
    if (pPin == nullptr)
        throw UserError("PinDoesNotExist");
    if (pPin->ownerID != charID)
        throw UserError("CanOnlyManageOwnPins");
    if (pPin->kind != PI::Pin::Kind::Process)
        throw UserError("CannotAssignSchematicToPinType");
    if (sPIDataMgr.GetSchematic(schematicID) == nullptr)
        throw UserError("InvalidSchematic");
}

void ColonySim::ValidateSetLinkLevel(uint32 charID, uint32 endpoint1, uint32 endpoint2, uint8 newLevel)
{
    // BaseColony.ValidateSetLinkLevel
    PI_Link* pLink = GetLink(endpoint1, endpoint2);
    if (pLink == nullptr)
        throw UserError("PinDoesNotExist");
    if (newLevel > PI::Sim::LinkMaxUpgrade)
        throw UserError("CannotUpgradeLinkAlreadyMaxed").AddTypeName("typeName", pLink->typeID);
    if (newLevel > pLink->level) {
        double distance = GetLinkDistance(*pLink);
        int64 cpuDelta = GetLinkCpuUsage(pLink->typeID, distance, newLevel) - GetLinkCpuUsage(pLink->typeID, distance, pLink->level);
        int64 powerDelta = GetLinkPowerUsage(pLink->typeID, distance, newLevel) - GetLinkPowerUsage(pLink->typeID, distance, pLink->level);
        double colonyCpuUsage(0), colonyCpuSupply(0), colonyPowerUsage(0), colonyPowerSupply(0);
        GetResourceUsage(colonyCpuUsage, colonyCpuSupply, colonyPowerUsage, colonyPowerSupply);
        if (cpuDelta + colonyCpuUsage > colonyCpuSupply)
            throw UserError("CannotAddToColonyCPUUsageExceeded").AddTypeName("typeName", pLink->typeID);
        if (powerDelta + colonyPowerUsage > colonyPowerSupply)
            throw UserError("CannotAddToColonyPowerUsageExceeded").AddTypeName("typeName", pLink->typeID);
    }
}

void ColonySim::ValidateCommandCenterUpgrade(uint8 level)
{
    // BaseColony.ValidateCommandCenterUpgrade
    if (level <= m_colony.level)
        throw CustomError ("We can't downgrade");
    if (level > PI::Sim::MaxCommandUpgradeLevel)
        throw CustomError ("Trying to upgrade past maximum level");
}

double ColonySim::ValidateExpeditedTransfer(uint32 charID, const std::vector<uint32>& path, const std::map<uint16, uint32>& commodities, int64 runTime)
{
    // BaseColony.ValidateExpeditedTransfer
    if (path.size() < 2)
        throw UserError("CreateRouteTooShort");
    if (commodities.empty())
        throw UserError("CreateRouteWithoutCommodities");
    PI_Pin* pSource = GetPin(path.front());
    PI_Pin* pDest = GetPin(path.back());
    if ((pSource == nullptr) or (pDest == nullptr))
        throw UserError("RouteFailedValidationPinDoesNotExist");
    if (!IsStorage(*pSource))
        throw UserError("RouteFailedValidationExpeditedSourceNotStorage");
    for (auto cur : commodities) {
        std::map<uint16, uint32>::const_iterator itr = pSource->contents.find(cur.first);
        if (itr == pSource->contents.end())
            throw UserError("RouteFailedValidationExpeditedSourceLacksCommodity").AddTypeName("typeName", cur.first);
        if (cur.second > itr->second)
            throw UserError("RouteFailedValidationExpeditedSourceLacksCommodityQty").AddTypeName("typeName", cur.first).AddAmount("qty", (int)cur.second);
        if (CanAccept(*pDest, cur.first, cur.second) < 1)
            throw UserError("RouteFailedValidationExpeditedDestinationCannotAccept").AddTypeName("typeName", cur.first);
    }
    double minBandwidth = -1;
    uint32 prevID = 0;
    bool first = true;
    for (auto pinID : path) {
        PI_Pin* pPin = GetPin(pinID);
        if (pPin == nullptr)
            throw UserError("RouteFailedValidationPinDoesNotExist");
        if (pPin->ownerID != charID)
            throw UserError("RouteFailedValidationPinNotYours");
        if (first) {
            first = false;
            prevID = pinID;
            continue;
        }
        PI_Link* pLink = GetLink(prevID, pinID);
        if (pLink == nullptr)
            throw UserError("RouteFailedValidationLinkDoesNotExist");
        double bandwidth = GetLinkTotalBandwidth(*pLink);
        if ((minBandwidth < 0) or (bandwidth < minBandwidth))
            minBandwidth = bandwidth;
        prevID = pinID;
    }
    if (minBandwidth <= 0)
        throw UserError("RouteFailedValidationNoBandwidthAvailable");
    // StoragePin.CanTransfer
    for (auto cur : commodities) {
        std::map<uint16, uint32>::const_iterator itr = pSource->contents.find(cur.first);
        if ((itr == pSource->contents.end()) or (cur.second > itr->second))
            throw UserError("RouteFailedValidationExpeditedSourceNotReady");
    }
    if ((pSource->lastRunTime != 0) and (pSource->lastRunTime > runTime))
        throw UserError("RouteFailedValidationExpeditedSourceNotReady");
    return minBandwidth;
}

void ColonySim::ValidateAddExtractorHead(uint32 pinID, uint16 headID, double latitude, double longitude)
{
    // BaseColony.ValidateAddExtractorHead
    if ((latitude < 0) or (latitude > M_PI))
        throw CustomError ("Invalid value for latitude - must be between 0..pi");
    if ((longitude < 0) or (longitude > PI2))
        throw CustomError ("Invalid value for longitude - must be between 0..2pi");
    PI_Pin* pPin = GetPin(pinID);
    if (pPin == nullptr)
        throw UserError("PinDoesNotExist");
    if (!IsExtractor(*pPin))
        throw UserError("PinDoesNotHaveHeads");
    if (pPin->heads.find(headID) != pPin->heads.end())
        throw UserError("CannotAddHeadAlreadyExists");
    if (pPin->heads.size() >= PI::Sim::EcuMaxHeads)
        throw UserError("CannotPlaceHeadLimitReached");
    double cpuDelta = GetPinCpuUsage(*pPin, pPin->heads.size() + 1) - GetPinCpuUsage(*pPin);
    double powerDelta = GetPinPowerUsage(*pPin, pPin->heads.size() + 1) - GetPinPowerUsage(*pPin);
    double colonyCpuUsage(0), colonyCpuSupply(0), colonyPowerUsage(0), colonyPowerSupply(0);
    GetResourceUsage(colonyCpuUsage, colonyCpuSupply, colonyPowerUsage, colonyPowerSupply);
    if (cpuDelta + colonyCpuUsage > colonyCpuSupply)
        throw UserError("CannotAddToColonyCPUUsageExceeded").AddUELocalization("typeName", "UI/PI/Common/ExtractorHead");
    if (powerDelta + colonyPowerUsage > colonyPowerSupply)
        throw UserError("CannotAddToColonyPowerUsageExceeded").AddUELocalization("typeName", "UI/PI/Common/ExtractorHead");
    double angleBetween = PI::Sim::GetAngleBetween(pPin->latitude, pPin->longitude, latitude, longitude);
    double areaOfInfluence = sPIDataMgr.GetTypeAttribute(pPin->typeID, AttrECUAreaOfInfluence);
    if (angleBetween > areaOfInfluence)
        throw UserError("CannotPlaceHeadTooFarAway").AddDistance("maxDist", areaOfInfluence * m_planetRadius);
}

void ColonySim::ValidateRemoveExtractorHead(uint32 pinID, uint16 headID)
{
    // BaseColony.ValidateRemoveExtractorHead
    PI_Pin* pPin = GetPin(pinID);
    if (pPin == nullptr)
        throw UserError("PinDoesNotExist");
    if (!IsExtractor(*pPin))
        throw UserError("PinDoesNotHaveHeads");
    if (pPin->heads.find(headID) == pPin->heads.end())
        throw UserError("CannotRemoveHeadNotPresent");
}

void ColonySim::ValidateMoveExtractorHead(uint32 pinID, uint16 headID, double latitude, double longitude)
{
    // BaseColony.ValidateMoveExtractorHead
    if ((latitude < 0) or (latitude > M_PI))
        throw CustomError ("Invalid value for latitude - must be between 0..pi");
    if ((longitude < 0) or (longitude > PI2))
        throw CustomError ("Invalid value for longitude - must be between 0..2pi");
    PI_Pin* pPin = GetPin(pinID);
    if (pPin == nullptr)
        throw UserError("PinDoesNotExist");
    if (!IsExtractor(*pPin))
        throw UserError("PinDoesNotHaveHeads");
    if (pPin->heads.find(headID) == pPin->heads.end())
        throw UserError("CannotMoveHeadNotPresent");
    double angleBetween = PI::Sim::GetAngleBetween(pPin->latitude, pPin->longitude, latitude, longitude);
    double areaOfInfluence = sPIDataMgr.GetTypeAttribute(pPin->typeID, AttrECUAreaOfInfluence);
    if (angleBetween > areaOfInfluence)
        throw UserError("CannotPlaceHeadTooFarAway").AddDistance("maxDist", angleBetween * m_planetRadius);
}

void ColonySim::ValidateInstallProgram(uint32 pinID, uint16 typeID, double headRadius)
{
    // BaseColony.ValidateInstallProgram
    PI_Pin* pPin = GetPin(pinID);
    if (pPin == nullptr)
        throw UserError("PinDoesNotExist");
    if (!IsExtractor(*pPin))
        throw UserError("PinDoesNotHaveHeads");
    if ((headRadius < PI::Sim::RadiusDrillAreaMin) or (headRadius > PI::Sim::RadiusDrillAreaMax))
        throw CustomError ("Cannot install a program with a completely bonkers radius");
}

/* ----------------------------------------------------------------------------------------------------------------
 *  BaseColony.CreateProgram
 */

void ColonySim::CreateProgram(const ResourceField& field, uint32 ecuPinID, uint16 ecuTypeID, uint16 resourceTypeID, const std::vector<Head>& heads,
                              double headRadius, uint32& maxValue, int64& cycleTime, uint32& numCycles, std::map<uint16, double>* overlapModifiers/*nullptr*/)
{
    double overlapFactor = sPIDataMgr.GetTypeAttribute(ecuTypeID, AttrECUOverlapFactor);
    double maxVolume = sPIDataMgr.GetTypeAttribute(ecuTypeID, AttrECUMaxVolume);

    // heads are (index, latitude, longitude), seen as SurfacePoint(theta=longitude, phi=latitude)
    std::map<uint16, double> modifiers, valueByIndex;
    for (auto cur : heads) {
        modifiers[cur.id] = 1.0;
        // builder.GetValueAt(harmonic, 2pi - theta, phi)
        valueByIndex[cur.id] = std::max(field(PI2 - cur.longitude, cur.latitude), 0.0);
    }

    // OverlapOwnHeads
    for (size_t i = 0; i < heads.size(); ++i)
        for (size_t j = i + 1; j < heads.size(); ++j) {
            double dist = PI::Sim::GetAngleBetween(heads[i].latitude, heads[i].longitude, heads[j].latitude, heads[j].longitude);
            if (dist < headRadius * 2) {
                double radiusSquared = headRadius * headRadius;
                double overlap = (2 * radiusSquared * std::acos(0.5 * (dist / headRadius)) - 0.5 * dist * std::sqrt(4 * radiusSquared - dist * dist)) / (M_PI * radiusSquared);
                double modifier = std::min(1.0, std::max(0.0, 1 - overlap * overlapFactor));
                valueByIndex[heads[i].id] *= modifier;
                valueByIndex[heads[j].id] *= modifier;
                modifiers[heads[i].id] *= modifier;
                modifiers[heads[j].id] *= modifier;
            }
        }

    // OverlapOthersHeads:  this colony's other ecus extracting the same resource
    double headArea = M_PI * headRadius * headRadius;
    for (auto head : heads) {
        double modifier = 1;
        for (auto& cur : m_colony.pins) {
            if (modifier == 0)
                break;
            if ((cur.first == ecuPinID) or (cur.second.kind != PI::Pin::Kind::ECU) or (cur.second.programType != resourceTypeID))
                continue;
            // EcuPin.GetExtractorHeadRadius
            double r = (cur.second.headRadius > 0 ? cur.second.headRadius : 0.01), R = headRadius;
            for (auto& other : cur.second.heads) {
                double d = PI::Sim::GetAngleBetween(head.latitude, head.longitude, other.second.latitude, other.second.longitude);
                if (d > R + r)
                    continue;
                double overlap = 1;
                if (d > 0) {
                    double r2 = r * r, R2 = R * R, d2 = d * d;
                    double alpha = (d2 + r2 - R2) / (2 * d * r);
                    if (alpha < -1) {
                        overlap = M_PI * r2 / headArea;
                    } else if (alpha > 1) {
                        overlap = 1;
                    } else {
                        double f1 = r2 * std::acos(alpha);
                        double f2 = R2 * std::acos(std::min(1.0, std::max(-1.0, (d2 + R2 - r2) / (2 * d * R))));
                        double f3 = 0.5 * std::sqrt(std::max(0.0, (-d + r + R) * (d + r - R) * (d - r + R) * (d + r + R)));
                        overlap = (f1 + f2 - f3) / headArea;
                    }
                }   // heads on the same spot overlap fully (python divides by zero here)
                modifier *= std::min(1.0, std::max(0.0, 1 - overlap * 2 * overlapFactor));
            }
        }
        valueByIndex[head.id] *= modifier;
        modifiers[head.id] *= modifier;
    }

    if (overlapModifiers != nullptr) {
        overlapModifiers->clear();
        for (auto cur : modifiers)
            (*overlapModifiers)[cur.first] = 1.0 - cur.second;
    }

    double sum = 0;
    for (auto cur : valueByIndex)
        sum += cur.second;
    double value = maxVolume * sum;
    double programLength = PI::Sim::GetProgramLengthFromHeadRadius(headRadius);
    double cycleHours = PI::Sim::GetCycleTimeFromProgramLength(programLength);
    numCycles = (uint32)(programLength / cycleHours);
    cycleTime = (int64)(cycleHours * HOUR);
    maxValue = (value > 0 ? (uint32)value : 0);
}
