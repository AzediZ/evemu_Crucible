
 /**
  * @name Prospector.cpp
  *   prospector module class (salvage, hacking, data mining)
  * @Author:         Allan
  * @date:   11 August 2016   -UD/RW 12 April 2017  -UD/RW 10 February 2018
  * @update: F11 - Crucible per-cycle roll on site containers and wrecks
  */


#include "StaticDataMgr.h"
#include "StatisticMgr.h"
#include "exploration/Exploration.h"
#include "ship/modules/Prospector.h"
#include "system/Container.h"
#include "system/SystemManager.h"
#include "system/cosmicMgrs/DungeonMgr.h"
#include "system/cosmicMgrs/IncursionMgr.h"

/* this class is for all salvage and data mining types */

// a wreck holding more than one jetcan can take can't be salvaged (SalvageTooMuchLoot)
static const double maxSalvageLootVolume = 27500.0;

Prospector::Prospector(ModuleItemRef mRef, ShipItemRef sRef)
: ActiveModule(mRef, sRef),
m_salvager((m_modRef->groupID() == EVEDB::invGroups::Salvager)),
m_dataMiner((m_modRef->groupID() == EVEDB::invGroups::Data_Miner)),
m_targetGone(false),
m_targetTypeID(0),
m_stopMsg(nullptr),
m_holdFlag(flagCargoHold)
{
    if (m_shipRef->HasAttribute(AttrSalvageHoldCapacity))
        m_holdFlag = flagSalvageHold;
}

Prospector::~Prospector()
{
    PySafeDecRef(m_stopMsg);
}

void Prospector::Update()
{
    // cycle time and access bonus come from dogma (skills, rigs, implants, hulls)
    ActiveModule::Update();
}

void Prospector::Activate(uint16 effectID, uint32 targetID, int16 repeat)
{
    // reset for each activation.  MUST reset BEFORE ActiveModule::Activate() is called.....it calls CanActivate()
    m_targetGone = false;
    m_targetTypeID = 0;
    PySafeDecRef(m_stopMsg);
    m_stopMsg = nullptr;

    ActiveModule::Activate(effectID, targetID, repeat);

    if (!m_needsTarget or (m_targetSE == nullptr)) {
        ActiveModule::Deactivate();
        return;
    }

    _log(MODULE__DEBUG, "Prospector::Activate() - %s on %s:  target difficulty %.6f, module bonus %.2f, chance %i%%", \
            m_modRef->name(), m_targetSE->GetName(), m_targetSE->GetSelf()->GetAttribute(AttrAccessDifficulty).get_float(), \
            GetAttribute(AttrAccessDifficultyBonus).get_float(), GetChance());
}

bool Prospector::CanActivate()
{
    try {
        CheckTarget();
    } catch (...) {
        Clear();
        throw;
    }

    return ActiveModule::CanActivate();
}

