
#include "Client.h"
#include "EntityList.h"
#include "EVEServerConfig.h"
#include "EVE_Charges.h"
#include "EVE_Mail.h"
#include "Profiler.h"
#include "StaticDataMgr.h"
#include "account/AccountService.h"
#include "character/Character.h"
#include "effects/EffectsProcessor.h"
#include "inventory/Inventory.h"
#include "mail/NotificationMgr.h"
#include "npc/Drone.h"
#include "market/MarketMgr.h"
#include "ship/Ship.h"
#include "packets/DogmaIM.h"
#include "ship/modules/GenericModule.h"
#include "ship/modules/EwarModule.h"
#include "ship/modules/ModuleManager.h"
#include "station/Station.h"
#include "system/DestinyManager.h"
#include "system/BubbleManager.h"
#include "system/SolarSystem.h"
#include "system/SystemBubble.h"
#include "system/SystemManager.h"

/*
 * ShipItem
 */
ShipItem::ShipItem(uint32 shipID, const ItemType &type, const ItemData &data)
: InventoryItem(shipID, type, data),
m_pilot(nullptr),
m_targetRef(InventoryItemRef(nullptr)),
m_ModuleManager(new ModuleManager(this)),
m_loaded(false),
m_isActive(false),
m_isPopped(false),
m_isDocking(false),
m_isUndocking(false),
m_warpLockUntil(0),
m_jumpLockUntil(0)
{
    m_onlineModuleVec.clear();
    pInventory = new Inventory(InventoryItemRef(this));

    _log(ITEM__TRACE, "Created ShipItem for %s(%u).", name(), itemID());
}

ShipItem::~ShipItem()
{
    SafeDelete(pInventory);
    SafeDelete(m_ModuleManager);
}

int64 ShipItem::WarpLockoutRemaining()
{
    int64 now(GetFileTimeNow());
    return ((m_warpLockUntil > now) ? (m_warpLockUntil - now) : 0);
}

int64 ShipItem::JumpLockoutRemaining()
{
    int64 now(GetFileTimeNow());
    return ((m_jumpLockUntil > now) ? (m_jumpLockUntil - now) : 0);
}

ShipItemRef ShipItem::Load( uint32 shipID)
{
    return InventoryItem::Load<ShipItem>( shipID);
}

ShipItemRef ShipItem::Spawn( ItemData &data) {
    uint32 shipID(InventoryItem::CreateItemID(data));
    if (shipID == 0)
        return ShipItemRef(nullptr);

    return ShipItem::Load( shipID);
}

uint32 ShipItem::CreateItemID( ItemData &data) {
    return InventoryItem::CreateItemID(data);
}

bool ShipItem::_Load()
{
    if (m_loaded and (m_ModuleManager != nullptr))
        return true;

    Client* pClient = sItemFactory.GetUsingClient();
    // test for character creation (which throws errors on following *load() calls and not really needed)
    if ((pClient != nullptr) and pClient->IsCharCreation())
        return true;
    // load attributes
    if (!InventoryItem::_Load())
        return false;
    // load contents
    if (!pInventory->LoadContents())
        return false;

    SetAttribute(AttrVolume, GetPackagedVolume(), false);

    return (m_loaded = true);
}

void ShipItem::LogOut()
{
    SetAttribute(AttrOnline, EvilZero, false);
    SaveShip();

    pInventory->Unload();

    // remove ship item from factory master list here, as *something* changes ship position when saving from factory.
    sItemFactory.RemoveItem(m_itemID);

    // remove ship item from its' container's inventory list also.
    Inventory* pInv(nullptr);
    if (sDataMgr.IsStation(locationID())) {
        pInv = sItemFactory.GetStationRef(locationID())->GetMyInventory();
    } else {
        pInv = sItemFactory.GetSolarSystemRef(locationID())->GetMyInventory();
    }

    if (pInv != nullptr)
        pInv->RemoveItem(ShipItemRef(this));
}

void ShipItem::SetPlayer(Client* pClient) {
    if (m_pilot == pClient)
        return;
    if (pClient == nullptr) {
        SetAttribute(AttrOnline, EvilZero, false);
        if (m_pilot != nullptr)
            pInventory->RemoveItem(m_pilot->GetChar());
        // remove ship effects and char skill effects for char leaving ship.
        ProcessEffects(false);
        // without the pilot's skills the ship's hp and capacitor may be smaller
        ClampDamageState();
        m_onlineModuleVec.clear();
        m_pilot = nullptr;
        m_isActive = false;
        return;
    }

    m_pilot = pClient;
    if (pClient->IsCharCreation())
        return;

    Init();

    // proc effects when changing ships, or on login.
    ProcessEffects(true, sDataMgr.IsSolarSystem(locationID()));
    ClampDamageState();

    // this hits on login and when boarding ship in space.  will not hit on Undock() (location is still station at this point of execution)
    if (sDataMgr.IsSolarSystem(locationID())) {
        SetFlag(flagNone);
        /*  not sure if we're gonna keep this in here....
        if (pClient->IsLogin()) {
            if (sConfig.debug.IsTestServer) {
                // Heal Ship completely on test server
                Heal();
            } else {
                // live server will Recharge shields and cap if session change isnt active
                if (!m_pilot->IsSessionChange()) {
                    SetShipShield(1.0);
                    SetShipCapacitorLevel(1.0);
                }
            }
        } */
    }
}

void ShipItem::ClampDamageState()
{
    /* hp and capacitor depend on the pilot's skills.  after a pilot change, charge and damage can be past the new capacities:
     *  cut them down (never to the point of destroying the ship).  cargo over capacity is allowed (Crucible keeps items)
     */
    if (m_isPopped)
        return;
    bool changed(false);
    auto clamp = [&] (uint16 attrID, EvilNumber max) {
        if (max < EvilZero)
            max = EvilZero;
        if (GetAttribute(attrID) > max) {
            SetAttribute(attrID, max, false);
            changed = true;
        }
    };
    clamp(AttrShieldCharge, GetAttribute(AttrShieldCapacity));
    clamp(AttrCapacitorCharge, GetAttribute(AttrCapacitorCapacity));
    clamp(AttrArmorDamage, GetAttribute(AttrArmorHP));
    clamp(AttrDamage, GetAttribute(AttrHP) - EvilOne);
    if (!changed)
        return;

    pAttributeMap->SaveShipState();
    if (!sDataMgr.IsSolarSystem(locationID()))
        return;
    SystemManager* pSystem(sEntityList.FindSystem(locationID()));
    if (pSystem == nullptr)
        return;
    SystemEntity* pSE(pSystem->GetSE(m_itemID));
    if (pSE != nullptr)
        pSE->SendDamageStateChanged();
}

void ShipItem::Init()
{
    // pods have 57 attribs and 0 effects
    if (groupID() == EVEDB::invGroups::Capsule) {
        InitPod();
        return;
    }

    InitAttribs();

    m_isActive = true;

    m_ModuleManager->Initialize();

    // load linked weapons (if available)
    LoadWeaponGroups();

    if (sConfig.server.CargoMassAdditive)
        UpdateMass();
}

void ShipItem::InitPod() {
    SetAttribute(AttrOnline,                            EvilOne, false);
    SetAttribute(AttrVolume,                            GetPackagedVolume(), false);
    SetAttribute(AttrCpuLoad,                           EvilZero, false);
    SetAttribute(AttrPowerLoad,                         EvilZero, false);

    // Check for existence of attributes.  if not loaded then set them to default values:
    if (!HasAttribute(AttrDamage))                      SetAttribute(AttrDamage, EvilZero, false);
    if (!HasAttribute(AttrArmorDamage))                 SetAttribute(AttrArmorDamage, EvilZero, false);
    // shield and cap are part of persistance, and loaded on attrib map initalization.  check for and set to full if no saved value found
    if (!HasAttribute(AttrShieldCharge))                SetAttribute(AttrShieldCharge,  GetAttribute(AttrShieldCapacity), false);

    m_ModuleManager->Initialize();

    // pod will always be full when activated
    if (m_pilot != nullptr)
        if (m_pilot->IsInSpace())
            Heal();
}

void ShipItem::InitAttribs()
{
    // Create default dynamic attributes in the AttributeMap
    SetAttribute(AttrOnline,                            EvilOne, false);
    SetAttribute(AttrVolume,                            GetPackagedVolume(), false);
    SetAttribute(AttrMass,                              type().mass());
    SetAttribute(AttrCpuLoad,                           EvilZero, false);
    SetAttribute(AttrPowerLoad,                         EvilZero, false);
    // rig shit
    SetAttribute(AttrUpgradeLoad,                       EvilZero, false);

    // Check for existence of attributes.  if not loaded then set them to default values:
    if (!HasAttribute(AttrDamage))                      SetAttribute(AttrDamage, EvilZero, false);
    if (!HasAttribute(AttrArmorDamage))                 SetAttribute(AttrArmorDamage, EvilZero, false);
    // shield and cap are part of persistance, and loaded on attrib map initalization.  check for and set to full if no saved value found
    if (!HasAttribute(AttrShieldCharge))                SetAttribute(AttrShieldCharge,  GetAttribute(AttrShieldCapacity), false);
    if (!HasAttribute(AttrCapacitorCharge))             SetAttribute(AttrCapacitorCharge,  GetAttribute(AttrCapacitorCapacity), false);
    if (!HasAttribute(AttrMaximumRangeCap))             SetAttribute(AttrMaximumRangeCap, ((float)BUBBLE_RADIUS_METERS), false);
    // Warp Scramble Status of the ship (most ships have zero warp scramble status, but some (t2 indy) already have it defined):
    if (!HasAttribute(AttrWarpScrambleStatus))          SetAttribute(AttrWarpScrambleStatus, EvilZero, false);
    if (!HasAttribute(AttrWarpSpeedMultiplier))         SetAttribute(AttrWarpSpeedMultiplier, EvilOne, false);
    if (!HasAttribute(AttrArmorMaxDamageResonance))     SetAttribute(AttrArmorMaxDamageResonance, EvilOne, false);
    if (!HasAttribute(AttrShieldMaxDamageResonance))    SetAttribute(AttrShieldMaxDamageResonance, EvilOne, false);
    // hull res is stored in item type as AttrHull*Resonance for 6 ships.  set accordingly
    if (!HasAttribute(AttrEmDamageResonance))           SetAttribute(AttrEmDamageResonance,  GetAttribute(AttrHullEmDamageResonance), false);
    if (!HasAttribute(AttrKineticDamageResonance))      SetAttribute(AttrKineticDamageResonance,  GetAttribute(AttrHullKineticDamageResonance), false);
    if (!HasAttribute(AttrThermalDamageResonance))      SetAttribute(AttrThermalDamageResonance,  GetAttribute(AttrHullThermalDamageResonance), false);
    if (!HasAttribute(AttrExplosiveDamageResonance))    SetAttribute(AttrExplosiveDamageResonance,  GetAttribute(AttrHullExplosiveDamageResonance), false);

    // ship bonuses....are these set in ship fx?
    /*
    AttrMaxRangeBonus = 351,
    AttrDroneRangeBonus = 459,
    AttrExplosionRangeBonus = 558,
    AttrMaxRangeBonus2 = 769,

    */

    // cap ships have bonuses to various systems....these may be set in ship fx
    /*
    AttrSurveyScannerRangeBonus = 1234,
    AttrCargoScannerRangeBonus = 1235,
    AttrCommandBonusEffective = 1236,
    AttrCommandBonusAdd = 1237,
    AttrCommandBonusEffectiveAdd = 1238,
    AttrShipBonusORECapital1 = 1239,
    AttrShipBonusORECapital2 = 1240,
    AttrShipBonusORECapital3 = 1243,
    AttrShipBonusORECapital4 = 1244,

    AttrEliteBonusHeavyInterdictors1 = 1246,
    AttrEliteBonusHeavyInterdictors2 = 1247,
    AttrEliteBonusElectronicAttackShip1 = 1249,
    AttrEliteBonusElectronicAttackShip2 = 1250,
    AttrEliteBonusViolators1 = 1265,
    AttrEliteBonusViolators2 = 1266,
    AttrEliteBonusJumpFreighter1 = 1311,
    AttrEliteBonusJumpFreighter2 = 1312,
    AttrMaxTargetRangeBonusBonus = 1313,
    AttrScanResolutionBonusBonus = 1314,
    AttrMaxRangeBonusBonus = 1315,
    AttrTrackingSpeedBonusBonus = 1316,
    AttrEliteBonusViolatorsRole1 = 1268,
    AttrEliteBonusViolatorsRole2 = 1269,
    AttrWarpScrambleRangeBonus = 1327,
    AttrDroneBandwidthMultiplier = 1328,
    AttrDroneBandwidthBonusAdd = 1329,
    AttrShipOrcaTractorBeamRangeBonus1 = 1355,
    AttrShipOrcaCargoBonusOrca1 = 1356,
    AttrShipOrcaTractorBeamVelocityBonus2 = 1357,
    AttrShipOrcaForemanBonus = 1358,
    AttrShipOrcaSurveyScannerBonus = 1359,
    AttrShipBonusHPExtender1 = 1360,
    AttrEliteIndustrialCovertCloakBonus = 1361,

    AttrShipBonusOreIndustrial1 = 1669,
    AttrShipBonusOreIndustrial2 = 1670,

    AttrtitanAmarrBonus5 = 1592,
    AttrtitanGallenteBonus5 = 1593,
    AttrtitanMinmatarBonus5 = 1594,
    AttrtitanCaldariBonus5 = 1596,

    */

    // t3 ships
    /*
    AttrSubsystemBonusAmarrEngineering = 1431,
    AttrSubsystemBonusAmarrElectronic = 1432,
    AttrSubsystemBonusAmarrDefensive = 1433,
    AttrSubsystemBonusAmarrOffensive = 1434,
    AttrSubsystemBonusAmarrPropulsion = 1435,
    AttrSubsystemBonusGallenteEngineering = 1436,
    AttrSubsystemBonusGallenteElectronic = 1437,
    AttrSubsystemBonusGallenteDefensive = 1438,
    AttrSubsystemBonusGallenteOffensive = 1439,
    AttrSubsystemBonusGallentePropulsion = 1440,
    AttrSubsystemBonusCaldariEngineering = 1441,
    AttrSubsystemBonusCaldariElectronic = 1442,
    AttrSubsystemBonusCaldariDefensive = 1443,
    AttrSubsystemBonusCaldariOffensive = 1444,
    AttrSubsystemBonusCaldariPropulsion = 1445,
    AttrSubsystemBonusMinmatarEngineering = 1446,
    AttrSubsystemBonusMinmatarElectronic = 1447,
    AttrSubsystemBonusMinmatarDefensive = 1448,
    AttrSubsystemBonusMinmatarOffensive = 1449,
    AttrSubsystemBonusMinmatarPropulsion = 1450,

    AttrShipBonusStrategicCruiserAmarr = 1503,
    AttrShipBonusStrategicCruiserCaldari = 1504,
    AttrShipBonusStrategicCruiserGallente = 1505,
    AttrShipBonusStrategicCruiserMinmatar = 1506,

    AttrSubsystemBonusAmarrDefensive2 = 1507,
    AttrSubsystemBonusAmarrElectronic2 = 1508,
    AttrSubsystemBonusAmarrEngineering2 = 1509,
    AttrSubsystemBonusCaldariOffensive2 = 1510,
    AttrSubsystemBonusAmarrOffensive2 = 1511,
    AttrSubsystemBonusAmarrPropulsion2 = 1512,
    AttrSubsystemBonusCaldariPropulsion2 = 1513,
    AttrSubsystemBonusCaldariElectronic2 = 1514,
    AttrSubsystemBonusCaldariEngineering2 = 1515,
    AttrSubsystemBonusCaldariDefensive2 = 1516,
    AttrSubsystemBonusGallenteDefensive2 = 1517,
    AttrSubsystemBonusGallenteElectronic2 = 1518,
    AttrSubsystemBonusGallenteEngineering2 = 1519,
    AttrSubsystemBonusGallentePropulsion2 = 1520,
    AttrSubsystemBonusGallenteOffensive2 = 1521,
    AttrSubsystemBonusMinmatarOffensive2 = 1522,
    AttrSubsystemBonusMinmatarPropulsion2 = 1523,
    AttrSubsystemBonusMinmatarElectronic2 = 1524,
    AttrSubsystemBonusMinmatarEngineering2 = 1525,
    AttrSubsystemBonusMinmatarDefensive2 = 1526,

    AttrSubsystemBonusAmarrOffensive3 = 1531,
    AttrSubsystemBonusGallenteOffensive3 = 1532,
    AttrSubsystemBonusCaldariOffensive3 = 1533,
    AttrSubsystemBonusMinmatarOffensive3 = 1534,
    */
}

void ShipItem::Delete() {
    pInventory->DeleteContents();
    InventoryItem::Delete();
}

/** @todo this will need more work to correctly check hold capacity for offline/unloaded ships */
float ShipItem::GetRemainingVolumeByFlag(EVEItemFlags flag) const {
    // updated to use inventory  -allan 26Jul16  -fixed 22Nov18  -updated to new call 08Aug20
    return pInventory->GetRemainingCapacity(flag);
}

void ShipItem::ProcessModules() {
    if (m_pilot->IsDocked())
        return;

    m_ModuleManager->Process();
}

void ShipItem::Eject()
{
    if (m_ModuleManager != nullptr) {
        // overload effects need the pilot, so they come off before the pilot leaves
        m_ModuleManager->ClearOverloads();
        StopHeating();
        m_ModuleManager->AbortCycle();
    }
    // the ship keeps nothing from the pilot's fleet links.  after the modules stop: our own links refresh the fleet as they stop
    if (HasPilot() and (GetPilot()->GetShipSE() != nullptr) and (GetPilot()->GetShipSE()->GetID() == itemID()))
        GetPilot()->GetShipSE()->RemoveLinkBonuses();
    if (m_ModuleManager != nullptr)
        m_ModuleManager->CharacterLeavingShip();
    SaveShip();
    // linked weapons will be persistant here.
    //UnlinkAllWeapons();
}

void ShipItem::Dock() {
    m_isDocking = true;

    m_ModuleManager->DeactivateAllModules();
    // modules stop cycling on docking, and lose their overload
    m_ModuleManager->ClearOverloads();
    StopHeating();
    m_onlineModuleVec.clear();
    // the reset below reloads charge attributes from the db (crystal damage)
    m_ModuleManager->SaveModules();
    // remove ship effects and char skill effects for docking.
    ProcessEffects();
}

void ShipItem::SetDocked()
{
    /* Dock() took every effect off while the ship was still in space.  in the station the ship keeps the pilot's skills, its
     *  hull bonuses and its fitting (modules set online, rigs, subsystems), as it does after a login or boarding here:  the
     *  server's cargo capacity (and the rest) must match what the client's fitting shows.  nothing is sent, the client
     *  works its station numbers out itself
     */
    if (m_isDocking and (m_pilot != nullptr) and m_pilot->IsDocked())
        ProcessEffects(true, false);
    m_isDocking = false;
}

void ShipItem::Undock() {
    m_isUndocking = true;

    //HorribleFittingProblems

    ResetEffects();
    //ProcessEffects(true, true);

    // horrible hack to set charge qty in fit window
    // on the off-chance player opens the fit window
    m_ModuleManager->UpdateChargeQty();

    // Recharge shields and cap if session change isnt active (undocking too fast)
    if (!m_pilot->IsSessionChange()) {
        SetShipShield(1.0);
        SetShipCapacitorLevel(1.0);
    }
}

void ShipItem::UpdateMass()
{
    std::map< uint32, InventoryItemRef > invMap;
    pInventory->GetInventoryMap(invMap);
    uint32 mass(GetAttribute(AttrMass).get_uint32());
    for (auto cur : invMap)
        mass += cur.second->type().mass() * cur.second->quantity();

    // may have to adjust this for player login
    SetAttribute(AttrMass, mass, HasPilot());
}

void ShipItem::Warp() {
    m_ModuleManager->ShipWarping();
}

