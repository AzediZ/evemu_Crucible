/*
 * SystemCommands.cpp
 *   this file is commands related to an item's creation/deletion and location/position
 *
 */

#include "EVE_Defines.h"
#include "eve-server.h"

#include "Client.h"
#include "ConsoleCommands.h"
#include "log/logsys.h"
#include "npc/NPC.h"
#include "npc/NPCAI.h"
#include "admin/AllCommands.h"
#include "admin/CommandDB.h"
#include "inventory/AttributeEnum.h"
#include "inventory/InventoryDB.h"
#include "inventory/InventoryItem.h"
#include "manufacturing/Blueprint.h"
#include "map/MapConnections.h"
#include "map/MapData.h"
#include "npc/Drone.h"
#include "npc/LawEnforcement.h"
#include "system/Damage.h"
#include "system/DestinyManager.h"
#include "system/SystemManager.h"
#include "system/SystemBubble.h"
#include "system/cosmicMgrs/BeltMgr.h"
#include "admin/CommandHelper.h"
#include "fleet/FleetService.h"
#include "station/StationDataMgr.h"
#include "inventory/Inventory.h"
#include "planet/CustomsOffice.h"
#include "pos/PosMgrDB.h"
#include "pos/Tower.h"
#include "pos/Structure.h"
#include "system/sov/SovereigntyDataMgr.h"
#include "tables/invGroups.h"
#include "tables/invCategories.h"

PyResult Command_goto(Client* pClient, CommandDB* db, EVEServiceManager &services, const Seperator& args)
{
    if (args.argCount() != 4
        || !args.isNumber(1)
        || !args.isNumber(2)
        || !args.isNumber(3))
    {
        throw CustomError ("Correct Usage: /goto [x coord] [y coor] [z coord]");
    }

    GPoint p(atoll(args.arg(1).c_str()),
             atoll(args.arg(2).c_str()),
             atoll(args.arg(3).c_str()));

    pClient->MoveToPosition(p);
    return new PyString("Goto successful.");
}

PyResult Command_translocate(Client* pClient, CommandDB* db, EVEServiceManager &services, const Seperator& args) {
    return Command_tr(pClient,db,services,args);
}

// `UpdateBubble` is a reusable function that synchronizes the player's position
// and ensures that their bubble exists.
static PyResult UpdateBubble(Client *pClient) {
    if (!pClient->IsInSpace())
        throw CustomError ("You're not in space.");
    if (pClient->GetShipSE()->DestinyMgr() == nullptr)
        pClient->SetDestiny(NULL_ORIGIN);
    if (pClient->GetShipSE()->SysBubble() == nullptr)
        pClient->EnterSystem(pClient->GetSystemID());
    if (pClient->IsSessionChange()) {
        pClient->SendInfoModalMsg("Session Change Active.  Wait %u seconds before issuing another command.",
                                  pClient->GetSessionChangeTime());
        return new PyString("SessionChange Active.  Request Denied.");
    }

    pClient->GetShipSE()->DestinyMgr()->SetPosition(pClient->GetShipSE()->GetPosition(), true);

    SystemBubble *pBubble = pClient->GetShipSE()->SysBubble();
    if (pBubble == nullptr) {
        sBubbleMgr.Add(pClient->GetShipSE());
        pBubble = pClient->GetShipSE()->SysBubble();
    }
    pBubble->SendAddBalls(pClient->GetShipSE());

    pClient->SetStateSent(false);
    pClient->GetShipSE()->DestinyMgr()->SendSetState();
    pClient->SetSessionTimer();
    return new PyString("Update sent.");
}

/*   hardcoded menu translocates in client
 * ('/tr me ' + str(mapItem.itemID),)   << mapItem
 * ('/tr me ' + str(charID),)           << charItem
 * ('/tr me ' + str(itemID),)           << slimItem
 *
 * ('/tr me last')                      << last Station
 * ('/tr me %s' % stationID)
 * ('/tr me offset=randvec(100au)')     << hop (x,y,z)
 * ('/tr me home')                      << home station
 * ('/tr %d' % solarSystemID)
 * ('/tr %d' % locationID)
 * ('/tr me me offset=%d,%d,%d' % (int(v.x), int(v.y), int(v.z)))
 * ('/tr %d me noblock' % charID)       << used to group-move all players in <chat>
 *
 *  /tr <who> [<where>] [offset=x,y,z | offset=randvec(<dist>)] [noblock]
 *  /tr <where>                          (who = me)
 *  /tr [<who>] <systemID> x y z
 *  /tr help
 */
namespace {
    struct TrDestination {
        uint32 locationID = 0;
        GPoint pt = NULL_ORIGIN;
        std::string name;
    };

    // the radius of a pilot's ship, docked or not
    double TrShipRadius(Client* pClient) {
        if (pClient->GetShipSE() != nullptr)
            return pClient->GetShipSE()->GetRadius();
        if (pClient->GetShip().get() != nullptr)
            return pClient->GetShip()->radius();
        return 50;
    }

    // a point next to a pilot: their station, or 100m off their ship's hull
    bool TrNextTo(Client* pTarget, Client* pSubject, TrDestination& dest) {
        if (pTarget->IsDocked()) {
            dest.locationID = pTarget->GetLocationID();
            dest.name = stDataMgr.GetStationName(dest.locationID);
            return true;
        }
        if (!pTarget->IsInSpace() or (pTarget->GetShipSE() == nullptr))
            return false;
        dest.locationID = pTarget->GetSystemID();
        dest.pt = pTarget->GetShipSE()->GetPosition();
        if (pTarget != pSubject)
            dest.pt.MakeRandomPointOnSphere(100 + TrShipRadius(pTarget) + TrShipRadius(pSubject));
        dest.name = std::string(pTarget->GetName()) + " in " + sDataMgr.GetSystemName(dest.locationID);
        return true;
    }

    // a point off an object of this radius
    void TrOffHull(TrDestination& dest, double radius, Client* pSubject) {
        dest.pt.MakeRandomPointOnSphere(radius + 500 + TrShipRadius(pSubject));
    }

    /* who to move:  me, fleet (every online member of your fleet), a charID, a ship or item (its owner) or a name.
     *  only online pilots.  throws when there is nobody
     */
    void TrResolveSubjects(Client* self, const Seperator& args, size_t idx, std::vector<Client*>& out) {
        const std::string& arg = args.arg(idx);
        if (arg == "me") {
            out.push_back(self);
        } else if (arg == "fleet") {
            if (!self->InFleet())
                throw CustomError ("Translocate: You are not in a fleet.");
            out = sFltSvc.GetFleetClients(self->GetFleetID());
        } else {
            Client* pClient(cmd_find_client(args, idx, self));
            if (pClient == nullptr) {
                // a ship or item that isn't loaded:  its owner
                if (args.isNumber(idx)) {
                    uint32 id(strtoul(arg.c_str(), nullptr, 10));
                    if (IsPlayerItem(id)) {
                        InventoryItemRef iRef(sItemFactory.GetItemRef(id));
                        if ((iRef.get() != nullptr) and IsCharacterID(iRef->ownerID()))
                            pClient = sEntityList.FindClientByCharID(iRef->ownerID());
                    }
                }
            }
            if (pClient == nullptr)
                throw CustomError ("Translocate: %s is not online", arg.c_str());
            out.push_back(pClient);
        }
        if (out.empty())
            throw CustomError ("Translocate: nobody to move");
    }