void Prospector::CheckTarget()
{
    if (m_targetSE == nullptr)
        throw UserError ("DeniedActivateTargetModuleDisallowed");

    m_targetTypeID = m_targetSE->GetTypeID();
    const ItemType& tType(m_targetSE->GetSelf()->type());

    if (m_targetSE->IsContainerSE()) {
        if (!sExplorationMgr.IsLockedType(m_targetTypeID))
            throw UserError ("DeniedActivateTargetModuleDisallowed");
        if (!m_targetSE->GetContSE()->IsLocked())
            throw UserError ("LockedContainerOpenedAlready").AddFormatValue("typeID", new PyInt(m_targetTypeID));

        // the container's requiredSkill1 names the module that opens it (Hacking: codebreakers, Archaeology: analyzers,
        //  Salvaging: salvagers).  a container without one opens with any data miner
        uint16 needSkill(tType.HasAttribute(AttrRequiredSkill1) ? tType.GetAttribute(AttrRequiredSkill1).get_uint32() : 0);
        if (needSkill > 0) {
            if (needSkill != ModuleSkill())
                throw UserError ("DeniedActivateTargetModuleDisallowed");
        } else if (!m_dataMiner) {
            throw UserError ("DeniedActivateTargetModuleDisallowed");
        }

        Client* pClient(m_shipRef->GetPilot());
        if ((needSkill > 0) and tType.HasAttribute(AttrRequiredSkill1Level) and (pClient != nullptr))
            if (pClient->GetChar()->GetSkillLevel(needSkill) < (int8)tType.GetAttribute(AttrRequiredSkill1Level).get_uint32())
                throw UserError ("LockedContainerTooComplex").AddFormatValue("typeID", new PyInt(m_targetTypeID));

        if (GetChance() <= 0)
            throw UserError ("LockedContainerTooComplex").AddFormatValue("typeID", new PyInt(m_targetTypeID));
    } else if (m_salvager and m_targetSE->IsWreckSE()) {
        if (m_targetSE->GetWreckSE()->IsSalvageDone())
            throw UserError ("SalvagingTooLate").AddTypeName("type", m_targetTypeID);

        // loot left in the wreck is spilled into one jetcan
        double volume(0.0);
        std::vector<InventoryItemRef> loot;
        m_targetSE->GetSelf()->GetMyInventory()->GetInventoryVec(loot);
        for (auto cur : loot)
            volume += cur->GetAttribute(AttrVolume).get_double() * cur->quantity();
        if (volume > maxSalvageLootVolume)
            throw UserError ("SalvageTooMuchLoot")
                    .AddFormatValue("volume", new PyFloat(volume))
                    .AddFormatValue("maxvolume", new PyFloat(maxSalvageLootVolume));

        std::vector<uint32> list;
        sDataMgr.GetSalvage(atoi(m_targetSE->GetSelf()->customInfo().c_str()), list);
        if (list.empty())
            throw UserError ("SalvagingPointless").AddTypeName("type", m_targetTypeID);

        if (GetChance() <= 0)
            throw UserError ("SalvagingTooComplex").AddTypeName("type", m_targetTypeID);
    } else {
        throw UserError ("DeniedActivateTargetModuleDisallowed");
    }
}

int16 Prospector::GetChance()
{
    if (m_targetSE == nullptr)
        return 0;
    // accessDifficulty is 30/10/~0/-10 on site containers (Basic/Standard/Fortified/Secure), -20..30 on wrecks
    float chance(m_targetSE->GetSelf()->GetAttribute(AttrAccessDifficulty).get_float());
    chance += GetAttribute(AttrAccessDifficultyBonus).get_float();
    return (int16)std::floor(chance + 0.5f);
}

void Prospector::CycleComplete()
{
    if (m_targetGone or (m_targetSE == nullptr) or (m_shipRef->GetPilot() == nullptr)) {
        m_Stop = true;
        return;
    }

    // someone else got there first
    if (m_targetSE->IsContainerSE() and !m_targetSE->GetContSE()->IsLocked()) {
        SendRemote("LockedContainerOpenedAlready", ItemArg(m_targetTypeID));
        m_Stop = true;
        return;
    }

    int16 chance(GetChance());
    bool success((chance > 0) and (MakeRandomFloat(0, 100) < chance));
    _log(MODULE__DEBUG, "Prospector::CycleComplete() - %s on %s:  chance %i%%, %s", \
            m_modRef->name(), m_targetSE->GetName(), chance, (success ? "success" : "failure"));

    SendTrigger(m_targetSE->GetID(), success);

    if (m_targetSE->IsContainerSE()) {
        if (success) {
            // incursion logistics control arrays do their own thing when cracked, and stay locked
            if (sIncursionMgr.Hacked(m_shipRef->GetPilot(), m_targetSE)) {
                m_Stop = true;
            } else {
                OnContainerSuccess();
            }
        } else {
            SendRemote("LockedContainerNotOpened", ItemArg(m_targetTypeID));
        }
    } else if (success) {
        OnSalvageSuccess();
    } else {
        /*
                  [PyTuple 3 items]
                    [PyString "OnRemoteMessage"]
                    [PyString "SalvagingFailure"]
                    [PyDict 1 kvp]
                      [PyString "type"]
                      [PyTuple 2 items]
                        [PyInt 4]           << UE_TYPEID
                        [PyInt 26513]       << wreck type id
         */
        SendRemote("SalvagingFailure", TypeArg(m_targetTypeID));
    }
}

void Prospector::OnContainerSuccess()
{
    ContainerSE* pCont(m_targetSE->GetContSE());
    // open for everyone from now on.  the loot is made now
    pCont->Unlock();
    if (!pCont->IsLootGenerated()) {
        sExplorationMgr.FillContainer(pCont->GetSelf());
        pCont->SetLootGenerated();
    }

    PySafeDecRef(m_stopMsg);
    m_stopMsg = new PyTuple(2);
        m_stopMsg->SetItem(0, new PyString("LockedContainerOpened"));
        m_stopMsg->SetItem(1, ItemArg(m_targetTypeID));
    m_Stop = true;
}