void ShipItem::Jump() {
    m_ModuleManager->ShipJumping();
}
/* {'messageKey': 'JumpAlreadyHaveStationClone', 'dataID': 17882749, 'suppressable': False, 'bodyID': 259250, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 1009}
 * {'messageKey': 'JumpAttemptTooSoon', 'dataID': 17883129, 'suppressable': False, 'bodyID': 259393, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 1010}
 * {'messageKey': 'JumpBeaconInWormholeSpace', 'dataID': 17877290, 'suppressable': False, 'bodyID': 257194, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 2829}
 * {'messageKey': 'JumpCantAcceptOwnInstall', 'dataID': 17883135, 'suppressable': False, 'bodyID': 259395, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 1011}
 * {'messageKey': 'JumpCantDestroyCloneInOthersShip', 'dataID': 17882752, 'suppressable': False, 'bodyID': 259251, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 1012}
 * {'messageKey': 'JumpCantDestroyCloneNoRight', 'dataID': 17882755, 'suppressable': False, 'bodyID': 259252, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 1013}
 * {'messageKey': 'JumpCantUseCFG', 'dataID': 17882760, 'suppressable': False, 'bodyID': 259254, 'messageType': 'info', 'urlAudio': '', 'urlIcon': '', 'titleID': 259253, 'messageID': 1015}
 * {'messageKey': 'JumpCharStoringMaxClones', 'dataID': 17883140, 'suppressable': False, 'bodyID': 259397, 'messageType': 'info', 'urlAudio': '', 'urlIcon': '', 'titleID': 259396, 'messageID': 1016}
 * {'messageKey': 'JumpCharStoringMaxClonesNone', 'dataID': 17879907, 'suppressable': False, 'bodyID': 258189, 'messageType': 'info', 'urlAudio': '', 'urlIcon': '', 'titleID': 258188, 'messageID': 2182}
 * {'messageKey': 'JumpCheckIntoShip', 'dataID': 17879917, 'suppressable': False, 'bodyID': 258193, 'messageType': 'question', 'urlAudio': '', 'urlIcon': '', 'titleID': 258192, 'messageID': 2187}
 * {'messageKey': 'JumpCheckWillLoseExistingClone', 'dataID': 17882765, 'suppressable': False, 'bodyID': 259256, 'messageType': 'warning', 'urlAudio': '', 'urlIcon': '', 'titleID': 259255, 'messageID': 1017}
 * {'messageKey': 'JumpCheckWillLoseExistingCloneAndImplants', 'dataID': 17882770, 'suppressable': False, 'bodyID': 259258, 'messageType': 'warning', 'urlAudio': '', 'urlIcon': '', 'titleID': 259257, 'messageID': 1018}
 * {'messageKey': 'JumpCloneInstallationOffered', 'dataID': 17883248, 'suppressable': False, 'bodyID': 259436, 'messageType': 'question', 'urlAudio': '', 'urlIcon': '', 'titleID': 259435, 'messageID': 1020}
 * {'messageKey': 'JumpCloneNotFound', 'dataID': 17882773, 'suppressable': False, 'bodyID': 259259, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 1021}
 * {'messageKey': 'JumpCriminalConfirm', 'dataID': 17877821, 'suppressable': False, 'bodyID': 257396, 'messageType': 'warning', 'urlAudio': '', 'urlIcon': '', 'titleID': 257395, 'messageID': 2544}
 * {'messageKey': 'JumpDestinationBlocked', 'dataID': 17875972, 'suppressable': False, 'bodyID': 256704, 'messageType': 'info', 'urlAudio': '', 'urlIcon': '', 'titleID': 256703, 'messageID': 3497}
 * {'messageKey': 'JumpDestinationInvalid', 'dataID': 17882778, 'suppressable': False, 'bodyID': 259261, 'messageType': 'info', 'urlAudio': '', 'urlIcon': '', 'titleID': 259260, 'messageID': 1022}
 * {'messageKey': 'JumpDestinationIsWormholeSpace', 'dataID': 17877287, 'suppressable': False, 'bodyID': 257193, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 2828}
 * {'messageKey': 'JumpDestinationNotWilling', 'dataID': 17883025, 'suppressable': False, 'bodyID': 259352, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 1023}
 * {'messageKey': 'JumpDriveActive', 'dataID': 2039122, 'suppressable': False, 'bodyID': None, 'messageType': 'audio', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 1024}
 * {'messageKey': 'JumpHarmonicsDoNotMatch', 'dataID': 17878894, 'suppressable': False, 'bodyID': 257799, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 2383}
 * {'messageKey': 'JumpInstalleeNotPresent', 'dataID': 17882781, 'suppressable': False, 'bodyID': 259262, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 1025}
 * {'messageKey': 'JumpInstalleeTooDistant', 'dataID': 17883170, 'suppressable': False, 'bodyID': 259408, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 1026}
 * {'messageKey': 'JumpInstallerNotPresent', 'dataID': 17882784, 'suppressable': False, 'bodyID': 259263, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 1027}
 * {'messageKey': 'JumpMustBeInPod', 'dataID': 17878190, 'suppressable': False, 'bodyID': 257533, 'messageType': 'info', 'urlAudio': '', 'urlIcon': '', 'titleID': 257532, 'messageID': 2744}
 * {'messageKey': 'JumpNoBridge', 'dataID': 17882789, 'suppressable': False, 'bodyID': 259265, 'messageType': 'info', 'urlAudio': '', 'urlIcon': '', 'titleID': 259264, 'messageID': 1028}
 * {'messageKey': 'JumpNoCloneAtDestination', 'dataID': 17882792, 'suppressable': False, 'bodyID': 259266, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 1029}
 * {'messageKey': 'JumpNoDrive', 'dataID': 17882797, 'suppressable': False, 'bodyID': 259268, 'messageType': 'info', 'urlAudio': '', 'urlIcon': '', 'titleID': 259267, 'messageID': 1030}
 * {'messageKey': 'JumpNoPortal', 'dataID': 17882802, 'suppressable': False, 'bodyID': 259270, 'messageType': 'info', 'urlAudio': '', 'urlIcon': '', 'titleID': 259269, 'messageID': 1031}
 * {'messageKey': 'JumpNoShipCloneForAbsentChar', 'dataID': 17883173, 'suppressable': False, 'bodyID': 259409, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 1032}
 * {'messageKey': 'JumpNoShipCloneForBusyChar', 'dataID': 17883176, 'suppressable': False, 'bodyID': 259410, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 1033}
 * {'messageKey': 'JumpNoShipCloneWhenDoingShipOne', 'dataID': 17883179, 'suppressable': False, 'bodyID': 259411, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 1034}
 * {'messageKey': 'JumpNoStationCloneWhenDoingShipOne', 'dataID': 17882805, 'suppressable': False, 'bodyID': 259271, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 1035}
 * {'messageKey': 'JumpNotEnoughCap', 'dataID': 17883189, 'suppressable': False, 'bodyID': 259415, 'messageType': 'info', 'urlAudio': '', 'urlIcon': '', 'titleID': 259414, 'messageID': 1036}
 * {'messageKey': 'JumpNotEnoughCharge', 'dataID': 17883194, 'suppressable': False, 'bodyID': 259417, 'messageType': 'info', 'urlAudio': '', 'urlIcon': '', 'titleID': 259416, 'messageID': 1037}
 * {'messageKey': 'JumpNotEnoughCharge2', 'dataID': 17883199, 'suppressable': False, 'bodyID': 259419, 'messageType': 'info', 'urlAudio': '', 'urlIcon': '', 'titleID': 259418, 'messageID': 1038}
 * {'messageKey': 'JumpNotEnoughCharge3', 'dataID': 17879346, 'suppressable': False, 'bodyID': 257973, 'messageType': 'info', 'urlAudio': '', 'urlIcon': '', 'titleID': 257972, 'messageID': 2342}
 * {'messageKey': 'JumpOtherAlreadyHasShipClone', 'dataID': 17883205, 'suppressable': False, 'bodyID': 259421, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 1039}
 * {'messageKey': 'JumpShipCloneInstallAcceptTooLate', 'dataID': 17882808, 'suppressable': False, 'bodyID': 259272, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 1040}
 * {'messageKey': 'JumpShipCloneInstallAcceptWTF', 'dataID': 17882811, 'suppressable': False, 'bodyID': 259273, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 1041}
 * {'messageKey': 'JumpShipCloneInstallCancelTooLate', 'dataID': 17882814, 'suppressable': False, 'bodyID': 259274, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 1042}
 * {'messageKey': 'JumpShipCloneInstallCancelWTF', 'dataID': 17882817, 'suppressable': False, 'bodyID': 259275, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 1043}
 * {'messageKey': 'JumpShipLacksActiveCloneVatBay', 'dataID': 17877847, 'suppressable': False, 'bodyID': 257406, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 2557}
 * {'messageKey': 'JumpShipLacksCloneSupport', 'dataID': 17882820, 'suppressable': False, 'bodyID': 259276, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 1044}
 * {'messageKey': 'JumpShipNotOwnedByCharacter', 'dataID': 17882823, 'suppressable': False, 'bodyID': 259277, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 1045}
 * {'messageKey': 'JumpShipStoringMaxClones', 'dataID': 17883211, 'suppressable': False, 'bodyID': 259423, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 1046}
 * {'messageKey': 'JumpSkillInTraining', 'dataID': 17880967, 'suppressable': False, 'bodyID': 258596, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 1817}
 * {'messageKey': 'JumpSuperweaponActive', 'dataID': 17879575, 'suppressable': False, 'bodyID': 258058, 'messageType': 'info', 'urlAudio': '', 'urlIcon': '', 'titleID': 258057, 'messageID': 2306}
 * {'messageKey': 'JumpTooFar', 'dataID': 17883219, 'suppressable': False, 'bodyID': 259426, 'messageType': 'info', 'urlAudio': '', 'urlIcon': '', 'titleID': 259425, 'messageID': 1047}
 */
void ShipItem::Heal()
{
    // Repair Ship, Modules and Recharge Capacitor:
    SetShipShield(1.0);
    SetShipCapacitorLevel(1.0);
    SetShipArmor(1.0);
    SetShipHull(1.0);
    m_ModuleManager->RepairModules();
}

void ShipItem::AddItem(InventoryItemRef iRef)
{
    if (iRef.get() == nullptr)
        return;

    InventoryItem::AddItem(iRef);

    // add item mass to ship's mass if set in options (additive...loaded ship should be heavy)
    if (sConfig.server.CargoMassAdditive) {
        uint32 mass = GetAttribute(AttrMass).get_uint32();
        uint32 addition = iRef->type().mass() * iRef->quantity();
        SetAttribute(AttrMass, mass + addition, HasPilot());
        UpdateDestinyMass();
    }
}

// cannot throw
uint32 ShipItem::AddItemByFlag(EVEItemFlags flag, InventoryItemRef iRef, Client* pClient/*nullptr*/)
{
    if (iRef.get() == nullptr)
        return 0;

    if (flag == flagNone) {
        // make error.  nothing at this point should be "autoFit"
        codelog(SHIP__ERROR, "ShipItem::AddItem() - flag = flagNone.");
        if (sConfig.debug.IsTestServer)
            EvE::traceStack();
        flag = flagCargoHold;   //default to cargo (cause this is a ship)
    }
    // CantFitModuleToThatShip
    // u'CantFitModuleToThatShipBody'}(u"You can't fit {item} to {ship}", None, {u'{ship}': {'conditionalValues': [], 'variableType': 10, 'propertyName': None, 'args': 0, 'kwargs': {}, 'variableName': 'ship'}, u'{item}': {'conditionalValues': [], 'variableType': 10, 'propertyName': None, 'args': 0, 'kwargs': {}, 'variableName': 'item'}})

    if (IsModuleSlot(flag)) {
        if (iRef->categoryID() == EVEDB::invCategories::Charge) {
            // damaged crystals stay assembled
            if (iRef->GetAttribute(AttrDamage) <= EvilZero)
                iRef->ChangeSingleton(false, false);
            m_ModuleManager->LoadCharge(iRef, flag);
            // the charge in the slot (also while it is still loading in space)
            iRef = m_ModuleManager->GetChargeInSlot(flag);
            if (iRef.get() == nullptr)
                return 0;
            return iRef->itemID();
        } else if (iRef->categoryID() == EVEDB::invCategories::Module) {
            ModuleItemRef mRef = ModuleItemRef::StaticCast(iRef);
            mRef->ChangeSingleton(true, false);
            // rigs are classed in the module category.  check here and call appropriate method as needed.
            if (IsRigSlot(flag)) {
                if (!m_ModuleManager->InstallRig(mRef, flag))
                    return 0;
            } else if (!m_ModuleManager->AddModule(mRef, flag))
                return 0;
        } else if (iRef->categoryID() == EVEDB::invCategories::Subsystem) {
            ModuleItemRef mRef = ModuleItemRef::StaticCast(iRef);
            //mRef->SetOnline(true);  // is this needed here?
            mRef->ChangeSingleton(true, false);
            if (!m_ModuleManager->InstallSubSystem(mRef, flag))
                return 0;
        }
        //m_ModuleManager->UpdateModules(flag);
    }

    iRef->Move(m_itemID, flag, true);
    return iRef->itemID();
}

void ShipItem::RemoveItem(InventoryItemRef iRef)
{
    if (iRef.get() == nullptr)
        return;

    InventoryItem::RemoveItem(iRef);

    if (m_pilot == nullptr)
        return;

    // check to see if item is currently in a module slot.
    if (IsModuleSlot(iRef->flag())) {
        if (IsRigSlot(iRef->flag())) {
            m_ModuleManager->UninstallRig(iRef->itemID());
        } else if (iRef->categoryID() == EVEDB::invCategories::Charge) {
            m_ModuleManager->UnloadModule(iRef->flag());
        } else {
            m_ModuleManager->UnfitModule(iRef->itemID());
        }
        //m_ModuleManager->UpdateModules(iRef->flag());
    }

    // remove item mass to ship's mass if set in options (additive...loaded ship should be heavy)
    if (sConfig.server.CargoMassAdditive) {
        uint32 mass = GetAttribute(AttrMass).get_uint32();
        uint32 addition = iRef->type().mass() * iRef->quantity();
        SetAttribute(AttrMass, mass - addition, HasPilot());
        UpdateDestinyMass();
    }
}

void ShipItem::UpdateDestinyMass()
{
    // the ball's mass, agility and speed follow the ship's (SetBallMass/SetBallAgility/SetBallSpeed)
    if ((m_pilot == nullptr) or !m_pilot->IsInSpace() or (m_pilot->GetShipSE() == nullptr))
        return;
    if ((m_pilot->GetShipSE()->GetID() != itemID()) or (m_pilot->GetShipSE()->DestinyMgr() == nullptr))
        return;
    m_pilot->GetShipSE()->DestinyMgr()->UpdateShipVariables();
}

void ShipItem::AddModuleToOnlineVec(uint32 modID)
{
    m_onlineModuleVec.push_back(modID);
    _log(MODULE__INFO, "Added ModuleID %u to Online List", modID);
}

void ShipItem::GetModuleItemVec( std::vector< InventoryItemRef >& iRefVec ) {
    std::map<uint32, InventoryItemRef> invMap;
    pInventory->GetInventoryMap( invMap );
    for (auto cur : invMap )
        if (IsModuleSlot(cur.second->flag()))
            iRefVec.push_back(cur.second);
}

/* Begin new Module Manager Interface */
void ShipItem::GetModuleRefVec(std::vector< InventoryItemRef >& iRefVec)
{
    m_ModuleManager->GetModuleListOfRefsAsc(iRefVec);
}

InventoryItemRef ShipItem::GetModuleRef(EVEItemFlags flag)
{
    GenericModule* pMod(m_ModuleManager->GetModule(flag));
    if (pMod != nullptr)
        return pMod->GetSelf();

    return InventoryItemRef(nullptr);
}

InventoryItemRef ShipItem::GetModuleRef(uint32 modID)
{
    GenericModule* pMod(m_ModuleManager->GetModule(modID));
    if (pMod != nullptr)
        return pMod->GetSelf();

    return InventoryItemRef(nullptr);
}

// only called from dogma call.  can throw
void ShipItem::LoadCharge(InventoryItemRef cRef, EVEItemFlags flag)
{
    if (cRef.get() == nullptr)
        throw UserError ("CantFindChargeToAdd");

    if (!IsFittingSlot(flag))
        throw CustomError ("Destination is not weapon.");

    std::vector<int32> chargeIDs;
    chargeIDs.push_back(cRef->itemID());
    LoadAmmo(m_ModuleManager->GetModule(flag), chargeIDs);
}
/*
 * ChargeRequiresLauncher
 * DragDropCharge ??

 * IncompatibleChargeGroup1  {'FullPath': u'UI/Messages', 'messageID': 259621, 'label': u'IncompatibleChargeGroup1Body'}(u'You cannot load {ammoGroup} into a {[item]moduleName.name}, this module needs to be loaded with {group1}.', None, {u'{ammoGroup}': {'conditionalValues': [], 'variableType': 10, 'propertyName': None, 'args': 0, 'kwargs': {}, 'variableName': 'ammoGroup'}, u'{[item]moduleName.name}': {'conditionalValues': [], 'variableType': 2, 'propertyName': 'name', 'args': 0, 'kwargs': {}, 'variableName': 'moduleName'}, u'{group1}': {'conditionalValues': [], 'variableType': 10, 'propertyName': None, 'args': 0, 'kwargs': {}, 'variableName': 'group1'}})
NotEnoughChargeSpace  {'FullPath': u'UI/Messages', 'messageID': 259162, 'label': u'NotEnoughChargeSpaceBody'}(u'There is not enough space to add those charges. There is only {[numeric]capacity, decimalPlaces=2} {[numeric]capacity -> "unit", "units"} of space, but the charges require {[numeric]volume, decimalPlaces=2} {[numeric]volume -> "unit", "units"}.', None, {u'{[numeric]capacity, decimalPlaces=2}': {'conditionalValues': [], 'variableType': 9, 'propertyName': None, 'args': 512, 'kwargs': {'decimalPlaces': 2}, 'variableName': 'capacity'}, u'{[numeric]capacity -> "unit", "units"}': {'conditionalValues': [u'unit', u'units'], 'variableType': 9, 'propertyName': None, 'args': 320, 'kwargs': {}, 'variableName': 'capacity'}, u'{[numeric]volume, decimalPlaces=2}': {'conditionalValues': [], 'variableType': 9, 'propertyName': None, 'args': 512, 'kwargs': {'decimalPlaces': 2}, 'variableName': 'volume'}, u'{[numeric]volume -> "unit", "units"}': {'conditionalValues': [u'unit', u'units'], 'variableType': 9, 'propertyName': None, 'args': 320, 'kwargs': {}, 'variableName': 'volume'}})
{'FullPath': u'UI/Messages', 'messageID': 257216, 'label': u'NoSuitableModulesBody'}(u'These charges cannot be fitted to any of the currently fitted modules', None, None)

 */

// can throw.  the client's errors
void ShipItem::CheckChargeLoadable(GenericModule* pMod, InventoryItemRef cRef)
{
    if (pMod == nullptr)
        throw UserError ("ModuleNoLongerPresentForCharges");
    // a bank loads (and fires) as one.  its master's state is the bank's
    std::vector<GenericModule*> modules;
    GetBankMembers(pMod, modules);
    for (auto cur : modules) {
        // "You cannot load or unload {typeID} while it is active."
        if (cur->IsActive())
            throw UserError ("CannotLoadActiveLauncher")
                    .AddFormatValue ("typeID", new PyInt (cur->typeID()));
        // "...because the module is already involved in another loading operation."
        if (cur->IsLoading())
            throw UserError ("LoadingChargeSlotAlready")
                    .AddFormatValue ("chargeType", new PyInt (cRef.get() == nullptr ? cur->typeID() : cRef->typeID()));
    }
}

/* only called from dogma calls.  can throw.
 *  the client's LoadAmmoToBank (reload, ammo change, auto reload) and LoadAmmoToModules send the stacks to load from, all of
 *  one type:  the cargo hold, or the hangar when docked.  a grouped module loads its whole bank, the charges split evenly
 *  over it (Charges::SplitAcrossBank).  a module loaded with another type has it swapped out.  qty > 0 loads at most that many
 */
