/**
  * @name MissionObjectives.cpp
  *   mission objective checks and turn-in
  */


#include "eve-server.h"

#include "Client.h"
#include "EntityList.h"
#include "agents/Agent.h"
#include "inventory/Inventory.h"
#include "missions/MissionInstance.h"
#include "missions/MissionObjectives.h"


uint8 MissionObjectives::GetObjective(const MissionOffer& offer)
{
    if (offer.objectiveType != Mission::ObjType::None)
        return offer.objectiveType;
    // offers made before mission content had objective types
    switch (offer.typeID) {
        case Mission::Type::Courier:    return Mission::ObjType::Deliver;
        case Mission::Type::Mining:     return Mission::ObjType::Mine;
        case Mission::Type::Trade:      return Mission::ObjType::Trade;
        case Mission::Type::Encounter:  return Mission::ObjType::KillAll;
    }
    return Mission::ObjType::None;
}

uint32 MissionObjectives::GetDeliveryLocation(const MissionOffer& offer)
{
    switch (GetObjective(offer)) {
        case Mission::ObjType::Deliver:
            return offer.destinationID;
        case Mission::ObjType::Mine:
        case Mission::ObjType::Fetch:
        case Mission::ObjType::Trade:
            // the goods are brought back to the agent
            return offer.originID;
    }
    // kill objectives aren't turned in anywhere
    return 0;
}

uint8 MissionObjectives::Check(Client* pClient, const MissionOffer& offer)
{
    if (offer.stateID != Mission::State::Accepted)
        return Mission::Objective::NotAccepted;

    switch (GetObjective(offer)) {
        case Mission::ObjType::Deliver:
        case Mission::ObjType::Mine:
        case Mission::ObjType::Fetch:
        case Mission::ObjType::Trade: {
            uint32 locationID = GetDeliveryLocation(offer);
            if ((locationID == 0) or ((uint32)pClient->GetStationID() != locationID))
                return Mission::Objective::WrongLocation;
            if (CountDelivered(pClient, locationID, offer.courierTypeID) < offer.courierAmount)
                return Mission::Objective::MissingItems;
            return Mission::Objective::Complete;
        }
        case Mission::ObjType::KillAll:
        case Mission::ObjType::KillTrigger:
        case Mission::ObjType::DestroyStructure:
        case Mission::ObjType::TalkToAgent:
            // set by the mission pocket's events (MissionInstanceMgr) or a GM
            return (offer.objectiveCompleted ? Mission::Objective::Complete : Mission::Objective::Incomplete);
    }

    return Mission::Objective::Unsupported;
}

bool MissionObjectives::Consume(Client* pClient, const MissionOffer& offer)
{
    switch (GetObjective(offer)) {
        case Mission::ObjType::Deliver:
        case Mission::ObjType::Mine:
        case Mission::ObjType::Fetch:
        case Mission::ObjType::Trade: {
            if ((offer.courierTypeID == 0) or (offer.courierAmount == 0))
                return true;
            return TakeDelivered(pClient, GetDeliveryLocation(offer), offer.courierTypeID, offer.courierAmount);
        }
    }

    // kill objectives take nothing
    return true;
}

bool MissionObjectives::ForceComplete(uint32 charID, uint32 agentID)
{
    Agent* pAgent(sEntityList.GetAgent(agentID));
    MissionOffer offer = MissionOffer();
    if ((pAgent == nullptr) or !pAgent->HasMission(charID, offer) or (offer.stateID != Mission::State::Accepted))
        return false;
    switch (GetObjective(offer)) {
        case Mission::ObjType::KillAll:
        case Mission::ObjType::KillTrigger:
        case Mission::ObjType::DestroyStructure:
        case Mission::ObjType::TalkToAgent:
            return sMissionInstanceMgr.SetObjectiveComplete(offer.offerID);
    }
    // item objectives are done by bringing the items
    return false;
}

uint32 MissionObjectives::CountDelivered(Client* pClient, uint32 stationID, uint16 typeID)
{
    uint32 charID(pClient->GetCharacterID()), count(0);
    if (stationID != 0) {
        Inventory* pHangar = pClient->GetStationHangar(stationID);
        if (pHangar != nullptr)
            count += pHangar->GetTypeQtyForOwner(typeID, flagHangar, charID);
    }

    if ((stationID == 0) or ((uint32)pClient->GetStationID() == stationID)) {
        ShipItemRef sRef = pClient->GetShip();
        if ((sRef.get() != nullptr) and (sRef->GetMyInventory() != nullptr))
            count += sRef->GetMyInventory()->GetTypeQtyForOwner(typeID, flagCargoHold, charID);
    }

    return count;
}

bool MissionObjectives::TakeDelivered(Client* pClient, uint32 stationID, uint16 typeID, uint32 qty, bool allowPartial/*false*/)
{
    if (qty < 1)
        return true;

    uint32 charID(pClient->GetCharacterID());
    Inventory* pHangar(nullptr);
    Inventory* pCargo(nullptr);
    if (stationID != 0)
        pHangar = pClient->GetStationHangar(stationID);
    if ((stationID == 0) or ((uint32)pClient->GetStationID() == stationID)) {
        ShipItemRef sRef = pClient->GetShip();
        if (sRef.get() != nullptr)
            pCargo = sRef->GetMyInventory();
    }

    uint32 inHangar(pHangar == nullptr ? 0 : pHangar->GetTypeQtyForOwner(typeID, flagHangar, charID));
    uint32 inCargo(pCargo == nullptr ? 0 : pCargo->GetTypeQtyForOwner(typeID, flagCargoHold, charID));
    if ((inHangar + inCargo) < qty) {
        if (!allowPartial)
            return false;
        qty = inHangar + inCargo;
    }

    uint32 fromHangar(std::min(inHangar, qty));
    uint32 fromCargo(qty - fromHangar);
    std::vector<InventoryItemRef> taken;
    if ((fromHangar > 0) and !pHangar->TakeTypeQtyForOwner(typeID, flagHangar, charID, fromHangar, taken))
        _log(PLAYER__ERROR, "MissionObjectives::TakeDelivered() - failed to take %u of type %u from %s's hangar in %u.", fromHangar, typeID, pClient->GetName(), stationID);
    if ((fromCargo > 0) and !pCargo->TakeTypeQtyForOwner(typeID, flagCargoHold, charID, fromCargo, taken))
        _log(PLAYER__ERROR, "MissionObjectives::TakeDelivered() - failed to take %u of type %u from %s's cargo.", fromCargo, typeID, pClient->GetName());

    for (auto cur : taken)
        cur->Delete();

    return true;
}