    // a celestial, stargate or solar system, from mapDenormalize
    bool TrMapItem(uint32 itemID, Client* pSubject, TrDestination& dest) {
        DBQueryResult res;
        DBResultRow row;
        if (IsSolarSystemID(itemID)) {
            // next to the sun, not in it
            if (!sDataMgr.IsSolarSystem(itemID))
                return false;
            dest.locationID = itemID;
            dest.pt = NULL_ORIGIN;
            dest.name = sDataMgr.GetSystemName(itemID);
            double radius(0);
            if (sDatabase.RunQuery(res, "SELECT radius FROM mapDenormalize WHERE solarSystemID = %u AND groupID = %u LIMIT 1",
                                   itemID, EVEDB::invGroups::Sun) and res.GetRow(row) and !row.IsNull(0))
                radius = row.GetDouble(0);
            TrOffHull(dest, radius, pSubject);
            return true;
        }
        if (!sDatabase.RunQuery(res, "SELECT solarSystemID, x, y, z, radius, itemName FROM mapDenormalize WHERE itemID = %u LIMIT 1", itemID)) {
            codelog(DATABASE__ERROR, "Error in query: %s", res.error.c_str());
            throw CustomError ("Translocate: Unable to find celestial location %u", itemID);
        }
        if (!res.GetRow(row))
            throw CustomError ("Translocate: No data for celestial location %u", itemID);
        dest.locationID = row.GetUInt(0);
        dest.pt = GPoint(row.GetDouble(1), row.GetDouble(2), row.GetDouble(3));
        dest.name = row.IsNull(5) ? std::to_string(itemID) : row.GetText(5);
        // asteroid belts have no real radius; planets, moons and gates do
        TrOffHull(dest, row.IsNull(4) ? 0 : row.GetDouble(4), pSubject);
        return true;
    }

    /* where to:  me, home, last, moon, planet, a charID (online), a stationID, a solar system, celestial or stargate,
     *  anything in space in your system or the subject's (NPCs, asteroids, drones, wrecks, structures, dungeon objects),
     *  a player item (its position, its station, or its owner's location), or a name (pilot, solar system, station)
     */
    void TrResolveDestination(Client* self, Client* pSubject, CommandDB* db, const Seperator& args, size_t idx, TrDestination& dest) {
        const std::string& arg = args.arg(idx);
        if (arg == "me") {
            if (!TrNextTo(self, pSubject, dest))
                throw CustomError ("Translocate: you are neither docked nor in space");
            return;
        } else if (arg == "home") {
            dest.locationID = pSubject->GetCloneStationID();
            if (!sDataMgr.IsStation(dest.locationID))
                throw CustomError ("Translocate: %s has no home station", pSubject->GetName());
            dest.name = stDataMgr.GetStationName(dest.locationID);
            return;
        } else if (arg == "last") {
            dest.locationID = pSubject->GetDockStationID();
            if (!sDataMgr.IsStation(dest.locationID))
                throw CustomError ("Translocate: %s hasn't docked anywhere yet", pSubject->GetName());
            dest.name = stDataMgr.GetStationName(dest.locationID);
            return;
        } else if ((arg == "moon") or (arg == "planet")) {
            dest.locationID = pSubject->GetSystemID();
            dest.pt = (arg == "moon" ? sMapData.GetRandPointOnMoon(dest.locationID) : sMapData.GetRandPointOnPlanet(dest.locationID));
            dest.name = std::string("a random ") + arg + " in " + sDataMgr.GetSystemName(dest.locationID);
            return;
        }

        if (args.isNumber(idx)) {
            uint32 id(strtoul(arg.c_str(), nullptr, 10));
            if (IsCharacterID(id)) {
                Client* pTarget(sEntityList.FindClientByCharID(id));
                if (pTarget == nullptr)
                    throw CustomError ("Translocate: character %u is not online", id);
                if (!TrNextTo(pTarget, pSubject, dest))
                    throw CustomError ("Translocate: %s is neither docked nor in space", pTarget->GetName());
                return;
            }
            if (IsStationID(id)) {
                if (!sDataMgr.IsStation(id))
                    throw CustomError ("Translocate: Invalid Location %u", id);
                dest.locationID = id;
                dest.name = stDataMgr.GetStationName(id);
                return;
            }
            if (IsRegionID(id) or IsConstellationID(id)) {
                // the starmap's 'TR me here!' on a region or constellation:  a random system in it
                DBQueryResult res;
                DBResultRow row;
                if (!sDatabase.RunQuery(res, "SELECT solarSystemID FROM mapSolarSystems WHERE %s = %u ORDER BY RAND() LIMIT 1",
                                        (IsRegionID(id) ? "regionID" : "constellationID"), id) or !res.GetRow(row))
                    throw CustomError ("Translocate: Invalid Location %u", id);
                id = row.GetUInt(0);
            }
            if (IsSolarSystemID(id) or IsCelestialID(id) or IsStargateID(id)) {
                if (!TrMapItem(id, pSubject, dest))
                    throw CustomError ("Translocate: Invalid Location %u", id);
                return;
            }
            // anything in space here:  NPCs, asteroids, drones, wrecks, structures, ships
            SystemEntity* pSE(nullptr);
            if (self->SystemMgr() != nullptr)
                pSE = self->SystemMgr()->GetSE(id);
            if ((pSE == nullptr) and (pSubject->SystemMgr() != nullptr))
                pSE = pSubject->SystemMgr()->GetSE(id);
            if (pSE != nullptr) {
                dest.locationID = pSE->SystemMgr()->GetID();
                dest.pt = pSE->GetPosition();
                dest.name = std::string(pSE->GetName()) + " in " + sDataMgr.GetSystemName(dest.locationID);
                TrOffHull(dest, pSE->GetRadius(), pSubject);
                return;
            }
            if (IsPlayerItem(id)) {
                InventoryItemRef iRef(sItemFactory.GetItemRef(id));
                if (iRef.get() == nullptr)
                    throw CustomError ("Translocate: item %u was not found", id);
                if (sDataMgr.IsSolarSystem(iRef->locationID())) {
                    dest.locationID = iRef->locationID();
                    dest.pt = iRef->position();
                    dest.name = std::string(iRef->name()) + " in " + sDataMgr.GetSystemName(dest.locationID);
                    TrOffHull(dest, iRef->radius(), pSubject);
                    return;
                }
                if (sDataMgr.IsStation(iRef->locationID())) {
                    dest.locationID = iRef->locationID();
                    dest.name = stDataMgr.GetStationName(dest.locationID);
                    return;
                }
                // in a container, hangar or ship:  where its owner is
                Client* pOwner(sEntityList.FindClientByCharID(iRef->ownerID()));
                if ((pOwner != nullptr) and TrNextTo(pOwner, pSubject, dest))
                    return;
                throw CustomError ("Translocate: %s (%u) is inside something, and its owner is not online", iRef->name(), id);
            }
            throw CustomError ("Translocate: %u is not a location, a pilot, or anything in space in your system", id);
        }

        // a name:  an online pilot, then a solar system, then a station
        Client* pTarget(cmd_find_client(args, idx, self));
        if (pTarget != nullptr) {
            if (!TrNextTo(pTarget, pSubject, dest))
                throw CustomError ("Translocate: %s is neither docked nor in space", pTarget->GetName());
            return;
        }
        // this hits db directly, so test for possible sql injection code
        for (const auto cur : badCharsSearch)
            if (EvE::icontains(arg, cur))
                throw CustomError ("Location contains invalid characters");
        uint32 locationID(db->GetSolarSystem(arg.c_str()));
        if (sDataMgr.IsSolarSystem(locationID)) {
            if (!TrMapItem(locationID, pSubject, dest))
                throw CustomError ("Translocate: Invalid Location %u", locationID);
            return;
        }
        locationID = db->GetStation(arg.c_str());
        if (!sDataMgr.IsStation(locationID))
            throw CustomError ("Translocate: %s is neither an online pilot, a solar system nor a station name", arg.c_str());
        dest.locationID = locationID;
        dest.name = stDataMgr.GetStationName(locationID);
    }

