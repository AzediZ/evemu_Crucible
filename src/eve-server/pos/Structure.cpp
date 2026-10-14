
/**
 * @name Structure.cpp
 *   Generic Base Class for POS items and entities.
 *
 * @Author:         Allan
 * @date:   unknown
 * Updates:         James
 */

/*
 * POS__ERROR
 * POS__WARNING
 * POS__MESSAGE
 * POS__DUMP
 * POS__DEBUG
 * POS__DESTINY
 * POS__SLIMITEM
 * POS__TRACE
 */

#include "Client.h"
#include "EntityList.h"
#include "EVEServerConfig.h"
#include "StaticDataMgr.h"
#include "manufacturing/Blueprint.h"
#include "map/MapDB.h"
#include "math/Trig.h"
#include "planet/Moon.h"
#include "planet/Planet.h"
#include "planet/CustomsOffice.h"
#include "pos/Tower.h"
#include "pos/sovStructures/TCU.h"
#include "pos/sovStructures/SBU.h"
#include "pos/sovStructures/IHub.h"
#include "pos/JumpBridge.h"
#include "pos/Array.h"
#include "pos/Structure.h"
#include "station/StationDataMgr.h"
#include "manufacturing/RamMethods.h"
#include "system/Container.h"
#include "system/Damage.h"
#include "system/DestinyCollision.h"
#include "system/SystemManager.h"
#include "system/cosmicMgrs/AnomalyMgr.h"
#include "system/cosmicMgrs/DungeonMgr.h"
#include "system/sov/SovereigntyDataMgr.h"
#include "system/CrimeWatch.h"
#include "standing/StandingDB.h"

namespace {
    // the client's error for a pilot the tower's deploy/usage level (EVEPOS::Access) refuses
    [[noreturn]] void ThrowAccessDenied(Client* pClient, uint32 corpID, int8 level)
    {
        if ((uint32)pClient->GetCorporationID() != corpID)
            throw UserError("CantConfigureNotInRightCorp");
        if (level == EVEPOS::Access::FuelTechRole)
            throw UserError("CantConfigureCorpStarbaseOrCaretakerRoleLacking");
        throw UserError("PermissionDeniedNeedStarbaseRole")
                .AddOwnerName("corp", corpID);
    }
}

/*
 * Base Structure Item for all POS types
 */
StructureItem::StructureItem(uint32 _structureID, const ItemType &_itemType, const ItemData &_data)
    : InventoryItem(_structureID, _itemType, _data)
{
    pInventory = new Inventory(InventoryItemRef(this));
    _log(ITEM__TRACE, "Created StructureItem for %s (%u).", name(), itemID());
    _log(POS__TRACE, "Created StructureItem for %s (%u).", name(), itemID());
}

StructureItem::~StructureItem()
{
    SafeDelete(pInventory);
}

StructureItemRef StructureItem::Load(uint32 structureID)
{
    return InventoryItem::Load<StructureItem>(structureID);
}

bool StructureItem::_Load()
{
    if (!pInventory->LoadContents())
        return false;

    return InventoryItem::_Load();
}

StructureItemRef StructureItem::Spawn(ItemData &data)
{
    uint32 structureID = InventoryItem::CreateItemID(data);
    if (structureID < 1)
        return StructureItemRef(nullptr);
    StructureItemRef sRef = StructureItem::Load(structureID);
    // check for customs offices and set global flag
    if ((data.typeID == EVEDB::invTypes::InterbusCustomsOffice) or (data.typeID == EVEDB::invTypes::PlanetaryCustomsOffice))
    {
        sRef->SetAttribute(AttrIsGlobal, EvilOne);
    }
    // Create default dynamic attributes in the AttributeMap:
    sRef->SetAttribute(AttrMass, sRef->type().mass(), false);
    sRef->SetAttribute(AttrRadius, sRef->type().radius(), false);
    sRef->SetAttribute(AttrVolume, sRef->type().volume(), false);
    sRef->SetAttribute(AttrCapacity, sRef->type().capacity(), false);
    sRef->SetAttribute(AttrShieldCharge, sRef->GetAttribute(AttrShieldCapacity), false);

    // Check for existence of some attributes that may or may not have already been loaded and set them
    // to default values:
    if (!sRef->HasAttribute(AttrDamage))
        sRef->SetAttribute(AttrDamage, EvilZero, false);
    if (!sRef->HasAttribute(AttrArmorDamage))
        sRef->SetAttribute(AttrArmorDamage, EvilZero, false);
    if (!sRef->HasAttribute(AttrArmorMaxDamageResonance))
        sRef->SetAttribute(AttrArmorMaxDamageResonance, EvilOne, false);
    if (!sRef->HasAttribute(AttrShieldMaxDamageResonance))
        sRef->SetAttribute(AttrShieldMaxDamageResonance, EvilOne, false);

    // Shield Resonance
    if (!sRef->HasAttribute(AttrShieldEmDamageResonance))
        sRef->SetAttribute(AttrShieldEmDamageResonance, EvilOne, false);
    if (!sRef->HasAttribute(AttrShieldExplosiveDamageResonance))
        sRef->SetAttribute(AttrShieldExplosiveDamageResonance, EvilOne, false);
    if (!sRef->HasAttribute(AttrShieldKineticDamageResonance))
        sRef->SetAttribute(AttrShieldKineticDamageResonance, EvilOne, false);
    if (!sRef->HasAttribute(AttrShieldThermalDamageResonance))
        sRef->SetAttribute(AttrShieldThermalDamageResonance, EvilOne, false);
    // Armor Resonance
    if (!sRef->HasAttribute(AttrArmorEmDamageResonance))
        sRef->SetAttribute(AttrArmorEmDamageResonance, EvilOne, false);
    if (!sRef->HasAttribute(AttrArmorExplosiveDamageResonance))
        sRef->SetAttribute(AttrArmorExplosiveDamageResonance, EvilOne, false);
    if (!sRef->HasAttribute(AttrArmorKineticDamageResonance))
        sRef->SetAttribute(AttrArmorKineticDamageResonance, EvilOne, false);
    if (!sRef->HasAttribute(AttrArmorThermalDamageResonance))
        sRef->SetAttribute(AttrArmorThermalDamageResonance, EvilOne, false);
    // Hull Resonance
    if (!sRef->HasAttribute(AttrEmDamageResonance))
        sRef->SetAttribute(AttrEmDamageResonance, EvilOne, false);
    if (!sRef->HasAttribute(AttrExplosiveDamageResonance))
        sRef->SetAttribute(AttrExplosiveDamageResonance, EvilOne, false);
    if (!sRef->HasAttribute(AttrKineticDamageResonance))
        sRef->SetAttribute(AttrKineticDamageResonance, EvilOne, false);
    if (!sRef->HasAttribute(AttrThermalDamageResonance))
        sRef->SetAttribute(AttrThermalDamageResonance, EvilOne, false);

    return sRef;
}

void StructureItem::Delete()
{
    // delete contents first
    pInventory->DeleteContents();

    InventoryItem::Delete();
}

PyObject *StructureItem::StructureGetInfo()
{
    if (!pInventory->LoadContents()) {
        codelog(ITEM__ERROR, "%s (%u): Failed to load contents for Structure", name(), m_itemID);
        return nullptr;
    }

    Rsp_CommonGetInfo result;
    Rsp_CommonGetInfo_Entry entry;

    //first populate the Structure.
    if (!Populate(entry))
        return nullptr;

    result.items[m_itemID] = entry.Encode();

    return result.Encode();
}

void StructureItem::AddItem(InventoryItemRef iRef)
{
    if (iRef.get() == nullptr)
        return;

    // test for item types and verify flags here...

    InventoryItem::AddItem(iRef);

    if (mySE->IsCOSE())
        mySE->GetCOSE()->VerifyAddItem(iRef);
}

void StructureItem::RemoveItem(InventoryItemRef iRef)
{
    if (iRef.get() == nullptr)
        return;

    InventoryItem::RemoveItem(iRef);
}

void StructureItem::Rename(std::string name)
{
    if (mySE->GetPOSSE()->GetState() > EVEPOS::StructureState::Unanchored)
    {
        InventoryItem::Rename(name);
        mySE->GetPOSSE()->SendSlimUpdate();
    }
    else
    {
        throw UserError ("SetNameObjectMustBeAnchoredInSpace");
    }
    // {'FullPath': u'UI/Messages', 'messageID': 258480, 'label': u'SetNameObjectMustBeAnchoredInSpaceBody'}(u'You can only rename this type of object if it is anchored in space (and you have a right to do so).', None, None)
}

StructureSE::StructureSE(StructureItemRef structure, EVEServiceManager&services, SystemManager *system, const FactionData &data)
    : ObjectSystemEntity(structure, services, system),
    m_moonSE(nullptr),
    m_planetSE(nullptr),
    m_towerSE(nullptr),
    m_tcuSE(nullptr),
    m_sbuSE(nullptr),
    m_ihubSE(nullptr),
    m_gateSE(nullptr),
    m_bridgeSE(nullptr),
    m_procTimer(10000), // arbitrary default
    m_regenTimer(1000),
    m_regenTicks(0),
    m_controllerID(0),
    m_tcu(false),
    m_sbu(false),
    m_ihub(false),
    m_miner(false),
    m_tower(false),
    m_bridge(false),
    m_jammer(false),
    m_generator(false),
    m_platform(false),
    m_loaded(false),
    m_module(false),
    m_reactor(false),
    m_cargo(false),
    m_skipDelay(false),
    m_duration(0),
    m_anchorPointID(0),
    m_delayTime(0),
    /** @todo  this is direction from customs office to planet and set when co is created */
    m_rotation(NULL_ORIGIN),
    m_procState(EVEPOS::ProcState::Invalid),
    m_data(EVEPOS::StructureData())// zero-init data
{
    m_procTimer.Disable();

    m_warID = data.factionID;
    m_allyID = data.allianceID;
    m_corpID = data.corporationID;
    m_ownerID = data.ownerID;

    // update StructureItem with SE;
    structure->SetMySE(this);

    // set valid stateName
    m_data.state = EVEPOS::EntityState::Unanchored;

    _log(SE__DEBUG, "Created StructureSE for item %s (%u).", structure->name(), structure->itemID());
}

void StructureSE::InitData()
{
    // this item is a module.  get towerID from bubble and save
    if (m_module)
        if (m_bubble->HasTower())
            m_data.towerID = m_bubble->GetTowerSE()->GetID();

    if (m_sbu)
    { //SBUs are placed near stargates
        SystemEntity* pGate(m_system->GetClosestGateSE(GetPosition()));
        m_gateSE = (pGate == nullptr ? nullptr : pGate->GetGateSE());
        if (m_gateSE != nullptr)
            m_data.anchorpointID = m_gateSE->GetID();
    }
    if (m_platform and IsOutpostUpgradePlatform())
    { //outpost upgrade and improvement platforms are anchored at the system's outpost (one per system)
        uint32 outpostID(stDataMgr.GetOutpostInSystem(m_system->GetID()));
        if (outpostID != 0)
            m_data.anchorpointID = outpostID;
    }
    else if (m_platform)
    { //Construction Platforms are anchored near planets
        SystemEntity* pPlanet(m_system->GetClosestPlanetSE(GetPosition()));
        m_planetSE = (pPlanet == nullptr ? nullptr : pPlanet->GetPlanetSE());
        if (m_planetSE != nullptr)
            m_data.anchorpointID = m_planetSE->GetID();
    }
    else
    { //Everything else is places near a moon
        SystemEntity* pMoon(m_system->GetClosestMoonSE(GetPosition()));
        m_moonSE = (pMoon == nullptr ? nullptr : pMoon->GetMoonSE());
        if (m_moonSE != nullptr)
            m_data.anchorpointID = m_moonSE->GetID();
    }
}