void ShipItem::LoadAmmo(GenericModule* pMod, std::vector<int32>& chargeIDs, int32 qty/*0*/)
{
    if (pMod == nullptr)
        throw UserError ("ModuleNoLongerPresentForCharges");
    if (m_pilot == nullptr)
        return;

    bool docked(m_pilot->IsDocked());
    std::vector<InventoryItemRef> stacks;
    for (auto id : chargeIDs) {
        InventoryItemRef cRef(sItemFactory.GetItemRef(id));
        if ((cRef.get() == nullptr) or (cRef->quantity() < 1))
            continue;
        if (IsFittingSlot(cRef->flag())) {
            _log(MODULE__TRACE, "ShipItem::LoadAmmo - Trying to load %s from %s to %s.", \
                        cRef->name(), sDataMgr.GetFlagName(cRef->flag()), sDataMgr.GetFlagName(pMod->flag()));
            throw UserError ("CantMoveChargesBetweenModules");
        }
        if (cRef->categoryID() != EVEDB::invCategories::Charge)
            continue;
        bool inCargo((cRef->locationID() == m_itemID) and IsCargoHoldFlag(cRef->flag()));
        bool inHangar(docked and (cRef->locationID() == locationID()) and (cRef->flag() == flagHangar)
                and (cRef->ownerID() == m_pilot->GetCharacterID()));
        if (!inCargo and !inHangar)
            continue;
        if (!stacks.empty() and (cRef->typeID() != stacks.front()->typeID()))
            continue;
        stacks.push_back(cRef);
    }
    if (stacks.empty())
        throw UserError ("CantFindChargeToAdd");

    InventoryItemRef first(stacks.front());
    std::vector<GenericModule*> modules;
    GetBankMembers(pMod, modules);
    // every module of the bank must take this charge (size, group, skills), and be idle
    for (auto cur : modules)
        VerifyHoldType(cur->flag(), first, m_pilot);
        /*  this doesnt work right....comment for now.
        std::map<std::string, PyRep *> args;
        args["charge"] = new PyInt(iRef->itemID());
        throw UserError ("ChargeLoadingFailedWithRefund");
        */
        /* ChargeLoadingFailedWithRefundBody'}(u'Your {[item]charge.name} failed to load and was returned to your cargo.',
         * None, {u'{[item]charge.name}': {'conditionalValues': [], 'variableType': 2, 'propertyName': 'name', 'args': 0, 'kwargs': {}, 'variableName': 'charge'}})
         */
    CheckChargeLoadable(pMod, first);

    // what each module has room for.  one holding another type gets all of its room, its charges go out
    double volume(first->GetAttribute(AttrVolume).get_double());
    std::vector<int32> room, share;
    std::vector<double> swapVolume;
    for (auto cur : modules) {
        InventoryItemRef loaded(m_ModuleManager->GetChargeInSlot(cur->flag()));
        int32 have(0);
        double swap(0);
        if (loaded.get() != nullptr) {
            if (loaded->typeID() == first->typeID()) {
                have = loaded->quantity();
            } else {
                swap = loaded->quantity() * loaded->GetAttribute(AttrVolume).get_double();
            }
        }
        room.push_back(Charges::RoomFor(cur->GetAttribute(AttrCapacity).get_double(), volume, have));
        swapVolume.push_back(swap);
    }
    int64 available(0);
    for (auto cur : stacks)
        available += cur->quantity();
    if ((qty > 0) and (available > qty))
        available = qty;
    if (available > 0x7fffffff)
        available = 0x7fffffff;
    Charges::SplitAcrossBank((int32)available, room, share);

    /* swapped out charges go to the cargo hold in space ("There is not enough space in the cargo hold for the charges
     *  currently in the {item} to be moved into.").  the charges leaving the cargo make room for them
     */
    if (!docked) {
        double out(0), in(0);
        for (size_t i = 0; i < modules.size(); ++i)
            if (share[i] > 0) {
                out += swapVolume[i];
                in += share[i] * volume;
            }
        if ((out > 0) and ((GetRemainingVolumeByFlag(flagCargoHold) + (first->locationID() == m_itemID ? in : 0)) < out))
            throw UserError ("NoSpaceForReplacedItem")
                    .AddTypeName ("item", pMod->typeID());
    }

    bool full(true);
    size_t s(0);
    for (size_t i = 0; i < modules.size(); ++i) {
        int32 want(share[i]);
        if (want < room[i])
            full = false;
        while ((want > 0) and (s < stacks.size())) {
            InventoryItemRef cRef(stacks[s]);
            // used up (merged in, deleted), or gone into a module whole
            if ((cRef->quantity() < 1) or IsFittingSlot(cRef->flag())) {
                ++s;
                continue;
            }
            int32 got(m_ModuleManager->LoadCharge(cRef, modules[i]->flag(), want));
            if (got < 1)
                break;
            want -= got;
        }
    }

    //{'FullPath': u'UI/Messages', 'messageID': 256676, 'label': u'CannotLoadNotEnoughChargesBody'}(u'There are not enough charges to fully load all of your modules. Some of your modules have been left partially loaded or empty.', None, None)
    if (!full)
        m_pilot->SendRemoteMessage("CannotLoadNotEnoughCharges");
}

//{'FullPath': u'UI/Messages', 'messageID': 257270, 'label': u'NotEnoughCargoSpaceToUnloadBankBody'}(u'There is not enough cargo space left to unload the charges in the weapon bank. Try freeing up some space and try again.', None, None)

// only called from client call.  can throw
void ShipItem::RemoveCharge(EVEItemFlags fromFlag)
{
    if (IsFittingSlot(fromFlag)) {
        GenericModule* pMod(m_ModuleManager->GetModule(fromFlag));
        if (pMod == nullptr)
            throw CustomError ("Module was not found at %s.", sDataMgr.GetFlagName(fromFlag));

        // a bank fires as one:  its slaves are in use while its master is
        GenericModule* pMaster(GetBankMaster(pMod));
        if (pMod->IsActive() or ((pMaster != nullptr) and pMaster->IsActive()))
            throw UserError ("CannotAccessChargeWhileInUse");

        // "{item} is currently being reloaded. Please wait until it is finished and try again."
        if (pMod->IsLoading())
            throw UserError ("AlreadyBeingReloaded")
                    .AddTypeName ("item", pMod->typeID());

        if (!pMod->IsLoaded())
            throw CustomError ("Your %s is not loaded.", pMod->GetSelf()->name());

        m_ModuleManager->UnloadCharge(pMod);
    }
}

/* T3 subsystems: each fits one hull (fitsToShipType) in one slot (its subSystemSlot attribute)
 *  {'messageKey': 'CannotFitSubSystemNotShip'}  You can't fit {subSystemName}. It only fits to {validShipName} but you're trying to fit it to {shipName}.
 *  {'messageKey': 'NoSuitableSlotsForThisSubSystem'}  There are no suitable slots available for {subSystemName}.
 */
void ShipItem::CheckSubSystemFit(EVEItemFlags flag, InventoryItemRef iRef)
{
    if (IsSubSystem(flag) != (iRef->categoryID() == EVEDB::invCategories::Subsystem)) {
        if (iRef->categoryID() == EVEDB::invCategories::Subsystem)
            throw UserError ("NoSuitableSlotsForThisSubSystem")
                    .AddTypeName ("subSystemName", iRef->typeID ());
        throw UserError ("NoFreeShipSlots");
    }
    if (!IsSubSystem(flag))
        return;

    uint32 fitsTo(iRef->GetAttribute(AttrFitsToShipType).get_uint32());
    if (fitsTo != typeID())
        throw UserError ("CannotFitSubSystemNotShip")
                .AddTypeName ("subSystemName", iRef->typeID ())
                .AddTypeName ("validShipName", fitsTo)
                .AddTypeName ("shipName", typeID ());
    if (iRef->GetAttribute(AttrSubSystemSlot).get_uint32() != (uint32)flag)
        throw UserError ("NoSuitableSlotsForThisSubSystem")
                .AddTypeName ("subSystemName", iRef->typeID ());
}

// only called from inv bound call.  can throw
void ShipItem::TryModuleLimitChecks(EVEItemFlags flag, InventoryItemRef iRef, bool swap/*false*/)
{
    // a charge dropped on a module loads it (VerifyHoldType has checked that it fits).  the module must be idle
    if (iRef->categoryID() == EVEDB::invCategories::Charge) {
        CheckChargeLoadable(m_ModuleManager->GetModule(flag), iRef);
        return;
    }

    // a subsystem dropped on a fitted one replaces it
    if (!(swap and IsSubSystem(flag)) and m_ModuleManager->IsSlotOccupied(flag))
        throw UserError ("SlotAlreadyOccupied");

    CheckSubSystemFit(flag, iRef);
    m_ModuleManager->CheckSlotFitLimited(flag);
    m_ModuleManager->CheckGroupFitLimited(flag, iRef);

    if (IsHiSlot(flag)) {
        // check available turret/launcher hardpoints
        if (iRef->type().HasEffect(EVEEffectID::turretFitted)) {
            if (GetAttribute(AttrTurretSlotsLeft) < 1) {
                throw UserError ("NotEnoughTurretSlots")
                        .AddTypeName ("moduleName", iRef->typeID ());
                /*u'NotEnoughTurretSlotsBody'}(u"You cannot fit the {moduleName} because your ship doesn't have any turret slots left for fitting, possibly because you have already filled your ship with turrets or that the ship simply can not be fitted with turrets.\r\n<br>
                 * <br>Turret slots represent how many weapons of a certain type can be fitted on a ship. The current design is over a hundred years old, and is modular enough to allow for a great leeway in the fitting of various weaponry.", None,
                 * {u'{moduleName}': {'conditionalValues': [], 'variableType': 10, 'propertyName': None, 'args': 0, 'kwargs': {}, 'variableName': 'moduleName'}})
                 */
            }
        } else if (iRef->type().HasEffect(EVEEffectID::launcherFitted)) {
            if (GetAttribute(AttrLauncherSlotsLeft) < 1) {
                throw UserError ("NotEnoughLauncherSlots")
                        .AddTypeName ("moduleName", iRef->typeID ());
                /*NotEnoughLauncherSlotsBody'}(u"You cannot fit the {moduleName} because your ship doesn't have any launcher slots left for fitting, possibly because you have already filled your ship with launchers or that the ship simply can not be fitted with launchers.<br>
                 * <br>Launcher slots represent how many weapons of a certain type can be fitted on a ship. The current design is over a hundred years old, and is modular enough to allow for a great leeway in the fitting of various weaponry.", None,
                 * {u'{moduleName}': {'conditionalValues': [], 'variableType': 10, 'propertyName': None, 'args': 0, 'kwargs': {}, 'variableName': 'moduleName'}})
                 */
            }
        }
    } else if (IsRigSlot(flag)) {
        if (GetAttribute(AttrRigSize) != iRef->GetAttribute(AttrRigSize)) {
            throw UserError ("CannotFitRigWrongSize")
                    .AddFormatValue ("rigSize", new PyString (sDataMgr.GetRigSizeName (iRef->GetAttribute (AttrRigSize).get_uint32())))
                    .AddTypeName ("item", iRef->typeID ())
                    .AddFormatValue ("shipRigSize", new PyString (sDataMgr.GetRigSizeName (GetAttribute (AttrRigSize).get_uint32())));
            /* CannotFitRigWrongSizeBody'}(u'{item} does not fit in this slot.
             * The slot takes size {shipRigSize} rigs, but the item is size {rigSize}.', None,
             * {u'{rigSize}': {'conditionalValues': [], 'variableType': 10, 'propertyName': None, 'args': 0, 'kwargs': {}, 'variableName': 'rigSize'},
             * u'{item}': {'conditionalValues': [], 'variableType': 10, 'propertyName': None, 'args': 0, 'kwargs': {}, 'variableName': 'item'},
             * u'{shipRigSize}': {'conditionalValues': [], 'variableType': 10, 'propertyName': None, 'args': 0, 'kwargs': {}, 'variableName': 'shipRigSize'}})
             * check avalible rig slots and ship upgrade capy
             */
        }
        if (GetAttribute(AttrUpgradeSlotsLeft) < 1) {
            throw UserError ("NotEnoughUpgradeSlots")
                    .AddFormatValue ("moduleType", new PyInt (iRef->typeID ()));
            /*NotEnoughUpgradeSlotsBody'}(u"You cannot fit the {[item]moduleType.name} because your ship doesn't have any upgrade slots left for fitting, possibly because you have already filled your ship with upgrades or that the ship simply can not be fitted with upgrades.", None,
             * {u'{[item]moduleType.name}': {'conditionalValues': [], 'variableType': 2, 'propertyName': 'name', 'args': 0, 'kwargs': {}, 'variableName': 'moduleType'}})
             */
        }

        if ((GetAttribute(AttrUpgradeLoad) + iRef->GetAttribute(AttrUpgradeCost)) > GetAttribute(AttrUpgradeCapacity)) {
            throw UserError ("NotEnoughUpgradeCapacity")
                    .AddTypeName ("moduleName", iRef->typeID ());
            /*NotEnoughUpgradeCapacityBody'}(u'You cannot fit the {moduleName} because your ship cannot handle it. Your ship can only fit so many upgrades as each interferes with its calibration, and past a certain point your ship is rendered unusable.', None,
             * {u'{moduleName}': {'conditionalValues': [], 'variableType': 10, 'propertyName': None, 'args': 0, 'kwargs': {}, 'variableName': 'moduleName'}})
             */
        }
    }
}

EVEItemFlags ShipItem::FindAvailableModuleSlot(InventoryItemRef iRef) {
    uint16 slotFound(flagIllegal);
    if (iRef->type().HasEffect(EVEEffectID::loPower)) {
        slotFound = m_ModuleManager->GetAvailableSlotInBank(EVEEffectID::loPower);
    } else if (iRef->type().HasEffect(EVEEffectID::medPower)) {
        slotFound = m_ModuleManager->GetAvailableSlotInBank(EVEEffectID::medPower);
    } else if (iRef->type().HasEffect(EVEEffectID::hiPower)) {
        slotFound = m_ModuleManager->GetAvailableSlotInBank(EVEEffectID::hiPower);
    } else if (iRef->type().HasEffect(EVEEffectID::subSystem)) {
        // a subsystem has one slot.  a fitted one there is swapped out
        slotFound = iRef->GetAttribute(AttrSubSystemSlot).get_uint32();
        if (!IsSubSystem(slotFound))
            slotFound = flagIllegal;
    } else if (iRef->type().HasEffect(EVEEffectID::rigSlot)) {
        slotFound = m_ModuleManager->GetAvailableSlotInBank(EVEEffectID::rigSlot);
    } else {
        codelog(SHIP__ERROR, "ShipItem::FindAvailableModuleSlot() - iRef %s has no bank effect.", iRef->name());
    }

    return (EVEItemFlags)slotFound;
}


// only called from inv bound call.  can throw
void ShipItem::MoveModuleSlot(EVEItemFlags slot1, EVEItemFlags slot2) {
    // this will never hit.  client checks before call.
    if (!m_ModuleManager->VerifySlotExchange(slot1, slot2))
        throw CustomError ("Those locations are not compatible.");

    // test for active module(s) before moving
    GenericModule* pMod(GetModule(slot1));
    if (pMod->IsActive())
        throw CustomError ("Your %s is currently active.  You must wait for the cycle to complete before it can be removed.", pMod->GetSelf()->name());

    pMod = GetModule(slot2);
    if (pMod != nullptr)
        if (pMod->IsActive())
            throw CustomError ("Your %s is currently active.  You must wait for the cycle to complete before it can be removed.", pMod->GetSelf()->name());

    // slot1 is occupied, as this is where module is from.
    InventoryItemRef modItemRef1(GetModuleRef(slot1));
    if (modItemRef1.get() == nullptr) {
        _log(MODULE__TRACE, "ShipItem::MoveModuleSlot - modItemRef1 is null.");
        throw CustomError ("The module to move was not found.");
    }
    InventoryItemRef chargeItemRef1(m_ModuleManager->GetLoadedChargeOnModule(slot1));
    RemoveItem(modItemRef1);

    if (m_ModuleManager->IsSlotOccupied(slot2)) {
        // dropped-on slot is occupied.  proceed with moving the module currently in this slot.
        InventoryItemRef modItemRef2 = GetModuleRef(slot2);
        InventoryItemRef chargeItemRef2 = m_ModuleManager->GetLoadedChargeOnModule(slot2);
        RemoveItem(modItemRef2);
        AddItemByFlag(slot1, modItemRef2);
        if (modItemRef2->GetAttribute(AttrOnline).get_bool())
            m_ModuleManager->Online(slot1);
        if (chargeItemRef2.get() != nullptr)
            m_ModuleManager->LoadCharge(chargeItemRef2, slot1);
    }

    AddItemByFlag(slot2, modItemRef1);
    if (modItemRef1->GetAttribute(AttrOnline).get_bool())
        m_ModuleManager->Online(slot2);

    if (chargeItemRef1.get() != nullptr)
        m_ModuleManager->LoadCharge(chargeItemRef1, slot2);
}

void ShipItem::UpdateModules()
{
    /* this is only called when ship is in space
     * this will call Online() on all modules, which will apply passive and online effects.
     */
    m_ModuleManager->UpdateModules(m_onlineModuleVec);
    m_onlineModuleVec.clear();
    //FailedToOnlineModulesOnUndock
}

void ShipItem::UpdateModules(EVEItemFlags flag)
{
    // List of callees to put this function into context as to what it should be doing:
    // ShipSE::AddItem()
    // Client::MoveItem()               - something has been moved into or out of the ship, recheck all modules for... some reason
    m_ModuleManager->UpdateModules(flag);
}

//  Updated fractional ship defense settings.  -allan 1Feb15
void ShipItem::SetShipCapacitorLevel(float fraction)
{
    if (fraction > 1.0f) fraction = 1.0f;
    if (fraction < 0.0f) fraction = 0.0f;

    EvilNumber newCapacitorCharge(GetAttribute(AttrCapacitorCapacity) * fraction);
    if ((newCapacitorCharge + 0.5f) > GetAttribute(AttrCapacitorCapacity).get_float())
        newCapacitorCharge = GetAttribute(AttrCapacitorCapacity);
    if ((newCapacitorCharge - 0.5f) < 0)
        newCapacitorCharge = 0;

    _log(SHIP__MESSAGE, "SetShipCapacitorLevel() to %.1f%%.  new value is %.1f", fraction, newCapacitorCharge.get_float());
    SetAttribute(AttrCapacitorCharge, newCapacitorCharge);
}

void ShipItem::SetShipShield(float fraction)
{
    if (fraction > 1.0f) fraction = 1.0f;
    if (fraction < 0.0f) fraction = 0.0f;

    EvilNumber newShieldCharge(GetAttribute(AttrShieldCapacity) * fraction);
    if ((newShieldCharge + 0.2f) > GetAttribute(AttrShieldCapacity).get_float())
        newShieldCharge = GetAttribute(AttrShieldCapacity);
    if ((newShieldCharge - 0.2f) < 0)
        newShieldCharge = 0;

    _log(SHIP__MESSAGE, "SetShipShield() to %.1f%%.  new value is %.1f", fraction, newShieldCharge.get_float());
    SetAttribute(AttrShieldCharge, newShieldCharge);
}

void ShipItem::SetShipArmor(float fraction)
{
    fraction = 1 - fraction;

    if (fraction > 1.0f) fraction = 1.0f;
    if (fraction < 0.0f) fraction = 0.0f;

    EvilNumber newArmorDamage(GetAttribute(AttrArmorHP) * fraction);
    if ((newArmorDamage + 0.2f) > GetAttribute(AttrArmorHP).get_float())
        newArmorDamage = GetAttribute(AttrArmorHP);
    if ((newArmorDamage - 0.2f) < 0)
        newArmorDamage = 0;

    _log(SHIP__MESSAGE, "SetShipArmor() to %.1f%%.  new value is %.1f", fraction, newArmorDamage.get_float());
    SetAttribute(AttrArmorDamage, newArmorDamage);
}

void ShipItem::SetShipHull(float fraction)
{
    fraction = 1 - fraction;

    if (fraction > 1.0f) fraction = 1.0f;
    if (fraction < 0.0f) fraction = 0.0f;

    EvilNumber newHullDamage(GetAttribute(AttrHP) * fraction);
    if ((newHullDamage + 0.2f) > GetAttribute(AttrHP).get_float())
        newHullDamage = GetAttribute(AttrHP);
    if ((newHullDamage - 0.2f) < 0)
        newHullDamage = 0;

    _log(SHIP__MESSAGE, "SetShipHull() to %.1f%%.  new value is %.1f", fraction, newHullDamage.get_float());
    SetAttribute(AttrDamage, newHullDamage);
}

// not used
void ShipItem::RepairShip(float fraction)
{
    if (fraction > 1)
        fraction = 1;

    if (fraction == 1) {
         SetAttribute(AttrDamage, EvilZero);
         SetAttribute(AttrArmorDamage, EvilZero);
        return;
    }

    uint32 cHull(GetAttribute(AttrDamage).get_uint32());
    uint32 cArmor(GetAttribute(AttrArmorDamage).get_uint32());
    uint32 damage(cHull + cArmor);
    EvilNumber amount(damage * fraction);
    // this will repair hull first, then armor
    if (amount > cHull) {
        amount -= cHull;
         SetAttribute(AttrDamage, EvilZero);
        if (amount >= cArmor) {
             SetAttribute(AttrArmorDamage, EvilZero);
        } else {
            amount = cArmor - amount;
             SetAttribute(AttrArmorDamage, amount);
        }
    } else {
         SetAttribute(AttrDamage, amount);
    }
}