void Prospector::OnSalvageSuccess()
{
    WreckSE* pWreck(m_targetSE->GetWreckSE());
    // other salvagers on it get SalvagingTooLate
    pWreck->SetSalvageDone();

    Client* pClient(m_shipRef->GetPilot());
    bool overloaded(false);
    uint32 dropped(0);
    std::vector<uint32> list;
    sDataMgr.GetSalvage(atoi(m_targetSE->GetSelf()->customInfo().c_str()), list);
    if (!list.empty()) {
        uint8 drop = 1;
        switch (m_targetSE->GetSelf()->GetAttribute(AttrAccessDifficulty).get_int()) {       // drop qty * rate in config
            case  30: drop = 1; break;  //  1 to 3
            case  20: drop = 2; break;  //  2 to 6
            case  10: drop = 3; break;  //  3 to 9
            case   0: drop = 4; break;  //  4 to 12
            case -10: drop = 5; break;  //  5 to 15
            case -20: drop = 6; break;  //  6 to 18
        }

        InventoryItemRef iRef(nullptr);
        Inventory* sInv(m_shipRef->GetMyInventory());
        uint32 quantity = 0, minDrop = drop, maxDrop = (drop * sConfig.rates.DropSalvage);
        for (auto cur : list) {
            // each drop has 50/50 chance.  may need to change this later.   base on char's salvage skill?
            if (IsEven(MakeRandomInt(0,10)))
                continue;
            quantity = (MakeRandomInt(minDrop, maxDrop));
            if (quantity < 1)
                quantity = 1;
            ItemData iLoot(cur, pClient->GetCharacterID(), locTemp, flagNone, quantity);
            iRef = sItemFactory.SpawnItem(iLoot);
            if (iRef.get() == nullptr) // we'll get over it...continue
                continue;
            if (sInv->HasAvailableSpace(m_holdFlag, iRef)) {
                iRef->MergeTypesInCargo(m_shipRef.get(), m_holdFlag);
                ++dropped;
                _log(MODULE__DEBUG, "Prospector::OnSalvageSuccess - dropped %u %s of %u/%u", quantity, iRef->name(), minDrop, maxDrop);
            } else {
                _log(MODULE__DEBUG, "Prospector::OnSalvageSuccess - %s's %s is full.", m_shipRef->name(), sDataMgr.GetFlagName(m_holdFlag));
                // what doesn't fit is destroyed in the process
                iRef->Delete();
                overloaded = true;
                break;
            }
        }
    }

    if (!m_targetSE->GetSelf()->GetMyInventory()->IsEmpty()) {
        // tell wreck it's being salvaged, so do not broadcast slim updates...may no longer need this.  jetcan set to initial location 0
        pWreck->Salvaged();

        std::map<uint32, InventoryItemRef> shipLoot;
        shipLoot.clear();
        m_targetSE->GetSelf()->GetMyInventory()->GetInventoryMap(shipLoot);

        // create new container
        ItemData p_idata(23,   // 23 = cargo container
                        m_targetSE->GetSelf()->ownerID(),
                        locTemp,
                        flagNone,
                        "Jettisoned Loot Container",
                        m_targetSE->GetPosition());

        CargoContainerRef jetCanRef = sItemFactory.SpawnCargoContainer(p_idata);
        if (jetCanRef.get() != nullptr) {
            for (auto cur : shipLoot)
                cur.second->Move(jetCanRef->itemID(),flagNone);
            FactionData data = FactionData();
                data.allianceID = m_targetSE->GetAllianceID();
                data.corporationID = m_targetSE->GetCorporationID();
                data.factionID = m_targetSE->GetWarFactionID();
                data.ownerID = m_targetSE->GetSelf()->ownerID();
            ContainerSE* cSE = new ContainerSE(jetCanRef, m_targetSE->GetServices(), m_sysMgr, data);
            jetCanRef->SetMySE(cSE);
            m_sysMgr->AddEntity(cSE);
            m_targetSE->DestinyMgr()->SendJettisonPacket();
        }
    }

    // add data to StatisticMgr
    sStatMgr.Increment(Stat::shipsSalvaged);

    // the result rides on the effect stop
    PySafeDecRef(m_stopMsg);
    m_stopMsg = new PyTuple(2);
        m_stopMsg->SetItem(0, new PyString(dropped == 0 ? "SalvagingSuccessHollow" : (overloaded ? "SalvagingSuccessOverloaded" : "SalvagingSuccess")));
        m_stopMsg->SetItem(1, TypeArg(m_targetTypeID));

    // the wreck is gone
    _log(MODULE__DEBUG, "Prospector::OnSalvageSuccess() - Salvage successful.  deleting target %s.", m_targetSE->GetName());
    SystemEntity* pTarget(m_targetSE);
    m_Stop = true;
    if (m_targMgr != nullptr)
        m_targMgr->RemoveTarget(pTarget);
    // just in case other modules are targeting this object, let them know it was destroyed.
    if (pTarget->TargetMgr() != nullptr) {
        pTarget->TargetMgr()->RemoveTargetModule(this);
        pTarget->TargetMgr()->Destroyed();
    }
    m_targetSE = nullptr;
    pTarget->Delete();
    SafeDelete(pTarget);
}