void StructureSE::Init()
{
    // are we already initialized?
    if (m_loaded)
        return;

    // init all data here.
    m_data.itemID = m_self->itemID();
    if (m_data.itemID == 0)
    {
        // how the hell is itemID 0 here??
        sLog.Error("StructureSE::Init", "ItemID is 0.");
        EvE::traceStack();
        m_system->RemoveEntity(this); //this may segfault here....
        return;
    }
    m_data.state = EVEPOS::StructureState::Unanchored;
    m_self->SetFlag(flagStructureInactive);

    switch (m_self->groupID()) {
        case EVEDB::invGroups::Orbital_Infrastructure: {
            // should not hit here, but dont want to use 'default'
            sLog.Error("StructureSE::Init", "groupID is Orbital_Infrastructure.");
            EvE::traceStack();
        } break;
        case EVEDB::invGroups::Sovereignty_Blockade_Units: {
            m_sbu = true;
        } break;
        case EVEDB::invGroups::Control_Tower: {
            m_tower = true;
            m_data.towerID = m_data.itemID;
        } break;
        case EVEDB::invGroups::Territorial_Claim_Units: {
            m_tcu = true;
        } break;
        case EVEDB::invGroups::Jump_Portal_Array: {
            m_bridge = true;
            m_module = true;
        } break;
        case EVEDB::invGroups::Cynosural_System_Jammer: {
            /** @todo (Allan) do we need anything else here?  check for and set system-wide cyno jammer?
             *    as we're nowhere even close to needing/using cyno, this can wait
             * note: set generator active in mapDynamicData on activation of this module
             */
            m_jammer = true;
            m_module = true;
        } break;
        case EVEDB::invGroups::Cynosural_Generator_Array: {
            m_generator = true;
            m_module = true;
        } break;
        case EVEDB::invGroups::Silo:
        case EVEDB::invGroups::Mobile_Reactor: {
            m_module = true;
            m_reactor = true;
        } break;
        case EVEDB::invGroups::Moon_Mining: {
            m_miner = true;
            m_module = true;
            m_reactor = true;
        } break;
        case EVEDB::invGroups::Infrastructure_Hubs: {
            m_ihub = true;
        } break;
        case EVEDB::invGroups::Construction_Platform:
        case EVEDB::invGroups::Station_Upgrade_Platform:
        case EVEDB::invGroups::Station_Improvement_Platform: {
            m_platform = true;
        } break;
        case EVEDB::invGroups::Secure_Cargo_Container:
        case EVEDB::invGroups::Audit_Log_Secure_Container:
        case EVEDB::invGroups::Cargo_Container: {
            m_cargo = true;
        } break;
        default: {
            m_module = true;
        }
    }

    if (!m_db.GetBaseData(m_data))
    {
        _log(POS__MESSAGE, "StructureSE::Init %s(%u) has no saved data.  Initializing default set.", m_self->name(), m_data.itemID);
        InitData();
        m_db.SaveBaseData(m_data);
    }

    m_loaded = true;

    if (m_tcu or m_ihub) {
        ResumeStateChange();
        if (m_data.state > EVEPOS::StructureState::Anchored)
            m_self->SetFlag(flagStructureActive);
        return;
    }

    if (!IsStaticMapItem(m_data.anchorpointID)) {
        // make error here.  this should never hit.
        _log(POS__MESSAGE, "StructureSE::Init %s(%u) - anchorpointID is invalid.  Load Failure.", m_self->name(), m_data.itemID);
        m_loaded = false;
        return;
    }

    //Get entity for anchor point (type is determined by what is being anchored)
    SystemEntity *pSE = m_system->GetSE(m_data.anchorpointID);
    if (pSE == nullptr)
    {
        _log(POS__WARNING, "StructureSE::Init %s(%u) - Cannot find SE for anchorpointID %u. Load Failure.", \
                m_self->name(), m_data.itemID, m_data.anchorpointID);
        m_loaded = false;
        return;
    }

    if (m_tcu)
    {
        m_tcuSE = pSE->GetTCUSE();
    }

    if (m_sbu) //anchored near stargates, so we need a stargate
    {
        m_sbuSE = pSE->GetSBUSE();
        if (m_gateSE == nullptr)
        {
            m_gateSE = pSE->GetGateSE();
        }
    }
    if (m_ihub)
    {
        m_ihubSE = pSE->GetIHubSE();
    }
    if (m_bridge)
    {
        m_bridgeSE = pSE->GetJumpBridgeSE();
    }
    if (m_platform)
    {
        if ((m_planetSE == nullptr) and !IsOutpostUpgradePlatform())
        {
            m_planetSE = pSE->GetPlanetSE();
        }
    }
    else //everything else is anchored near a moon
    {
        if (m_moonSE == nullptr)
        {
            m_moonSE = pSE->GetMoonSE();
        }
    }

    if (m_miner)
    {
        // need to verify m_moonSE isnt null at this point.
        if (m_moonSE == nullptr)
        {
            //iRef->Delete(); // really delete this?
            _log(POS__ERROR, "StructureSE::Init MoonSE for %s(%u) is invalid.", m_self->name(), m_data.itemID);
            return;
        }
        GVector dir(m_self->position(), m_moonSE->GetPosition());
        dir.normalize();
        m_rotation = dir;
    }
    if (m_cargo)
    {
        if (pSE == nullptr)
        {
            _log(POS__ERROR, "StructureSE::Init %s(%u) is invalid.  why are we here?", m_self->name(), m_data.itemID);
            return;
        }
        m_duration = 60000; //cargo default to 60 seconds
    }
    // all POS have 1h duration
    if (m_tower)
    {
        m_duration = m_self->GetAttribute(AttrPosControlTowerPeriod).get_uint32();
    }
    else if (m_module)
    {
        m_duration = 3600000;   // modules default to 1h
        SystemEntity *pSE = (m_data.towerID == 0 ? nullptr : m_system->GetSE(m_data.towerID));
        if ((pSE == nullptr) or (pSE->GetTowerSE() == nullptr))
        {
            // orphaned:  the tower is gone.  anyone may unanchor it, and it can't online until anchored at a tower
            _log(POS__MESSAGE, "StructureSE::Init %s(%u) has no control tower (%u).", m_self->name(), m_data.itemID, m_data.towerID);
            m_data.towerID = 0;
        }
        else
        {
            m_towerSE = pSE->GetTowerSE();
            m_towerSE->AddModule(this);
        }
    }

    ResumeStateChange();

    if (m_data.state > EVEPOS::StructureState::Anchored)
        m_self->SetFlag(flagStructureActive);
}

void StructureSE::ResumeStateChange()
{
    // an anchoring, onlining or unanchoring that was running when the server stopped carries on (or completes now)
    int64 delay(0);
    int8 procState(EVEPOS::ProcState::Invalid);
    switch (StructureTiming::ChangeFor(m_data.state)) {
        case StructureTiming::Change::Anchoring: {
            delay = m_self->GetAttribute(AttrAnchoringDelay).get_int();
            procState = EVEPOS::ProcState::Anchoring;
        } break;
        case StructureTiming::Change::Onlining: {
            delay = m_self->GetAttribute(AttrOnliningDelay).get_int();
            procState = EVEPOS::ProcState::Onlining;
        } break;
        case StructureTiming::Change::Unanchoring: {
            delay = m_self->GetAttribute(AttrUnanchoringDelay).get_int();
            procState = EVEPOS::ProcState::Unanchoring;
        } break;
        default:
            return;
    }
    int64 left(StructureTiming::ResumeTimerMs(m_data.state, m_data.timestamp, delay, GetFileTimeNow()));
    if (left < 1)
        return;
    m_procState = procState;
    m_delayTime = delay;
    m_procTimer.SetTimer(left);
}

void StructureSE::Delete()
{
    // killed and scooped modules have left their tower already
    if (m_module)
        SetControlTower(nullptr);
    ObjectSystemEntity::Delete();
}

void StructureSE::Scoop()
{
    // a module launched near a tower is listed with it until it anchors elsewhere
    if (m_module)
        SetControlTower(nullptr);
    OnRemovedFromSpace(false);
    m_db.DeleteData(m_data.itemID);
    m_data = EVEPOS::StructureData();

    // repackage item?
    m_self->ChangeSingleton(false);
}

void StructureSE::Process()
{
    /* called by EntityList::Process on every loop */
    /*  Enable base call to Process Targeting and Movement  */
    SystemEntity::Process();

    if (m_regenTimer.Check())
        ProcessShieldRegen();

    if (m_procTimer.Check(false))
        ProcessStateChange();
}

void StructureSE::ProcessStateChange()
{
    m_procTimer.Disable();
    m_delayTime = 0;

    _log(POS__DEBUG, "Module %s(%u) Processing State '%s'", GetName(), m_data.itemID, sDataMgr.GetProcStateName(m_procState));

    using namespace EVEPOS;
    switch (m_procState)
    {
    case ProcState::Unanchoring:
    {
        // free floating again.  the client shows 'unanchoring' until posTimestamp + unanchoringDelay:  settled now
        m_data.timestamp = StructureTiming::SettledStamp(GetFileTimeNow(), m_self->GetAttribute(AttrUnanchoringDelay).get_int());
        m_procState = ProcState::Invalid;
        SendBallFree(true);
        if (m_tower) {
            if ((m_moonSE != nullptr) and (m_moonSE->GetMyTower() == this))
                m_moonSE->SetTower(nullptr);
        } else if (m_module) {
            // a module belongs to the tower it is anchored at
            SetControlTower(nullptr);
        } else if (m_sbu and (GetSBUSE() != nullptr)) {
            // the stargate takes another SBU
            GetSBUSE()->ReleaseGate();
        }
        if (IsStarbaseStructure())
            OnRemovedFromSpace(false);
        SendSlimUpdate();
        m_destiny->SendSpecialEffect(m_data.itemID, m_data.itemID, m_self->typeID(), 0, 0, "effects.AnchorLift", 0, 0, 0, -1, 0);
        m_db.UpdateBaseData(m_data);
    }
    break;
    case ProcState::Anchoring:
    {
        // the client shows 'anchoring' until posTimestamp + anchoringDelay:  settled now
        m_data.timestamp = StructureTiming::SettledStamp(GetFileTimeNow(), m_self->GetAttribute(AttrAnchoringDelay).get_int());
        m_procState = ProcState::Invalid;
        SendBallFree(false);
        SendSlimUpdate();
        m_destiny->SendSpecialEffect(m_data.itemID, m_data.itemID, m_self->typeID(), 0, 0, "effects.AnchorDrop", 0, 0, 0, -1, 0);
        if (m_tower and (m_moonSE != nullptr))
            m_moonSE->SetTower(this);
        if (m_sbu and (GetSBUSE() != nullptr))
            GetSBUSE()->OnAnchored();

        m_db.UpdateBaseData(m_data);
        if (m_tower)
            GetTowerSE()->OnAnchored();
    }
    break;
    case ProcState::Offlining:
    {
        // anchored (posState 1) with a settled stamp:  the client shows 'anchored', not 'anchoring'
        m_procState = ProcState::Invalid;
        m_data.state = StructureState::Anchored;
        m_data.timestamp = StructureTiming::SettledStamp(GetFileTimeNow(), m_self->GetAttribute(AttrAnchoringDelay).get_int());
        SetOffline();
        SendSlimUpdate();
        m_destiny->SendSpecialEffect(m_data.itemID, m_data.itemID, m_self->typeID(), 0, 0, "effects.StructureOffline", 0, 1, 1, -1, 0);
        m_db.UpdateBaseData(m_data);
    }
    break;
    case ProcState::Onlining:
    {
        m_data.state = StructureState::Online;
        SetOnline();
        SendSlimUpdate();
        m_db.UpdateBaseData(m_data);
    }
    break;
    case ProcState::Online:
    {
        Online();
    }
    break;
    case ProcState::Operating:
    {
        // this is virtual, overridden in specific class' for their needs
        // take resources or whatever needs to be done
        Operating();
    }
    break;
    // those below are not coded yet
    case ProcState::Reinforcing:
    case ProcState::SheildReinforcing:
    case ProcState::ArmorReinforcing:
    {
        m_self->SetFlag(flagStructureInactive);
        m_data.state = StructureState::Invulnerable;
        //m_data.state = StructureState::Reinforced;
        m_db.UpdateBaseData(m_data);
        // set timer for time to come out of reinforced
        /*Reinforcement is an established system in EVE where a structure becomes invulnerable for a period of time.
             * The purpose is for the defending corporation to have a chance to react when their structure is attacked.
             * The Control Tower enters reinforced mode when it reaches 25% shield and exits reinforcement somewhere
             * within the time of day, specified by the owner at least 24 hours after it entered reinforcement.
             * So if you set the reinforcement time to “23.00 – 01.00” that means that the Control Tower will, if attacked,
             * exit reinforcement at some time between 23.00 and 01.00 the day after the attack was initiated.
             * Your corporation will receive notification if your Control Tower enters reinforcement mode and that leaves you
             * at least 24 hours to plan the defense.
             * In order to reset the reinforcement cycle, the Control Tower Shield must be repaired back above 25%;
             * note that it exits reinforcement with 0% shield.
             *
             */
    }
    break;
    default:
    case ProcState::Invalid:
    {
        // not sure what needs to be done at this point....we're nowhere close to having this coded.
    }
    break;
    }

    if (m_procState < ProcState::Online)
        m_procState = ProcState::Invalid;
}