// not used
void ShipItem::RepairModules(std::vector<InventoryItemRef>& itemRefVec, float fraction)
{
    /** @todo  this isnt right....needs update */
    EvilNumber amount(EvilZero), damage(EvilZero);
    for (auto cur : itemRefVec) {
        damage = cur->GetAttribute(AttrDamage);
        if (damage < 0.01)
            continue;
        amount = cur->GetAttribute(AttrDamage);
        if ((amount / cur->GetAttribute(AttrHP)) > fraction) {
            amount = cur->GetAttribute(AttrHP) *  fraction;
        } else {
            amount = 1;
        }
        m_ModuleManager->RepairModule(cur->itemID(), amount);
    }
}

void ShipItem::Online(uint32 modID)
{
    /* without a station's fitting service a module needs 95% capacitor to come online, and onlining uses it all.
     *  the client checks nothing itself, so this throws its NotEnoughCapacitorForOnline {module, need, have}
     */
    bool inSpace(sDataMgr.IsSolarSystem(locationID()));
    GenericModule* pMod(m_ModuleManager->GetModule(modID));
    if (inSpace and (pMod != nullptr) and !pMod->isOnline()) {
        float charge(GetAttribute(AttrCapacitorCharge).get_float());
        float capacity(GetAttribute(AttrCapacitorCapacity).get_float());
        if ((capacity > 0) and (charge < (0.95f * capacity)))
            throw UserError ("NotEnoughCapacitorForOnline")
                    .AddFormatValue("module", new PyInt(pMod->GetSelf()->typeID()))
                    .AddFormatValue("need", new PyInt(95))
                    .AddFormatValue("have", new PyInt((int32)(charge * 100.0f / capacity)));
    }
    /* {'messageKey': 'EffectAlreadyActive2', 'dataID': 17876243, 'suppressable': False, 'bodyID': 256802, 'messageType': 'hint', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 3252}
     * {'messageKey': 'EffectCantHaveNegativeDuration', 'dataID': 17883801, 'suppressable': False, 'bodyID': 259632, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 847}
     * {'messageKey': 'EffectCrowdedOut', 'dataID': 17883719, 'suppressable': False, 'bodyID': 259602, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 848}
     * {'messageKey': 'EffectDeactivationCloaking', 'dataID': 17883455, 'suppressable': False, 'bodyID': 259510, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 849}
     * {'messageKey': 'EffectDeactivationWarping', 'dataID': 17883458, 'suppressable': False, 'bodyID': 259511, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 850}
     * {'messageKey': 'EffectNotActivatible', 'dataID': 17880378, 'suppressable': False, 'bodyID': 258369, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 2228}
     * {'messageKey': 'EffectNotDeactivatible', 'dataID': 17883847, 'suppressable': False, 'bodyID': 259649, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 851}
     * {'messageKey': 'EffectStillActivating', 'dataID': 17883850, 'suppressable': False, 'bodyID': 259650, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 852}
     * {'messageKey': 'EffectStillActive', 'dataID': 17883853, 'suppressable': False, 'bodyID': 259651, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 853}
     */
    /*{'FullPath': u'UI/Messages', 'messageID': 256802, 'label': u'EffectAlreadyActive2Body'}(u'{modulename} is already active', None, {u'{modulename}': {'conditionalValues': [], 'variableType': 10, 'propertyName': None, 'args': 0, 'kwargs': {}, 'variableName': 'modulename'}})
     * {'FullPath': u'UI/Messages', 'messageID': 258369, 'label': u'EffectNotActivatibleBody'}(u'You do not have the ability to engage the {moduleName} in that action.', None, {u'{moduleName}': {'conditionalValues': [], 'variableType': 10, 'propertyName': None, 'args': 0, 'kwargs': {}, 'variableName': 'moduleName'}})
     * {'FullPath': u'UI/Messages', 'messageID': 259510, 'label': u'EffectDeactivationCloakingBody'}(u'As certain activated effects interfere with the warping process, these are automatically being deactivated before the warp proceeds.', None, None)
     * {'FullPath': u'UI/Messages', 'messageID': 259511, 'label': u'EffectDeactivationWarpingBody'}(u'As certain activated effects interfere with the warping process, these are automatically being deactivated before the warp proceeds.', None, None)
     * {'FullPath': u'UI/Messages', 'messageID': 259602, 'label': u'EffectCrowdedOutBody'}(u'The {[item]module.name} cannot be activated as it requires access to resources and/or skills which {[numeric]count} modules of the same purpose are already using up.', None, {u'{[numeric]count}': {'conditionalValues': [], 'variableType': 9, 'propertyName': None, 'args': 0, 'kwargs': {}, 'variableName': 'count'}, u'{[item]module.name}': {'conditionalValues': [], 'variableType': 2, 'propertyName': 'name', 'args': 0, 'kwargs': {}, 'variableName': 'module'}})
     * {'FullPath': u'UI/Messages', 'messageID': 259632, 'label': u'EffectCantHaveNegativeDurationBody'}(u'{modulename} has somehow come to have a negative duration of {duration} and cannot operate. If you know how, obtain a breakdown of its modifiers and report it, otherwise submit a detailed bug report.', None, {u'{modulename}': {'conditionalValues': [], 'variableType': 10, 'propertyName': None, 'args': 0, 'kwargs': {}, 'variableName': 'modulename'}, u'{duration}': {'conditionalValues': [], 'variableType': 10, 'propertyName': None, 'args': 0, 'kwargs': {}, 'variableName': 'duration'}})
     * {'FullPath': u'UI/Messages', 'messageID': 259649, 'label': u'EffectNotDeactivatibleBody'}(u'The {moduleName} cannot be interrupted in that action.', None, {u'{moduleName}': {'conditionalValues': [], 'variableType': 10, 'propertyName': None, 'args': 0, 'kwargs': {}, 'variableName': 'moduleName'}})
     * {'FullPath': u'UI/Messages', 'messageID': 259650, 'label': u'EffectStillActivatingBody'}(u'The {moduleName} cannot be manually deactivated while it is still being activated.', None, {u'{moduleName}': {'conditionalValues': [], 'variableType': 10, 'propertyName': None, 'args': 0, 'kwargs': {}, 'variableName': 'moduleName'}})
     * {'FullPath': u'UI/Messages', 'messageID': 259651, 'label': u'EffectStillActiveBody'}(u'The {[item]moduleTypeID.name} cannot be manually deactivated in the middle of an operation, it will deactivate without repeating in {[timeinterval]timeLeft.writtenForm, from=second, to=second} (its activation duration is {[timeinterval]duration.writtenForm, from=second, to=second}).', None, {u'{[timeinterval]timeLeft.writtenForm, from=second, to=second}': {'conditionalValues': [], 'variableType': 8, 'propertyName': 'writtenForm', 'args': 0, 'kwargs': {'to': 'second', 'from': 'second'}, 'variableName': 'timeLeft'}, u'{[timeinterval]duration.writtenForm, from=second, to=second}': {'conditionalValues': [], 'variableType': 8, 'propertyName': 'writtenForm', 'args': 0, 'kwargs': {'to': 'second', 'from': 'second'}, 'variableName': 'duration'}, u'{[item]moduleTypeID.name}': {'conditionalValues': [], 'variableType': 2, 'propertyName': 'name', 'args': 0, 'kwargs': {}, 'variableName': 'moduleTypeID'}})
     */
	m_ModuleManager->Online(modID);

    if (inSpace and (pMod != nullptr) and pMod->isOnline()) {
        SetShipCapacitorLevel(0.0f);
        _log(SHIP__MESSAGE, "ShipItem::Online(): %s(%u) - capacitor used to online %s", name(), itemID(), pMod->GetSelf()->name());
    }
}

void ShipItem::Offline(uint32 modID)
{
    m_ModuleManager->Offline(modID);
}

void ShipItem::Activate(int32 itemID, std::string effectName, int32 targetID, int32 repeat)
{
    if (IsValidTarget(targetID)) {
        m_targetRef = sItemFactory.GetItemRef(targetID);
    } else {
        m_targetRef = InventoryItemRef(nullptr);
    }

    m_ModuleManager->Activate( itemID, sFxDataMgr.GetEffectID(effectName), targetID, repeat);
}

void ShipItem::OnlineAll()
{
    m_ModuleManager->OnlineAll();
}

void ShipItem::OfflineAll()
{
    m_ModuleManager->OfflineAll();
}

void ShipItem::SaveShip()
{
    SaveItem();                         // Save ship info
    pAttributeMap->SaveShipState();     // save ship damage
    m_ModuleManager->SaveModules();     // Save item info for modules fitted to this ship
}

void ShipItem::RemoveRig(InventoryItemRef iRef) {
    if (iRef.get() == nullptr)
        return;
    m_ModuleManager->UninstallRig(iRef->itemID());
    // Crucible:  a rig is destroyed when it's taken off (the client warns first).  A test server keeps it, in the cargo hold
    if (sConfig.debug.IsTestServer) {
        iRef->Move(itemID(), flagCargoHold, true);
    } else {
        iRef->Delete();
    }
}

void ShipItem::ReplaceCharges(EVEItemFlags flag, InventoryItemRef newCharge)
{
    _log(MODULE__ERROR, "ReplaceCharges() called by %s(%u).  It still needs to be written.", name(), itemID());
}


/*  heat-related shit......
 * see also file:///home/allan/Desktop/backups/GoonSwarm_wiki/www.eveinfo.net/wiki/ind~2149.htm - "Thermodynamics"
 *
    AttrHeatGenerationMultiplier = 1224,
    AttrHeatHi = 1175,
    AttrHeatMed = 1176,
    AttrHeatLow = 1177,

    AttrHeatDissipationRateHi = 1179,
    AttrHeatDissipationRateMed = 1196,
    AttrHeatDissipationRateLow = 1198,

    AttrHeatCapacityHi = 1178,
    AttrHeatCapacityMed = 1199,
    AttrHeatCapacityLow = 1200,

    AttrHeatAttenuationHi = 1259,
    AttrHeatAttenuationMed = 1261,
    AttrHeatAttenuationLow = 1262,

    AttrHeatAbsorbtionRateModifier = 1180,      // active modules only

    created ship attribs (rifter)
    1179    heatDissipationRateHi       NULL    0.01
    1196    heatDissipationRateMed      NULL    0.01
    1198    heatDissipationRateLow      NULL    0.01
    1178    heatCapacityHi              NULL    100
    1199    heatCapacityMed             NULL    100
    1200    heatCapacityLow             NULL    100
    1224    heatGenerationMultiplier    NULL    1
    1259    heatAttenuationHi           NULL    0.63
    1261    heatAttenuationMed          NULL    0.5
    1262    heatAttenuationLow          NULL    0.5
    created module attribs (200mm ac)
    1180    heatAbsorbtionRateModifier  NULL    0.01
    1211    heatDamage                  NULL    1
    (1mn ab I)
    1180    heatAbsorbtionRateModifier  NULL    0.04
    1211    heatDamage                  NULL    9.6
    (sebo I)
    1180    heatAbsorbtionRateModifier  NULL    0.01
    1211    heatDamage                  NULL    3.4

    // these are calculated from fx shit (not saved)    {cannot find where these are used..}
    AttrHeatAbsorbtionRateHi = 1182,        eID 10326
    AttrHeatAbsorbtionRateMed = 1183,       eID 10327
    AttrHeatAbsorbtionRateLow = 1184,       eID 10328

*/

namespace {
    // per rack: high, medium, low
    const uint16 heatAttr[3]        = { AttrHeatHi, AttrHeatMed, AttrHeatLow };
    const uint16 capacityAttr[3]    = { AttrHeatCapacityHi, AttrHeatCapacityMed, AttrHeatCapacityLow };
    const uint16 dissipationAttr[3] = { AttrHeatDissipationRateHi, AttrHeatDissipationRateMed, AttrHeatDissipationRateLow };
    const uint16 rateAttr[3]        = { AttrHeatAbsorbtionRateHi, AttrHeatAbsorbtionRateMed, AttrHeatAbsorbtionRateLow };
    const uint16 attenuationAttr[3] = { AttrHeatAttenuationHi, AttrHeatAttenuationMed, AttrHeatAttenuationLow };
    const EVEItemFlags firstSlot[3] = { flagHiSlot0, flagMidSlot0, flagLowSlot0 };
}

int8 ShipItem::GetHeatRack(EVEItemFlags flag)
{
    if ((flag >= flagHiSlot0) and (flag <= flagHiSlot7))
        return 0;
    if ((flag >= flagMidSlot0) and (flag <= flagMidSlot7))
        return 1;
    if ((flag >= flagLowSlot0) and (flag <= flagLowSlot7))
        return 2;
    return -1;
}

ShipItem::RackHeat& ShipItem::GetRackState(int8 rack)
{
    RackHeat& r = m_rackHeat[rack];
    if (!r.init) {
        // heat saved with the ship starts cooling from now
        r.init = true;
        r.heat = GetAttribute(heatAttr[rack]).get_double();
        r.time = GetFileTimeNow();
        r.rate = 0.0;
    }
    return r;
}

/* the client's godma GetHeatState():
 *   not heating:  H(t) = H * e^(-t * heatDissipationRate)
 *   heating:      H(t) = C - (C - H) * e^(-t * rate * heatGenerationMultiplier)
 *  t in seconds, C = heat capacity.  a rack doesn't cool while it is heating.
 */
double ShipItem::RackHeatAt(int8 rack, int64 time)
{
    RackHeat& r = GetRackState(rack);
    double capacity(GetAttribute(capacityAttr[rack]).get_double());
    if (capacity <= 0)
        capacity = 100.0;
    double seconds((time - r.time) / (double)EvE::Time::Second);
    if (seconds < 0)
        seconds = 0;

    double heat(0.0);
    if (r.rate < 5e-08) {
        heat = r.heat * exp(-seconds * GetAttribute(dissipationAttr[rack]).get_double());
    } else {
        double k(exp(-seconds * r.rate * GetAttribute(AttrHeatGenerationMultiplier).get_double()));
        heat = capacity - (capacity - r.heat) * k;
    }
    if (heat < 0)
        heat = 0;
    if (heat > capacity)
        heat = capacity;
    return heat;
}

double ShipItem::GetRackHeat(int8 rack)
{
    if ((rack < 0) or (rack > 2))
        return 0;
    return RackHeatAt(rack, GetFileTimeNow());
}

void ShipItem::SetRackHeat(int8 rack, double heat)
{
    if ((rack < 0) or (rack > 2))
        return;
    RackHeat& r = GetRackState(rack);
    r.heat = (heat < 0 ? 0 : heat);
    r.time = GetFileTimeNow();
    SyncRackHeat(rack);
}

void ShipItem::SetModuleHeating(GenericModule* pMod, bool heating)
{
    int8 rack(GetHeatRack(pMod->flag()));
    if (rack < 0)
        return;
    RackHeat& r = GetRackState(rack);
    if (heating) {
        if (!r.modules.insert(pMod->itemID()).second)
            return;
    } else if (r.modules.erase(pMod->itemID()) == 0) {
        return;
    }
    SyncRackHeat(rack);
}

void ShipItem::SyncRackHeat(int8 rack)
{
    int64 now(GetFileTimeNow());
    RackHeat& r = GetRackState(rack);
    double heat(RackHeatAt(rack, now));
    double rate(0.0);
    for (auto id : r.modules) {
        GenericModule* pMod(m_ModuleManager->GetModule(id));
        if (pMod != nullptr)
            rate += pMod->GetAttribute(AttrHeatAbsorbtionRateModifier).get_double();
    }
    r.heat = heat;
    r.time = now;
    r.rate = rate;
    _log(SHIP__HEAT, "%s(%u): rack %i heat %.2f, heating rate %.4f (%u modules)", name(), itemID(), rack, heat, rate, r.modules.size());

    SetAttribute(heatAttr[rack], heat, false);
    SetAttribute(rateAttr[rack], rate, false);
    // the client needs both with the same time, even when the value didn't change, or its gauges jump
    SendHeatChange(heatAttr[rack], heat, now);
    SendHeatChange(rateAttr[rack], rate, now);
}

void ShipItem::SendHeatChange(uint16 attrID, double value, int64 time)
{
    Client* pClient(GetPilot());
    if ((pClient == nullptr) or pClient->IsLogin() or (pClient->GetShipID() != itemID()))
        return;
    Notify_OnModuleAttributeChange modChange;
        modChange.ownerID = ownerID();
        modChange.itemKey = new PyInt(itemID());
        modChange.attributeID = attrID;
        modChange.time = time;
        modChange.newValue = new PyFloat(value);
        modChange.oldValue = new PyFloat(value);
    PyTuple* tuple = modChange.Encode();
    pClient->QueueDestinyEvent(&tuple);
}

void ShipItem::UpdateHeatAttributes()
{
    int64 now(GetFileTimeNow());
    for (int8 rack = 0; rack < 3; ++rack) {
        RackHeat& r = GetRackState(rack);
        r.heat = RackHeatAt(rack, now);
        r.time = now;
        SetAttribute(heatAttr[rack], r.heat, false);
        SetAttribute(rateAttr[rack], r.rate, false);
    }
}

void ShipItem::StopHeating()
{
    for (int8 rack = 0; rack < 3; ++rack) {
        RackHeat& r = GetRackState(rack);
        if (r.modules.empty())
            continue;
        r.modules.clear();
        SyncRackHeat(rack);
    }
}

/* heat damage, after each overloaded cycle (Kadesh Priestess' 2011 analysis):
 *  every online module in the rack, the overloaded one included, may take the overloaded module's heatDamage.
 *  chance = rack heat (0-1) * (online high+medium+low modules / high+medium+low+rig slots) * heatAttenuation ^ distance
 *  distance is in slots from the overloaded module, without wrapping round the rack.  heatAttenuation is per rack, from the ship.
 *  a module whose damage reaches its hp burns out and goes offline.
 */
void ShipItem::HeatDamageCheck(GenericModule* pMod)
{
    int8 rack(GetHeatRack(pMod->flag()));
    if (rack < 0)
        return;
    float heatDamage(pMod->GetAttribute(AttrHeatDamage).get_float());
    if (heatDamage <= 0)
        return;
    double capacity(GetAttribute(capacityAttr[rack]).get_double());
    if (capacity <= 0)
        capacity = 100.0;
    double heat(GetRackHeat(rack) / capacity);
    if (heat <= 0)
        return;

    uint32 slots(GetAttribute(AttrHiSlots).get_uint32() + GetAttribute(AttrMedSlots).get_uint32()
               + GetAttribute(AttrLowSlots).get_uint32() + GetAttribute(AttrRigSlots).get_uint32());
    if (slots == 0)
        return;
    uint32 online(0);
    for (int8 i = 0; i < 3; ++i)
        for (uint8 slot = 0; slot < 8; ++slot) {
            GenericModule* cur(m_ModuleManager->GetModule((EVEItemFlags)(firstSlot[i] + slot)));
            if ((cur != nullptr) and cur->isOnline())
                ++online;
        }
    double slotFactor(online / (double)slots);
    if (slotFactor > 1)
        slotFactor = 1;
    double attenuation(GetAttribute(attenuationAttr[rack]).get_double());
    int srcSlot(pMod->flag() - firstSlot[rack]);

    std::vector<GenericModule*> damaged;
    for (uint8 slot = 0; slot < 8; ++slot) {
        GenericModule* cur(m_ModuleManager->GetModule((EVEItemFlags)(firstSlot[rack] + slot)));
        if ((cur == nullptr) or !cur->isOnline() or cur->IsBurntOut())
            continue;
        double chance(heat * slotFactor * pow(attenuation, abs(slot - srcSlot)));
        if (MakeRandomFloat() < chance)
            damaged.push_back(cur);
    }

    for (auto cur : damaged) {
        float hp(cur->GetAttribute(AttrHP).get_float());
        float damage(cur->GetAttribute(AttrDamage).get_float() + heatDamage);
        if (damage > hp)
            damage = hp;
        cur->SetAttribute(AttrDamage, damage);
        _log(SHIP__HEAT, "%s(%u): %s takes %.2f heat damage from %s (%.2f/%.2f)", name(), itemID(), \
                cur->GetSelf()->name(), heatDamage, pMod->GetSelf()->name(), damage, hp);
        if (damage >= hp)
            BurnOut(cur);
    }
}