    // offset=x,y,z (meters) or offset=randvec(<distance>).  false when there is no offset
    bool TrParseOffset(const Seperator& args, GVector& offset) {
        std::string text(cmd_parse_eq_arg(args, "offset="));
        if (text.empty())
            return false;
        if (text.compare(0, 8, "randvec(") == 0) {
            size_t end(text.find(')'));
            double distance(0);
            if ((end == std::string::npos) or !cmd_parse_distance(text.substr(8, end - 8), distance))
                throw CustomError ("Translocate: bad offset %s.  Use offset=randvec(100km)", text.c_str());
            GPoint random(NULL_ORIGIN);
            random.MakeRandomPointOnSphere(distance);
            offset = random;
            return true;
        }
        double v[3];
        size_t start(0);
        for (int i = 0; i < 3; ++i) {
            size_t comma(text.find(',', start));
            std::string part(text.substr(start, (comma == std::string::npos ? std::string::npos : comma - start)));
            char* end(nullptr);
            v[i] = strtod(part.c_str(), &end);
            if (part.empty() or (*end != '\0') or ((i < 2) and (comma == std::string::npos)) or ((i == 2) and (comma != std::string::npos)))
                throw CustomError ("Translocate: bad offset %s.  Use offset=x,y,z in meters", text.c_str());
            start = comma + 1;
        }
        offset = GVector(v[0], v[1], v[2]);
        return true;
    }

    // moves one pilot.  returns false (and tells the caller why) when the pilot can't be moved now
    bool TrMove(Client* self, Client* pClient, uint32 locationID, const GPoint& pt) {
        if (pClient->IsSessionChange()) {
            self->SendNotifyMsg("Translocate: %s has a session change active.  Wait %u seconds and try again.",
                                pClient->GetName(), pClient->GetSessionChangeTime());
            return false;
        }
        bool undocked(false);
        if (pClient->IsDocked() and sDataMgr.IsSolarSystem(locationID)) {
            pClient->UndockFromStation();
            undocked = true;
        }
        pClient->JumpOutEffect(pClient->GetLocationID());
        pClient->MoveToLocation(locationID, pt);
        pClient->JumpInEffect();
        if (pClient->IsInSpace() and (pClient->GetShipSE() != nullptr)) {
            pClient->GetShipSE()->DestinyMgr()->Stop();
            // an undock sends its own state.  so does a session change, which would refuse the update anyway
            if (!undocked and !pClient->IsSessionChange())
                UpdateBubble(pClient);
        }

        // translocating can interrupt the login warp bubble, leaving the
        // player unable to do anything
        pClient->SetLoginWarpComplete();
        return true;
    }
}

PyResult Command_tr(Client* pClient, CommandDB* db, EVEServiceManager &services, const Seperator& args) {
    if (pClient == nullptr) // should never hit,
        throw CustomError ("Translocate: Invalid Caller - Client sent NULL.");

    // the plain (non key=value) arguments after the command.  noblock is accepted and ignored
    std::vector<size_t> plain;
    for (size_t i = 1; i < args.argCount(); ++i) {
        if (args.arg(i).find('=') != std::string::npos)
            continue;
        if (args.arg(i) == "noblock")
            continue;
        plain.push_back(i);
    }

    if ((plain.size() == 1) and (args.arg(plain[0]) == "help")) {
        std::ostringstream str;
        str << "/tr (who) [where] [offset=x,y,z | offset=randvec(distance)] [noblock]<br>";
        str << "/tr (where) moves you.  /tr [who] (systemID) x y z moves to coordinates<br><br>";
        str << "who = me | fleet (your fleet's online pilots) | charID | shipID or itemID (its owner) | \"pilot name\"<br>";
        str << "where = me | home | last | moon | planet | charID | stationID | solarSystemID | celestialID | stargateID |"
               " itemID (anything in space in your system, or a player item) | \"pilot, system or station name\"<br>";
        str << "offset moves that far from the destination (meters, or randvec(100km|3au)).  with no 'where' it moves within the system<br>";
        str << "NPCs and other objects are found only in your system or the pilot's.  only online pilots can be moved<br>";
        pClient->SendInfoModalMsg(str.str().c_str());
        return nullptr;
    }

    GVector offset(NULL_ORIGIN);
    bool hasOffset(TrParseOffset(args, offset));

    size_t whoIdx(0), whereIdx(0);
    bool coords(false), self(false);
    // [who] systemID x y z
    auto isCoords = [&](size_t first) {
        if ((plain.size() != first + 4) or !args.isNumber(plain[first]))
            return false;
        for (size_t i = first + 1; i < plain.size(); ++i) {
            char* end(nullptr);
            const std::string& text = args.arg(plain[i]);
            strtod(text.c_str(), &end);
            if (text.empty() or (*end != '\0'))
                return false;
        }
        return IsSolarSystemID(strtoul(args.arg(plain[first]).c_str(), nullptr, 10));
    };
    if (isCoords(0)) {
        self = true;
        coords = true;
    } else if (isCoords(1)) {
        whoIdx = plain[0];
        coords = true;
    } else if (plain.empty()) {
        if (!hasOffset)
            throw CustomError ("Translocate: Missing Arguments (use '/tr help' for usage)");
        self = true;
    } else if (plain.size() == 1) {
        // '/tr me offset=...' moves within the system.  otherwise the one argument is where to go
        if (hasOffset and ((args.arg(plain[0]) == "me") or (args.arg(plain[0]) == "fleet"))) {
            whoIdx = plain[0];
        } else {
            self = true;
            whereIdx = plain[0];
        }
    } else if (plain.size() == 2) {
        whoIdx = plain[0];
        whereIdx = plain[1];
    } else {
        throw CustomError ("Translocate: Too Many Arguments (use '/tr help' for usage)");
    }

    std::vector<Client*> subjects;
    if (self) {
        subjects.push_back(pClient);
    } else {
        TrResolveSubjects(pClient, args, whoIdx, subjects);
    }

    uint32 moved(0);
    std::string lastName;
    for (auto pSubject : subjects) {
        TrDestination dest;
        if (coords) {
            size_t first(plain.size() - 4);
            dest.locationID = strtoul(args.arg(plain[first]).c_str(), nullptr, 10);
            if (!sDataMgr.IsSolarSystem(dest.locationID))
                throw CustomError ("Translocate: Invalid Arguments - locationID %u is not a SolarSystemID.", dest.locationID);
            dest.pt = GPoint(strtod(args.arg(plain[first + 1]).c_str(), nullptr), strtod(args.arg(plain[first + 2]).c_str(), nullptr),
                             strtod(args.arg(plain[first + 3]).c_str(), nullptr));
            dest.name = sDataMgr.GetSystemName(dest.locationID);
        } else if (whereIdx == 0) {
            // offset from where the pilot is now
            if (!pSubject->IsInSpace() or (pSubject->GetShipSE() == nullptr)) {
                pClient->SendNotifyMsg("Translocate: %s is docked.  An offset alone moves pilots in space", pSubject->GetName());
                continue;
            }
            dest.locationID = pSubject->GetSystemID();
            dest.pt = pSubject->GetShipSE()->GetPosition();
            dest.name = sDataMgr.GetSystemName(dest.locationID);
        } else {
            // the caller going to 'me' only makes sense with an offset
            if ((pSubject == pClient) and (args.arg(whereIdx) == "me")) {
                if (!hasOffset)
                    continue;
                if (!pClient->IsInSpace() or (pClient->GetShipSE() == nullptr))
                    throw CustomError ("Translocate: an offset needs a destination in space");
                dest.locationID = pClient->GetSystemID();
                dest.pt = pClient->GetShipSE()->GetPosition();
                dest.name = sDataMgr.GetSystemName(dest.locationID);
            } else {
                TrResolveDestination(pClient, pSubject, db, args, whereIdx, dest);
            }
        }
        if (hasOffset) {
            if (!sDataMgr.IsSolarSystem(dest.locationID))
                throw CustomError ("Translocate: an offset needs a destination in space, not %s", dest.name.c_str());
            dest.pt += offset;
        }
        if (!sDataMgr.IsStation(dest.locationID) and !sDataMgr.IsSolarSystem(dest.locationID))
            throw CustomError ("Translocate: Invalid Location %u", dest.locationID);

        if (TrMove(pClient, pSubject, dest.locationID, dest.pt)) {
            ++moved;
            lastName = dest.name;
        }
    }

    if (moved == 0)
        return new PyString("Nobody was moved.");
    if (moved == 1)
        return new PyString("Moved " + std::string(subjects.size() == 1 ? subjects[0]->GetName() : "1 pilot") + " to " + lastName + ".");
    return new PyString("Moved " + std::to_string(moved) + " pilots to " + lastName + ".");
}