void StructureSE::Drop(SystemBubble *pBubble)
{
    // this is for dropping POS where Init() and Add() each need info from the other.
    m_bubble = pBubble;
    Init();
    // now null bubble to be added correctly
    m_bubble = nullptr;
}

/*{'messageKey': 'CantObjectInsideForceField', 'dataID': 17885214, 'suppressable': False, 'bodyID': 260153, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 399}
 * {'messageKey': 'CantOnlineAnotherClaimMarkerOnlining', 'dataID': 17876439, 'suppressable': False, 'bodyID': 256876, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 3091}
 * {'messageKey': 'CantOnlineDamagedGoods', 'dataID': 17885104, 'suppressable': False, 'bodyID': 260116, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 400}
 * {'messageKey': 'CantOnlineDisruptorAnchored', 'dataID': 17877056, 'suppressable': False, 'bodyID': 257107, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 3116}
 * {'messageKey': 'CantOnlineDisruptorNotClaimed', 'dataID': 17877530, 'suppressable': False, 'bodyID': 257286, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 2931}
 * {'messageKey': 'CantOnlineDisruptorOutpostProtecting', 'dataID': 17876457, 'suppressable': False, 'bodyID': 256882, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 3106}
 * {'messageKey': 'CantOnlineDisruptorsOnline', 'dataID': 17877687, 'suppressable': False, 'bodyID': 257346, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 2932}
 * {'messageKey': 'CantOnlineInfrastructureDontOwnFlag', 'dataID': 17876442, 'suppressable': False, 'bodyID': 256877, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 3153}
 * {'messageKey': 'CantOnlineInfrastructureHubAlreadyInSystem', 'dataID': 17876267, 'suppressable': False, 'bodyID': 256810, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 3056}
 * {'messageKey': 'CantOnlineInfrastructureHubNoSovereignty', 'dataID': 17876796, 'suppressable': False, 'bodyID': 257014, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 3125}
 * {'messageKey': 'CantOnlineInfrastructurePresent', 'dataID': 17877059, 'suppressable': False, 'bodyID': 257108, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 3115}
 * {'messageKey': 'CantOnlineMissingUpgrade', 'dataID': 17876891, 'suppressable': False, 'bodyID': 257047, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 3080}
 * {'messageKey': 'CantOnlineNoInfrastructureHub', 'dataID': 17876280, 'suppressable': False, 'bodyID': 256815, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 3078}
 * {'messageKey': 'CantOnlineNotLinked', 'dataID': 17879371, 'suppressable': False, 'bodyID': 257982, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 2294}
 * {'messageKey': 'CantOnlineNotSovereign', 'dataID': 17876923, 'suppressable': False, 'bodyID': 257058, 'messageType': 'hint', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 3144}
 * {'messageKey': 'CantOnlineRequireTower', 'dataID': 17885220, 'suppressable': False, 'bodyID': 260155, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 401}
 * {'messageKey': 'CantOnlineSovInWormhole', 'dataID': 17877461, 'suppressable': False, 'bodyID': 257260, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 2935}
 * {'messageKey': 'CantOnlineSovereigntyAlreadyClaimed', 'dataID': 17876445, 'suppressable': False, 'bodyID': 256878, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 3044}
 * {'messageKey': 'CantOnlineStructureGroupLimit', 'dataID': 17879595, 'suppressable': False, 'bodyID': 258065, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 2407}
 * {'messageKey': 'CantOnlineStructureOwnerBad', 'dataID': 17885124, 'suppressable': False, 'bodyID': 260123, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 402}
 * {'messageKey': 'CantOnlineStructureRequiresSovereigntyLevel', 'dataID': 17879395, 'suppressable': False, 'bodyID': 257991, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 2254}
 * {'messageKey': 'CantOnlineTowerLacksCpuResources', 'dataID': 17885223, 'suppressable': False, 'bodyID': 260156, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 403}
 * {'messageKey': 'CantOnlineTowerLacksPowerResources', 'dataID': 17885226, 'suppressable': False, 'bodyID': 260157, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 404}
 * {'messageKey': 'CantOnlineTowerLacksResources', 'dataID': 17885363, 'suppressable': False, 'bodyID': 260207, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 405}
 * {'messageKey': 'CantOnlineWithinGlobalDisruptor', 'dataID': 17876951, 'suppressable': False, 'bodyID': 257068, 'messageType': 'hint', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 3180}
 *{'FullPath': u'UI/Messages', 'messageID': 260213, 'label': u'CannotRemoveFromLowShieldTowerBody'}(u'You cannot remove {[item]item.name} from the {[item]type.name} while it is reinforced or has a shield level lower than {[numeric]level}%.', None, {u'{[item]item.name}': {'conditionalValues': [], 'variableType': 2, 'propertyName': 'name', 'args': 0, 'kwargs': {}, 'variableName': 'item'}, u'{[item]type.name}': {'conditionalValues': [], 'variableType': 2, 'propertyName': 'name', 'args': 0, 'kwargs': {}, 'variableName': 'type'}, u'{[numeric]level}': {'conditionalValues': [], 'variableType': 9, 'propertyName': None, 'args': 0, 'kwargs': {}, 'variableName': 'level'}})
 *
 * {'FullPath': u'UI/Messages', 'messageID': 259215, 'label': u'OnlineRequiredAnchorBody'}(u'The {item} can only be brought online when it is firmly anchored. It cannot be brought online while unanchored, unanchoring or while anchoring.', None, {u'{item}': {'conditionalValues': [], 'variableType': 10, 'propertyName': None, 'args': 0, 'kwargs': {}, 'variableName': 'item'}})
 *
 */
void StructureSE::SetAnchor(Client *pClient, GPoint &pos)
{
    if (IsStateChangePending() and (m_procState == EVEPOS::ProcState::Unanchoring))
        throw UserError("AlreadyUnanchoring").AddFormatValue("groupName", new PyString(sDataMgr.GetGroupName(m_self->groupID())));
    if (IsStateChangePending() and (m_procState == EVEPOS::ProcState::Anchoring))
        throw UserError("AlreadyAnchoring").AddFormatValue("groupName", new PyString(sDataMgr.GetGroupName(m_self->groupID())));
    if (m_data.state > EVEPOS::StructureState::Unanchored)
        throw CustomError ("The %s is already anchored.", m_self->name());

    // starbase rules (pilots only).  modules anchor at the tower that controls the chosen position
    TowerSE* pControlTower(nullptr);
    if ((pClient != nullptr) and IsStarbaseStructure())
        pControlTower = CheckAnchorRules(pClient, pos);

    // Check for required sovereignty and upgrades for certain structures
    CheckSovRequirements(false);
    if ((m_generator) or (m_jammer)) {
        SovereigntyData sovData = svDataMgr.GetSovereigntyData(m_system->GetID());
        if ((m_generator) and m_db.HasBridge(m_system->GetID()))
            throw CustomError("The %s cannot be anchored as the system already contains a Jump Bridge.", m_self->name());
        if ((m_generator) and (sovData.jammerID != 0))
            throw CustomError("The %s cannot be anchored as the system is currently being jammed.", m_self->name());
        if ((m_jammer) and (sovData.beaconID != 0))
            throw CustomError("The %s cannot be anchored as the system currently has an active beacon.", m_self->name());
    }

    if (m_tower)
    {
        // the tower anchors where it floats (the client sends 0,0,0 for towers) at the moon CheckAnchorRules found
        std::vector<SystemEntity*> moons;
        m_system->GetMoonsInRange(GetPosition(), m_self->GetAttribute(AttrMoonAnchorDistance).get_double(), moons);
        if (!moons.empty() and (moons.front()->GetMoonSE() != nullptr))
            m_moonSE = moons.front()->GetMoonSE();
        if (m_moonSE == nullptr)
            throw CustomError ("The %s can only be anchored near a moon.", m_self->name());
        m_data.anchorpointID = m_moonSE->GetID();
        // one tower per moon, anchoring ones included
        m_moonSE->SetTower(this);

        pos = GetPosition();
        if (sConfig.pos.TowerSnapToMoon)
        {
            // legacy:  a fixed point at the moon
            uint32 dist = m_self->GetAttribute(AttrMoonAnchorDistance).get_uint32();
            uint32 radius(m_moonSE->GetRadius());
            float rad(EvE::Trig::Deg2Rad(90));

            pos = m_moonSE->GetPosition();
            pos.x += ((radius + dist) * std::sin(rad));
            pos.z += ((radius + dist) * std::cos(rad));
            m_destiny->SetPosition(pos);
            sBubbleMgr.Add(this);
        }
        if (m_bubble != nullptr)
            m_bubble->SetTowerSE(GetTowerSE());

        if (is_log_enabled(POS__TRACE))
            _log(POS__TRACE, "StructureSE::Anchor() - TowerSE %s(%u) anchoring at %.2f, %.2f, %.2f at %s",
                 GetName(), m_data.itemID, pos.x, pos.y, pos.z, m_moonSE->GetName());
    }
    else if (m_sbu)
    {
        if (m_gateSE == nullptr)
            throw CustomError ("The %s can only be anchored near a stargate.", m_self->name());
        //verify anchor distance from stargate:  anchorDistanceMin/Max (30-150 km).  GMs may anchor closer
        uint32 distance(m_gateSE->GetPosition().distance(m_self->position()));
        uint32 anchorMin(pClient == nullptr ? 1000 : m_self->GetAttribute(AttrAnchorDistanceMin).get_uint32());
        uint32 anchorMax(m_self->GetAttribute(AttrAnchorDistanceMax).get_uint32());
        if ((distance > anchorMax) or (distance < anchorMin))
            throw UserError("CantAnchorDisruptorNoStargate")
                    .AddFormatValue("minDist", new PyInt(anchorMin / 1000))
                    .AddFormatValue("maxDist", new PyInt(anchorMax / 1000));

        if (pClient != nullptr) {
            // a blockade needs a claim to blockade
            if (svDataMgr.GetSystemAllianceID(m_system->GetID()) == 0)
                throw UserError("CantAnchoredDisruptorNotClaimed");
            // one SBU per stargate
            for (auto cur : m_system->GetOperationalStatics()) {
                if ((cur.first == m_data.itemID) or (cur.second == nullptr) or !cur.second->IsSBUSE())
                    continue;
                StructureSE* pSBU(cur.second->GetPOSSE());
                if ((pSBU->GetGateSE() == m_gateSE) and (pSBU->GetState() > EVEPOS::StructureState::Unanchored))
                    throw UserError("CantAnchorDisruptorTooClose");
            }
        }

        // position is within range.  allow anchoring
        m_destiny->SetPosition(pos);

        if (is_log_enabled(POS__TRACE))
            _log(POS__TRACE, "StructureSE::Anchor() - SBUSE %s(%u) is anchoring %u m from %s",
                 GetName(), m_data.itemID, distance, m_gateSE->GetName());
    }
    else if (m_tcu or m_ihub or m_cargo)
    {
        // these are anchored anywhere in system.
        m_destiny->SetPosition(pos);
    }
    else if (m_platform and IsOutpostUpgradePlatform())
    {
        /* "deployed within range of the station" (Revelations II, 2007).  no era distance found:  within the outpost's
         *  radius + 100 km, the anchoring corp's own outpost */
        uint32 outpostID(stDataMgr.GetOutpostInSystem(m_system->GetID()));
        SystemEntity* pOutpost(outpostID == 0 ? nullptr : m_system->GetSE(outpostID));
        if (pOutpost == nullptr)
            throw CustomError ("The %s can only be anchored at an outpost.", m_self->name());
        if (stDataMgr.GetOwnerID(outpostID) != (uint32)m_corpID)
            throw CustomError ("The %s can only be anchored at an outpost of its own corporation.", m_self->name());
        double anchorMax(pOutpost->GetRadius() + 100000.0);
        if (pOutpost->GetPosition().distance(pos) > anchorMax)
            throw CustomError ("You cannot anchor the %s farther than %.0f km from %s.", m_self->name(), anchorMax / 1000.0, pOutpost->GetName());
        m_data.anchorpointID = outpostID;
        m_destiny->SetPosition(pos);
    }
    else if (m_platform)
    {
        if (m_planetSE == nullptr)
            throw CustomError ("The %s can only be anchored near a planet.", m_self->name());
        //verify anchor distance from planet
        uint32 distance(m_planetSE->GetPosition().distance(m_self->position()));
        uint32 anchorMax = (m_planetSE->GetRadius() + 150000000);

        if (distance > anchorMax)
            throw CustomError ("You cannot anchor the %s farther than %u meters from the planet.", m_self->name(), anchorMax);
        m_destiny->SetPosition(pos);
    }
    else
    {
        // starbase modules belong to the tower they anchor at.  GMs anchor at the tower they were launched near
        if (pControlTower == nullptr)
            pControlTower = (m_towerSE != nullptr ? m_towerSE : FindControlTower(pos));
        if (pControlTower == nullptr)
            throw UserError("CantAnchorRequireTower").AddFormatValue("itemTypeID", new PyInt(m_self->typeID()));
        SetControlTower(pControlTower);
        if (pControlTower->GetMoonSE() != nullptr) {
            m_moonSE = pControlTower->GetMoonSE();
            m_data.anchorpointID = m_moonSE->GetID();
        }
        m_destiny->SetPosition(pos);
    }

    m_self->SaveItem();

    StartStateChange(EVEPOS::ProcState::Anchoring, EVEPOS::StructureState::Anchored, m_self->GetAttribute(AttrAnchoringDelay).get_uint32());

    SendSlimUpdate();

    std::vector<PyTuple *> updates;
    SetBallFree sbf;
    sbf.entityID = m_self->itemID();
    sbf.is_free = false;
    updates.push_back(sbf.Encode());
    SetBallRadius sbr;
    sbr.entityID = m_self->itemID();
    sbr.radius = m_self->radius();
    updates.push_back(sbr.Encode());
    m_destiny->SendDestinyUpdate(updates); //consumed
    m_destiny->SendSpecialEffect(m_data.itemID, m_data.itemID, m_self->typeID(), 0, 0, "effects.AnchorDrop", 0, 1, 1, -1, 0);
    m_db.UpdateBaseData(m_data);
}