void ShipItem::BurnOut(GenericModule* pMod)
{
    Client* pClient(GetPilot());
    if (pClient != nullptr)
        pClient->SendNotifyMsg("Your %s has burned out.", pMod->GetSelf()->name());
    // stops it now, takes it out of overload, and tells the client it's offline.  it can't be onlined until repaired
    pMod->Offline();
    // its bank stays, but stops and can't be activated until it is back online
    m_ModuleManager->OnModuleWentOffline(pMod);
}

void ShipItem::StripFitting(bool all/*false*/)
{
    bool banked(HasLinkedWeapons());
    UnlinkAllWeapons(true);
    if (banked)
        SendWeaponBanks();

    EVEItemFlags flag = flagCargoHold;
    if (sDataMgr.IsStation(locationID()))
        flag = flagHangar;

    std::vector<InventoryItemRef> moduleList;
    m_ModuleManager->GetModuleListOfRefsAsc(moduleList);
    for (auto cur : moduleList) {
        // a T3 keeps its subsystems (CantUnfitSubSystems), and rigs are only removed by destroying them
        if (!all and (IsRigSlot(cur->flag()) or IsSubSystem(cur->flag())))
            continue;
        m_ModuleManager->UnfitModule(cur->itemID());
        cur->Move(locationID(), flag, true);
    }
}

void ShipItem::EvictExcessModules()
{
    /* a subsystem change can leave a T3 with fewer slots or hardpoints than it has modules fitted.  modules in slots the ship
     *  no longer has, and the turrets/launchers past its hardpoints (highest slots first), go to the hangar (cargo in space)
     */
    int16 hi(0), med(0), low(0), turrets(0), launchers(0);
    m_ModuleManager->GetSlotLayout(hi, med, low, turrets, launchers);

    std::vector<GenericModule*> evict;
    const EVEItemFlags first[3] = { flagHiSlot0, flagMidSlot0, flagLowSlot0 };
    const int16 count[3] = { hi, med, low };
    for (uint8 rack = 0; rack < 3; ++rack)
        for (int16 i = (count[rack] < 0 ? 0 : count[rack]); i < 8; ++i) {
            GenericModule* pMod(m_ModuleManager->GetModule((EVEItemFlags)(first[rack] + i)));
            if (pMod != nullptr)
                evict.push_back(pMod);
        }
    int16 nTurret(0), nLauncher(0);
    for (int16 i = 0; (i < hi) and (i < 8); ++i) {
        GenericModule* pMod(m_ModuleManager->GetModule((EVEItemFlags)(flagHiSlot0 + i)));
        if (pMod == nullptr)
            continue;
        if (pMod->isTurretFitted() and (++nTurret > turrets)) {
            evict.push_back(pMod);
        } else if (pMod->isLauncherFitted() and (++nLauncher > launchers)) {
            evict.push_back(pMod);
        }
    }
    if (evict.empty())
        return;

    uint32 toID(m_itemID);
    EVEItemFlags toFlag(flagCargoHold);
    if (sDataMgr.IsStation(locationID())) {
        toID = locationID();
        toFlag = flagHangar;
    }
    for (auto pMod : evict) {
        InventoryItemRef iRef(pMod->GetSelf());
        EVEItemFlags flag(iRef->flag());
        // unfitting unloads it and takes it out of its weapon bank
        m_ModuleManager->UnfitModule(iRef->itemID());
        iRef->Move(toID, toFlag, true);
        if (m_pilot != nullptr)
            m_pilot->SendNotifyMsg("Your %s was removed from %s and placed in your %s.", iRef->name(), \
                    sDataMgr.GetFlagName(flag), (toFlag == flagHangar ? "hangar" : "cargo hold"));
    }
}

uint8 ShipItem::PrepareRepackage()
{
    UnlinkAllWeapons(true);

    std::vector<InventoryItemRef> items;
    // a ship boarded this session has module objects:  take them off first (charges are unloaded into the cargo hold)
    pInventory->GetInventoryVec(items);
    for (auto cur : items)
        if ((IsModuleSlot(cur->flag()) or IsRigSlot(cur->flag()) or IsSubSystem(cur->flag()))
        and (m_ModuleManager->GetModule(cur->itemID()) != nullptr))
            m_ModuleManager->UnfitModule(cur->itemID());

    uint8 rigs(0);
    items.clear();
    pInventory->GetInventoryVec(items);
    for (auto cur : items) {
        if (IsRigSlot(cur->flag())) {
            ++rigs;
            cur->Delete();
            continue;
        }
        cur->Move(locationID(), flagHangar, true);
    }
    return rigs;
}

void ShipItem::EmptyCargo()
{
    std::map<uint32, InventoryItemRef> invMap;
    pInventory->GetInventoryMap( invMap );
    for (auto cur : invMap )
        cur.second->Move(locationID(), flagHangar, true);
}

void ShipItem::CargoFull() {
    // loop thru modules and deactivate any type of miner
    m_ModuleManager->CargoFull();
    // tell drones cargo is full.  this will command mining types to return and orbit
    // drones->return();
}

/* weapon banks
 *  client contract (clientDogmaLocation.py):
 *    LinkWeapons(shipID, masterID, fromID)         fromID isnt banked.  returns {masterID: [slaveIDs]} or None
 *    MergeModuleGroups(shipID, masterID, slaveID)  slaveID is the master of the dragged module's bank.  the whole bank joins masterID's
 *    PeelAndLink(shipID, masterID, slaveID)        slaveID is the master of the dragged module's bank.  one module leaves it for masterID's
 *    UnlinkModule(shipID, moduleID)                moduleID is a bank master.  returns a slaveID in that bank, which the client removes
 *    UnlinkAllModules / LinkAllWeapons             return the bank dict
 *    DestroyWeaponBank(shipID, masterID)           the client deletes the bank itself
 *  Crucible rules (dev blog "Weapon Grouping", Oct 2008): same type, hi slot turrets and launchers only,
 *    online, undamaged, not active or loading and not loaded (server.UnloadOnLinkAll unloads them instead)
 */
bool ShipItem::IsGroupable(GenericModule* pMod)
{
    if (pMod == nullptr)
        return false;
    if (!IsHiSlot(pMod->flag()))
        return false;
    // const.dgmGroupableGroupIDs
    switch (pMod->groupID()) {
        case EVEDB::invGroups::Energy_Weapon:
        case EVEDB::invGroups::Projectile_Weapon:
        case EVEDB::invGroups::Hybrid_Weapon:
        case EVEDB::invGroups::Missile_Launcher:
        case EVEDB::invGroups::Missile_Launcher_Assault:
        case EVEDB::invGroups::Missile_Launcher_Citadel:
        case EVEDB::invGroups::Missile_Launcher_Cruise:
        case EVEDB::invGroups::Missile_Launcher_Defender:
        case EVEDB::invGroups::Missile_Launcher_Heavy:
        case EVEDB::invGroups::Missile_Launcher_Heavy_Assault:
        case EVEDB::invGroups::Missile_Launcher_Rocket:
        case EVEDB::invGroups::Missile_Launcher_Siege:
        case EVEDB::invGroups::Missile_Launcher_Standard:
            return true;
    }
    return false;
}

GenericModule* ShipItem::GetBankMaster(GenericModule* pMod)
{
    if (pMod == nullptr)
        return nullptr;
    if (m_linkedWeapons.find(pMod) != m_linkedWeapons.end())
        return pMod;
    for (auto& cur : m_linkedWeapons)
        if (std::find(cur.second.begin(), cur.second.end(), pMod) != cur.second.end())
            return cur.first;
    return nullptr;
}

void ShipItem::GetBankMembers(GenericModule* pMod, std::vector<GenericModule*>& modules)
{
    GenericModule* pMaster(GetBankMaster(pMod));
    if (pMaster == nullptr) {
        if (pMod != nullptr)
            modules.push_back(pMod);
        return;
    }
    modules.push_back(pMaster);
    std::map<GenericModule*, std::list<GenericModule*>>::iterator itr = m_linkedWeapons.find(pMaster);
    for (auto cur : itr->second)
        modules.push_back(cur);
}

bool ShipItem::BankHasOfflineMember(GenericModule* pMod)
{
    std::vector<GenericModule*> modules;
    GetBankMembers(pMod, modules);
    for (auto cur : modules)
        if (!cur->isOnline())
            return true;
    return false;
}

void ShipItem::CheckLinkable(GenericModule* pMod)
{
    if (!pMod->isOnline())
        throw UserError ("CantLinkModuleNotOnline");
    if (pMod->IsActive())
        throw UserError ("CantLinkModuleActive");
    if (pMod->IsLoading())
        throw UserError ("CantLinkModuleLoading");
    if (pMod->IsDamaged())
        throw UserError ("CantLinkModuleDamaged");
    // loaded weapons are unloaded by PrepareLink() when the server allows it
    if (pMod->IsLoaded() and !sConfig.server.UnloadOnLinkAll)
        throw UserError ("CantLinkAmmoInWeapon");
}

void ShipItem::CheckLinkable(GenericModule* pMod1, GenericModule* pMod2)
{
    if ((pMod1 == nullptr) or (pMod2 == nullptr))
        throw CustomError ("That module is not fitted to your ship.");
    // the client checks both of these and doesnt call us, so these are catchalls
    if (pMod1->typeID() != pMod2->typeID())
        throw CustomError ("You can only group modules of the same type and meta level together.");
    if (!IsGroupable(pMod1) or !IsGroupable(pMod2))
        throw CustomError ("Only turrets and launchers fitted in high slots can be grouped.");
}

void ShipItem::PrepareLink(std::vector<GenericModule*>& joining, std::vector<GenericModule*>& bank)
{
    // check everything before changing anything.  modules joining a bank must be linkable,
    //  the bank they join must be idle and unloaded
    for (auto cur : joining)
        CheckLinkable(cur);
    for (auto cur : bank) {
        if (cur->IsActive())
            throw UserError ("CantLinkModuleActive");
        if (cur->IsLoading())
            throw UserError ("CantLinkModuleLoading");
        if (cur->IsLoaded() and !sConfig.server.UnloadOnLinkAll)
            throw UserError ("CantLinkAmmoInWeapon");
    }
    for (auto cur : joining)
        if (cur->IsLoaded())
            m_ModuleManager->UnloadCharge(cur);
    for (auto cur : bank)
        if (cur->IsLoaded())
            m_ModuleManager->UnloadCharge(cur);
}

void ShipItem::CheckUnlinkable(GenericModule* pMod)
{
    // if the master is loading or active, so is the whole group
    std::vector<GenericModule*> modules;
    GetBankMembers(pMod, modules);
    for (auto cur : modules) {
        if (cur->IsActive())
            throw UserError ("CantUngroupModuleActive");
        if (cur->IsLoading())
            throw UserError ("CantUngroupModuleLoading");
    }
}

void ShipItem::RemoveFromBank(GenericModule* pMod)
{
    GenericModule* pMaster(GetBankMaster(pMod));
    if (pMaster == nullptr)
        return;

    std::map<GenericModule*, std::list<GenericModule*>>::iterator itr = m_linkedWeapons.find(pMaster);
    if (pMod == pMaster) {
        // the first slave takes over the rest of the bank
        std::list<GenericModule*> slaves(itr->second);
        m_linkedWeapons.erase(itr);
        if (slaves.size() > 1) {
            GenericModule* pNew(slaves.front());
            slaves.pop_front();
            pNew->SetLinkMaster(true);
            m_linkedWeapons[pNew] = slaves;
        } else if (!slaves.empty()) {
            slaves.front()->SetLinked(false);
            slaves.front()->SetLinkMaster(false);
        }
    } else {
        itr->second.remove(pMod);
        if (itr->second.empty()) {
            pMaster->SetLinked(false);
            pMaster->SetLinkMaster(false);
            m_linkedWeapons.erase(itr);
        }
    }
    pMod->SetLinked(false);
    pMod->SetLinkMaster(false);
}

void ShipItem::SendWeaponBanks()
{
    if (m_pilot == nullptr)
        return;
    PyTuple* tuple = new PyTuple(3);
        tuple->SetItem(0, new PyString("OnWeaponBanksChanged"));
        tuple->SetItem(1, new PyInt(m_itemID));
        tuple->SetItem(2, GetLinkedWeapons());
    m_pilot->QueueDestinyEvent(&tuple);
}

void ShipItem::GetLinkedWeaponMods( EVEItemFlags flag, std::vector<GenericModule*> &modules ) {
    GenericModule* pMaster(GetBankMaster(m_ModuleManager->GetModule(flag)));
    if (pMaster != nullptr)
        GetBankMembers(pMaster, modules);
}

// only called from dogma call.  can throw
void ShipItem::LinkWeapon(uint32 masterID, uint32 slaveID)
{
    if (masterID == slaveID)
        return;
    GenericModule* pMod1(m_ModuleManager->GetModule(masterID));
    GenericModule* pMod2(m_ModuleManager->GetModule(slaveID));
    CheckLinkable(pMod1, pMod2);

    // a banked module joins with its whole bank
    if (GetBankMaster(pMod2) != nullptr) {
        MergeModuleGroups(masterID, slaveID);
        return;
    }

    GenericModule* pMaster(GetBankMaster(pMod1));
    if (pMaster == nullptr)
        pMaster = pMod1;

    std::vector<GenericModule*> joining, bank;
    joining.push_back(pMod2);
    GetBankMembers(pMaster, bank);
    PrepareLink(joining, bank);

    LinkWeapon(pMaster, pMod2);
    SaveWeaponGroups();
}

// adds pSlave to pMaster's bank (making one if needed).  no checks here
void ShipItem::LinkWeapon(GenericModule* pMaster, GenericModule* pSlave)
{
    if ((pMaster == nullptr) or (pSlave == nullptr))
        return;
    if (pMaster == pSlave)
        return;

    std::map<GenericModule*, std::list<GenericModule*>>::iterator itr = m_linkedWeapons.find(pMaster);
    if (itr == m_linkedWeapons.end()) {
        std::list<GenericModule*> slaves;
        slaves.push_back(pSlave);
        m_linkedWeapons[pMaster] = slaves;
        pMaster->SetLinked(true);
        pMaster->SetLinkMaster(true);
    } else {
        itr->second.push_back(pSlave);
    }
    pSlave->SetLinked(true);
    pSlave->SetLinkMaster(false);
}

// only called from dogma call.  can throw
void ShipItem::MergeModuleGroups(uint32 masterID, uint32 slaveID)
{
    GenericModule* pMod1(m_ModuleManager->GetModule(masterID));
    GenericModule* pMod2(m_ModuleManager->GetModule(slaveID));
    CheckLinkable(pMod1, pMod2);

    GenericModule* pMaster(GetBankMaster(pMod1));
    if (pMaster == nullptr)
        pMaster = pMod1;
    GenericModule* pOther(GetBankMaster(pMod2));
    if (pOther == nullptr)
        pOther = pMod2;
    if (pMaster == pOther)
        return;

    std::vector<GenericModule*> joining, bank;
    GetBankMembers(pOther, joining);
    GetBankMembers(pMaster, bank);
    PrepareLink(joining, bank);

    // pOther and all of its slaves join pMaster's bank
    std::list<GenericModule*> moved;
    std::map<GenericModule*, std::list<GenericModule*>>::iterator itr = m_linkedWeapons.find(pOther);
    if (itr != m_linkedWeapons.end()) {
        moved = itr->second;
        m_linkedWeapons.erase(itr);
    }
    LinkWeapon(pMaster, pOther);
    for (auto cur : moved)
        LinkWeapon(pMaster, cur);

    SaveWeaponGroups();
}

// only called from dogma call.  can throw
void ShipItem::PeelAndLink(uint32 masterID, uint32 slaveID)
{
    GenericModule* pMod1(m_ModuleManager->GetModule(masterID));
    GenericModule* pMod2(m_ModuleManager->GetModule(slaveID));
    CheckLinkable(pMod1, pMod2);

    GenericModule* pOther(GetBankMaster(pMod2));
    if (pOther == nullptr) {
        LinkWeapon(masterID, slaveID);
        return;
    }
    GenericModule* pMaster(GetBankMaster(pMod1));
    if (pMaster == nullptr)
        pMaster = pMod1;
    if (pMaster == pOther)
        return;

    // the client sends the master of the source bank, not the dragged module.
    //  peel its last slave so the source bank keeps its master (and its button)
    CheckUnlinkable(pOther);
    GenericModule* pPeeled(m_linkedWeapons[pOther].back());

    std::vector<GenericModule*> joining, bank;
    joining.push_back(pPeeled);
    GetBankMembers(pMaster, bank);
    PrepareLink(joining, bank);

    RemoveFromBank(pPeeled);
    LinkWeapon(pMaster, pPeeled);
    SaveWeaponGroups();
}

// only called from dogma call.  can throw
void ShipItem::LinkAllWeapons()
{
    // one bank per type of the online, undamaged, idle weapons.  existing banks are kept and merged
    std::list< GenericModule* > weaponList;
    m_ModuleManager->GetWeapons(weaponList);

    // GetWeapons() is in flag order, so the first of a type has the lowest slot
    std::map<uint32, std::vector<GenericModule*>> buckets;
    for (auto cur : weaponList) {
        if (!IsGroupable(cur) or !cur->isOnline() or cur->IsDamaged())
            continue;
        bool busy(false);
        std::vector<GenericModule*> bank;
        GetBankMembers(cur, bank);
        for (auto mod : bank)
            if (mod->IsActive() or mod->IsLoading())
                busy = true;
        if (busy)
            continue;
        if (cur->IsLoaded() and !sConfig.server.UnloadOnLinkAll) {
            if (is_log_enabled(MODULE__INFO))
                _log(MODULE__INFO, "ShipItem::LinkAllWeapons() - %s(%s-%u) IsLoaded.  Skipping.", \
                    cur->GetSelf()->name(), sDataMgr.GetFlagName(cur->flag()), cur->itemID());
            if (m_pilot != nullptr)
                m_pilot->SendErrorMsg("You cannot group the %s while loaded with %s", cur->GetSelf()->name(), cur->GetLoadedChargeRef()->name());
            continue;
        }
        buckets[cur->typeID()].push_back(cur);
    }

    for (auto& cur : buckets) {
        if (cur.second.size() < 2)
            continue;
        // use the first existing bank of this type, else the lowest slot
        GenericModule* pMaster(nullptr);
        for (auto mod : cur.second)
            if (mod->IsMaster()) {
                pMaster = mod;
                break;
            }
        if (pMaster == nullptr)
            pMaster = cur.second.front();

        for (auto mod : cur.second)
            if (mod->IsLoaded())
                m_ModuleManager->UnloadCharge(mod);

        for (auto mod : cur.second) {
            if ((mod == pMaster) or (GetBankMaster(mod) == pMaster))
                continue;
            RemoveFromBank(mod);
            LinkWeapon(pMaster, mod);
        }
    }

    SaveWeaponGroups();
}

/*
 * {'messageKey': 'CantUngroupModuleActive', 'dataID': 17874986, 'suppressable': False, 'bodyID': 256339, 'messageType': 'hint', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 3540}
 *    (256339, `You cannot ungroup active modules, please deactivate them first.`)
 * {'messageKey': 'CantUngroupModuleLoading', 'dataID': 17874989, 'suppressable': False, 'bodyID': 256340, 'messageType': 'hint', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 3541}
 *    (256340, `You can't ungroup weapons while they are being loaded with charges.`)
 * {'messageKey': 'CantUnlinkModuleActive', 'dataID': 17878129, 'suppressable': False, 'bodyID': 257511, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 2679}
 *    (257511, `You cannot ungroup active modules, please deactivate them first.`)
 */
// only called from dogma call.  can throw
uint32 ShipItem::UnlinkWeapon(uint32 moduleID)
{
    // the client removes the returned module from moduleID's bank, so it must be one of its slaves
    GenericModule* pMaster(GetBankMaster(m_ModuleManager->GetModule(moduleID)));
    if (pMaster == nullptr)
        throw CustomError ("That module is not grouped.");
    CheckUnlinkable(pMaster);

    GenericModule* pSlave(m_linkedWeapons[pMaster].back());
    RemoveFromBank(pSlave);
    SaveWeaponGroups();
    return pSlave->itemID();
}