static PyResult generic_createitem(Client *pClient, CommandDB *db, EVEServiceManager &services, const Seperator &args) {
    int typeID = -1;
    if (args.isNumber(1)) {
        typeID = atoi(args.arg(1).c_str());
    } else {
        // this hits db directly, so test for possible sql injection code
        for (const auto cur : badCharsSearch)
            if (EvE::icontains(args.arg(1), cur))
                throw CustomError ("Name contains invalid characters");
        std::map<uint32_t, std::string> matches;
        if (!db->ItemSearch(args.arg(1).c_str(), matches))
            throw CustomError ("Item not found");

        if (matches.size() > 1) {
            auto c = matches.begin();
            auto e = matches.end();
            for (; c != e; c++) {
                _log(COMMAND__MESSAGE, "Got match: %s\n", c->second.c_str());

                // POSIX standard btw
                if (strcasecmp(c->second.c_str(), args.arg(1).c_str()) == 0)
                    typeID = c->first;
            }
            if (typeID == -1)
                throw CustomError ("Item name is ambiguous.  Please use a full item name");

        } else if (matches.size() == 1) {
            auto cur = matches.begin();
            _log(COMMAND__MESSAGE,
                 "ItemSearch returned type: \"%s\" given \"%s\"\n",
                 cur->second.c_str(), args.arg(1).c_str());
            typeID = cur->first;
        }
    }
    if (typeID == -1)
        throw CustomError ("Unable to find valid type to create");

    if (typeID < 34)
        throw CustomError ("Invalid Type ID.");

    int qty = 1;
    if (2 < args.argCount()) {
        if (args.isNumber(2))
            qty = atoi(args.arg(2).c_str());
    }

    _log(COMMAND__MESSAGE, "Create %s %u times", args.arg(1).c_str(), qty);

    //create into their cargo hold unless they are docked in a station,
    //then stick it in their hangar instead.
    uint32 locationID = 0;
    EVEItemFlags flag;
    if (pClient->IsInSpace()) {
        locationID = pClient->GetShipID();
        flag = flagCargoHold;
    } else {
        locationID = pClient->GetStationID();
        flag = flagHangar;
    }

    ItemData idata(
        typeID,
        pClient->GetCharacterID(),
                   locTemp, //temp location
                   flag,
                   qty
    );

    InventoryItemRef iRef = sItemFactory.SpawnItem(idata);
    if (iRef.get() == nullptr)
        throw CustomError ("Unable to create item of type %s.", args.arg(1).c_str());

    //Move to location
    if (pClient->IsInSpace())
        pClient->GetShip()->AddItemByFlag(flag, iRef);
    else
        iRef->Move(locationID, flag, true);

    iRef->SaveItem();

    return new PyInt(iRef.get()->itemID());
}

static PyResult generic_loaditem(Client *pClient, CommandDB *db, EVEServiceManager &services, const Seperator &args) {
    int typeID = -1;
    if (args.isNumber(2)) {
        typeID = atoi(args.arg(2).c_str());
    } else {
        // this hits db directly, so test for possible sql injection code
        for (const auto cur : badCharsSearch)
            if (EvE::icontains(args.arg(2), cur))
                throw CustomError ("Name contains invalid characters");
        std::map<uint32_t, std::string> matches;
        if (!db->ItemSearch(args.arg(2).c_str(), matches))
            throw CustomError ("Item not found");

        if (matches.size() > 1) {
            auto c = matches.begin();
            auto e = matches.end();
            for (; c != e; c++) {
                _log(COMMAND__MESSAGE, "Got match: %s\n", c->second.c_str());

                // POSIX standard btw
                if (strcasecmp(c->second.c_str(), args.arg(2).c_str()) == 0)
                    typeID = c->first;
            }
            if (typeID == -1)
                throw CustomError ("Item name is ambiguous.  Please use a full item name");

        } else if (matches.size() == 1) {
            auto cur = matches.begin();
            _log(COMMAND__MESSAGE,
                 "ItemSearch returned type: \"%s\" given \"%s\"\n",
                 cur->second.c_str(), args.arg(2).c_str());
            typeID = cur->first;
        }
    }
    if (typeID == -1)
        throw CustomError ("Unable to find valid type to create");

    if (typeID < 34)
        throw CustomError ("Invalid Type ID.");

    int qty = 1;
    if (3 < args.argCount()) {
        if (args.isNumber(3))
            qty = atoi(args.arg(3).c_str());
    }

    _log(COMMAND__MESSAGE, "Create %s %u times", args.arg(2).c_str(), qty);

    //create into their cargo hold unless they are docked in a station,
    //then stick it in their hangar instead.
    uint32 locationID = 0;
    EVEItemFlags flag;
    if (pClient->IsInSpace()) {
        locationID = pClient->GetShipID();
        flag = flagCargoHold;
    } else {
        locationID = pClient->GetStationID();
        flag = flagHangar;
    }

    ItemData idata(
        typeID,
        pClient->GetCharacterID(),
                   locTemp, //temp location
                   flag,
                   qty
    );

    InventoryItemRef iRef = sItemFactory.SpawnItem(idata);
    if (iRef.get() == nullptr)
        throw CustomError ("Unable to create item of type %s.", args.arg(2).c_str());

    //Move to location
    if (pClient->IsInSpace())
        pClient->GetShip()->AddItemByFlag(flag, iRef);
    else
        iRef->Move(locationID, flag, true);

    iRef->SaveItem();

    return new PyInt(iRef.get()->itemID());
}

PyResult Command_create(Client* pClient, CommandDB* db, EVEServiceManager &services, const Seperator& args) {
    if (args.argCount() < 2) {
        throw CustomError ("Correct Usage: /create [typeID|\"Type Name\"] [qty] [where]");
    }
    return generic_createitem(pClient, db, services, args);
}

PyResult Command_load(Client* pClient, CommandDB* db, EVEServiceManager &services, const Seperator& args) {
    if (args.argCount() < 3) {
        throw CustomError ("Correct Usage: /load me [typeID|\"Type Name\"] [qty] [where]");
    }
    return generic_loaditem(pClient, db, services, args);
}

PyResult Command_createitem(Client* pClient, CommandDB* db, EVEServiceManager &services, const Seperator& args) {
    if (args.argCount() < 2) {
        throw CustomError ("Correct Usage: /createitem [typeID|\"Type Name\"] [qty] [where]");
    }
    return generic_createitem(pClient, db, services, args);
}