void StructureSE::PullAnchor(Client* pClient)
{
    if ((pClient != nullptr) and IsStarbaseStructure())
        CheckUnanchorRules(pClient);
    else if (IsStateChangePending() or (m_data.state != EVEPOS::StructureState::Anchored))
        return;

    // R.A.M. jobs of assembly arrays and labs end; their items go back into the structure
    if (IsArraySE() and GetArraySE()->IsRamInstallation())
        sRamMthd.TerminateStructureJobs(m_data.itemID, EvERam::Status::Unanchor);

    StartStateChange(EVEPOS::ProcState::Unanchoring, EVEPOS::StructureState::Unanchored, m_self->GetAttribute(AttrUnanchoringDelay).get_uint32());

    SendSlimUpdate();
    m_db.UpdateBaseData(m_data);

    if (m_tower and (m_bubble != nullptr) and (m_bubble->GetTowerSE() == GetTowerSE()))
        m_bubble->SetTowerSE(nullptr);

    /*
    if (m_tower)
        SendEffectUpdate(anchorLiftForStructures, true);
    else if (m_co)
        SendEffectUpdate(anchorLiftForOrbitals, true);
    else
        */
    m_destiny->SendSpecialEffect(m_data.itemID, m_data.itemID, m_self->typeID(), 0, 0, "effects.AnchorLift", 0, 1, 1, -1, 0);
}

/*
 * 556     anchoringDelay  1800000     NULL
 * 650     maxStructureDistance    50000   NULL
 * 676     unanchoringDelay    NULL    3600000
 * 677     onliningDelay   1800000     NULL
 * 680     shieldRadius    30000   NULL
 * 711     moonAnchorDistance  100000  NULL
 * 1214    posStructureControlDistanceMax  NULL    15000
 */

void StructureSE::Activate(Client* pClient, int32 effectID)
{
    // already online or onlining
    if (IsPowered() or (m_data.state == EVEPOS::StructureState::Onlining))
        return;

    if ((pClient != nullptr) and IsStarbaseStructure())
    {
        CheckDeployPermission(pClient, EVEPOS::Deploy::Online);
        CheckOnlineRules(pClient);
    }
    else if (IsStateChangePending() or (m_data.state != EVEPOS::StructureState::Anchored))
    {
        if (pClient != nullptr)
            return;
        // GMs:  finish the anchoring first
        throw CustomError ("The %s must be anchored before it can be put online.", m_self->name());
    }

    // sovereignty and infrastructure upgrade requirements
    CheckSovRequirements(true);
    // a blockade needs a claim to blockade
    if (m_sbu and (pClient != nullptr) and (svDataMgr.GetSystemAllianceID(m_system->GetID()) == 0))
        throw UserError("CantOnlineDisruptorNotClaimed");
    // no claim while SBUs are online at more than half the stargates, whoever owns them
    if (m_tcu and SovereigntyDataMgr::IsBlockaded(m_system))
        throw UserError("CantOnlineDisruptorsOnline")
                .AddFormatValue("tower", new PyString(m_self->itemName()));
    // jump bridges need a link
    if (m_bridge and (GetJumpBridgeSE() != nullptr))
        GetJumpBridgeSE()->CheckOnline();

    StartStateChange(EVEPOS::ProcState::Onlining, EVEPOS::StructureState::Onlining, m_self->GetAttribute(AttrOnliningDelay).get_uint32());

    SendSlimUpdate();
    m_destiny->SendSpecialEffect(m_data.itemID, m_data.itemID, m_self->typeID(), 0, 0, "effects.StructureOnline", 0, 1, 1, -1, 0);

    // the module uses the tower's power and cpu from now on
    if (m_module and (m_towerSE != nullptr))
        m_towerSE->OnlineModule(this);

    m_db.UpdateBaseData(m_data);
}

void StructureSE::Deactivate(Client* pClient, int32 effectID)
{
    if ((pClient != nullptr) and IsStarbaseStructure())
    {
        CheckDeployPermission(pClient, EVEPOS::Deploy::Offline);
        // a reinforced tower is locked down
        if (m_tower and IsReinforced())
            throw UserError("CantPerformTowerReinforced").AddFormatValue("typeID", new PyInt(m_self->typeID()));
    }
    if (!IsPowered() and (m_data.state != EVEPOS::StructureState::Onlining))
        return;

    /* Crucible has no offlining delay (no such attribute, and pwn.GetStructureState has no 'offlining'):  the structure
     *  is anchored on the next tic.  posState and posTimestamp stay as they are until then, else the client would
     *  restart an onlining countdown from now
     */
    m_procState = EVEPOS::ProcState::Offlining;
    m_delayTime = 0;
    m_procTimer.SetTimer(500);
}

void StructureSE::ForceOffline()
{
    if (!IsPowered() and (m_data.state != EVEPOS::StructureState::Onlining)
    and !(m_procTimer.Enabled() and (m_procState == EVEPOS::ProcState::Offlining)))
        return;

    m_procTimer.Disable();
    m_procState = EVEPOS::ProcState::Invalid;
    m_delayTime = 0;
    m_data.state = EVEPOS::StructureState::Anchored;
    m_data.timestamp = StructureTiming::SettledStamp(GetFileTimeNow(), m_self->GetAttribute(AttrAnchoringDelay).get_int());
    SetOffline();
    SendSlimUpdate();
    m_destiny->SendSpecialEffect(m_data.itemID, m_data.itemID, m_self->typeID(), 0, 0, "effects.StructureOffline", 0, 1, 1, -1, 0);
    m_db.UpdateBaseData(m_data);
}

void StructureSE::StartStateChange(int8 procState, int8 state, uint32 delay)
{
    m_procState = procState;
    m_data.state = state;
    if (m_skipDelay) {
        // GM:  done as it starts.  the client shows the final state at once, the change ends right after (GMChangeState)
        m_delayTime = 0;
        m_data.timestamp = StructureTiming::SettledStamp(GetFileTimeNow(), delay);
        m_procTimer.SetTimer(1);
        return;
    }
    // the client counts the type's delay down from posTimestamp
    m_delayTime = delay;
    m_data.timestamp = GetFileTimeNow();
    m_procTimer.SetTimer(m_delayTime);
}

bool StructureSE::CompleteStateChange()
{
    if (!m_procTimer.Enabled())
        return false;
    switch (m_procState) {
        case EVEPOS::ProcState::Anchoring:
        case EVEPOS::ProcState::Onlining:
        case EVEPOS::ProcState::Offlining:
        case EVEPOS::ProcState::Unanchoring:
            break;
        default:
            // online and operating cycles aren't a state change
            return false;
    }
    if (m_procState != EVEPOS::ProcState::Offlining) {
        // the client stops counting:  the change is done as of now
        int64 delay(m_self->GetAttribute(m_procState == EVEPOS::ProcState::Onlining ? AttrOnliningDelay
                    : (m_procState == EVEPOS::ProcState::Anchoring ? AttrAnchoringDelay : AttrUnanchoringDelay)).get_int());
        m_data.timestamp = StructureTiming::SettledStamp(GetFileTimeNow(), delay);
    }
    ProcessStateChange();
    return true;
}

bool StructureSE::GMChangeState(StructureTiming::GMAction action)
{
    using StructureTiming::Step;
    m_skipDelay = true;
    bool done(false);
    try {
        // the change that is running first
        done = CompleteStateChange();
        for (auto step : StructureTiming::GMPlan(m_data.state, action)) {
            switch (step) {
                case Step::Anchor: {
                    GPoint pos(GetPosition());
                    SetAnchor(nullptr, pos);
                } break;
                case Step::Online: {
                    // construction platforms and containers have no online state (no onlineForStructures effect)
                    if (m_platform or m_cargo)
                        throw CustomError ("The %s can't be put online.", m_self->name());
                    Activate(nullptr, EVEEffectID::onlineForStructures);
                } break;
                case Step::Offline: {
                    Deactivate(nullptr, EVEEffectID::onlineForStructures);
                } break;
                case Step::Unanchor: {
                    PullAnchor(nullptr);
                } break;
            }
            CompleteStateChange();
            done = true;
        }
    } catch (...) {
        m_skipDelay = false;
        throw;
    }
    m_skipDelay = false;
    return done;
}

void StructureSE::SetOnline()
{
    // this state is persistent until out of resources or changed
    m_self->SetFlag(flagStructureActive);
    m_procState = EVEPOS::ProcState::Online;
    m_data.state = EVEPOS::StructureState::Online;

    SetTimer(m_duration);
    m_db.UpdateBaseData(m_data);
    m_destiny->SendSpecialEffect(m_data.itemID, m_data.itemID, m_self->typeID(), 0, 0, "effects.StructureOnlined", 0, 1, 1, -1, 0);

    if (m_generator) {
        svDataMgr.UpdateSystemBeaconID(m_self->locationID(),m_self->itemID());
    }
    if (m_jammer) {
        svDataMgr.UpdateSystemJammerID(m_self->locationID(),m_self->itemID());
    }
}

void StructureSE::SetOffline()
{
    m_self->SetFlag(flagStructureInactive);
    if (m_module and (m_towerSE != nullptr))
        m_towerSE->OfflineModule(this);
    if (m_generator)
        svDataMgr.ClearSystemBeaconID(m_self->locationID(), m_data.itemID);
    if (m_jammer)
        svDataMgr.ClearSystemJammerID(m_self->locationID(), m_data.itemID);
}

bool StructureSE::IsOnline()
{
    switch (m_data.state) {
        case EVEPOS::StructureState::Online:
        case EVEPOS::StructureState::Operating:
        case EVEPOS::StructureState::Vulnerable:
        case EVEPOS::StructureState::Invulnerable:
            return true;
    }
    return false;
}

void StructureSE::ProcessShieldRegen()
{
    // anchored starbase structures recharge their shield like ships do.  a reinforced tower's shield is frozen
    if (!IsStarbaseStructure() or !IsAnchored() or IsReinforced() or m_killed)
        return;
    float capacity(m_self->GetAttribute(AttrShieldCapacity).get_float());
    float charge(m_self->GetAttribute(AttrShieldCharge).get_float());
    if ((capacity <= 0.0f) or (charge >= capacity))
        return;

    float newCharge(charge + CalcRechargeRate(capacity, charge, m_self->GetAttribute(AttrShieldRechargeRate).get_float()));
    if ((newCharge > capacity) or ((capacity - newCharge) < 0.3f))
        newCharge = capacity;
    m_self->SetAttribute(AttrShieldCharge, newCharge, false);

    // those watching see it every 5s, and when it is full
    if ((++m_regenTicks >= 5) or (newCharge >= capacity)) {
        m_regenTicks = 0;
        SendDamageStateChanged();
    }
}