void ShipItem::UnlinkWeapon(uint32 masterID, uint32 slaveID)
{
    if (masterID == slaveID)
        return;
    GenericModule* pMod1(m_ModuleManager->GetModule(masterID));
    GenericModule* pMod2(m_ModuleManager->GetModule(slaveID));
    if ((pMod1 == nullptr) or (pMod2 == nullptr))
        return;
    if (GetBankMaster(pMod2) != pMod1)
        return;

    CheckUnlinkable(pMod1);
    RemoveFromBank(pMod2);
    SaveWeaponGroups();
}

// called from dogma call (can throw) and unfit (force, doesnt throw)
void ShipItem::UnlinkGroup(uint32 memberID, bool update/*false*/, bool force/*false*/)
{
    GenericModule* pMaster(GetBankMaster(m_ModuleManager->GetModule(memberID)));
    if (pMaster == nullptr)
        return;
    if (!force)
        CheckUnlinkable(pMaster);

    std::map<GenericModule*, std::list<GenericModule*>>::iterator itr = m_linkedWeapons.find(pMaster);
    for (auto cur : itr->second) {
        cur->SetLinked(false);
        cur->SetLinkMaster(false);
    }
    m_linkedWeapons.erase(itr);
    pMaster->SetLinked(false);
    pMaster->SetLinkMaster(false);
    SaveWeaponGroups();

    if (update and (m_pilot != nullptr)) {
        PyTuple* tuple = new PyTuple(3);
            tuple->SetItem(0, new PyString("OnWeaponGroupDestroyed"));
            tuple->SetItem(1, new PyInt(m_itemID));
            tuple->SetItem(2, new PyInt(pMaster->itemID()));
        m_pilot->QueueDestinyEvent(&tuple);
    }
}

// called from here and dogma call.  can throw unless forced
void ShipItem::UnlinkAllWeapons(bool force/*false*/)
{
    // check everything before changing anything
    if (!force)
        for (auto cur : m_linkedWeapons)
            CheckUnlinkable(cur.first);

    for (auto cur : m_linkedWeapons) {
        cur.first->SetLinked(false);
        cur.first->SetLinkMaster(false);
        for (auto slave : cur.second) {
            slave->SetLinked(false);
            slave->SetLinkMaster(false);
        }
    }
    m_linkedWeapons.clear();
    ShipDB::ClearWeaponGroups(m_itemID);
}

uint8 ShipItem::GetLinkedCount(GenericModule* pMod)
{
    std::vector<GenericModule*> modules;
    GetBankMembers(pMod, modules);
    return (modules.empty() ? 1 : modules.size());
}

uint8 ShipItem::GetLoadedLinkedCount(GenericModule* pMod)
{
    // only online, loaded weapons fire
    if (GetBankMaster(pMod) == nullptr)
        return 1;
    std::vector<GenericModule*> modules;
    GetBankMembers(pMod, modules);
    uint8 count(0);
    for (auto cur : modules)
        if (cur->isOnline() and cur->IsLoaded())
            ++count;
    return count;
}

PyRep* ShipItem::GetLinkedWeapons()
{
    if (m_linkedWeapons.empty())
        return PyStatic.NewNone();

    PyDict* result = new PyDict();
    for (auto cur : m_linkedWeapons) {
        PyList* slaves = new PyList();
        for (auto slave : cur.second)
            slaves->AddItem(new PyInt(slave->itemID()));
        result->SetItem(new PyInt(cur.first->itemID()), slaves);
    }

    if (is_log_enabled(MODULE__MESSAGE)) {
        _log(MODULE__MESSAGE, "GetLinkedWeapons()");
        result->Dump(MODULE__MESSAGE, "    ");
    }
    return result;
}

void ShipItem::SaveWeaponGroups()
{
    std::multimap< uint32, uint32 > data;
    data.clear();
    if (!m_linkedWeapons.empty())
        for (auto cur : m_linkedWeapons)
            for (auto slave : cur.second)
                data.emplace(cur.first->itemID(), slave->itemID());

    ShipDB::SaveWeaponGroups(m_itemID, data);
}

void ShipItem::LoadWeaponGroups()
{
    // NOTE:  there probably isnt a pilot at this point, so no sending of errors on load.
    if (m_ModuleManager == nullptr) {
        if (m_pilot != nullptr)
            m_pilot->SendErrorMsg("There was an error loading your weapon groups.  Ref: ServerError 06071");
        return;
    }
    DBQueryResult* res = new DBQueryResult();
    ShipDB::LoadWeaponGroups(m_itemID, *res);

    bool error(false);
    GenericModule* pMaster(nullptr);
    GenericModule* pSlave(nullptr);
    DBResultRow row;
    // this is gonna be slow as shit...i dont like this, but best way to do it.
    while (res->GetRow(row)) {
        //SELECT masterID, slaveID FROM shipWeaponGroups
        pMaster = m_ModuleManager->GetModule(row.GetInt(0));
        pSlave = m_ModuleManager->GetModule(row.GetInt(1));
        if ((pMaster == nullptr) or (pSlave == nullptr) or (pMaster == pSlave)) {
            error = true;
            continue;
        }
        // the module moved or was swapped since the group was saved
        if ((pMaster->typeID() != pSlave->typeID()) or !IsGroupable(pMaster) or !IsGroupable(pSlave)) {
            error = true;
            continue;
        }
        // a module in two banks
        if ((GetBankMaster(pSlave) != nullptr) or (pMaster->IsLinked() and !pMaster->IsMaster())) {
            error = true;
            continue;
        }
        //LinkWeapon(masterID, slaveID);  <<<--- this will throw.  DO NOT use here.
        LinkWeapon(pMaster, pSlave);
    }

    if (error) {
        UnlinkAllWeapons(true);
        if (m_pilot != nullptr)
            m_pilot->SendErrorMsg("There was an error loading a weapon group master.  Ref: ServerError 06123");
    }

    SafeDelete(res);
}
/* End new Module Manager Interface */

// new effects system.  wip
void ShipItem::ProcessEffects(bool add/*false*/, bool update/*false*/)
{
    double startTime(GetTimeMSeconds());
    _log(EFFECTS__TRACE, "ShipItem::ProcessEffects()");
    /*
    Effects processing order...
        Skills     //char effect
        Implants   //char effect
        Boosters   //char effect
        Ship       //ship effect
        Subsystem  //module effect
        Rigs       //module effect
        Low        //module effect
        Mid        //module effect
        Hi         //module effect
    */
    if (add) {
        m_pilot->GetChar()->ProcessEffects(this);
        // the reset put the hull's slot and hardpoint values back
        m_ModuleManager->UpdateSlotAttributes(update);
        ProcessShipEffects(update);
    } else {
        ClearModifiers();
        pAttributeMap->SaveShipState();      // save ship damage as it's removed on next call
        ResetAttributes();
        ClearModuleModifiers();
        m_pilot->GetChar()->ResetModifiers();
        std::vector< InventoryItemRef > modVec;
        m_ModuleManager->GetModuleListOfRefsAsc(modVec);
        for (auto cur : modVec)
            cur->ResetAttributes();
        std::map<EVEItemFlags, InventoryItemRef> charges;
        m_ModuleManager->GetLoadedCharges(charges);
        for (auto cur : charges) {
            /* the reset reloads the charge's attributes from the type and the db, and a crystal's damage taken in space was
             *  never written there (SaveItem() doesn't save attributes):  it is carried over (dock, eject, boarding) */
            EvilNumber damage(EvilZero);
            bool damaged(cur.second->HasAttribute(AttrDamage, damage) and (damage.get_double() > 0.0));
            cur.second->ResetAttributes();
            if (damaged)
                cur.second->SetAttribute(AttrDamage, damage, false);
        }
        // the charges' effects on their modules went with the reset, and so did the modules' docked effects
        m_ModuleManager->ChargeFxReset();
        m_ModuleManager->DockedFxReset();
        // the system effects went with the reset too
        m_systemFx = InventoryItemRef(nullptr);

        // do we remove fx here?  nah, we've reset everything at this point.
    }

    _log(EFFECTS__DEBUG, "ShipItem::ProcessEffects() - effects processed and applied in %.3fms", (GetTimeMSeconds() - startTime));
}

void ShipItem::ProcessShipEffects(bool update/*false*/)
{
    _log(EFFECTS__TRACE, "ShipItem::ProcessShipEffects()");
    for (auto it : type().m_stateFxMap) {
        fxData data = fxData();
        data.action = FX::Action::Invalid;
        data.srcRef = static_cast<InventoryItemRef>(this);
        sFxProc.ParseExpression(this, sFxDataMgr.GetExpression(it.second.preExpression), data);
    }

    /* the system's effects (w-space phenomena), with the hull's so they are worked out in the same order.  in space only:
     *  an undocking ship is still in the station here, and gets them from UpdateSystemEffects() once it is out */
    m_systemFx = InventoryItemRef(nullptr);
    if ((m_pilot != nullptr) and (m_pilot->SystemMgr() != nullptr) and (m_pilot->SystemMgr()->GetID() == locationID())) {
        InventoryItemRef beacon(m_pilot->SystemMgr()->GetEffectBeacon());
        if (beacon.get() != nullptr) {
            ParseSystemEffects(beacon, this, true);
            m_systemFx = beacon;
        }
    }

    if (m_isUndocking) {
        // online modules sent from client (these are onlined in fit window while docked)
        m_ModuleManager->UpdateModules(m_onlineModuleVec);
        m_onlineModuleVec.clear();
        //FailedToOnlineModulesOnUndock
    } else {
        // this will set module to last saved online state in the case of BoardShip() and Login()
        m_ModuleManager->LoadOnline();
    }

    // apply processed effects
    sFxProc.ApplyEffects(this, m_pilot->GetChar().get(), this, update);
    //ClearModifiers();
}

void ShipItem::ParseSystemEffects(InventoryItemRef beacon, InventoryItem* pContainer, bool add)
{
    // the beacon's system effects (category 7):  added with their pre-expressions, taken off with their post-expressions
    std::map<uint16, Effect> effectMap;
    beacon->type().GetEffectMap(FX::State::System, effectMap);
    for (auto& cur : effectMap) {
        fxData data = fxData();
        data.action = FX::Action::Invalid;
        data.srcRef = beacon;
        sFxProc.ParseExpression(pContainer, sFxDataMgr.GetExpression(add ? cur.second.preExpression : cur.second.postExpression), data);
    }
}

/* a ship in space that has moved to pSys (jump, undock) swaps the old system's effects for the new one's.
 *  the change is made on the ship's current values:  the old beacon's math is undone, then the new beacon's is applied.
 *  the beacon is its own scratch container (ApplyEffects() clears it)
 */
void ShipItem::UpdateSystemEffects(SystemManager* pSys, bool update/*true*/)
{
    if ((m_pilot == nullptr) or (pSys == nullptr) or (pSys->GetID() != locationID()))
        return;
    InventoryItemRef beacon(pSys->GetEffectBeacon());
    if (beacon.get() == m_systemFx.get())
        return;
    Character* pChar(m_pilot->GetChar().get());
    if (pChar == nullptr)
        return;

    if (m_systemFx.get() != nullptr) {
        _log(EFFECTS__DEBUG, "ShipItem::UpdateSystemEffects() - %s(%u) loses the effects of %s(%u)", name(), itemID(), m_systemFx->name(), m_systemFx->itemID());
        ParseSystemEffects(m_systemFx, m_systemFx.get(), false);
        sFxProc.ApplyEffects(m_systemFx.get(), pChar, this, update);
    }
    m_systemFx = beacon;
    if (beacon.get() != nullptr) {
        _log(EFFECTS__DEBUG, "ShipItem::UpdateSystemEffects() - %s(%u) gets the effects of %s(%u) in %s", name(), itemID(), beacon->name(), beacon->itemID(), pSys->GetName());
        ParseSystemEffects(beacon, beacon.get(), true);
        sFxProc.ApplyEffects(beacon.get(), pChar, this, update);
    }

    // speed, agility and mass for the movement code
    ShipSE* pShipSE(m_pilot->GetShipSE());
    if ((pShipSE != nullptr) and (pShipSE->DestinyMgr() != nullptr))
        pShipSE->DestinyMgr()->UpdateShipVariables();
}

void ShipItem::ClearModuleModifiers()
{
    _log(EFFECTS__TRACE, "ShipItem::ClearModuleModifiers()");
    //m_ModuleManager->OfflineAll();
    std::vector< InventoryItemRef > modVec;
    m_ModuleManager->GetModuleListOfRefsAsc(modVec);
    for (auto cur : modVec)
        cur->ClearModifiers();
    std::map<EVEItemFlags, InventoryItemRef> charges;
    m_ModuleManager->GetLoadedCharges(charges);
    for (auto cur : charges)
        cur.second->ClearModifiers();
}

void ShipItem::ResetEffects() {
    _log(EFFECTS__TRACE, "ShipItem::ResetEffects()");
    double start = GetTimeMSeconds();

    m_ModuleManager->OfflineAll();

    // reset attributes on char, ship, all modules and charges
    pAttributeMap->SaveShipState();      // save ship damage as it's removed on next call
    ResetAttributes();
    m_pilot->GetChar()->ResetModifiers();
    std::vector< InventoryItemRef > modVec;
    m_ModuleManager->GetModuleListOfRefsAsc(modVec);
    for (auto cur : modVec)
        cur->ResetAttributes();
    std::map<EVEItemFlags, InventoryItemRef> charges;
    m_ModuleManager->GetLoadedCharges(charges);
    for (auto cur : charges)
        cur.second->ResetAttributes();
    // the charges' effects on their modules went with the reset, and so did the modules' docked effects
    m_ModuleManager->ChargeFxReset();
    m_ModuleManager->DockedFxReset();
    m_systemFx = InventoryItemRef(nullptr);

    ProcessEffects(true, true/*sDataMgr.IsSolarSystem(locationID())*/);
    _log(EFFECTS__DEBUG, "ShipItem::ResetEffects() - Effects reset in %.3fms", (GetTimeMSeconds() - start));
}

void ShipItem::PrepForUndock() {
    //  "Offline" modules (see notes in GM::Online and GM::Offline
    m_ModuleManager->OfflineAll();
    // clear fx maps before undock
    ProcessEffects();
}

std::string ShipItem::GetShipDNA()
{
    /* ship dna is shorthand notation to describe a ship and it's fittings purely thru the use of typeIDs and quantities
     *
     * the format is as follows:
     * <shipTypeID>:
     *    <subsystemID>:
     *      <moduleType-Highslot>;<quantity>:
     *      <moduleType-Midslot>;<quantity>:
     *      <moduleType-Lowslot>;<quantity>:
     *      <moduleType-Rigslot>;<quantity>:
     *      <chargeType>;<quantity>:
     *      <droneType>;<quantity>
     *
     * Condensed version:
     *  Ship:Subsystem:Highs:Mids:Lows:Rigs:Charges:Drones
     *
     * [PyString "<url=fitting:24698:3841;2:2531;1:19812;1:23527;1:2410;7:1422;4:2547;1:31802;3:2301;1:2454;5::>Anchor</url>"]
     *
     *  modules (and charges, drones) of one type are grouped:  "587:8863;3:499;1:578;1:1798;1:6485;1:2046;1:8325;1:31788;2:31800;1::"
     */
    if (typeID() == EVEDB::invTypes::Capsule) {
        std::stringstream dna;
        dna << typeID() << ":";
        _log(SHIP__MESSAGE, "ShipDNA has compiled DNA of \"%s\" for %s(%u) ", dna.str().c_str(), name(), itemID());
        return dna.str();
    }

    /* each typeID once per group, with the summed quantity, in the order first seen */
    typedef std::vector<std::pair<uint32, uint32>> TypeCounts;
    auto addType = [](TypeCounts& into, uint32 typeID, uint32 qty) {
        for (auto& cur : into)
            if (cur.first == typeID) {
                cur.second += qty;
                return;
            }
        into.push_back(std::make_pair(typeID, qty));
    };
    auto encode = [](std::stringstream& out, const TypeCounts& from) {
        for (auto cur : from)
            out << cur.first << ";" << cur.second << ":";
    };

    TypeCounts modHi, modMid, modLow, modRig, charges, drones;
    std::stringstream subSys;

    std::vector<InventoryItemRef> moduleList;
    m_ModuleManager->GetModuleListOfRefsAsc(moduleList);
    for (auto cur : moduleList) {
        if (IsRigSlot(cur->flag())) {
            addType(modRig, cur->typeID(), 1);
        } else if (IsHiSlot(cur->flag())) {
            addType(modHi, cur->typeID(), 1);
        } else if (IsMidSlot(cur->flag())) {
            addType(modMid, cur->typeID(), 1);
        } else if (IsLowSlot(cur->flag())) {
            addType(modLow, cur->typeID(), 1);
        } else if (IsSubSystem(cur->flag())) {
            subSys << cur->typeID() << ":";
        }
    }

    std::map<EVEItemFlags, InventoryItemRef> chargeList;
    m_ModuleManager->GetLoadedCharges(chargeList);
    for (auto cur : chargeList)
        addType(charges, cur.second->typeID(), cur.second->quantity());

    std::vector<InventoryItemRef> droneList;
    pInventory->GetItemsByFlag(flagDroneBay, droneList);
    for (auto cur : droneList)
        addType(drones, cur->typeID(), cur->quantity());

    /* build the dna stream:  ship:subsystems:highs:mids:lows:rigs:charges:drones:: */
    std::stringstream dna;
    dna << typeID() << ":" << subSys.str();
    encode(dna, modHi);
    encode(dna, modMid);
    encode(dna, modLow);
    encode(dna, modRig);
    encode(dna, charges);
    encode(dna, drones);
    dna << ":";

    _log(SHIP__MESSAGE, "ShipDNA has compiled \"%s\" for %s(%u) ", dna.str().c_str(), name(), itemID());
    return dna.str();
}