namespace {
    // me, a character (their ship), or anything in your system.  never celestials, stations or gates
    SystemEntity* FindKillTarget(Client* pClient, const Seperator& args, const char* usage) {
        if (args.argCount() < 2)
            throw CustomError (usage);
        SystemEntity* pSE(nullptr);
        if (args.arg(1) == "me") {
            pSE = pClient->GetShipSE();
        } else if (args.isNumber(1)) {
            uint32 id(atoi(args.arg(1).c_str()));
            if (IsCharacterID(id)) {
                Client* pTarget(sEntityList.FindClientByCharID(id));
                if (pTarget == nullptr)
                    throw CustomError ("Character %u is not online", id);
                if (!pTarget->IsInSpace())
                    throw CustomError ("%s is docked", pTarget->GetName());
                pSE = pTarget->GetShipSE();
            } else if (pClient->SystemMgr() != nullptr) {
                pSE = pClient->SystemMgr()->GetSE(id);
            }
        } else {
            throw CustomError (usage);
        }
        if (pSE == nullptr)
            throw CustomError ("%s is not in space in your system", args.arg(1).c_str());
        if (pSE->IsStaticEntity())
            throw CustomError ("%s can't be destroyed", pSE->GetName());
        return pSE;
    }
}

PyResult Command_kill(Client* pClient, CommandDB* db, EVEServiceManager &services, const Seperator& args)
{
    SystemEntity* pSE(FindKillTarget(pClient, args, "Correct Usage: /kill (itemID|charID|me)"));
    if (pSE->IsDead())
        throw CustomError ("%s is already dead", pSE->GetName());

    // the killer is the GM's ship.  killmails need a weapon, so that's the ship too
    SystemEntity* pKiller(pClient->IsInSpace() ? pClient->GetShipSE() : nullptr);
    if ((pKiller == nullptr) or (pKiller == pSE))
        pKiller = pSE;
    Damage damage(pKiller, true);
    damage.weaponRef = pKiller->GetSelf();

    std::string name(pSE->GetName());
    uint32 itemID(pSE->GetID());
    SystemEntity::SetUnkillable(itemID, false);
    pSE->Destroy(damage);

    sLog.Yellow("Command", "%s: killed %s(%u).", pClient->GetName(), name.c_str(), itemID);
    return new PyString("Destroyed " + name + ".");
}

PyResult Command_unkillable(Client* pClient, CommandDB* db, EVEServiceManager &services, const Seperator& args)
{
    const char* usage("Correct Usage: /unkillable (itemID|charID|me) [on|off]");
    SystemEntity* pSE(FindKillTarget(pClient, args, usage));
    bool set(!SystemEntity::IsUnkillable(pSE->GetID()));
    if (args.argCount() > 2) {
        if (strcasecmp(args.arg(2).c_str(), "on") == 0) {
            set = true;
        } else if (strcasecmp(args.arg(2).c_str(), "off") == 0) {
            set = false;
        } else {
            throw CustomError (usage);
        }
    }
    SystemEntity::SetUnkillable(pSE->GetID(), set);

    std::string msg(pSE->GetName());
    msg += (set ? " is now unkillable." : " can be killed again.");
    return new PyString(msg);
}

PyResult Command_killallnpcs(Client* pClient, CommandDB* db, EVEServiceManager &services, const Seperator& args)
{
    if (!pClient->IsInSpace())
        throw CustomError ("You're not in space.");
    if (args.argCount() != 1)
        throw CustomError ("Correct Usage: /killallnpcs");
    if (pClient->GetShipSE() == nullptr)
        throw CustomError ("ShipSE invalid.");
    if (pClient->GetShipSE()->SysBubble() == nullptr)
        throw CustomError ("SysBubble invalid.");

    std::map< uint32, SystemEntity* > entityVec;
    pClient->GetShipSE()->SysBubble()->GetEntities(entityVec);
    for (auto cur : entityVec) {
        if (cur.second == nullptr)
            continue;
        if (cur.second->IsNPCSE()) {
            Damage damage(pClient->GetShipSE(),true);
            cur.second->GetNPCSE()->Killed(damage);
        }
    }

    return nullptr;
}

namespace {
    // only=  names from CCP's /unspawn macros, and the generic group<N> / category<N>
    bool ParseUnspawnFilter(const std::string& only, bool& isCategory, uint16& matchID) {
        static const std::map<std::string, std::pair<bool, uint16>> s_filters = {
            {"categoryDrone",                   {true,  EVEDB::invCategories::Drone}},
            {"categoryEntity",                  {true,  EVEDB::invCategories::Entity}},
            {"categoryShip",                    {true,  EVEDB::invCategories::Ship}},
            {"groupWreck",                      {false, EVEDB::invGroups::Wreck}},
            {"groupCargoContainer",             {false, EVEDB::invGroups::Cargo_Container}},
            {"groupSecureCargoContainer",       {false, EVEDB::invGroups::Secure_Cargo_Container}},
            {"groupAuditLogSecureContainer",    {false, EVEDB::invGroups::Audit_Log_Secure_Container}},
            {"groupFreightContainer",           {false, EVEDB::invGroups::Freight_Container}}
        };
        auto itr = s_filters.find(only);
        if (itr != s_filters.end()) {
            isCategory = itr->second.first;
            matchID = itr->second.second;
            return true;
        }
        std::string number;
        if (only.compare(0, 8, "category") == 0) {
            isCategory = true;
            number = only.substr(8);
        } else if (only.compare(0, 5, "group") == 0) {
            isCategory = false;
            number = only.substr(5);
        }
        if (number.empty() or !IsNumber(number))
            return false;
        matchID = atoi(number.c_str());
        return (matchID > 0);
    }
}