void StructureSE::OnDamaged(SystemEntity* pAttacker)
{
    TowerSE* pTower(m_tower ? GetTowerSE() : m_towerSE);
    if (pTower != nullptr)
        pTower->OnStructureDamaged(this, pAttacker);
}

float StructureSE::GetShieldFraction()
{
    float capacity(m_self->GetAttribute(AttrShieldCapacity).get_float());
    if (capacity <= 0.0f)
        return 1.0f;
    return (m_self->GetAttribute(AttrShieldCharge).get_float() / capacity);
}

void StructureSE::ValidateAddItem(Client* pClient, InventoryItemRef iRef, EVEItemFlags toFlag)
{
    if ((pClient == nullptr) or !IsStarbaseStructure())
        return;
    if (!IsAnchored())
        throw UserError("CannotPlaceInUnanchoredStructure")
                .AddFormatValue("item", new PyInt(iRef->typeID()))
                .AddFormatValue("type", new PyInt(GetTypeID()));
    // the tower's bays have their own rules (TowerSE)
    if (m_tower)
        return;
    if (!CanUseAccess(pClient, EVEPOS::Usage::View))
        throw UserError("CrpAccessDenied").AddFormatValue("reason", new PyString("The starbase's access settings don't let you put items into this structure."));
    // an offline structure of a tower that is reinforced (or could be) is locked
    if (!IsPowered() and (m_towerSE != nullptr)
    and (m_towerSE->IsReinforced() or (m_towerSE->IsPowered() and (m_towerSE->GetShieldFraction() < EVEPOS::ShieldFluctuationLevel))))
        throw UserError("CannotPlaceInOfflineStructure")
                .AddFormatValue("item", new PyInt(iRef->typeID()))
                .AddFormatValue("type", new PyInt(GetTypeID()));
}

void StructureSE::ValidateRemoveItem(Client* pClient, InventoryItemRef iRef)
{
    if ((pClient == nullptr) or !IsStarbaseStructure())
        return;
    if (!IsAnchored())
        throw UserError("CannotRemoveFromUnanchoredStructure")
                .AddFormatValue("item", new PyInt(iRef->typeID()))
                .AddFormatValue("type", new PyInt(GetTypeID()));
    if (m_tower)
        return;
    if (!CanUseAccess(pClient, EVEPOS::Usage::Take))
        throw UserError("CrpAccessDenied").AddFormatValue("reason", new PyString("The starbase's access settings don't let you take items from this structure."));
    if (!IsPowered() and (m_towerSE != nullptr)
    and (m_towerSE->IsReinforced() or (m_towerSE->IsPowered() and (m_towerSE->GetShieldFraction() < EVEPOS::ShieldFluctuationLevel))))
        throw UserError("CannotRemoveFromOfflineStructure")
                .AddFormatValue("item", new PyInt(iRef->typeID()))
                .AddFormatValue("type", new PyInt(GetTypeID()));
}

bool StructureSE::IsControllable()
{
    EvilNumber value(EvilZero);
    return ((IsWeaponSE() or IsBatterySE()) and m_self->HasAttribute(AttrPosPlayerControlStructure, value) and (value.get_int() != 0));
}

bool StructureSE::HasRole(Client* pClient, int64 role)
{
    if (pClient == nullptr)
        return true;
    if ((uint32)pClient->GetCorporationID() != m_corpID)
        return false;
    return ((pClient->GetCorpRole() & (role | Corp::Role::Director)) != 0);
}

bool StructureSE::CheckAccessLevel(Client* pClient, int8 level)
{
    if (pClient == nullptr)
        return true;
    bool sameCorp((uint32)pClient->GetCorporationID() == m_corpID);
    switch (level) {
        case EVEPOS::Access::ConfigRole:
            return HasRole(pClient, Corp::Role::StarbaseConfig);
        case EVEPOS::Access::FuelTechRole:
            // starbase managers may do what fuel technicians may
            return HasRole(pClient, Corp::Role::StarbaseCaretaker | Corp::Role::StarbaseConfig);
        case EVEPOS::Access::Corp:
            return sameCorp;
        case EVEPOS::Access::Alliance:
            return (sameCorp or (IsAlliance(m_allyID) and (pClient->GetAllianceID() == m_allyID)));
    }
    return false;
}

void StructureSE::CheckDeployPermission(Client* pClient, int8 action)
{
    if (pClient == nullptr)
        return;

    TowerSE* pTower(m_tower ? GetTowerSE() : m_towerSE);
    int8 level(EVEPOS::Access::ConfigRole);
    if (pTower != nullptr) {
        level = pTower->GetDeployFlag(action);
    } else if (m_module and (action == EVEPOS::Deploy::Unanchor)) {
        // orphaned structures (their tower is gone) can be unanchored by anyone
        return;
    }
    if (!CheckAccessLevel(pClient, level))
        ThrowAccessDenied(pClient, m_corpID, level);
}

bool StructureSE::CanUseAccess(Client* pClient, int8 usage)
{
    switch (usage) {
        case EVEPOS::Usage::View:   return CheckAccessLevel(pClient, m_data.view);
        case EVEPOS::Usage::Take:   return CheckAccessLevel(pClient, m_data.take);
        case EVEPOS::Usage::Use:    return CheckAccessLevel(pClient, m_data.use);
    }
    return false;
}

bool StructureSE::IsStateChangePending()
{
    if (!m_procTimer.Enabled())
        return false;
    return ((m_procState == EVEPOS::ProcState::Anchoring)
         or (m_procState == EVEPOS::ProcState::Onlining)
         or (m_procState == EVEPOS::ProcState::Unanchoring));
}

const char* StructureSE::GetPendingActionName()
{
    if (m_procTimer.Enabled())
        switch (m_procState) {
            case EVEPOS::ProcState::Anchoring:      return "anchoring";
            case EVEPOS::ProcState::Onlining:       return "onlining";
            case EVEPOS::ProcState::Unanchoring:    return "unanchoring";
        }
    return "";
}

bool StructureSE::IsFullyAnchored()
{
    return ((m_data.state == EVEPOS::StructureState::Anchored) and !IsStateChangePending());
}

bool StructureSE::IsAnchored()
{
    if (m_data.state <= EVEPOS::StructureState::Unanchored)
        return false;
    return !(m_procTimer.Enabled() and (m_procState == EVEPOS::ProcState::Anchoring));
}

void StructureSE::SetControlTower(TowerSE* pTower)
{
    if (m_towerSE == pTower)
        return;
    if (m_towerSE != nullptr)
        m_towerSE->RemoveModule(this);
    m_towerSE = pTower;
    m_data.towerID = (pTower == nullptr ? 0 : pTower->GetID());
    if (pTower != nullptr)
        pTower->AddModule(this);
}

TowerSE* StructureSE::FindControlTower(const GPoint& pos)
{
    // the closest anchored tower whose control distance (maxStructureDistance) reaches pos.  any owner:  the
    //  ownership rule has its own message
    TowerSE* pBest(nullptr);
    double best(-1);
    for (auto cur : m_system->GetEntities()) {
        if ((cur.second == this) or !cur.second->IsTowerSE())
            continue;
        TowerSE* pTower(cur.second->GetTowerSE());
        if (pTower->GetState() <= EVEPOS::StructureState::Unanchored)
            continue;
        double dist(pos.distance(pTower->GetPosition()));
        if (dist > pTower->GetSelf()->GetAttribute(AttrMaxStructureDistance).get_double())
            continue;
        if ((best < 0) or (dist < best)) {
            best = dist;
            pBest = pTower;
        }
    }
    return pBest;
}

uint32 StructureSE::CountGroupInSystem(bool onlineOnly)
{
    // structures of this group owned by this structure's alliance (its corp without one)
    uint32 count(0);
    for (auto cur : m_system->GetEntities()) {
        if ((cur.second == this) or !cur.second->IsPOSSE())
            continue;
        StructureSE* pSE(cur.second->GetPOSSE());
        if (pSE->GetSelf()->groupID() != m_self->groupID())
            continue;
        if (IsAlliance(m_allyID) ? (pSE->GetAllianceID() != m_allyID) : (pSE->GetCorporationID() != m_corpID))
            continue;
        if (onlineOnly ? (pSE->IsPowered() or (pSE->GetState() == EVEPOS::StructureState::Onlining))
                       : (pSE->GetState() > EVEPOS::StructureState::Unanchored))
            ++count;
    }
    return count;
}