void ShipItem::VerifyHoldType(EVEItemFlags flag, InventoryItemRef iRef, Client* pClient/*nullptr*/)
{
    // if *this ship isnt active, it wont have a pilot to send errors to.  test and set as needed.
    //  to fix this, set client to character calling the move
    if (pClient == nullptr) {
        if (m_pilot == nullptr)
            return;     /** @todo  this is an error  */
        pClient = m_pilot;
    }

    switch (flag) {
        case flagCargoHold:
            // everything can be stored in general cargo
            //  update this later to disallow specialized items?
            return;
        case flagDroneBay: {
            if (iRef->categoryID() != EVEDB::invCategories::Drone) {
                throw CustomError ("%s cannot be stowed in the Drone Bay", sDataMgr.GetGroupName(iRef->groupID()));
            }
            if (groupID() == EVEDB::invGroups::Supercarrier) {
                // these can only carry fighters and fighter/bombers in drone bay.  enforce that here.
                if ((iRef->groupID() != EVEDB::invGroups::Fighter_Bomber)
                and (iRef->groupID() != EVEDB::invGroups::Fighter_Drone)) {
                    throw CustomError ("The %s can only carry fighter drones in it's Drone Bay.  The %s is not allowed.", name(), iRef->name());
                }
            }
        } break;
        case flagShipHangar: {    //AttrShipMaintenanceBayCapacity
            if (!HasAttribute(AttrHasShipMaintenanceBay)) {
                throw CustomError ("Your %s has no ship maintenance bay.", name());
            }
            // the Rorqual's bay only takes the classes the client lists
            if ((typeID() == EVEDB::invTypes::Rorqual)
            and (iRef->categoryID() == EVEDB::invCategories::Ship)
            and (iRef->groupID() != EVEDB::invGroups::MiningBarge)
            and (iRef->groupID() != EVEDB::invGroups::Exhumer)
            and (iRef->groupID() != EVEDB::invGroups::Industrial)
            and (iRef->groupID() != EVEDB::invGroups::TransportShip))
                throw UserError ("CannotAddShipTypeToMaintainanceBay");
            if (iRef->categoryID() != EVEDB::invCategories::Ship) {
                throw CustomError ("Only ships may be placed into the maintenance bay.");
            }
        } break;
        case flagFuelBay: {    //  AttrFuelBayCapacity
            if ((iRef->groupID() != EVEDB::invGroups::FuelBlock)
            and (iRef->groupID() != EVEDB::invGroups::Ice_Product)) {
                throw CustomError ("Only fuel types may be stored in the fuel bay.");
            }
        } break;
        case flagOreHold: {
            if (iRef->categoryID() != EVEDB::invCategories::Asteroid) {
                throw CustomError ("Only mined ore may be stored in the ore hold.");
            }
        } break;
        case flagGasHold: {
            if (iRef->groupID() != EVEDB::invGroups::Gas_Isotopes) {
                throw CustomError ("Only gas products may be stored in the gas hold.");
            }
        } break;
        case flagMineralHold: {
            if (iRef->groupID() != EVEDB::invGroups::Mineral) {
                throw CustomError ("Only refined minerals may be placed into the mineral hold.");
            }
        } break;
        case flagSalvageHold: {
            if (iRef->groupID() != EVEDB::invGroups::Salvage_Materials) {
                throw CustomError ("Only salvaged materials may be placed into the salvage bay.");
            }
        } break;
        // not sure if all of these flag* are used.  if not, *may* update dgmData to add them....later.
        case flagShipHold: {
            if (iRef->categoryID() != EVEDB::invCategories::Ship) {
                throw CustomError ("Only ships may be placed into the ship hold.");
            }
        } break;

        // ship size from the rig size (1 small, 2 medium, 3 large).  only the unpublished Cockroach has these holds
        case flagSmallShipHold:
        case flagMediumShipHold:
        case flagLargeShipHold: {
            uint8 size(flag == flagSmallShipHold ? 1 : (flag == flagMediumShipHold ? 2 : 3));
            if ((iRef->categoryID() != EVEDB::invCategories::Ship)
            or  !iRef->HasAttribute(AttrRigSize) or (iRef->GetAttribute(AttrRigSize).get_uint32() != size))
                throw UserError ("CannotStoreDestinationRestricted")
                        .AddFormatValue("item", new PyString(iRef->itemName()))
                        .AddFormatValue("ship", new PyInt(typeID()));
        } break;
        case flagIndustrialShipHold: {
            if ((iRef->groupID() != EVEDB::invGroups::MiningBarge)
            and (iRef->groupID() != EVEDB::invGroups::Exhumer)
            and (iRef->groupID() != EVEDB::invGroups::Industrial)
            and (iRef->groupID() != EVEDB::invGroups::TransportShip)
            and (iRef->groupID() != EVEDB::invGroups::Freighter)
            and (iRef->groupID() != EVEDB::invGroups::Prototype_Exploration_Ship)
            and (iRef->groupID() != EVEDB::invGroups::CapitalIndustrialShip)) {
                throw CustomError ("Only indy ships may be placed into the ship's industrial ship hold.");
            }
        } break;
        case flagAmmoHold: {
            if ((iRef->groupID() != EVEDB::invGroups::Ammo)
            and (iRef->groupID() != EVEDB::invGroups::Frequency_Crystal)
            and (iRef->groupID() != EVEDB::invGroups::Mining_Crystal)
            and (iRef->groupID() != EVEDB::invGroups::Mercoxit_Mining_Crystal)
            and (iRef->groupID() != EVEDB::invGroups::Advanced_Beam_Laser_Crystal)
            and (iRef->groupID() != EVEDB::invGroups::Advanced_Pulse_Laser_Crystal)
            and (iRef->groupID() != EVEDB::invGroups::Advanced_Artillery_Ammo)
            and (iRef->groupID() != EVEDB::invGroups::Advanced_Autocannon_Ammo)
            and (iRef->groupID() != EVEDB::invGroups::Advanced_Blaster_Ammo)
            and (iRef->groupID() != EVEDB::invGroups::Advanced_Railgun_Ammo)
            and (iRef->groupID() != EVEDB::invGroups::Hybrid_Ammo)) {
                throw CustomError ("Only ammunition and crystals may be placed into the ammo bay.");
                }
        } break;
        case flagHangar:
        case flagCorpHangar2:
        case flagCorpHangar3:
        case flagCorpHangar4:
        case flagCorpHangar5:
        case flagCorpHangar6:
        case flagCorpHangar7: {    //AttrCorporateHangarCapacity
            if (GetAttribute(AttrHasCorporateHangars) == 0) {
                throw CustomError ("Your %s has no corporate hangars.", name());
            }
        } break;
        default: {
            // {'FullPath': u'UI/Messages', 'messageID': 259450, 'label': u'ItemNotHardwareBody'}(u'{[item]itemname.name} cannot be fitted onto a ship. Only hardware modules can be fitted.', None, {u'{[item]itemname.name}': {'conditionalValues': [], 'variableType': 2, 'propertyName': 'name', 'args': 0, 'kwargs': {}, 'variableName': 'itemname'}})

            if ((iRef->categoryID() != EVEDB::invCategories::Module)
            and (iRef->categoryID() != EVEDB::invCategories::Charge)
            and (iRef->categoryID() != EVEDB::invCategories::Subsystem)) {
                throw CustomError ("%s cannot be fitted onto a ship. Only hardware modules may be fitted.", iRef->name());
            }

            if (IsModuleSlot(flag)) {
                // the subsystem errors are more telling than the generic group check below
                CheckSubSystemFit(flag, iRef);
                if (!Skill::FitModuleSkillCheck(iRef, pClient->GetChar())) {
                    throw CustomError ("You do not have the required skills to fit this %s.  Ref: ServerError 25163.", iRef->name());
                }
                if (!ValidateItemSpecifics(iRef)) {
                    throw CustomError ("Your ship cannot equip the %s.<br>The group '%s' is not allowed on your %s.", \
                            iRef->name(), sDataMgr.GetGroupName(iRef->groupID()), type().name().c_str());
                }
                if (iRef->categoryID() == EVEDB::invCategories::Charge) {
                    GenericModule* pMod(m_ModuleManager->GetModule(flag));
                    if (pMod != nullptr) {
                        // note:  this is also checked in client before calling Load()
                        //  "The {ammoGroupName} is size {sizeGot}, this {weaponGroupName} requires size {sizeWant}."
                        if (iRef->HasAttribute(AttrChargeSize) and pMod->HasAttribute(AttrChargeSize)
                        and (pMod->GetAttribute(AttrChargeSize) != iRef->GetAttribute(AttrChargeSize))) {
                            _log(MODULE__TRACE, "ShipItem::VerifyHoldType - Charge size %u for %s does not match Module size %u for %s.",\
                                    iRef->GetAttribute(AttrChargeSize).get_uint32(), iRef->name(),\
                                    pMod->GetAttribute(AttrChargeSize).get_uint32(), pMod->GetSelf()->name());
                            throw UserError ("BadAmmoSize")
                                    .AddGroupName ("ammoGroupName", iRef->groupID())
                                    .AddFormatValue ("sizeGot", new PyInt (iRef->GetAttribute(AttrChargeSize).get_int()))
                                    .AddGroupName ("weaponGroupName", pMod->groupID())
                                    .AddFormatValue ("sizeWant", new PyInt (pMod->GetAttribute(AttrChargeSize).get_int()));
                        }
                        //  "You cannot load {ammoGroup} into a {moduleName}, this module needs to be loaded with {group1}."
                        static const uint16 chargeGroups[] = { AttrChargeGroup1, AttrChargeGroup2, AttrChargeGroup3, AttrChargeGroup4, AttrChargeGroup5 };
                        bool fits(false);
                        for (auto attrID : chargeGroups)
                            if (pMod->HasAttribute(attrID) and (pMod->GetAttribute(attrID).get_uint32() == iRef->groupID()))
                                fits = true;
                        if (!fits)
                            throw UserError ("IncompatibleChargeGroup1")
                                    .AddGroupName ("ammoGroup", iRef->groupID())
                                    .AddFormatValue ("moduleName", new PyInt (pMod->typeID()))
                                    .AddGroupName ("group1", pMod->GetAttribute(AttrChargeGroup1).get_uint32());
                        // NOTE: Module Manager will check for actual room to load charges and make stack splits, or reject loading altogether
                    } else {
                        throw CustomError ("There is no module in %s.  Ref: ServerError 25162.", sDataMgr.GetFlagName(flag));
                    }
                }
            } else {
                sLog.Error("ShipItem::VerifyHoldType", "testing %s to add %u %s of cat %s has reached the end.",
                        sDataMgr.GetFlagName(flag), iRef->quantity(), iRef->name(), sDataMgr.GetCategoryName(iRef->categoryID()));
                throw CustomError ("Internal Server Error.  Ref: ServerError 25162.");
            }
        }
    }
}

// this one is called from GetAllInfo
PyDict* ShipItem::GetShipInfo()
{
    UpdateHeatAttributes();
    if (!pInventory->LoadContents())  {
        _log( INV__ERROR, "%s(%u): Failed to load contents for ShipGetInfo", name(), itemID());
        return nullptr;
    }

    //first populate the ship...
    Rsp_CommonGetInfo_Entry entry;
    if (!Populate( entry))
        return nullptr;

    PyDict* result = new PyDict();
    result->SetItem(new PyInt( itemID()), new PyObject("util.KeyVal", entry.Encode()));

    //get modules and charges
    std::vector<InventoryItemRef> equipped;
    pInventory->GetItemsByFlagRange(flagLowSlot0, flagHiSlot7, equipped);
    pInventory->GetItemsByFlagRange(flagRigSlot0, flagRigSlot2, equipped);
    pInventory->GetItemsByFlagRange(flagSubSystem0, flagSubSystem4, equipped);
    //encode each one...
    for (auto cur : equipped) {
        Rsp_CommonGetInfo_Entry entry2;
        if (cur->Populate(entry2)) {
            if (cur->categoryID() == EVEDB::invCategories::Charge) {
                PyTuple* tuple = new PyTuple(3);
                    tuple->SetItem(0, new PyInt(cur->locationID()));
                    tuple->SetItem(1, new PyInt(cur->flag()));
                    tuple->SetItem(2, new PyInt(cur->typeID()));
                result->SetItem(tuple, new PyObject("util.KeyVal", entry2.Encode()));
            } else {
                result->SetItem(new PyInt(cur->itemID()), new PyObject("util.KeyVal", entry2.Encode()));
            }
        } else {
            _log( SHIP__ERROR, "%s(%u): Failed to Populate() %s(%u) for ShipGetInfo", \
                        name(), itemID(), cur->name(), cur->itemID());
        }
    }

    if (is_log_enabled(SHIP__INFO)) {
        _log(SHIP__INFO, "ShipItem::GetShipInfo() decoded:");
        result->Dump(SHIP__INFO, "    ");
    }
    return result;
}

PyDict* ShipItem::GetShipState() {
    if (!pInventory->ContentsLoaded()) {
        if (!pInventory->LoadContents()) {
            _log(INV__ERROR, "%s(%u): Failed to load contents for GetShipState", name(), itemID());
            return nullptr;
        }
    }
    // Create new dictionary for shipState:
    PyDict *result = new PyDict();
    // Create entry for ShipItem itself:
    result->SetItem(new PyInt(itemID()), GetItemStatusRow());
    // Check for and Create entry for pilot:
    InventoryItemRef iRefPilot(nullptr);
    if (pInventory->GetSingleItemByFlag(flagPilot, iRefPilot))
        result->SetItem(new PyInt(iRefPilot->itemID()), iRefPilot->GetItemStatusRow());

    // Create entries for ALL modules, rigs, and subsystems present on ship:
    std::vector<InventoryItemRef> moduleList;
    m_ModuleManager->GetModuleListOfRefsAsc(moduleList);
    for (auto cur : moduleList)
        result->SetItem(new PyInt(cur->itemID()), cur->GetItemStatusRow());

    return result;
}

PyDict* ShipItem::GetChargeState() {
    if (!pInventory->ContentsLoaded()) {
        if (!pInventory->LoadContents()) {
            _log(INV__ERROR, "%s(%u): Failed to load contents for GetShipState", name(), itemID());
            return nullptr;
        }
    }

    /* get list of all charges loaded in modules */
    std::map< EVEItemFlags, InventoryItemRef > charges;
    m_ModuleManager->GetLoadedCharges(charges);

    PyDict *result = new PyDict();
    if (charges.empty())
        return result;

    // Create entries in "shipState" dictionary for loaded charges on ship:
    for (auto cur : charges)
        result->SetItem(new PyInt((uint16)cur.first), cur.second->GetChargeStatusRow(itemID()));

    return result;
}

PyList* ShipItem::ShipGetModuleList() {
    if (!pInventory->LoadContents()) {
        _log(INV__ERROR, "%s(%u): Failed to load contents for ShipGetModuleList", name(), itemID());
        return nullptr;
    }

    PyList* result = new PyList();
    // Create entries in "onslimitemchange" modules list for ALL modules, rigs, and subsystems present on ship:
    std::vector<InventoryItemRef> moduleList;
    m_ModuleManager->GetModuleListOfRefsAsc(moduleList);
    for (auto cur : moduleList) {
        PyTuple* module = new PyTuple(2);
        module->SetItem(0, new PyInt(cur->typeID()));
        module->SetItem(1, new PyInt(cur->itemID()));
        result->AddItem(module);
    }

    return result;
}

bool ShipItem::ValidateBoardShip(CharacterRef character) {
    bool result = false;
    EvilNumber skillTypeID(EvilZero);

    if (HasAttribute(AttrRequiredSkill1, skillTypeID)) {
        if (character->HasSkillTrainedToLevel( skillTypeID.get_uint32(), GetAttribute(AttrRequiredSkill1Level).get_uint32()))
            result = true;
        if (HasAttribute(AttrRequiredSkill2, skillTypeID)) {
            if (character->HasSkillTrainedToLevel( skillTypeID.get_uint32(), GetAttribute(AttrRequiredSkill2Level).get_uint32())) {
                result = true;
            } else {
                return false;
            }
            if (HasAttribute(AttrRequiredSkill3, skillTypeID)) {
                if (character->HasSkillTrainedToLevel( skillTypeID.get_uint32(), GetAttribute(AttrRequiredSkill3Level).get_uint32())) {
                    result = true;
                } else {
                    return false;
                }
                if (HasAttribute(AttrRequiredSkill4, skillTypeID)) {
                    if (character->HasSkillTrainedToLevel( skillTypeID.get_uint32(), GetAttribute(AttrRequiredSkill4Level).get_uint32())) {
                        result = true;
                    } else {
                        return false;
                    }
                    if (HasAttribute(AttrRequiredSkill5, skillTypeID)) {
                        if (character->HasSkillTrainedToLevel( skillTypeID.get_uint32(), GetAttribute(AttrRequiredSkill5Level).get_uint32())) {
                            result = true;
                        } else {
                            return false;
                        }
                        if (HasAttribute(AttrRequiredSkill6, skillTypeID)) {
                            if (character->HasSkillTrainedToLevel( skillTypeID.get_uint32(), GetAttribute(AttrRequiredSkill6Level).get_uint32())) {
                                result = true;
                            } else {
                                return false;
                            }
                        }
                    }
                }
            }
        }
    } else {
        result = true;
    }

    return result;
}

bool ShipItem::ValidateItemSpecifics(InventoryItemRef iRef)
{
    bool result(false);
    EvilNumber fitID(0);
    uint16 groupID(type().groupID());
    // If a ship group restriction is specified, the item must be able to fit to at least one ship group.
    _log(SHIP__TRACE, "ShipItem::ValidateItemSpecifics - Beginning the group validation for %s(%u):", iRef->name(), iRef->itemID());
    if (iRef->HasAttribute(AttrCanFitShipGroup1, fitID)) {
        if (fitID == groupID)
            result = true;
        if (iRef->HasAttribute(AttrCanFitShipGroup2, fitID)) {
            if (fitID == groupID)
                result = true;
            if (iRef->HasAttribute(AttrCanFitShipGroup3, fitID)) {
                if (fitID == groupID)
                    result = true;
                if (iRef->HasAttribute(AttrCanFitShipGroup4, fitID)) {
                    if (fitID == groupID)
                        result = true;
                    if (iRef->HasAttribute(AttrCanFitShipGroup5, fitID)) {
                        if (fitID == groupID)
                            result = true;
                        if (iRef->HasAttribute(AttrCanFitShipGroup6, fitID)) {
                            if (fitID == groupID)
                                result = true;
                            if (iRef->HasAttribute(AttrCanFitShipGroup7, fitID)) {
                                if (fitID == groupID)
                                    result = true;
                                if (iRef->HasAttribute(AttrCanFitShipGroup8, fitID)) {
                                    if (fitID == groupID)
                                        result = true;
                                }
                            }
                        }
                    }
                }
            }
        }
    } else {
        result = true;
    }

    _log(SHIP__TRACE, "ShipItem::ValidateItemSpecifics - Group Validation returning %s.", (result ? "true" : "false"));

    if (result) {
        _log(SHIP__TRACE, "ShipItem::ValidateItemSpecifics - Beginning the type validation for %s(%u):", iRef->name(), iRef->itemID());

        uint16 typeID(type().id());
        if (iRef->HasAttribute(AttrCanFitShipType1, fitID)) {
            result = false;
            if (fitID == typeID)
                result = true;
            if (iRef->HasAttribute(AttrCanFitShipType2, fitID)) {
                if (fitID == typeID)
                    result = true;
                if (iRef->HasAttribute(AttrCanFitShipType3, fitID)) {
                    if (fitID == typeID)
                        result = true;
                    if (iRef->HasAttribute(AttrCanFitShipType4, fitID)) {
                        if (fitID == typeID)
                            result = true;
                    }
                }
            }
        }

        _log(SHIP__TRACE, "ShipItem::ValidateItemSpecifics - Type Validation returning %s.", (result ? "true" : "false"));
    }

    return result;
}


/* DynamicSystemEntity representing ship object in space */
ShipSE::ShipSE(InventoryItemRef self, EVEServiceManager &services, SystemManager* pSystem, const FactionData& data)
: DynamicSystemEntity(self, services, pSystem),
m_shipRef(ShipItemRef::StaticCast(self)),
m_processTimerTick(SHIP_PROCESS_TICK_MS),   //5s
m_processTimer(SHIP_PROCESS_TICK_MS),
m_cloakCheckTimer(1000),
m_oldArmor(0),
m_oldShield(0),
m_oldScanRes(0),
m_oldInertia(0.0f),
m_oldTargetRange(0),
m_boosted(false),
m_podShipID(0),
m_allowFleetSMBUsage(false)
{
    m_warID = data.factionID;
    m_allyID = data.allianceID;
    m_corpID = data.corporationID;
    m_ownerID = data.ownerID;

    m_boost = BoostData();

    m_towerPass = "";
    m_processTimer.Start(m_processTimerTick);
    m_cloakCheckTimer.Start(1000);
    _log(SHIP__TRACE, "Created ShipSE %p for item %u", this, self->itemID());
}

void ShipSE::SetPassword(std::string pass)
{
    m_towerPass = pass;
    m_harmonic = EVEPOS::HarmonicFromPassword(pass);
    if (m_destiny == nullptr)
        return;
    // the client's SetBallHarmonic(ballID, harmonic, corpID, allianceID, field)
    std::vector<PyTuple*> updates;
    SetBallHarmonic sbh;
        sbh.itemID = GetID();
        sbh.harmonic = m_harmonic;
        sbh.corpID = m_corpID;
        sbh.allianceID = (IsAlliance(m_allyID) ? m_allyID : -1);
        sbh.field = 0;
    updates.push_back(sbh.Encode());
    m_destiny->SendDestinyUpdate(updates); //consumed
}

float ShipSE::CalculateRechargeRate(float Capacity, float Current, float RechargeTimeMS)
{
    return CalcRechargeRate(Capacity, Current, RechargeTimeMS);
}