PyTuple* Prospector::TakeStopMessage()
{
    PyTuple* msg(m_stopMsg);
    m_stopMsg = nullptr;
    return msg;
}

void Prospector::TargetDestroyed()
{
    if (m_targetGone)
        return;
    m_targetGone = true;
    // another salvager got this wreck
    if (IsSalvaging() and m_targetSE->GetWreckSE()->IsSalvageDone())
        SendRemote("SalvagingTooLate", TypeArg(m_targetTypeID));
}

uint16 Prospector::ModuleSkill() const
{
    const ItemType& type(m_modRef->type());
    for (uint16 attr : { AttrRequiredSkill1, AttrRequiredSkill2, AttrRequiredSkill3 }) {
        uint16 skill(type.GetAttribute(attr).get_uint32());
        if ((skill == EvESkill::Hacking) or (skill == EvESkill::Archaeology) or (skill == EvESkill::Salvaging))
            return skill;
    }
    return 0;
}

void Prospector::SendTrigger(uint32 itemID, bool success)
{
    Client* pClient(m_shipRef->GetPilot());
    if ((pClient == nullptr) or (pClient->SystemMgr() == nullptr))
        return;
    uint16 trigger(0);
    switch (ModuleSkill()) {
        case EvESkill::Hacking:     trigger = (success ? Dungeon::Trigger::HackingSuccess : Dungeon::Trigger::HackingFailure); break;
        case EvESkill::Archaeology: trigger = (success ? Dungeon::Trigger::ArchaeologySuccess : Dungeon::Trigger::ArchaeologyFailure); break;
        case EvESkill::Salvaging:   trigger = (success ? Dungeon::Trigger::SalvagingSuccess : Dungeon::Trigger::SalvagingFailure); break;
        default:
            return;
    }
    pClient->SystemMgr()->GetDungMgr()->OnObjectEvent(itemID, trigger, pClient->GetCharacterID(), m_targetTypeID);
}

void Prospector::SendRemote(const char* key, PyDict* args)
{
    Client* pClient(m_shipRef->GetPilot());
    if (pClient == nullptr) {
        PyDecRef(args);
        return;
    }
    PyTuple* tup = new PyTuple(3);
        tup->SetItem(0, new PyString("OnRemoteMessage"));
        tup->SetItem(1, new PyString(key));
        tup->SetItem(2, args);
    pClient->QueueDestinyEvent(&tup);
}

PyDict* Prospector::TypeArg(uint16 typeID)
{
    PyTuple* type = new PyTuple(2);
        type->SetItem(0, new PyInt(4));     // UE_TYPEID
        type->SetItem(1, new PyInt(typeID));
    PyDict* dict = new PyDict();
        dict->SetItemString("type", type);
    return dict;
}

PyDict* Prospector::ItemArg(uint16 typeID)
{
    // {[item]typeID.name}.  the client's message dict only converts tuples, so this is the raw typeID
    PyDict* dict = new PyDict();
        dict->SetItemString("typeID", new PyInt(typeID));
    return dict;
}

/*
 *  accessDifficultyBonus       << salvage tackle(10), salvage tackleII(15),  salvage skill : salvagerI +5 per level, salvagerII +7 per level
 *  accessDifficulty (s:30,m:20,l:10,f:0,t2:0,o:-10,s:-20)           << in the item to salvage
 *
 *
 *  accessDifficultyBonus       << civilian analyzer(2), implant(5), analyzerII(7)
 *  accessDifficulty (0.000001)    << for analyzing structures ()
 *
 *
 */