TowerSE* StructureSE::CheckAnchorRules(Client* pClient, const GPoint& pos)
{
    uint16 typeID(m_self->typeID());
    float trueSec(m_system->GetSystemSecurityRating());
    float secLevel(CrimeWatch::SecurityLevel(trueSec));

    // in reach of the pilot (the client asks for 30km to the tower and the configure distance to modules)
    if (pClient->GetShipSE() != nullptr) {
        double needed(m_tower ? 30000.0 : maxConfigureDistance);
        double actual(pClient->GetShipSE()->GetPosition().distance(GetPosition()) - GetRadius() - pClient->GetShipSE()->GetRadius());
        if (actual > needed + 1000.0)
            throw UserError("CantConfigureDistant2")
                    .AddFormatValue("actual", new PyFloat(actual))
                    .AddFormatValue("needed", new PyFloat(needed));
    }

    // deadspace
    if (m_system->GetDungMgr()->IsDeadspace(m_tower ? GetPosition() : pos))
        throw UserError("DunCantAnchor").AddFormatValue("type", new PyInt(typeID));

    // Anchoring (and the like) skills
    if (pClient->GetChar().get() != nullptr) {
        uint8 count(0);
        std::string missing;
        for (auto cur : m_self->type().GetRequiredSkills()) {
            if ((cur.first == 0) or pClient->GetChar()->HasSkillTrainedToLevel(cur.first, cur.second))
                continue;
            if (!missing.empty())
                missing += ", ";
            std::string name;
            sDataMgr.GetSkillName(cur.first, name);
            missing += name;
            missing += " ";
            missing += std::to_string(cur.second);
            ++count;
        }
        if (count > 0)
            throw UserError("SkillPrerequisitesForAnchoring")
                    .AddFormatValue("skillCount", new PyInt(count))
                    .AddFormatValue("requiredSkills", new PyString(missing));
    }

    if (m_tower) {
        GPoint towerPos(GetPosition());
        if (sConfig.pos.ConcordAnchorRestriction and ((secLevel + 0.001f) >= 0.8f))
            throw UserError("CantAnchorConcordRestriction").AddFormatValue("security", new PyFloat(secLevel));

        // exactly one moon in reach
        std::vector<SystemEntity*> moons;
        m_system->GetMoonsInRange(towerPos, m_self->GetAttribute(AttrMoonAnchorDistance).get_double(), moons);
        if (moons.empty())
            throw UserError("CantAnchorNoMoonNearby");
        if (moons.size() > 1) {
            std::string names;
            for (auto cur : moons) {
                if (!names.empty())
                    names += ", ";
                names += cur->GetName();
            }
            throw UserError("CantAnchorTooManyMoonsNearby").AddFormatValue("moons", new PyString(names));
        }
        MoonSE* pMoon(moons.front()->GetMoonSE());
        if ((pMoon != nullptr) and (pMoon->GetMyTower() != nullptr) and (pMoon->GetMyTower() != this))
            throw UserError("CantAnchorMoonAlreadyHasControlTower")
                    .AddFormatValue("towerTypeID", new PyInt(typeID))
                    .AddFormatValue("moon", new PyInt(moons.front()->GetID()));

        // nothing else anchored within the tower's control distance
        double maxDist(m_self->GetAttribute(AttrMaxStructureDistance).get_double());
        for (auto cur : m_system->GetEntities()) {
            if ((cur.second == this) or !cur.second->IsPOSSE())
                continue;
            if (cur.second->GetPOSSE()->GetState() <= EVEPOS::StructureState::Unanchored)
                continue;
            if (towerPos.distance(cur.second->GetPosition()) < maxDist)
                throw UserError("CantAnchorTowerObstruction")
                        .AddFormatValue("distance", new PyFloat(maxDist))
                        .AddFormatValue("typeID", new PyInt(cur.second->GetTypeID()));
        }

        // empire space:  corp standing to the system's faction and a starbase charter in the cargo hold
        SystemData sData = SystemData();
        sDataMgr.GetSystemData(m_system->GetID(), sData);
        if (IsFaction(sData.factionID) and (trueSec >= 0.45f)) {
            if (sConfig.pos.HighSecStandingCheck) {
                float want(secLevel * 10.0f);
                float have(StandingDB::GetStanding(sData.factionID, m_corpID));
                if (have < want)
                    throw UserError("CantAnchorStandingLow")
                            .AddOwnerName("owner", sData.factionID)
                            .AddFormatValue("want", new PyFloat(want))
                            .AddFormatValue("have", new PyFloat(have));
            }
            std::vector<uint16> charters;
            TowerSE::GetCharterTypes(typeID, m_system->GetID(), charters);
            if (!charters.empty() and (pClient->GetShip().get() != nullptr)) {
                bool found(false);
                std::string names;
                for (auto cur : charters) {
                    if (pClient->GetShip()->GetMyInventory()->ContainsTypeQtyByFlag(cur, flagCargoHold, 1))
                        found = true;
                    if (!names.empty())
                        names += ", ";
                    const ItemType* pType(sItemFactory.GetType(cur));
                    names += (pType == nullptr ? std::to_string(cur) : pType->name());
                }
                if (!found)
                    throw UserError("CantAnchorTowerRequiresResources")
                            .AddFormatValue("typeID", new PyInt(typeID))
                            .AddFormatValue("resources", new PyString(names));
            }
        }

        CheckDeployPermission(pClient, EVEPOS::Deploy::Anchor);
        return nullptr;
    }

    // modules:  at an online tower of the owner corp, inside its control distance
    TowerSE* pTower(FindControlTower(pos));
    if (pTower == nullptr)
        throw UserError("CantAnchorRequireTower").AddFormatValue("itemTypeID", new PyInt(typeID));
    if (!pTower->IsAnchored())
        throw UserError("CantTowerNotAnchored");
    if (!pTower->IsPowered())
        throw UserError("CantTowerNotOnline");
    if (pTower->GetCorporationID() != m_corpID)
        throw UserError("CantAnchorStructureOwnerBad")
                .AddTypeName("what", typeID)
                .AddOwnerName("whom", pTower->GetCorporationID());

    double dist(pos.distance(pTower->GetPosition()));
    double minDist(m_self->GetAttribute(AttrControlTowerMinimumDistance).get_double());
    if ((minDist > 0) and (dist < minDist))
        throw UserError("CantAnchorTowerProximity")
                .AddTypeName("structure", typeID)
                .AddFormatValue("limit", new PyFloat(minDist))
                .AddFormatValue("actual", new PyFloat(dist));

    // not overlapping another anchored structure
    for (auto cur : m_system->GetEntities()) {
        if ((cur.second == this) or !cur.second->IsPOSSE())
            continue;
        if (cur.second->GetPOSSE()->GetState() <= EVEPOS::StructureState::Unanchored)
            continue;
        if (pos.distance(cur.second->GetPosition()) < (GetRadius() + cur.second->GetRadius()))
            throw UserError("CantAnchorPositionObstructed")
                    .AddFormatValue("itemTypeID", new PyInt(typeID))
                    .AddFormatValue("obstructionTypeID", new PyInt(cur.second->GetTypeID()));
    }

    // CONCORD 'public resource consent' (moon mining and reactions only below 0.5, ...)
    EvilNumber value(EvilZero);
    if (sConfig.pos.ConcordAnchorRestriction and m_self->HasAttribute(AttrAnchoringSecurityLevelMax, value))
        if ((secLevel + 0.001f) >= value.get_float())
            throw UserError("CantAnchorStructureRestrictionSpecific")
                    .AddFormatValue("type", new PyInt(typeID))
                    .AddFormatValue("high", new PyFloat(value.get_float()))
                    .AddFormatValue("actual", new PyFloat(secLevel));

    // per-system limits (cyno generators and jammers, jump bridges, ...)
    if (m_self->HasAttribute(AttrPosAnchoredPerSolarSystemAmount, value) and (value.get_uint32() > 0))
        if (CountGroupInSystem(false) >= value.get_uint32())
            throw UserError("CantAnchorStructureGroupLimit")
                    .AddTypeName("type", typeID)
                    .AddFormatValue("limit", new PyInt(value.get_uint32()));

    // the tower's anchor permission, and one operation at a time
    int8 level(pTower->GetDeployFlag(EVEPOS::Deploy::Anchor));
    if (!CheckAccessLevel(pClient, level))
        ThrowAccessDenied(pClient, m_corpID, level);
    pTower->CheckNotBusy(this);

    return pTower;
}

void StructureSE::CheckOnlineRules(Client* pClient)
{
    uint16 typeID(m_self->typeID());
    if (!IsFullyAnchored())
        throw UserError("OnlineRequiredAnchor").AddTypeName("item", typeID);

    // only undamaged structures online
    double damage(m_self->GetAttribute(AttrArmorDamage).get_double() + m_self->GetAttribute(AttrDamage).get_double());
    double hp(m_self->GetAttribute(AttrArmorHP).get_double() + m_self->GetAttribute(AttrHP).get_double());
    if ((damage >= 1.0) and (hp > 0))
        throw UserError("CantOnlineDamagedGoods")
                .AddTypeName("what", typeID)
                .AddFormatValue("damage", new PyInt((int32)std::ceil(damage / hp * 100.0)));

    if (m_tower) {
        GetTowerSE()->CheckNotBusy(this);
        GetTowerSE()->CheckOnlineFuel();
        return;
    }

    if (m_towerSE == nullptr)
        throw UserError("CantOnlineRequireTower").AddFormatValue("itemTypeID", new PyInt(typeID));
    if (!m_towerSE->IsPowered())
        throw UserError("CantTowerNotOnline");
    if (m_towerSE->GetCorporationID() != m_corpID)
        throw UserError("CantOnlineStructureOwnerBad")
                .AddTypeName("what", typeID)
                .AddOwnerName("whom", m_towerSE->GetCorporationID());
    m_towerSE->CheckNotBusy(this);

    EvilNumber value(EvilZero);
    if (m_self->HasAttribute(AttrPosAnchoredPerSolarSystemAmount, value) and (value.get_uint32() > 0))
        if (CountGroupInSystem(true) >= value.get_uint32())
            throw UserError("CantOnlineStructureGroupLimit")
                    .AddFormatValue("type", new PyInt(typeID))
                    .AddFormatValue("limit", new PyInt(value.get_uint32()));

    // a harvester needs to know what to harvest
    if (m_miner and (m_data.provisionTypeID == 0))
        throw UserError("HarvestOnline");

    float cpu(m_self->GetAttribute(AttrCpu).get_float());
    if (!m_towerSE->HasCPU(cpu)) {
        float total(m_towerSE->GetSelf()->GetAttribute(AttrCpuOutput).get_float());
        throw UserError ("NotEnoughCpu")
                .AddFormatValue ("total", new PyFloat(total))
                .AddFormatValue ("require", new PyFloat(cpu))
                .AddFormatValue ("remaining", new PyFloat(total - m_towerSE->GetCPULoad()))
                .AddFormatValue ("moduleType", new PyInt (typeID));
    }
    float power(m_self->GetAttribute(AttrPower).get_float());
    if (!m_towerSE->HasPG(power)) {
        float total(m_towerSE->GetSelf()->GetAttribute(AttrPowerOutput).get_float());
        throw UserError ("NotEnoughPower")
                .AddFormatValue ("total", new PyFloat(total))
                .AddFormatValue ("require", new PyFloat(power))
                .AddFormatValue ("remaining", new PyFloat(total - m_towerSE->GetPGLoad()))
                .AddFormatValue ("moduleType", new PyInt (typeID));
    }
}

void StructureSE::CheckUnanchorRules(Client* pClient)
{
    uint16 typeID(m_self->typeID());
    if (IsStateChangePending()) {
        if (m_procState == EVEPOS::ProcState::Unanchoring)
            throw UserError("AlreadyUnanchoring").AddFormatValue("groupName", new PyString(sDataMgr.GetGroupName(m_self->groupID())));
        if (m_procState == EVEPOS::ProcState::Anchoring)
            throw UserError("AlreadyAnchoring").AddFormatValue("groupName", new PyString(sDataMgr.GetGroupName(m_self->groupID())));
        throw CustomError ("The %s must be offline before it can be unanchored.", m_self->name());
    }
    if (m_data.state <= EVEPOS::StructureState::Unanchored)
        throw UserError("CantDoWhileNotAnchored").AddFormatValue("typeID", new PyInt(typeID));
    if (m_data.state != EVEPOS::StructureState::Anchored)
        throw CustomError ("The %s must be offline before it can be unanchored.", m_self->name());

    CheckDeployPermission(pClient, EVEPOS::Deploy::Unanchor);

    if (m_tower) {
        if (GetTowerSE()->GetAnchoredModule() != nullptr)
            throw UserError("CantUnanchorTowerStructuresPresent").AddFormatValue("tower", new PyInt(typeID));
        GetTowerSE()->CheckNotBusy(this);
    } else if (m_towerSE != nullptr) {
        m_towerSE->CheckNotBusy(this);
    }
}

void StructureSE::CheckSovRequirements(bool online)
{
    uint32 upgradeID(0);
    if (online and m_self->HasAttribute(AttronliningRequiresSovUpgrade1))
        upgradeID = m_self->GetAttribute(AttronliningRequiresSovUpgrade1).get_uint32();
    if ((upgradeID == 0) and m_self->HasAttribute(AttranchoringRequiresSovUpgrade1))
        upgradeID = m_self->GetAttribute(AttranchoringRequiresSovUpgrade1).get_uint32();
    if ((upgradeID == 0) and !m_ihub)
        return;

    uint32 systemID(m_system->GetID());
    // starbase modules that list an upgrade (capital ship arrays) only need it where sovereignty can be held
    if (!m_generator and !m_jammer and !m_bridge and !m_ihub and !svDataMgr.IsClaimable(systemID))
        return;

    SovereigntyData sovData = svDataMgr.GetSovereigntyData(systemID);
    if ((sovData.claimID == 0) or (m_allyID == 0) or (sovData.allianceID != (uint32)m_allyID))
        throw UserError(online ? "CantOnlineNotSovereign" : "CantAnchorNotSovereign")
                .AddFormatValue("structure", new PyString(m_self->itemName()));

    // one hub serves the claim
    if (m_ihub and online and (sovData.hubID != 0) and (sovData.hubID != m_data.itemID))
        throw UserError("CantOnlineInfrastructureHubAlreadyInSystem");
    if (m_ihub or (upgradeID == 0))
        return;

    if (sovData.hubID == 0)
        throw UserError(online ? "CantOnlineNoInfrastructureHub" : "CantAnchorNoInfrastructureHub");

    if (!svDataMgr.HasActiveUpgrade(systemID, upgradeID)) {
        const ItemType* pType(sItemFactory.GetType(upgradeID));
        throw UserError(online ? "CantOnlineMissingUpgrade" : "CantAnchorMissingUpgrade")
                .AddFormatValue("needs", new PyString(pType == nullptr ? "Unknown" : pType->name()));
    }
}

void StructureSE::OnRemovedFromSpace(bool killed)
{
    if (m_generator)
        svDataMgr.ClearSystemBeaconID(m_self->locationID(), m_data.itemID);
    if (m_jammer)
        svDataMgr.ClearSystemJammerID(m_self->locationID(), m_data.itemID);
}

void StructureSE::SetInvulnerable()
{
    m_procState = EVEPOS::ProcState::Online;
    m_data.state = EVEPOS::StructureState::Invulnerable;
    SendSlimUpdate();
    m_db.UpdateBaseData(m_data);
}

void StructureSE::SetVulnerable()
{
    m_procState = EVEPOS::ProcState::Online;
    m_data.state = EVEPOS::StructureState::Vulnerable;
    SetTimer(m_duration);
    SendSlimUpdate();
    m_db.UpdateBaseData(m_data);
}

