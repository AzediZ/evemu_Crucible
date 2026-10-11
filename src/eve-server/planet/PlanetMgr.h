 /**
  * @name PlanetMgr.h
  *   Specific Class for managing planet resources
  * @Author:         Allan
  * @date:   30 April 2016
  *
  * F19:  this runs one UserUpdateNetwork command stream.  the commands are validated and applied to a copy of the colony
  *   with the client's own rules (ColonySim).  only a stream that fully succeeds and can be paid for changes the colony.
  */


#ifndef EVEMU_PLANET_PLANETMGR_H_
#define EVEMU_PLANET_PLANETMGR_H_

#include "planet/PlanetDB.h"
#include "planet/PlanetDataMgr.h"
#include "planet/ColonySim.h"


class Client;
class Colony;
class PlanetSE;

class PlanetMgr
{
public:
    PlanetMgr(Client* pClient, PlanetSE* pPlanet, Colony* pColony);
    virtual ~PlanetMgr();

    // UserUpdateNetwork(serializedStream):  a list of (commandID, argsTuple) (common/script/planet/commandStream.py)
    PyRep* UpdateNetwork(PyList* commandList);

protected:
    void CreatePin(PyTuple* args);
    void RemovePin(PyTuple* args);
    void CreateLink(PyTuple* args);
    void RemoveLink(PyTuple* args);
    void SetLinkLevel(PyTuple* args);
    void CreateRoute(PyTuple* args);
    void RemoveRoute(PyTuple* args);
    void SetSchematic(PyTuple* args);
    void UpgradeCommandCenter(PyTuple* args);
    void AddExtractorHead(PyTuple* args);
    void KillExtractorHead(PyTuple* args);
    void MoveExtractorHead(PyTuple* args);
    void InstallProgram(PyTuple* args);

    // the command center checks the client makes in ClientColony.PostValidateCreatePin and ClientPlanet.CreateCommandPin
    void ValidateCommandCenter(uint32 itemID, uint16 typeID);
    // ClientColony.PostValidateCreatePin:  the pin's required skills
    void CheckRequiredSkills(uint16 typeID);

    // pins and routes created in this stream have temporary tuple IDs:  (1, n) pins, (2, n) routes
    uint32 GetPinID(PyRep* id);
    uint32 GetRouteID(PyRep* id);
    static PyRep* GetArg(PyTuple* args, size_t index);

    // spawns the items for new pins and links, takes the real route IDs, deletes removed items, pays and saves
    void Commit();

private:
    Client* m_client;
    Colony* m_colony;
    PlanetSE* m_planet;

    PlanetDB m_db;

    uint32 m_charID;
    int64 m_now;
    double m_cost;

    PI_CCPin m_work;
    ColonySim* m_sim;

    // provisional IDs for pins and routes created in this stream
    uint32 m_nextPinID;
    uint32 m_nextRouteID;
    std::map<std::pair<int64, int64>, uint32> m_tempPins;
    std::map<std::pair<int64, int64>, uint32> m_tempRoutes;
    // a command center placed in this stream (the colony is new)
    uint32 m_newCommandCenter;
    // items of removed pins and links
    std::set<uint32> m_removedItems;
    // the resource taken out by programs installed in this stream:  resourceTypeID, rows
    std::map<uint16, std::vector<PIResource::Depletion>> m_depletion;
    // running programs stopped in this stream (their heads deplete less):  pinIDs
    std::set<uint32> m_stoppedPrograms;
};


#endif  // EVEMU_PLANET_PLANETMGR_H_
