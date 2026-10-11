/**
  * @name MissionObjectives.cpp
  *   mission objective checks and turn-in
  */


#include "eve-server.h"

#include "Client.h"
#include "inventory/Inventory.h"
#include "missions/MissionObjectives.h"


uint32 MissionObjectives::GetDeliveryLocation(const MissionOffer& offer)
{
    switch (offer.typeID) {
        case Mission::Type::Courier:
            return offer.destinationID;
        case Mission::Type::Mining:
            // ore is brought back to the agent
            return offer.originID;
    }
    return 0;
}

uint8 MissionObjectives::Check(Client* pClient, const MissionOffer& offer)
{
    if (offer.stateID != Mission::State::Accepted)
        return Mission::Objective::NotAccepted;

    switch (offer.typeID) {
        case Mission::Type::Courier:
        case Mission::Type::Mining: {
            uint32 locationID = GetDeliveryLocation(offer);
            if ((locationID == 0) or ((uint32)pClient->GetStationID() != locationID))
                return Mission::Objective::WrongLocation;
            if (CountDelivered(pClient, locationID, offer.courierTypeID) < offer.courierAmount)
                return Mission::Objective::MissingItems;
            return Mission::Objective::Complete;
        }
    }

    return Mission::Objective::Unsupported;
}

bool MissionObjectives::Consume(Client* pClient, const MissionOffer& offer)
{
    switch (offer.typeID) {
        case Mission::Type::Courier:
        case Mission::Type::Mining: {
            if ((offer.courierTypeID == 0) or (offer.courierAmount == 0))
                return true;
            return TakeDelivered(pClient, GetDeliveryLocation(offer), offer.courierTypeID, offer.courierAmount);
        }
    }

    return true;
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