void StructureSE::SetSovVulnerable(bool vulnerable)
{
    /* the claim marker shows posState online (4) and vulnerable (6) only (sovereigntyClaimMarker.stateModels), so an
     *  invulnerable claim is 'online'.  onlining, offline and reinforced structures keep their state
     */
    if ((m_data.state != EVEPOS::StructureState::Online) and (m_data.state != EVEPOS::StructureState::Vulnerable))
        return;
    int8 state(vulnerable ? EVEPOS::StructureState::Vulnerable : EVEPOS::StructureState::Online);
    if (m_data.state == state)
        return;

    _log(SOV__INFO, "StructureSE::SetSovVulnerable() - %s(%u) in %s is now %s", GetName(), m_data.itemID,
         m_system->GetName(), (vulnerable ? "vulnerable" : "invulnerable"));
    m_data.state = state;
    SendSlimUpdate();
    m_db.UpdateBaseData(m_data);
}

void StructureSE::Online()
{
    // structure online, but not operating
    // take resources or whatever needs to be done

    if (!m_tower)
    {
        // do module shit here....tower online methods handled in tower class
    }

    SetTimer(m_duration);
}

void StructureSE::SetOperating()
{
    // this state is persistant until out of resources or changed
    m_procState = EVEPOS::ProcState::Operating;
    m_data.state = EVEPOS::StructureState::Operating;
    m_data.timestamp = GetFileTimeNow();

    SetTimer(m_duration);

    SendSlimUpdate();

    m_db.UpdateBaseData(m_data);
}

void StructureSE::Operating()
{
    // structure operating
    // take resources, move items, process reactions or whatever needs to be done (follow PI proc code)

    if (!m_tower)
    {
        // do module shit here....tower operating methods handled in tower class
    }

    SetTimer(m_duration);
}

void StructureSE::SetUsageFlags(int8 view /*0*/, int8 take /*0*/, int8 use /*0*/)
{
    m_data.use = use;
    m_data.view = view;
    m_data.take = take;
    m_db.UpdateBaseData(m_data);
}

void StructureSE::SendSlimUpdate()
{
    if (m_destiny == nullptr)
        return;
    // the full slim item (tower, bridge and battery fields included)
    PyDict *slim = MakeSlimItem();
    PyTuple *shipData = new PyTuple(2);
    shipData->SetItem(0, new PyLong(m_data.itemID));
    shipData->SetItem(1, new PyObject("foo.SlimItem", slim));
    PyTuple *sItem = new PyTuple(2);
    sItem->SetItem(0, new PyString("OnSlimItemChange"));
    sItem->SetItem(1, shipData);
    m_destiny->SendSingleDestinyUpdate(&sItem); // consumed
}

void StructureSE::SendBallFree(bool free)
{
    if (m_destiny == nullptr)
        return;
    std::vector<PyTuple*> updates;
    SetBallFree sbf;
    sbf.entityID = m_data.itemID;
    sbf.is_free = free;
    updates.push_back(sbf.Encode());
    // anchored starbase structures are solid
    if (IsStarbaseStructure()) {
        SetBallMassive sbm;
        sbm.entityID = m_data.itemID;
        sbm.is_massive = !free;
        updates.push_back(sbm.Encode());
    }
    m_destiny->SendDestinyUpdate(updates); //consumed
}

void StructureSE::SendEffectUpdate(int16 effectID, bool active)
{
    GodmaEnvironment ge;
    ge.selfID = m_data.itemID;
    ge.charID = m_ownerID;
    ge.shipID = m_data.itemID;
    ge.target = PyStatic.NewNone();
    ge.subLoc = PyStatic.NewNone();
    ge.area = new PyList();
    ge.effectID = effectID;
    Notify_OnGodmaShipEffect shipEff;
    shipEff.itemID = ge.selfID;
    shipEff.effectID = effectID;
    shipEff.timeNow = GetFileTimeNow();
    shipEff.start = (active ? 1 : 0);
    shipEff.active = (active ? 1 : 0);
    shipEff.environment = ge.Encode();
    shipEff.startTime = shipEff.timeNow; // do we need to adjust this?
    shipEff.duration = (active ? 0 : -1);
    shipEff.repeat = (active ? 1 : 0);
    shipEff.error = PyStatic.NewNone();
    PyList *events = new PyList();
    events->AddItem(shipEff.Encode());
    PyTuple *event = new PyTuple(1);
    event->SetItem(0, events);
    m_destiny->SendSingleDestinyEvent(&event); // consumed
}

void StructureSE::EncodeDestiny(Buffer &into)
{
    using namespace Destiny;
    //const uint16 miniballsCount = GetMiniBalls();
    BallHeader head = BallHeader();
    head.entityID = m_data.itemID;
    head.radius = GetRadius();
    head.posX = x();
    head.posY = y();
    head.posZ = z();
    // free floating until anchored (an unanchoring structure stays put until it is done), solid once anchored
    bool unanchoring(m_procTimer.Enabled() and (m_procState == EVEPOS::ProcState::Unanchoring));
    bool anchoring(m_procTimer.Enabled() and (m_procState == EVEPOS::ProcState::Anchoring));
    bool isFree((m_data.state < EVEPOS::StructureState::Anchored) and !unanchoring);
    uint8 massive((IsStarbaseStructure() and !isFree and !anchoring) ? Ball::Flag::IsMassive : 0);
    // collision shape (invTypeMiniBalls)
    const std::vector<MiniBall>* pMiniBalls(sDataMgr.GetMiniBalls(m_self->typeID()));
    if (m_tcu or m_ihub or m_sbu)
    {
        // may need to update this after things are working
        head.mode = Ball::Mode::RIGID;
        head.flags = Ball::Flag::IsGlobal;
    }
    else if (m_tower)
    {
        head.mode = Ball::Mode::STOP;
        head.flags = (isFree ? Ball::Flag::IsFree : 0) | massive;
    }
    else
    {
        head.mode = Ball::Mode::RIGID;
        head.flags = (isFree ? Ball::Flag::IsFree : 0) | massive;
    }
    if (pMiniBalls != nullptr)
        head.flags |= Ball::Flag::HasMiniBalls;
    into.Append(head);

    /** @todo these may need more work....  */
    if (head.mode != Ball::Mode::RIGID)
    {
        MassSector mass = MassSector();
        mass.cloak = 0;
        mass.corporationID = m_corpID;
        mass.allianceID = (IsAlliance(m_allyID) ? m_allyID : -1);
        mass.harmonic = m_harmonic;
        mass.mass = m_self->type().mass();
        into.Append(mass);
    }

    if (isFree) {
        DataSector data = DataSector();
        data.inertia = 1;
        data.velX = 0;
        data.velY = 0;
        data.velZ = 0;
        data.maxSpeed = 1;
        data.speedfraction = 0;
        into.Append( data );
    }


    /* TODO  query and configure miniballs for entity
     * NOTE  MiniBalls are BROKEN!!!  DO NOT USE!
    into.Append( miniballsCount );
    MiniBall miniball;
    for (int16 i; i<miniballsCount; i++) {
        miniball.x = -7701.181;
        miniball.y = 8060.06;
        miniball.z = 27878.900;
        miniball.radius = 1639.241;
        into.Append( miniball );
        miniball.clear();
    }
    [MiniBall]
        [Radius: 963.8593]
        [Offset: (0, -2302, 1)]
    [MiniBall]
        [Radius: 1166.27]
        [Offset: (0, 1298, 1)]
    [MiniBall]
        [Radius: 876.2357]
        [Offset: (0, -502, 1)]
    [MiniBall]
        [Radius: 796.5781]
        [Offset: (0, 2598, 1)]
    */
    if (head.mode == Ball::Mode::RIGID)
    {
        RIGID_Struct main;
        main.formationID = 0xFF;
        into.Append(main);
    }
    else if (head.mode == Ball::Mode::STOP)
    {
        STOP_Struct main;
        main.formationID = 0xFF;
        into.Append(main);
    }
    // after the mode-specific part
    if (pMiniBalls != nullptr)
        AppendMiniBalls(into, *pMiniBalls);

    _log(SE__DESTINY, "StructureSE::EncodeDestiny(): %s - id:%lli, mode:%u, flags:0x%X", GetName(), head.entityID, head.mode, head.flags);
    _log(POS__DESTINY, "StructureSE::EncodeDestiny(): %s - id:%lli, mode:%u, flags:0x%X", GetName(), head.entityID, head.mode, head.flags);
}

PyDict *StructureSE::MakeSlimItem()
{
    _log(SE__SLIMITEM, "MakeSlimItem for StructureSE %u", m_data.itemID);
    _log(POS__SLIMITEM, "MakeSlimItem for StructureSE %u", m_data.itemID);
    /** @todo (Allan) *Timestamp will need to be set to time current state is started. */
    PyDict *slim = new PyDict();
    slim->SetItemString("name", new PyString(m_self->itemName()));
    slim->SetItemString("itemID", new PyLong(m_data.itemID));
    slim->SetItemString("typeID", new PyInt(m_self->typeID()));
    slim->SetItemString("posState", new PyInt(m_data.state));

    slim->SetItemString("ownerID", new PyInt(m_ownerID));
    slim->SetItemString("corpID", IsCorp(m_corpID) ? new PyInt(m_corpID) : PyStatic.NewNone());
    slim->SetItemString("allianceID", IsAlliance(m_allyID) ? new PyInt(m_allyID) : PyStatic.NewNone());
    slim->SetItemString("warFactionID", IsFaction(m_warID) ? new PyInt(m_warID) : PyStatic.NewNone());

    /* every structure (towers, modules, sovereignty structures, platforms):  the client counts anchoring, onlining and
     *  unanchoring down from posTimestamp (pwn.GetStructureState), and without one shows the change as done.
     *  the claim marker (TCU) plays its build and online models over posDelayTime ms (sovereigntyClaimMarker.py)
     */
    slim->SetItemString("posTimestamp", (StructureTiming::HasStamp(m_data.timestamp) ? new PyLong(m_data.timestamp) : PyStatic.NewNone()));
    slim->SetItemString("posDelayTime", new PyInt(m_delayTime));
    slim->SetItemString("incapacitated", new PyInt(m_data.state == EVEPOS::StructureState::Incapacitated));

    if (m_miner)
    {
        PyTuple *tuple = new PyTuple(3);
        tuple->SetItem(0, new PyFloat(m_rotation.x));
        tuple->SetItem(1, new PyFloat(m_rotation.y));
        tuple->SetItem(2, new PyFloat(m_rotation.z));
        slim->SetItemString("dunRotation", tuple); // direction to moon
    }

    // the client finds a module's tower (and orphans) with this
    if (m_module)
        slim->SetItemString("controlTowerID", (m_data.towerID == 0 ? PyStatic.NewNone() : new PyLong(m_data.towerID)));
    // pilot manning a starbase weapon
    if (IsWeaponSE() or IsBatterySE())
        slim->SetItemString("controllerID", (m_controllerID == 0 ? PyStatic.NewNone() : new PyInt(m_controllerID)));

    if (is_log_enabled(POS__SLIMITEM))
    {
        _log(POS__SLIMITEM, "StructureSE::MakeSlimItem() - %s(%u)", GetName(), m_data.itemID);
        slim->Dump(POS__SLIMITEM, "     ");
    }
    return slim;
}

/*  Log events...not sure what this is for yet.
eventTCUExploded = 280
eventTCUInvulnerable = 283
eventTCUOffline = 259
eventTCUOnline = 258
eventTCUVulnerable = 282
eventControlTowerAnchored = 364
eventControlTowerUnanchored = 365
eventControlTowerDestroyed = 366
eventSovereigntyClaimed = 197
eventSovereigntyLost = 194
eventSBUExploded = 279
eventSBUOffline = 257
eventSBUOnline = 256
*/