void ShipSE::Process() {
    if (m_killed)
        return;
    /*  Enable base call to Process Targeting and Movement  */
    SystemEntity::Process();

    // check to see if this is an empty ship, and exit if so.
    //  we're not worried about recharge and modules for empty ships (segfaults)
    /** @todo m_self is NOT being populated for non-piloted ships...check later */
    if ((m_self.get() == nullptr) or (!m_self->HasPilot()))
        return;

    // anything within 2000m drops a cloaking device
    if (m_cloakCheckTimer.Check())
        if ((m_destiny != nullptr) and m_destiny->IsModuleCloaked() and !m_destiny->IsWarping())
            if (Ewar::FindDecloaker(this) != nullptr)
                Decloak();

    if (m_processTimer.Check()) {
        double profileStartTime(GetTimeUSeconds());
        // shield
        float Charge = m_self->GetAttribute(AttrShieldCharge).get_float();
        float Capacity = m_self->GetAttribute(AttrShieldCapacity).get_float();
        if (Charge < Capacity) {
            float newCharge = Charge + ((m_processTimerTick /1000) * CalculateRechargeRate(Capacity, Charge, m_self->GetAttribute(AttrShieldRechargeRate).get_float()));
            if (newCharge > Capacity) {
                newCharge = Capacity;
            } else if ((Capacity - newCharge) < 0.3) {
                newCharge = Capacity;
            }
            m_self->SetAttribute(AttrShieldCharge, newCharge);
            SendDamageStateChanged();
            _log(SHIP__RECHARGE, "ShipSE::Process(): %s(%u) - New Shield Charge: %f", m_self->GetPilot()->GetName(), m_self->itemID(), newCharge);
        }

        // cap
        Charge = m_self->GetAttribute(AttrCapacitorCharge).get_float();
        Capacity = m_self->GetAttribute(AttrCapacitorCapacity).get_float();
        if (Charge < Capacity) {
            float newCharge = Charge + ((m_processTimerTick /1000) * CalculateRechargeRate(Capacity, Charge, m_self->GetAttribute(AttrRechargeRate).get_float()));
            if (newCharge > Capacity) {
                newCharge = Capacity;
            } else if ((Capacity - newCharge) < 0.3) {
                newCharge = Capacity;
            }
            m_self->SetAttribute(AttrCapacitorCharge, newCharge);
            _log(SHIP__RECHARGE, "ShipSE::Process(): %s(%u) - New Cap Charge: %f", m_self->GetPilot()->GetName(), m_self->itemID(), newCharge);
        }
        // profile timer for the ship recharge shit
        if (sConfig.debug.UseProfiling)
            sProfiler.AddTime(Profile::ship, GetTimeUSeconds() - profileStartTime);

    }

    m_shipRef->ProcessModules();
}

void ShipSE::DamageRandModule(float chance) {
    if (chance == 0)
        return;
    if (chance > MakeRandomFloat())
        m_shipRef->DamageRandModule();
}

void ShipSE::PayInsurance() {
    // rookie ships and capsules have no insurance value
    double value(sMktMgr.GetInsuranceValue(m_self->typeID()));

    ShipDB::InsuranceRow row = ShipDB::InsuranceRow();
    bool insured(ShipDB::GetInsurance(m_self->itemID(), row));
    ShipDB::DeleteInsuranceByShipID(m_self->itemID());

    // the pilot's corp decides corp insurance (void when the ship dies in someone else's hands)
    uint32 pilotCorpID(0);
    if (m_shipRef->HasPilot())
        pilotCorpID = m_shipRef->GetPilot()->GetCorporationID();

    // an insured ship pays its coverage of the value at the time of the loss.  an invalid contract (not the owner any more,
    //  expired, no value) is reported and the ship gets the default payout like an uninsured one
    double fraction(sConfig.market.InsuranceDefaultPayout);
    bool isDefault(true);
    uint32 payeeID(m_ownerID);
    if (insured) {
        int8 reason(0);
        if (value <= 0) {
            reason = 3;
        } else if (row.endDate < GetFileTimeNow()) {
            reason = 2;
        } else if (row.isCorp ? (m_shipRef->HasPilot() ? (pilotCorpID != row.ownerID) : (m_ownerID != row.ownerID)) : (m_ownerID != row.ownerID)) {
            reason = 1;
        }

        if (reason == 0) {
            fraction = row.fraction;
            isDefault = false;
            payeeID = row.ownerID;
        } else {
            // Notifications/bodyInsuranceInvalid
            PyDict* data = new PyDict();
                data->SetItemString("shipID", new PyInt(m_self->itemID()));
                data->SetItemString("typeID", new PyInt(m_self->typeID()));
                data->SetItemString("startDate", new PyLong(row.startDate));
                data->SetItemString("endDate", new PyLong(row.endDate));
                data->SetItemString("reason", new PyInt(reason));
                data->SetItemString("ownerID", new PyInt(m_ownerID));
            if (row.isCorp) {
                NotificationMgr::SendToCorp(row.ownerID, Corp::Role::Accountant | Corp::Role::JuniorAccountant, Notify::Types::InsuranceInvalidated, corpSCC, data);
            } else if (IsCharacterID(row.ownerID)) {
                NotificationMgr::Send(row.ownerID, Notify::Types::InsuranceInvalidated, corpSCC, data);
            } else {
                PyDecRef(data);
            }
        }
    }

    double payout(std::round(value * fraction * 100.0) / 100.0);
    if ((payout <= 0) or (!IsCharacterID(payeeID) and !IsPlayerCorp(payeeID)))
        return;

    std::string reason = "Insurance payment for loss of the ship ";
    reason += m_self->itemName();
    AccountService::TransferFunds(
        corpSCC,
        payeeID,
        payout,
        reason,
        Journal::EntryType::Insurance,
        m_self->typeID()
    );

    // the payout notification (Notifications/bodyInsurancePayout).  'payout' set adds the default payout text
    PyDict* data = new PyDict();
        data->SetItemString("itemID", new PyInt(m_self->itemID()));
        data->SetItemString("amount", new PyFloat(payout));
        data->SetItemString("payout", new PyInt(isDefault ? 1 : 0));
    if (IsPlayerCorp(payeeID)) {
        NotificationMgr::SendToCorp(payeeID, Corp::Role::Accountant | Corp::Role::JuniorAccountant, Notify::Types::InsurancePayout, corpSCC, data);
    } else {
        NotificationMgr::Send(payeeID, Notify::Types::InsurancePayout, corpSCC, data);
    }
}

void ShipSE::ResetShipSystemMgr(SystemManager* pSystem) {
    m_system = pSystem;
}

void ShipSE::SetPilot(Client* pClient) {
    m_self->SetPlayer(pClient);
    if (pClient == nullptr) {
        m_allyID = 0;
        m_corpID = 0;
        return;
    }
    // set shipSE data
    m_allyID = pClient->GetAllianceID();
    m_corpID = pClient->GetCorporationID();
}

void ShipSE::RefreshOwnerIDs()
{
    Client* pPilot(GetPilot());
    if (pPilot == nullptr)
        return;
    if ((m_allyID == pPilot->GetAllianceID()) and (m_corpID == (uint32)pPilot->GetCorporationID()) and (m_warID == pPilot->GetWarFactionID()))
        return;
    m_allyID = pPilot->GetAllianceID();
    m_corpID = pPilot->GetCorporationID();
    m_warID = pPilot->GetWarFactionID();
    if (SysBubble() == nullptr)
        return;
    // the overview, brackets and war flags read corpID/allianceID/warFactionID from the slim item
    PyTuple* slimData = new PyTuple(2);
        slimData->SetItem(0, new PyLong(GetID()));
        slimData->SetItem(1, new PyObject("foo.SlimItem", MakeSlimItem()));
    PyTuple* itemData = new PyTuple(2);
        itemData->SetItem(0, new PyString("OnSlimItemChange"));
        itemData->SetItem(1, slimData);
    SysBubble()->BubblecastDestinyUpdate(&itemData, "OnSlimItemChange");
}

bool ShipSE::IsInvul()
{
    if (m_shipRef->HasPilot())
        return m_shipRef->GetPilot()->IsInvul();
    return false;
}

bool ShipSE::IsLogin()
{
    if (m_shipRef->HasPilot())
        return m_shipRef->GetPilot()->IsLogin();
    return false;
}

void ShipSE::Dock() {
    // links don't reach into stations
    RemoveLinkBonuses();
    if (m_targMgr != nullptr) {
        m_targMgr->ClearModules();
        m_targMgr->ClearAllTargets(false);
        //m_targMgr->OnTarget(nullptr, TargMgr::Mode::Clear, TargMgr::Msg::Docking);
    }

    m_shipRef->Dock();
}

void ShipSE::Jump(bool showCloak) {
    if (m_targMgr != nullptr) {
        m_targMgr->ClearModules();
        m_targMgr->ClearAllTargets(false);
        //m_targMgr->OnTarget(nullptr, TargMgr::Mode::Clear, TargMgr::Msg::Jumping);
    }

    m_shipRef->Jump();
    m_destiny->Jump(showCloak);
}

void ShipSE::Warp() {
    if (m_targMgr != nullptr)
        m_targMgr->ClearModules();

    m_shipRef->Warp();
}

void ShipSE::Decloak(const char* msgKey/*nullptr*/)
{
    if ((m_destiny == nullptr) or !m_destiny->IsModuleCloaked())
        return;
    GenericModule* pMod(m_shipRef->GetModuleManager()->GetActiveModuleByGroup(EVEDB::invGroups::Cloaking_Device));
    if (pMod == nullptr) {
        // shouldn't happen.  drop the cloak anyway
        m_destiny->UnCloak(Destiny::Cloak::Module);
        return;
    }
    pMod->Deactivate();
    if ((msgKey != nullptr) and HasPilot())
        GetPilot()->SendRemoteMessage(msgKey);
}

void ShipSE::RemoveTarget(SystemEntity* pSE) {
    // target has been unlocked
    m_shipRef->GetModuleManager()->RemoveTarget(pSE);

    m_targMgr->ClearTarget(pSE);
    //m_targMgr->OnTarget(pSE, TargMgr::Mode::Lost, TargMgr::Msg::StoppedTargeting);
}

void ShipSE::EncodeDestiny( Buffer& into) {
    using namespace Destiny;

    uint8 mode = m_destiny->GetState(); //Ball::Mode::STOP;
/*
    NameStruct name;
        name.name = GetName();
        name.name_len = sizeof(name.name);
  */
    BallHeader head = BallHeader();
        head.entityID = GetID();
        head.mode = mode;
        head.radius = GetRadius();
        head.posX = x();
        head.posY = y();
        head.posZ = z();
        head.flags = Ball::Flag::IsFree | (m_destiny->IsMassive() ? Ball::Flag::IsMassive : 0);
        if (m_self->HasPilot())
            head.flags |= Ball::Flag::IsInteractive;
    into.Append( head);
    MassSector mass = MassSector();
        mass.mass = m_destiny->GetMass();
        // a gate/login (session) cloak only hides the ball from everyone else;  it never reaches the pilot's own client
        //  (SendCloakBall(), UnCloak()), so its ball must not come in flagged cloaked either:  nothing would ever clear it
        mass.cloak = ((m_destiny->IsCloaked() and (m_destiny->GetCloakMode() != Destiny::Cloak::Session)) ? 1 : 0);
        mass.harmonic = m_harmonic;
        mass.corporationID = m_corpID;
        mass.allianceID = (IsAlliance(m_allyID) ? m_allyID : -1);
    into.Append( mass);
    // movement data and mode-specific part are written by destiny
    m_destiny->EncodeDestiny( into );

    std::string modeStr = "Goto";
    switch (mode) {
        case 1: modeStr = "Follow"; break;
        case 2: modeStr = "Stop"; break;
        case 3: modeStr = "Warp"; break;
        case 4: modeStr = "Orbit"; break;
        case 5: modeStr = "Missile"; break;
        case 6: modeStr = "Mushroom"; break;
        case 7: modeStr = "Boid"; break;
        case 8: modeStr = "Troll"; break;
        case 9: modeStr = "Miniball"; break;
        case 10: modeStr = "Field"; break;
        case 11: modeStr = "Rigid"; break;
        case 12: modeStr = "Formation"; break;
    }

    _log(SE__DESTINY, "ShipSE::EncodeDestiny(): %s - id:%lli, mode:%s, flags:0x%X, Vel:%.1f, %.1f, %.1f", \
            GetName(), head.entityID, modeStr.c_str(), head.flags, m_destiny->GetVelocity().x, m_destiny->GetVelocity().y, m_destiny->GetVelocity().z);
}

void ShipSE::MakeDamageState(DoDestinyDamageState &into) {
    into.shield = (m_self->GetAttribute(AttrShieldCharge).get_float() / m_self->GetAttribute(AttrShieldCapacity).get_float());
    into.recharge = m_self->GetAttribute(AttrShieldRechargeRate).get_float() + 7;
    into.timestamp = GetFileTimeNow();
    into.armor = 1.0 - (m_self->GetAttribute(AttrArmorDamage).get_float() / m_self->GetAttribute(AttrArmorHP).get_float());
    into.structure = 1.0 - (m_self->GetAttribute(AttrDamage).get_float() / m_self->GetAttribute(AttrHP).get_float());
}

PyDict* ShipSE::MakeSlimItem() {
    _log(SE__SLIMITEM, "MakeSlimItem for Ship %s(%u)", m_self->name(), m_self->itemID());
    PyDict *slim = new PyDict();
        slim->SetItemString("itemID",               new PyLong(m_self->itemID()));
        slim->SetItemString("typeID",               new PyInt(m_self->typeID()));
        slim->SetItemString("name",                 new PyString(m_self->itemName()));
        slim->SetItemString("ownerID",              new PyInt(m_ownerID));
        slim->SetItemString("charID",               new PyInt(m_self->GetPilot() ? m_self->GetPilot()->GetCharacterID() : 0));
        slim->SetItemString("corpID",           IsCorp(m_corpID) ? new PyInt(m_corpID) : PyStatic.NewNone());
        slim->SetItemString("allianceID",       IsAlliance(m_allyID) ? new PyInt(m_allyID) : PyStatic.NewNone());
        slim->SetItemString("warFactionID",     IsFaction(m_warID) ? new PyInt(m_warID) : PyStatic.NewNone());
        slim->SetItemString("bounty",               new PyFloat(m_self->GetPilot() ? m_self->GetPilot()->GetBounty() : 0));
        slim->SetItemString("securityStatus",       new PyFloat(m_self->GetPilot() ? m_self->GetPilot()->GetSecurityRating() : 0.0));
    if (m_self->typeID() == itemTypeCapsule) {
        slim->SetItemString("launcherID",           new PyInt(m_podShipID));
        return slim;
    } else {
        slim->SetItemString("categoryID",           new PyInt(m_self->categoryID()));
        slim->SetItemString("groupID",              new PyInt(m_self->groupID()));
    }

    //encode the hiSlot and Subsystem modules list ONLY.  the client builds a T3's model from the subsystems in here
    std::vector<InventoryItemRef> items;
    m_self->GetMyInventory()->GetItemsByFlagRange(flagHiSlot0, flagHiSlot7, items);
    m_self->GetMyInventory()->GetItemsByFlagRange(flagSubSystem0, flagSubSystem7, items);
    if (!items.empty()) {
        PyList *list = new PyList();
        for (auto cur : items)
            if (cur->categoryID() != EVEDB::invCategories::Charge)
                list->AddItem(new_tuple(cur->itemID(), cur->typeID()));

        slim->SetItemString("modules", list );
    }

    if (is_log_enabled(DESTINY__DEBUG)) {
        _log( DESTINY__DEBUG, "ShipSE::MakeSlimItem() - %s(%u)", GetName(), GetID());
        slim->Dump(DESTINY__DEBUG, "     ");
    }

    return slim;
}

void ShipSE::ClearBoostData()
{
    m_oldArmor       = 0;
    m_oldShield      = 0;
    m_oldScanRes     = 0;
    m_oldInertia     = 0.0f;
    m_oldTargetRange = 0;

    m_boost = BoostData();
    m_boosted = false;
}

void ShipSE::ApplyLinkBonuses(const LinkMap& wanted)
{
    if (wanted.empty() and m_linkMults.empty())
        return;
    std::set<std::pair<uint32, uint16>> keys;
    for (auto& cur : wanted)
        keys.insert(cur.first);
    for (auto& cur : m_linkMults)
        keys.insert(cur.first);

    for (auto& key : keys) {
        LinkMap::const_iterator wItr = wanted.find(key);
        LinkMap::iterator oItr = m_linkMults.find(key);
        float newMult(wItr == wanted.end() ? 1.0f : wItr->second);
        float oldMult(oItr == m_linkMults.end() ? 1.0f : oItr->second);
        if (std::fabs(newMult - oldMult) < 0.00001f)
            continue;
        InventoryItemRef iRef(nullptr);
        if (key.first == m_shipRef->itemID()) {
            iRef = m_shipRef;
        } else {
            GenericModule* pMod(m_shipRef->GetModuleManager()->GetModule(key.first));
            if (pMod != nullptr)
                iRef = pMod->GetSelf();
        }
        if ((iRef.get() != nullptr) and (oldMult != 0.0f)) {
            float value(iRef->GetAttribute(key.second).get_float());
            iRef->SetAttribute(key.second, value / oldMult * newMult);
            _log(FLEET__TRACE, "ShipSE::ApplyLinkBonuses() - %s(%u) attr %u: %.4f -> %.4f (x%.4f)", iRef->name(), iRef->itemID(), key.second, value, value / oldMult * newMult, newMult);
        }
        if ((iRef.get() == nullptr) or (newMult == 1.0f)) {
            if (oItr != m_linkMults.end())
                m_linkMults.erase(oItr);
        } else {
            m_linkMults[key] = newMult;
        }
    }
}

void ShipSE::RemoveBoost()
{
    _log( FLEET__TRACE, "ShipSE::RemoveBoost() - %s(%u)", GetName(), GetID());

    m_shipRef->SetAttribute(AttrArmorHP, m_oldArmor);
    m_shipRef->SetAttribute(AttrInetia, m_oldInertia);
    m_shipRef->SetAttribute(AttrShieldCapacity, m_oldShield);
    m_shipRef->SetAttribute(AttrScanResolution, m_oldScanRes);
    m_shipRef->SetAttribute(AttrMaxTargetRange, m_oldTargetRange);

    m_destiny->UpdateShipVariables();

    ClearBoostData();
}

void ShipSE::ApplyBoost(BoostData& bData)
{
    // note:  mining boost applied in mining module code

    // remove existing boost
    if (m_boosted)
        RemoveBoost();

    _log( FLEET__TRACE, "ShipSE::ApplyBoost() - %s(%u)", GetName(), GetID());

    m_boost             = bData;
    m_oldArmor          = m_shipRef->GetAttribute(AttrArmorHP).get_uint32();
    m_oldInertia        = m_shipRef->GetAttribute(AttrInetia).get_float();
    m_oldShield         = m_shipRef->GetAttribute(AttrShieldCapacity).get_uint32();
    m_oldScanRes        = m_shipRef->GetAttribute(AttrScanResolution).get_uint32();
    m_oldTargetRange    = m_shipRef->GetAttribute(AttrMaxTargetRange).get_uint32();

    uint16 armorHP      = m_oldArmor       * (1.0f + (0.02f * m_boost.armored)); // 2% increase/level
    uint16 shieldHP     = m_oldShield      * (1.0f + (0.02f * m_boost.siege));// 2% increase/level
    uint16 scanRes      = m_oldScanRes     * (1.0f + (0.02f * m_boost.leader));// 2% increase/level
    uint32 targRange    = m_oldTargetRange * (1.0f + (0.02f * m_boost.info));// 2% increase/level
    float inertia       = m_oldInertia     * (1.0f - (0.02f * m_boost.skirmish));// 2% decrease/level

    m_shipRef->SetAttribute(AttrInetia, inertia);   // lower inertia = lower agility = faster ship
    m_shipRef->SetAttribute(AttrArmorHP, armorHP);
    m_shipRef->SetAttribute(AttrScanResolution, scanRes);  // higher scanRes = faster targeting
    m_shipRef->SetAttribute(AttrShieldCapacity, shieldHP);
    m_shipRef->SetAttribute(AttrMaxTargetRange, targRange);

    m_destiny->UpdateShipVariables();

    m_boosted = true;
}
//{'FullPath': u'UI/Messages', 'messageID': 257802, 'label': u'DronesDroppedBecauseOfBandwidthModificationBody'}(u'The drone control bandwidth of your ship has been modified causing you to lose the ability to control some drones.', None, None)

/* launches one drone (a single item, not a stack) just outside the hull, under this ship's control.
 *  limits (drone count, bandwidth) are checked by the caller
 */
DroneSE* ShipSE::LaunchDrone(InventoryItemRef dRef) {
    Client* pClient(GetPilot());
    Character* pChar(pClient->GetChar().get());
    _log(DRONE__TRACE, "ShipSE::LaunchDrone() - %s: Launching drone %u", pChar->name(), dRef->itemID());

    dRef->Move(GetLocationID(), flagNone, true);
    dRef->ChangeSingleton(true, true);

    GPoint position(GetPosition());
    position.MakeRandomPointOnSphere(GetRadius() + dRef->type().radius() + MakeRandomFloat(100.0, 400.0));
    dRef->SetPosition(position);

    //now we create an SE to represent it.
    FactionData data = FactionData();
        data.allianceID = pChar->allianceID();
        data.corporationID = pChar->corporationID();
        data.factionID = pChar->warFactionID();
        data.ownerID = pChar->itemID();
    DroneSE* pDrone = new DroneSE(dRef, m_services, m_system, data);
    m_system->AddEntity(pDrone);
    pDrone->SetController(this);
    return pDrone;
}