PyResult Command_unspawn(Client* pClient, CommandDB* db, EVEServiceManager &services, const Seperator& args)
{
#define DEFAULT_RANGE 500000
    const char* usage("/unspawn usage:<br>  /unspawn [itemID]<br>/unspawn [range=meters] only=(groupWreck|groupCargoContainer|groupSecureCargoContainer|"
            "groupAuditLogSecureContainer|groupFreightContainer|categoryDrone|categoryEntity|categoryShip|group&lt;N&gt;|category&lt;N&gt;)<br>"
            "The default range is 500km.  Ships with a pilot are never removed.");
    if (!pClient->IsInSpace()) {
        throw CustomError ("You must be in space to unspawn things.");
    }

    if (pClient->GetShipSE() == nullptr) {
            throw CustomError ("/unspawn failed. You don't appear to have a ship?");
    }
    // the command name is the first non-eq argument
    int target_index = cmd_find_nth_noneq(args, 2);
    uint32 target = 0;
    if (target_index > 0) {
        if (!args.isNumber(target_index)) {
            throw CustomError ("/unspawn called with non number");
        }
        target = atoi(args.arg(target_index).c_str());
    }

    std::string range_str = cmd_parse_eq_arg(args, "range=");
    std::string only_str = cmd_parse_eq_arg(args, "only=");

    _log(COMMAND__MESSAGE, "unspawn got: %s %s %u", range_str.c_str(), only_str.c_str(), target);

    double range = DEFAULT_RANGE;

    if (range_str.size() > 0) {
        if (!cmd_parse_distance(range_str, range) or (range <= 0)) {
            throw CustomError ("/unspawn with range=x must be a distance");
        }
    }

    if ((range_str.size() > 0 or
            only_str.size() > 0) and
            target != 0) {
            throw CustomError ("/unspawn cannot be called with an explcit target and either range= or only=");
    }

    if (target != 0) {
        SystemEntity *sys_entity = pClient->SystemMgr()->GetSE(target);
        if (sys_entity == nullptr) {
            throw CustomError ("/unspawn failed.  Item %u not found.", target);
        }
        if (sys_entity->IsStaticEntity())
            throw CustomError ("/unspawn failed.  %s is a static object.", sys_entity->GetName());
        if (sys_entity->HasPilot())
            throw CustomError ("/unspawn failed.  %s has a pilot.", sys_entity->GetName());
        // law enforcement holds ships of logged-off pilots until their timers run out
        if (pClient->SystemMgr()->GetLaw()->GetLoggedOffOwner(target) != 0)
            throw CustomError ("/unspawn failed.  %s belongs to a logged-off pilot.", sys_entity->GetName());

        std::string name(sys_entity->GetName());
        sys_entity->Delete();
        _log(COMMAND__MESSAGE, "/unspawn called with single target successful");
        return new PyString("Removed " + name + ".");
    }

    if (only_str.size() == 0) {
        throw CustomError (usage);
    }

    bool is_category_match = false;
    uint16 match_id = 0;
    if (!ParseUnspawnFilter(only_str, is_category_match, match_id))
        throw CustomError ("only='%s' not a supported group or category", only_str.c_str());

    GPoint player_pos = pClient->GetShipSE()->GetPosition();

    // the whole system, not just the bubble, so the range means what it says.  collect first, as deleting changes the map
    std::vector<uint32> matches;
    for (auto cur : pClient->SystemMgr()->GetEntities()) {
        SystemEntity* pSE(cur.second);
        if ((pSE == nullptr) or pSE->IsStaticEntity() or pSE->HasPilot())
            continue;
        if (pSE->IsShipSE() and (pClient->SystemMgr()->GetLaw()->GetLoggedOffOwner(cur.first) != 0))
            continue;
        if (is_category_match ? (match_id != pSE->GetCategoryID()) : (match_id != pSE->GetGroupID()))
            continue;
        if (player_pos.distance(pSE->GetPosition()) > range)
            continue;
        matches.push_back(cur.first);
    }

    uint32 count(0);
    for (auto id : matches) {
        SystemEntity* pSE(pClient->SystemMgr()->GetSE(id));
        if (pSE == nullptr)
            continue;
        pSE->Delete();
        ++count;
    }

#undef DEFAULT_RANGE

    return new PyString("Removed " + std::to_string(count) + " objects.");
}

PyResult Command_location(Client* pClient, CommandDB* db, EVEServiceManager &services, const Seperator& args)
{
    if (!pClient->IsInSpace())
        throw CustomError ("You're not in space.");
    if (pClient->GetShipSE()->DestinyMgr() == nullptr)
        pClient->SetDestiny(NULL_ORIGIN);
    if (pClient->GetShipSE()->SysBubble() == nullptr)
        pClient->EnterSystem(pClient->GetSystemID());

    DestinyManager *dm = pClient->GetShipSE()->DestinyMgr();
    SystemBubble *pBubble = pClient->GetShipSE()->SysBubble();
    if (pBubble == nullptr) {
        sBubbleMgr.Add(pClient->GetShipSE());
        pBubble = pClient->GetShipSE()->SysBubble();
    }
    uint32 bubble = pBubble->GetID();

    const GPoint &loc = dm->GetPosition();
    const GVector &vel = dm->GetVelocity();

    char reply[140];
    snprintf(reply, 140,
             "SystemID: %u  BubbleID: %u<br>"
             "x: %.2f<br>"
             "y: %.2f<br>"
             "z: %.2f<br>"
             "speed: %.1f",
             pClient->GetSystemID(), bubble,
             loc.x, loc.y, loc.z,
             vel.length()
    );

    pClient->SendInfoModalMsg(reply);

    return new PyString(reply);
}

PyResult Command_syncloc(Client* pClient, CommandDB* db, EVEServiceManager &services, const Seperator& args)
{
    if (!pClient->IsInSpace())
        throw CustomError ("You're not in space.");
    if (pClient->GetShipSE()->DestinyMgr() == nullptr)
        pClient->SetDestiny(NULL_ORIGIN);
    if (pClient->GetShipSE()->SysBubble() == nullptr)
        pClient->EnterSystem(pClient->GetSystemID());
    if (pClient->IsSessionChange()) {
        pClient->SendInfoModalMsg("Session Change Active.  Wait %u seconds before issuing another command.",
                                  pClient->GetSessionChangeTime());
        return new PyString("SessionChange Active.  Request Denied.");
    }

    pClient->GetShipSE()->DestinyMgr()->SetPosition(pClient->GetShipSE()->GetPosition(), true);
    pClient->SetSessionTimer();

    return new PyString("Position synchronized.");
}

PyResult Command_syncpos(Client* pClient, CommandDB* db, EVEServiceManager &services, const Seperator& args)
{
    if (!pClient->IsInSpace())
        throw CustomError ("You're not in space.");
    if (pClient->GetShipSE()->DestinyMgr() == nullptr)
        pClient->SetDestiny(NULL_ORIGIN);
    if (pClient->GetShipSE()->SysBubble() == nullptr)
        pClient->EnterSystem(pClient->GetSystemID());
    if (pClient->IsSessionChange()) {
        pClient->SendInfoModalMsg("Session Change Active.  Wait %u seconds before issuing another command.",
                                  pClient->GetSessionChangeTime());
        return new PyString("SessionChange Active.  Request Denied.");
    }

    pClient->GetShipSE()->SysBubble()->SyncPos();
    pClient->SetSessionTimer();

    return new PyString("All Positions synchronized.");
}


PyResult Command_update(Client *pClient, CommandDB *db, EVEServiceManager &services, const Seperator &args) {
    return UpdateBubble(pClient);
}

PyResult Command_sendstate(Client *pClient, CommandDB *db, EVEServiceManager &services, const Seperator &args) {
    _log(COMMAND__MESSAGE, "SystemCommands::SendState() - checking if in space");
    if (!pClient->IsInSpace()) {
        throw CustomError ("You're not in space.");
    }

    _log(COMMAND__MESSAGE, "SystemCommands::SendState() - checking if destiny manager is null");
    if (pClient->GetShipSE()->DestinyMgr() == nullptr) {
        _log(COMMAND__MESSAGE, "SystemCommands::SendState() - destiny manager is null, setting to null origin");
        pClient->SetDestiny(NULL_ORIGIN);
    }

    _log(COMMAND__MESSAGE, "SystemCommands::SendState() - checking if bubble is null");
    if (pClient->GetShipSE()->SysBubble() == nullptr) {
        _log(COMMAND__MESSAGE, "SystemCommands::SendState() - bubble is null, setting system id");
        pClient->EnterSystem(pClient->GetSystemID());
    }

    _log(COMMAND__MESSAGE, "SystemCommands::SendState() - checking if session is changing");
    if (pClient->IsSessionChange()) {
        pClient->SendInfoModalMsg(
            "Session Change Active. Wait %u seconds before issuing another command.",
            pClient->GetSessionChangeTime()
        );

        return new PyString("SessionChange Active. Request Denied.");
    }

    _log(COMMAND__MESSAGE, "SystemCommands::SendState() - setting state sent to false");
    pClient->SetStateSent(false);

    _log(COMMAND__MESSAGE, "SystemCommands::SendState() - sending SetState");
    pClient->GetShipSE()->DestinyMgr()->SendSetState();

    _log(COMMAND__MESSAGE, "SystemCommands::SendState() - setting session timer");
    pClient->SetSessionTimer();

    return new PyString("Update sent.");
}