// this should only be for control towers
// this only hits when tower is in bubble when SetState is called (at login in tower soi)
void StructureSE::GetEffectState(PyList &into)
{
    // update to include all states and correct packet structure -allan 16.10.21
    // new effect packet code
    PyTuple* fxState = new PyTuple(14);
    if (m_module) {
        fxState->SetItemInt(0, m_data.towerID);      // towerID
    } else {
        fxState->SetItemInt(0, m_data.itemID);      // towerID
    }

    fxState->SetItemInt(1, m_data.itemID);      // moduleID
    fxState->SetItemInt(2, m_self->typeID());      // moduleTypeID
    fxState->SetItem(3, PyStatic.NewNone());         // targetID
    fxState->SetItem(4, PyStatic.NewNone());         // chargeTypeID
    fxState->SetItem(5, new PyList());         // area

    // set guid here depending on tower state
    switch (m_data.state) {
        case EVEPOS::StructureState::Online:
        case EVEPOS::StructureState::Operating:
            fxState->SetItemString(6, "effects.StructureOnline");
            break;
        case EVEPOS::StructureState::Incapacitated:
        case EVEPOS::StructureState::Unanchored:
        case EVEPOS::StructureState::Anchored:
        case EVEPOS::StructureState::Onlining:
        case EVEPOS::StructureState::Reinforced:
        case EVEPOS::StructureState::Vulnerable:
        case EVEPOS::StructureState::SheildReinforced:
        case EVEPOS::StructureState::ArmorReinforced:
        case EVEPOS::StructureState::Invulnerable:
            fxState->SetItemString(6, "effects.StructureOffline");
            break;
    }

    fxState->SetItem(7, PyStatic.NewFalse());         // isOffensive
    fxState->SetItem(8, PyStatic.NewOne());      // start
    fxState->SetItem(9, PyStatic.NewOne());      // active
    fxState->SetItem(10, PyStatic.NewNegOne());      // duration
    fxState->SetItem(11, PyStatic.NewZero());      // repeat
    fxState->SetItem(12, new PyLong(m_data.timestamp));      // startTime
    fxState->SetItem(13, PyStatic.NewNone());         // graphicInfo

    // add tuple directly to list.
    into.AddItem(fxState);
}

void StructureSE::Killed(Damage &damage)
{
    if ((m_bubble == nullptr) or (m_destiny == nullptr) or (m_system == nullptr))
        return; // make error here?

    OnRemovedFromSpace(true);

    // a module leaves its tower.  (the tower orphans its modules in TowerSE::Killed)
    if (m_module)
        SetControlTower(nullptr);
    m_db.DeleteData(m_data.itemID);

    uint32 killerID = 0;
    Client *pClient(nullptr);
    SystemEntity *killer = damage.srcSE;

    if (killer == nullptr)
    {
        // killed by nothing in space (gm, script)
    }
    else if (killer->HasPilot())
    {
        pClient = killer->GetPilot();
        killerID = pClient->GetCharacterID();
    }
    else if (killer->IsDroneSE())
    {
        pClient = sEntityList.FindClientByCharID(killer->GetSelf()->ownerID());
        if (pClient == nullptr)
        {
            sLog.Error("StructureSE::Killed()", "killer == IsDrone and pPlayer == nullptr");
        }
        else
        {
            killerID = pClient->GetCharacterID();
        }
    }
    else
    {
        killerID = killer->GetID();
    }

    std::stringstream blob;
    blob << "<items>";
    std::vector<InventoryItemRef> survivedItems;
    std::map<uint32, InventoryItemRef> deadShipInventory;
    deadShipInventory.clear();
    m_self->GetMyInventory()->GetInventoryMap(deadShipInventory);
    if (!deadShipInventory.empty())
    {
        uint32 s = 0, d = 0, x = 0;
        for (auto cur : deadShipInventory)
        {
            d = 0;
            x = cur.second->quantity();
            s = (cur.second->isSingleton() ? 1 : 0);
            if (cur.second->categoryID() == EVEDB::invCategories::Blueprint)
            {
                // singleton for bpo = 1, bpc = 2.
                BlueprintRef bpRef = BlueprintRef::StaticCast(cur.second);
                s = (bpRef->copy() ? 2 : s);
            }
            blob << "<i t=" << cur.second->typeID() << " f=" << cur.second->flag() << " s=" << s;
            // all items have 50% chance of drop, even from popped ship
            if (IsEven(MakeRandomInt(0, 100)))
            {
                // item survived.  check qty for drop
                if (x > 1)
                {
                    d = MakeRandomInt(0, x);
                    x -= d;
                }
                // move item to vector for insertion into wreck later on
                survivedItems.push_back(cur.second);
            }
            blob << " d=" << d << " x=" << x << "/>";
        }
    }
    blob << "</items>";

    /* populate kill data for killMail and save to db  -allan 01May16  --updated 13July17 */
    /** @todo  check for tower/tcu/sbu/jammer and make killmail */
    /** @todo send pos mail/notification to corp members */
    KillData data = KillData();
    data.solarSystemID = m_system->GetID();
    data.victimCharacterID = 0; // charID = 0 means strucuture/item
    data.victimCorporationID = m_corpID;
    data.victimAllianceID = m_allyID;
    data.victimFactionID = m_warID;
    data.victimShipTypeID = GetTypeID();

    data.finalCharacterID = killerID;
    data.finalCorporationID = (killer == nullptr ? 0 : killer->GetCorporationID());
    data.finalAllianceID = (killer == nullptr ? 0 : killer->GetAllianceID());
    data.finalFactionID = (killer == nullptr ? 0 : killer->GetWarFactionID());
    data.finalShipTypeID = (killer == nullptr ? 0 : killer->GetTypeID());
    data.finalWeaponTypeID = (damage.weaponRef.get() == nullptr ? 0 : damage.weaponRef->typeID());
    data.finalSecurityStatus = (pClient == nullptr ? 0 : pClient->GetSecurityRating());
    data.finalDamageDone = damage.GetTotal();

    uint32 totalHP = m_self->GetAttribute(AttrHP).get_uint32();
    totalHP += m_self->GetAttribute(AttrArmorHP).get_uint32();
    totalHP += m_self->GetAttribute(AttrShieldCapacity).get_uint32();
    data.victimDamageTaken = totalHP;

    data.killBlob = blob.str().c_str();
    data.killTime = GetFileTimeNow();
    data.moonID = (m_moonSE == nullptr ? 0 : m_moonSE->GetID()); /* denotes moonID for POS/Structure kills */

    ServiceDB::SaveKillOrLoss(data);

    uint32 locationID = GetLocationID();
    //  player structures count as kills, not npc (faction) kills, and give no npc security status
    MapDB::AddKill(locationID);

    GPoint wreckPosition = m_destiny->GetPosition();
    if (wreckPosition.isNaN())
    {
        sLog.Error("StructureSE::Killed()", "Wreck Position is NaN");
        return;
    }
    uint32 wreckTypeID = sDataMgr.GetWreckID(m_self->typeID());
    if (!IsWreckTypeID(wreckTypeID))
    {
        sLog.Error("StructureSE::Killed()", "Could not get wreckType for %s of type %u", m_self->name(), m_self->typeID());
        // default to generic frigate wreck till i get better checks and/or complete wreck data
        wreckTypeID = 26557;
    }

    std::string wreck_name = m_self->itemName();
    wreck_name += " Wreck";
    ItemData wreckItemData(wreckTypeID, killerID, locationID, flagNone, wreck_name.c_str(), wreckPosition, itoa(m_allyID));
    WreckContainerRef wreckItemRef = sItemFactory.SpawnWreckContainer(wreckItemData);
    if (wreckItemRef.get() == nullptr)
    {
        sLog.Error("StructureSE::Killed()", "Creating Wreck Item Failed for %s of type %u", wreck_name.c_str(), wreckTypeID);
        return;
    }

    if (is_log_enabled(PHYSICS__TRACE))
        _log(PHYSICS__TRACE, "StructureSE::Killed() - Structure %s(%u) Position: %.2f,%.2f,%.2f.  Wreck %s(%u) Position: %.2f,%.2f,%.2f.",
             GetName(), GetID(), x(), y(), z(), wreckItemRef->name(), wreckItemRef->itemID(), wreckPosition.x, wreckPosition.y, wreckPosition.z);

    DropLoot(wreckItemRef, m_self->groupID(), killerID);

    for (auto cur : survivedItems)
        cur->Move(wreckItemRef->itemID(), flagNone); // populate wreck with items that survived

    DBSystemDynamicEntity wreckEntity = DBSystemDynamicEntity();
    wreckEntity.allianceID = (killer == nullptr ? 0 : killer->GetAllianceID());
    wreckEntity.categoryID = EVEDB::invCategories::Celestial;
    wreckEntity.corporationID = (killer == nullptr ? 0 : killer->GetCorporationID());
    wreckEntity.factionID = m_warID;
    wreckEntity.groupID = EVEDB::invGroups::Wreck;
    wreckEntity.itemID = wreckItemRef->itemID();
    wreckEntity.itemName = wreck_name;
    wreckEntity.ownerID = killerID;
    wreckEntity.typeID = wreckTypeID;
    wreckEntity.position = wreckPosition;

    if (!m_system->BuildDynamicEntity(wreckEntity, m_self->itemID()))
    {
        sLog.Error("StructureSE::Killed()", "Spawning Wreck Failed: typeID or typeName not supported: '%u'", wreckTypeID);
        wreckItemRef->Delete();
        return;
    }
    m_destiny->SendJettisonPacket();
}

void StructureSE::Anchor()
{
}
void StructureSE::Offline()
{
}

/*      GetHybridBridgeMenu
     *      GetAllianceBeaconSubMenu
     * lots of other bridge/beacon/fleet menus in
  * /eve/client/script/ui/services/menusvc.py
  */
/*
 * class BasicOrbital(spaceObject.PlayerOwnedStructure):
 *
 *    def Assemble(self):
 *        self.SetStaticRotation()
 *
 *    def OnSlimItemUpdated(self, slimItem):
 *        orbitalState = getattr(slimItem, 'orbitalState', None)
 *        orbitalTimestamp = getattr(slimItem, 'orbitalTimestamp', blue.os.GetSimTime())
 *        fxSequencer = sm.GetService('FxSequencer')
 *        if not hasattr(self, 'orbitalState'):
 *            if orbitalState in (entities.STATE_ANCHORING, entities.STATE_ANCHORED):
 *                uthread.pool('SpaceObject::BasicOrbital::OnSlimItemUpdated', fxSequencer.OnSpecialFX, slimItem.itemID, slimItem.itemID, None, None, None, [], 'effects.AnchorDrop', 0, 1, 0)
 *            elif orbitalState in (entities.STATE_IDLE, entities.STATE_OPERATING):
 *                uthread.pool('SpaceObject::BasicOrbital::OnSlimItemUpdated', fxSequencer.OnSpecialFX, slimItem.itemID, slimItem.itemID, None, None, None, [], 'effects.StructureOnlined', 0, 1, 0)
 *        else:
 *            if orbitalState == entities.STATE_ANCHORING and self.orbitalState == entities.STATE_UNANCHORED:
 *                uthread.pool('SpaceObject::BasicOrbital::OnSlimItemUpdated', fxSequencer.OnSpecialFX, slimItem.itemID, slimItem.itemID, None, None, None, [], 'effects.AnchorDrop', 0, 1, 0)
 *            if orbitalState == entities.STATE_ONLINING and self.orbitalState == entities.STATE_ANCHORED:
 *                uthread.pool('SpaceObject::BasicOrbital::OnSlimItemUpdated', fxSequencer.OnSpecialFX, slimItem.itemID, slimItem.itemID, None, None, None, [], 'effects.StructureOnline', 0, 1, 0)
 *            if orbitalState == entities.STATE_IDLE and self.orbitalState == entities.STATE_ONLINING:
 *                uthread.pool('SpaceObject::BasicOrbital::OnSlimItemUpdated', fxSequencer.OnSpecialFX, slimItem.itemID, slimItem.itemID, None, None, None, [], 'effects.StructureOnlined', 0, 1, 0)
 *            if orbitalState == entities.STATE_ANCHORED and self.orbitalState in (entities.STATE_OFFLINING, entities.STATE_IDLE, entities.STATE_OPERATING):
 *                uthread.pool('SpaceObject::BasicOrbital::OnSlimItemUpdated', fxSequencer.OnSpecialFX, slimItem.itemID, slimItem.itemID, None, None, None, [], 'effects.StructureOffline', 0, 1, 0)
 *            if orbitalState == entities.STATE_UNANCHORING and self.orbitalState == entities.STATE_ANCHORED:
 *                uthread.pool('SpaceObject::BasicOrbital::OnSlimItemUpdated', fxSequencer.OnSpecialFX, slimItem.itemID, slimItem.itemID, None, None, None, [], 'effects.AnchorLift', 0, 1, 0)
 *        self.orbitalState = orbitalState
 *        self.orbitalTimestamp = orbitalTimestamp
 *
 *    def IsAnchored(self):
 *        self.LogInfo('Anchor State = ', not self.isFree)
 *        return not self.isFree
 *
 *    def IsOnline(self):
 *        slimItem = sm.StartService('michelle').GetBallpark().GetInvItem(self.id)
 *        return slimItem.orbitalState is not None and slimItem.orbitalState in (entities.STATE_OPERATING, entities.STATE_IDLE)
 *
 */
