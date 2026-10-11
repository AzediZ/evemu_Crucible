
 /**
  * @name MissionRewards.cpp
  *   agent mission rewards and fleet shares
  */


#include "eve-server.h"

#include "Client.h"
#include "EntityList.h"
#include "EVEServerConfig.h"
#include "StaticDataMgr.h"
#include "account/AccountService.h"
#include "agents/Agent.h"
#include "corporation/LPService.h"
#include "fleet/FleetService.h"
#include "inventory/ItemFactory.h"
#include "missions/MissionDataMgr.h"
#include "missions/MissionRewards.h"
#include "../../eve-common/EVE_Skills.h"


void MissionRewards::Compute(Agent* pAgent, Character* pChar, MissionOffer& offer)
{
    SystemData sysData = SystemData();
    sDataMgr.GetSystemData(pAgent->GetSystemID(), sysData);
    double secMult(1.6288 - std::min<double>(sysData.securityRating, 1.0));

    uint8 negotiation(0), connections(0);
    if (pChar != nullptr) {
        negotiation = pChar->GetSkillLevel(EvESkill::Negotiation);
        // +10% LP per level when working for the agents of the skill's division (Crucible static data)
        switch (pAgent->GetDivisionID()) {
            case 22:    connections = pChar->GetSkillLevel(EvESkill::DistributionConnections); break;
            case 23:    connections = pChar->GetSkillLevel(EvESkill::MiningConnections); break;
            case 24:    connections = pChar->GetSkillLevel(EvESkill::SecurityConnections); break;
        }
    }

    // the old courier rows have their own ISK rewards.  content rewards are base values
    double isk(offer.rewardISK), bonus(offer.bonusISK);
    if ((offer.contentID != 0) and sConfig.missions.IskSecurityScaling) {
        isk *= secMult;
        bonus *= secMult;
    }
    // "5% additional pay per skill level for agent missions"
    isk *= 1.0 + (0.05 * negotiation);
    bonus *= 1.0 + (0.05 * negotiation);
    double lp(offer.rewardLP * secMult * (1.0 + (0.1 * connections)));

    // storyline missions pay no LP and twice the ISK
    if (offer.storyline) {
        isk *= 2.0;
        bonus *= 2.0;
        lp = 0;
    }

    offer.rewardISK = (uint32)std::floor(isk);
    offer.bonusISK = (uint32)std::floor(bonus);
    offer.rewardLP = (uint32)std::floor(lp);
    if (offer.bonusTime == 0)
        offer.bonusTime = sConfig.missions.BonusMinutes[MissionDataMgr::LevelIndex(pAgent->GetLevel())];
}

void MissionRewards::GetSharers(Client* pClient, std::vector<Client*>& out)
{
    out.push_back(pClient);
    if (!pClient->InFleet())
        return;
    uint16 max(std::max<uint16>(sConfig.missions.FleetShareMax, 1));
    std::vector<Client*> members(sFltSvc.GetFleetClients(pClient->GetFleetID()));
    for (auto cur : members) {
        if (out.size() >= max)
            break;
        if ((cur == nullptr) or (cur == pClient))
            continue;
        if (sConfig.missions.FleetShareSameSystem and (cur->GetSystemID() != pClient->GetSystemID()))
            continue;
        out.push_back(cur);
    }
}

float MissionRewards::GetItemVolume(const MissionOffer& offer)
{
    if ((offer.rewardItemID == 0) or (offer.rewardItemQty == 0))
        return 0.0f;
    const ItemType* pType(sItemFactory.GetType(offer.rewardItemID));
    if (pType == nullptr)
        return 0.0f;
    return pType->volume() * offer.rewardItemQty;
}

void MissionRewards::Pay(Agent* pAgent, Client* pClient, MissionOffer& offer, const std::vector<Client*>& sharers)
{
    uint32 n(sharers.empty() ? 1 : (uint32)sharers.size());
    bool bonus((offer.bonusISK > 0) and (offer.bonusTime > 0)
            and ((GetFileTimeNow() - offer.dateAccepted) <= ((double)offer.bonusTime * EvE::Time::Minute)));

    std::vector<Client*> payees(sharers);
    if (payees.empty())
        payees.push_back(pClient);
    for (auto cur : payees) {
        uint32 charID(cur->GetCharacterID());
        if (offer.rewardISK > 0)
            AccountService::TransferFunds(pAgent->GetID(), charID, offer.rewardISK / n, "Mission Reward",
                    Journal::EntryType::AgentMissionReward, pAgent->GetID());
        // bonusTime is in minutes from acceptance
        if (bonus)
            AccountService::TransferFunds(pAgent->GetID(), charID, offer.bonusISK / n, "Mission Bonus Reward",
                    Journal::EntryType::AgentMissionTimeBonusReward, pAgent->GetID());
        if (offer.rewardLP / n > 0)
            LPService::AddLP(charID, pAgent->GetCorpID(), offer.rewardLP / n);
    }

    // the item reward goes to the completer:  the station he's docked in, else his cargo hold (checked by the caller)
    if ((offer.rewardItemID == 0) or (offer.rewardItemQty == 0))
        return;
    sItemFactory.SetUsingClient(pClient);
    ItemData data(offer.rewardItemID, pClient->GetCharacterID(), locTemp, flagNone, offer.rewardItemQty);
    InventoryItemRef iRef(sItemFactory.SpawnItem(data));
    if (iRef.get() != nullptr) {
        if (pClient->IsDocked()) {
            iRef->Move(pClient->GetStationID(), flagHangar, true);
        } else if (pClient->GetShip().get() != nullptr) {
            pClient->GetShip()->AddItemByFlag(flagCargoHold, iRef, pClient);
        } else {
            iRef->Move(pAgent->GetStationID(), flagHangar, true);
        }
    }
    sItemFactory.UnsetUsingClient();
}