PyResult Command_addball(Client *pClient, CommandDB *db, EVEServiceManager &services, const Seperator &args) {
    if (!pClient->IsInSpace())
        throw CustomError ("You're not in space.");
    if (pClient->GetShipSE()->DestinyMgr() == nullptr)
        pClient->SetDestiny(NULL_ORIGIN);
    if (pClient->GetShipSE()->SysBubble() == nullptr)
        pClient->EnterSystem(pClient->GetSystemID());

    SystemBubble *pBubble = pClient->GetShipSE()->SysBubble();
    if (pBubble == nullptr) {
        sBubbleMgr.Add(pClient->GetShipSE());
        pBubble = pClient->GetShipSE()->SysBubble();
    }
    pBubble->SendAddBalls(pClient->GetShipSE());

    return new PyString("Update sent.");
}

PyResult Command_addball2(Client *pClient, CommandDB *db, EVEServiceManager &services, const Seperator &args) {
    if (!pClient->IsInSpace())
        throw CustomError ("You're not in space.");
    if (pClient->GetShipSE()->DestinyMgr() == nullptr)
        pClient->SetDestiny(NULL_ORIGIN);
    if (pClient->GetShipSE()->SysBubble() == nullptr)
        pClient->EnterSystem(pClient->GetSystemID());

    SystemBubble *pBubble = pClient->GetShipSE()->SysBubble();
    if (pBubble == nullptr) {
        sBubbleMgr.Add(pClient->GetShipSE());
        pBubble = pClient->GetShipSE()->SysBubble();
    }
    pBubble->SendAddBalls2(pClient->GetShipSE());

    return new PyString("Update sent.");
}

PyResult Command_cloak(Client* pClient, CommandDB* db, EVEServiceManager &services, const Seperator& args)
{
    if (args.argCount() == 1) {
        if (pClient->IsInSpace()) {
            DestinyManager* pDestiny(pClient->GetShipSE()->DestinyMgr());
            if (pDestiny->IsModuleCloaked())
                pClient->GetShipSE()->Decloak();
            else if (pDestiny->IsCloaked())
                pDestiny->UnCloak();
            else
                pDestiny->Cloak(Destiny::Cloak::GM);
        } else
            throw CustomError ("ERROR!  You MUST be in space to cloak!");
    } else
        throw CustomError ("Correct Usage: /cloak");

    return nullptr;
}

PyResult Command_hop(Client* pClient, CommandDB* db, EVEServiceManager &services, const Seperator& args) {
    /* sm.RemoteSvc('slash').SlashCmd('/hop %s' % distance)
     * the benchmark script sends '/hop 10000' right after undocking, so plain numbers are meters.
     * moves the ship that far along its heading */
    if (args.argCount() < 2)
        throw CustomError ("Correct Usage: /hop (distance)[m|km|au]");
    if (!pClient->IsInSpace())
        throw CustomError ("You must be in space to hop.");

    // '/hop 10' 'km' is accepted too
    std::string arg(args.arg(1));
    if (args.argCount() > 2)
        arg += args.arg(2);
    double distance(0);
    if (!cmd_parse_distance(arg, distance))
        throw CustomError ("Correct Usage: /hop (distance)[m|km|au]");
    if ((distance <= 0) or (distance > (100 * ONE_AU_IN_METERS)))
        throw CustomError ("Distance must be between 0 and 100au");

    GVector heading(pClient->GetShipSE()->DestinyMgr()->GetHeading());
    if (heading.isZero())
        heading = pClient->GetShipSE()->DestinyMgr()->GetVelocity();
    if (heading.isZero()) {
        GPoint random(NULL_ORIGIN);
        random.MakeRandomPointOnSphere(1.0);
        heading = random;
    }
    heading.normalize();

    GPoint pos(pClient->GetShipSE()->GetPosition() + (heading * distance));
    pClient->MoveToLocation(pClient->GetLocationID(), pos);
    return new PyString("Hopped.");
}

namespace {
    StructureSE* GetStructure(Client* pClient, const Seperator& args, uint32 index) {
        if (!pClient->IsInSpace())
            throw CustomError ("You must be in space.");
        if ((args.argCount() <= index) or !args.isNumber(index))
            throw CustomError ("Missing structure itemID");
        uint32 itemID(atoi(args.arg(index).c_str()));
        SystemEntity* pSE(pClient->SystemMgr()->GetSE(itemID));
        if ((pSE == nullptr) or (pSE->GetPOSSE() == nullptr))
            throw CustomError ("%u is not a structure in this system", itemID);
        return pSE->GetPOSSE();
    }
}

namespace {
    const char* PosStateName(int8 state)
    {
        switch (state) {
            case EVEPOS::StructureState::Incapacitated:     return "incapacitated";
            case EVEPOS::StructureState::Unanchored:        return "unanchored";
            case EVEPOS::StructureState::Anchored:          return "anchored (offline)";
            case EVEPOS::StructureState::Onlining:          return "onlining";
            case EVEPOS::StructureState::Reinforced:        return "reinforced";
            case EVEPOS::StructureState::Online:            return "online";
            case EVEPOS::StructureState::Operating:         return "operating";
            case EVEPOS::StructureState::Vulnerable:        return "vulnerable";
            case EVEPOS::StructureState::SheildReinforced:  return "shield reinforced";
            case EVEPOS::StructureState::ArmorReinforced:   return "armor reinforced";
            case EVEPOS::StructureState::Invulnerable:      return "invulnerable";
        }
        return "unknown";
    }

    /* /sov and /pos (anchor|online|offline|unanchor|complete) (itemID)
     *  any structure in your system:  control towers and their modules, sovereignty structures (TCU, SBU, IHub),
     *  outpost construction platforms, customs offices and gantries.  it happens at once:  the delays, the pilot's
     *  starbase rules and permissions are skipped, and the client shows the final state without a countdown.
     *  online anchors an unanchored structure first, unanchor offlines an online one.  complete only finishes the
     *  change that is running.  sovereignty rules (claims, hubs, blockades) still apply and their errors come back
     */
    PyResult StructureStateCommand(Client* pClient, const Seperator& args, const char* cmd)
    {
        StructureTiming::GMAction action((args.argCount() > 1) ? StructureTiming::ParseGMAction(args.arg(1).c_str())
                                                                : StructureTiming::GMAction::Invalid);
        if ((action == StructureTiming::GMAction::Invalid) or (args.argCount() < 3) or !args.isNumber(2))
            throw CustomError ("Correct Usage: /%s (anchor|online|offline|unanchor|complete) (itemID)", cmd);
        if (!pClient->IsInSpace())
            throw CustomError ("You must be in space.");
        uint32 itemID(atoi(args.arg(2).c_str()));
        SystemEntity* pSE(pClient->SystemMgr()->GetSE(itemID));
        if (pSE == nullptr)
            throw CustomError ("%u is not a structure in this system", itemID);
        std::string name(pSE->GetName());

        // customs offices and gantries
        if (pSE->GetCOSE() != nullptr) {
            CustomsSE* pCO(pSE->GetCOSE());
            if (!pCO->GMChangeState(action))
                return new PyString("Nothing to do for " + name + ".");
            // an upgraded gantry is gone (it is a customs office now)
            SystemEntity* pNow(pClient->SystemMgr()->GetSE(itemID));
            if ((pNow == nullptr) or (pNow->GetCOSE() == nullptr))
                return new PyString("Done:  " + name + " is now a customs office.");
            return new PyString("Done:  " + name + ".");
        }
        if (pSE->GetPOSSE() == nullptr)
            throw CustomError ("%u is not a structure in this system", itemID);

        StructureSE* pStruct(pSE->GetPOSSE());
        if (!pStruct->GMChangeState(action))
            return new PyString("Nothing to do:  " + name + " is " + PosStateName(pStruct->GetState()) + ".");
        return new PyString("Done:  " + name + " is " + PosStateName(pStruct->GetState()) + ".");
    }
}

PyResult Command_sov(Client* pClient, CommandDB* db, EVEServiceManager &services, const Seperator& args) {
    /*
     *  ' /sov complete ' + str(itemID)     (the client's GM menu, 'Complete State')
     *  /sov (anchor|online|offline|unanchor|complete) (itemID)
     *  sovereignty structures, platforms, customs offices or any other structure.  at once, without the delays
     *  /sov tenure (level) [solarSystemID]
     *  sets the claim's strategic ("Sovereignty") index, 0-5, by backdating its claim time
     */
    if ((args.argCount() > 1) and (args.arg(1) == "tenure")) {
        if ((args.argCount() < 3) or !args.isNumber(2) or (atoi(args.arg(2).c_str()) < 0) or (atoi(args.arg(2).c_str()) > 5)
        or ((args.argCount() > 3) and !args.isNumber(3)))
            throw CustomError ("Correct Usage: /sov tenure (level 0-5) [solarSystemID]");
        uint8 level(static_cast<uint8>(atoi(args.arg(2).c_str())));
        uint32 systemID((args.argCount() > 3) ? static_cast<uint32>(atoi(args.arg(3).c_str())) : pClient->GetSystemID());
        uint8 was(svDataMgr.GetStrategicLevel(systemID));
        if (!svDataMgr.SetStrategicLevel(systemID, level))
            throw CustomError ("System %u has no sovereignty claim.", systemID);
        return new PyString("Strategic index of system " + std::to_string(systemID) + " set from level " + std::to_string(was)
                            + " to " + std::to_string(level) + " (held " + std::to_string(svDataMgr.GetClaimDays(systemID)) + " days).");
    }
    return StructureStateCommand(pClient, args, "sov");
}

namespace {
    /* /pos fuel [towerID] [hours]
     *  fills a control tower's fuel bay (online fuel, purpose 1) and strontium bay (reinforce fuel, purpose 4) with what
     *  invControlTowerResources lists for its type, so it works with either the pre- or post-fuel-block static data.
     *  charter rows (minSecurityLevel/factionID) only apply where the system needs them.  consumption is not done here
     */
    PyResult PosFuel(Client* pClient, const Seperator& args) {
        if (!pClient->IsInSpace() or (pClient->GetShipSE() == nullptr))
            throw CustomError ("You must be in space.");

        // the tower:  an itemID, or the nearest one in your bubble
        StructureSE* pSE(nullptr);
        size_t hoursIdx(2);
        if ((args.argCount() > 2) and args.isNumber(2)
        and (pClient->SystemMgr()->GetSE(atoi(args.arg(2).c_str())) != nullptr)) {
            pSE = GetStructure(pClient, args, 2);
            hoursIdx = 3;
        } else if (pClient->GetShipSE()->SysBubble() != nullptr) {
            std::map<uint32, SystemEntity*> entities;
            pClient->GetShipSE()->SysBubble()->GetEntities(entities);
            double best(-1);
            for (auto cur : entities) {
                if ((cur.second == nullptr) or !cur.second->IsTowerSE())
                    continue;
                double dist(pClient->GetShipSE()->GetPosition().distance(cur.second->GetPosition()));
                if ((best < 0) or (dist < best)) {
                    best = dist;
                    pSE = cur.second->GetPOSSE();
                }
            }
        }
        if ((pSE == nullptr) or !pSE->IsTowerSE())
            throw CustomError ("Correct Usage: /pos fuel [towerID] [hours]  (no control tower in your bubble)");
        int64 wantHours(-1);
        if (args.argCount() > hoursIdx) {
            if (!args.isNumber(hoursIdx) or (atoi(args.arg(hoursIdx).c_str()) < 1))
                throw CustomError ("Correct Usage: /pos fuel [towerID] [hours]");
            wantHours = atoll(args.arg(hoursIdx).c_str());
        }

        InventoryItemRef tRef(pSE->GetSelf());
        Inventory* pInv(tRef->GetMyInventory());
        if (pInv == nullptr)
            throw CustomError ("%s has no inventory", tRef->name());
        if (!pInv->ContentsLoaded())
            pInv->LoadContents();

        // what the tower burns here:  purpose 1 -> fuel bay (the tower's main inventory), purpose 4 -> strontium bay
        std::map<uint16, uint32> need[2];
        TowerSE::GetFuelRequirements(tRef->typeID(), pSE->SystemMgr()->GetID(),
                                     (IsAlliance(pSE->GetAllianceID()) ? pSE->GetAllianceID() : 0), need[0], need[1]);
        if (need[0].empty() and need[1].empty())
            throw CustomError ("No fuel requirements for %s (type %u)", tRef->name(), tRef->typeID());

        const EVEItemFlags flags[2] = { flagNone, flagSecondaryStorage };
        int64 added[2] = { 0, 0 };
        std::string types;
        for (int bay = 0; bay < 2; ++bay) {
            if (need[bay].empty())
                continue;
            double perHour(0);
            for (auto cur : need[bay]) {
                const ItemType* pType(sItemFactory.GetType(cur.first));
                if (pType != nullptr)
                    perHour += cur.second * pType->volume();
            }
            int64 hours(0);
            double space(pInv->GetRemainingCapacity(flags[bay]));
            if (perHour > 0)
                hours = static_cast<int64>(floor(space / perHour));
            if ((wantHours > 0) and (wantHours < hours))
                hours = wantHours;
            if (hours < 1)
                continue;
            for (auto cur : need[bay]) {
                int64 qty(cur.second * hours);
                if (qty > 2147483647)
                    qty = 2147483647;
                InventoryItemRef existing(pInv->GetByTypeFlag(cur.first, flags[bay]));
                if ((existing.get() != nullptr) and (existing->ownerID() == tRef->ownerID()) and !existing->isSingleton()) {
                    existing->AlterQuantity(static_cast<int32>(qty), true);
                } else {
                    ItemData idata(cur.first, tRef->ownerID(), locTemp, flags[bay], static_cast<uint32>(qty));
                    InventoryItemRef iRef(sItemFactory.SpawnItem(idata));
                    if (iRef.get() == nullptr)
                        throw CustomError ("Unable to create fuel of type %u", cur.first);
                    iRef->Move(tRef->itemID(), flags[bay], true);
                }
                if (bay == 0) {
                    const ItemType* pType(sItemFactory.GetType(cur.first));
                    if (!types.empty())
                        types += ", ";
                    types += (pType != nullptr ? pType->name() : std::to_string(cur.first));
                }
            }
            added[bay] = hours;
        }

        char reply[512];
        snprintf(reply, sizeof(reply), "%s: added %lli hours of fuel (%s) and %lli hours of strontium.", tRef->name(),
                 added[0], (types.empty() ? "none" : types.c_str()), added[1]);
        return new PyString(reply);
    }
}

PyResult Command_pos(Client* pClient, CommandDB* db, EVEServiceManager &services, const Seperator& args) {
    /*
     * ' /pos online ' + str(itemID)
     * ' /pos unanchor ' + str(itemID)
     * ' /pos anchor ' + str(itemID)
     * ' /pos offline ' + str(itemID)
     * ' /pos fuel [itemID] [hours]
     *  (the client's GM menu, 'Structure State').  these skip the anchoring/onlining delays, see StructureStateCommand
     */
    if ((args.argCount() > 1) and (strcasecmp(args.arg(1).c_str(), "fuel") == 0))
        return PosFuel(pClient, args);
    if ((args.argCount() < 2) or (StructureTiming::ParseGMAction(args.arg(1).c_str()) == StructureTiming::GMAction::Invalid))
        throw CustomError ("Correct Usage: /pos (anchor|online|offline|unanchor|complete) (itemID) | /pos fuel [towerID] [hours]");
    // the same as /sov:  any structure, at once
    return StructureStateCommand(pClient, args, "pos");
}
