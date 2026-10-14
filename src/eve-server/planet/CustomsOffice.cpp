
/**
 * @name CustomsOffice.h
 *   Class for Customs Offices.
 *
 * @Author:         Allan
 * @date:   14 July 2019
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
#include "EVE_Customs.h"
#include "EVEServerConfig.h"
#include "StaticDataMgr.h"
#include "manufacturing/Blueprint.h"
#include "map/MapDB.h"
#include "math/Trig.h"
#include "packets/Planet.h"
#include "planet/CustomsOffice.h"
#include "planet/Planet.h"
#include "planet/PlanetDataMgr.h"
#include "planet/PlanetDB.h"
#include "pos/Structure.h"
#include "system/Container.h"
#include "system/CrimeWatch.h"
#include "system/Damage.h"
#include "system/SystemBubble.h"
#include "system/SystemManager.h"
#include "system/cosmicMgrs/AnomalyMgr.h"


CustomsSE::CustomsSE(StructureItemRef sRef, EVEServiceManager &services, SystemManager* system, const FactionData& data)
: ObjectSystemEntity(sRef, services, system),
m_system(system),
m_reinforceArmed(true)
{
    m_warID = data.factionID;
    m_allyID = data.allianceID;
    m_corpID = data.corporationID;
    m_ownerID = data.ownerID;

    // zero-init data
    m_cData = EVEPOS::CustomsData();
    m_oData = EVEPOS::OrbitalData();

    m_oData.planetID = atoi(m_self->customInfo().c_str());
    SystemEntity* pPlanet(m_oData.planetID ? m_system->GetPlanet(m_oData.planetID) : nullptr);
    m_planetSE = (pPlanet == nullptr ? nullptr : pPlanet->GetPlanetSE());

    sRef->SetMySE(this);

    _log(SE__DEBUG, "Created CustomsSE for item %s (%u).", sRef->name(), sRef->itemID());
}

void CustomsSE::Init()
{
    // load data.
    m_cData.itemID = m_self->itemID();

    // pull saved data from db
    if (!m_db.GetCustomsData(m_cData, m_oData)) {
        _log(POS__MESSAGE, "CustomsSE::Init() - %s(%u) has no saved data.  Initializing default set.", m_self->name(), m_cData.itemID);
        InitData();
        m_db.SaveCustomsData(m_cData, m_oData);
    }

    m_self->SetAttribute(AttrIsGlobal, EvilOne, false);

    // this should be based on state/status
    m_self->SetFlag(flagStructureActive);
}

void CustomsSE::InitData()
{
    // init all data.
    //  a launched gantry is unanchored until it is anchored at a planet.  customs offices are anchored (the client's
    //  configure and transfer menus need that state)
    m_cData.state = (IsGantry() ? EVEPOS::EntityState::Unanchored : EVEPOS::EntityState::Anchored);
    m_cData.timestamp = 0;
    m_cData.status = EVEPOS::StructureState::Online;
    m_cData.allowAlliance = false;
    m_cData.allowStandings  = false;
    m_cData.ownerID = m_self->ownerID();
    m_cData.selectedHour = 0;
    m_cData.standingValue = EVEPOS::StandingValues::StandingNeutral;
    m_oData.level = 1;   //{1-CUSTOMSOFFICE_SPACEPORT, 2-CUSTOMSOFFICE_SPACEELEVATOR}   this is for display model
    m_oData.standingOwnerID = m_cData.ownerID;

    // Crucible 1.0.1:  a new office charges 5% to everyone it lets in (only its corporation until configured)
    std::map<uint8, float> taxRateValues;
        taxRateValues[EVEPOS::TaxValues::Corp]              = 0.05f;
        taxRateValues[EVEPOS::TaxValues::Alliance]          = 0.05f;
        taxRateValues[EVEPOS::TaxValues::StandingHorrible]  = 0.05f;
        taxRateValues[EVEPOS::TaxValues::StandingBad]       = 0.05f;
        taxRateValues[EVEPOS::TaxValues::StandingNeutral]   = 0.05f;
        taxRateValues[EVEPOS::TaxValues::StandingGood]      = 0.05f;
        taxRateValues[EVEPOS::TaxValues::StandingHigh]      = 0.05f;
    m_cData.taxRateValues = taxRateValues;

    UpdateRotation();
}

void CustomsSE::UpdateRotation()
{
    /* dunRotation = yaw, pitch, roll:  the office faces its planet
     *   yaw is rotation on y axis [-180/180]  (+yaw is counterclockwise, +x starting at z 0)
     *   pitch is rotation on x axis [-90/90]  (+pitch is downward, -y from 0)
     *   roll is rotation on z axis [-180/180]
     */
    GPoint pos(m_self->position());
    GPoint targ(m_planetSE == nullptr ? pos : m_planetSE->GetPosition());
    float z = targ.z - pos.z;   // rise on z axis
    float x = targ.x - pos.x;    // run on x axis
    float y = targ.y - pos.y;   // rise on y axis
    float yaw = atan2(x, z);  // rad from position to target on xz plane
    float hyp = sqrt(pow(z, 2) + pow(x, 2));   // run on y plane
    float pitch = atan2(y, hyp);  // rad from position to target on hy plane
    if (y > 0)
        pitch *= -1;

    // convert from radians to degrees
    m_oData.rotation.x = EvE::Trig::Rad2Deg(yaw);
    m_oData.rotation.y = EvE::Trig::Rad2Deg(pitch);
    m_oData.rotation.z = 0;
}

void CustomsSE::SetPlanet(uint32 planetID)
{
    m_oData.planetID = planetID;
    SystemEntity* pPlanet(planetID ? m_system->GetPlanet(planetID) : nullptr);
    m_planetSE = (pPlanet == nullptr ? nullptr : pPlanet->GetPlanetSE());
}

void CustomsSE::SaveState()
{
    m_cData.ownerID = m_ownerID;
    m_db.UpdateCustomsData(m_cData, m_oData);
}

void CustomsSE::Process() {
    /* called by EntityList::Process on every loop */
    /*  Enable base call to Process Targeting and Movement  */
    SystemEntity::Process();
}

/* ----------------------------------------------------------------------------------------------------------------
 *  orbital states
 */

void CustomsSE::CheckCorpAccess(Client* pClient, int64 role)
{
    // the client offers these to the owning corporation (menusvc checkIsMyCorps).  the starbase equipment role, as for starbases
    //  pClient nullptr is a GM command (GMChangeState)
    if (pClient == nullptr)
        return;
    if (pClient->GetCorporationID() != m_ownerID)
        throw CustomError ("Your corporation does not own this %s.", m_self->name());
    if (role and ((pClient->GetCorpRole() & role) == 0))
        throw CustomError ("You need the Config Starbase Equipment role to do that.");
    SystemManager* pSystem(pClient->SystemMgr());
    if (!pClient->IsInSpace() or (pSystem != m_system))
        throw CustomError ("You need to be in space in the same system as the %s.", m_self->name());
}

void CustomsSE::AnchorOrbital(Client* pClient)
{
    if (!IsGantry())
        throw CustomError ("Only a Customs Office Gantry can be anchored.");
    if (m_cData.state != EVEPOS::EntityState::Unanchored)
        throw CustomError ("The %s is already anchored.", m_self->name());
    CheckCorpAccess(pClient, Corp::Role::EquipmentConfig);
    // high-sec planets keep CONCORD's offices
    if (!CustomsRules::GantryAllowed(CrimeWatch::SecurityClass(m_system->GetSystemSecurityRating()) == Crime::SecClass::High))
        throw CustomError ("A %s can't be anchored in high security space.", m_self->name());

    // the planet closest to the gantry, no further than PIGantryMaxDistanceKm from its surface, without an office
    uint32 planetID(m_system->GetClosestPlanetID(GetPosition()));
    SystemEntity* pPlanet(planetID ? m_system->GetPlanet(planetID) : nullptr);
    if ((pPlanet == nullptr) or (pPlanet->GetPlanetSE() == nullptr))
        throw CustomError ("There is no planet near enough to anchor the %s.", m_self->name());
    double distance(GVector(GetPosition(), pPlanet->GetPosition()).length() - pPlanet->GetRadius());
    if (distance > sConfig.cosmic.PIGantryMaxDistanceKm * 1000.0)
        throw CustomError ("The %s must be within %.0f km of a planet to anchor.", m_self->name(), sConfig.cosmic.PIGantryMaxDistanceKm);
    if (pPlanet->GetPlanetSE()->GetCustomsOffice() != nullptr)
        throw CustomError ("%s already has a customs office.", pPlanet->GetName());

    SetPlanet(planetID);
    m_self->SetCustomInfo(itoa(planetID));
    m_self->SaveItem();
    m_planetSE->SetCustomsOffice(this);
    UpdateRotation();

    m_cData.state = EVEPOS::EntityState::Anchoring;
    m_cData.timestamp = GetFileTimeNow() + (int64)m_self->GetAttribute(AttrAnchoringDelay).get_float() * EvE::Time::Second / 1000;
    SaveState();
    SendSlimUpdate();
    m_destiny->SendSpecialEffect(m_self->itemID(), m_self->itemID(), m_self->typeID(), 0, 0, "effects.AnchorDrop", 0, 1, 1, -1, 0);
    _log(POS__MESSAGE, "CustomsSE::AnchorOrbital() - %s anchoring %s(%u) at %s", (pClient == nullptr ? "GM" : pClient->GetName()), GetName(), GetID(), pPlanet->GetName());
}

void CustomsSE::UnanchorOrbital(Client* pClient)
{
    if (!IsGantry())
        throw CustomError ("Only a Customs Office Gantry can be unanchored.");
    if (m_cData.state != EVEPOS::EntityState::Anchored)
        throw CustomError ("The %s is not anchored.", m_self->name());
    CheckCorpAccess(pClient, Corp::Role::EquipmentConfig);

    m_cData.state = EVEPOS::EntityState::Unanchoring;
    m_cData.timestamp = GetFileTimeNow() + (int64)m_self->GetAttribute(AttrUnanchoringDelay).get_float() * EvE::Time::Second / 1000;
    SaveState();
    SendSlimUpdate();
    m_destiny->SendSpecialEffect(m_self->itemID(), m_self->itemID(), m_self->typeID(), 0, 0, "effects.AnchorLift", 0, 1, 1, -1, 0);
}

void CustomsSE::OnlineOrbital(Client* pClient)
{
    // "start upgrade" of the orbital material window:  the upgrade materials (invTypeMaterials of the construction type) go
    if (!IsGantry())
        throw CustomError ("Only a Customs Office Gantry can be upgraded.");
    if (m_cData.state != EVEPOS::EntityState::Anchored)
        throw CustomError ("The %s must be anchored to be upgraded.", m_self->name());
    CheckCorpAccess(pClient, Corp::Role::EquipmentConfig);
    uint16 constructionType = (uint16)m_self->GetAttribute(AttrconstructionType).get_int();
    std::vector<EvERam::RamMaterials> materials;
    sDataMgr.GetRamMaterials(constructionType, materials);
    if ((constructionType == 0) or materials.empty())
        throw CustomError ("The %s can't be upgraded.", m_self->name());

    Inventory* pInv(m_self->GetMyInventory());
    for (auto cur : materials)
        if (!pInv->ContainsTypeQtyByFlag(cur.materialTypeID, flagMaterialBay, cur.quantity))
            throw CustomError ("The upgrade hold needs %u %s.", cur.quantity, sPIDataMgr.GetProductName(cur.materialTypeID));
    for (auto cur : materials) {
        uint32 left = cur.quantity;
        std::vector<InventoryItemRef> items;
        pInv->GetItemsByFlag(flagMaterialBay, items);
        for (auto item : items) {
            if ((left == 0) or (item->typeID() != cur.materialTypeID))
                continue;
            if (item->quantity() > left) {
                item->AlterQuantity(-(int32)left, true);
                left = 0;
            } else {
                left -= item->quantity();
                item->SetQuantity(0, true);
            }
        }
    }

    m_cData.state = EVEPOS::EntityState::Onlining;
    m_cData.timestamp = GetFileTimeNow() + (int64)m_self->GetAttribute(AttrOnliningDelay).get_float() * EvE::Time::Second / 1000;
    SaveState();
    SendSlimUpdate();
    _log(POS__MESSAGE, "CustomsSE::OnlineOrbital() - %s started upgrading %s(%u)", pClient->GetName(), GetName(), GetID());
}

void CustomsSE::ProcessState()
{
    if (!IsInTimedState() or (m_cData.timestamp > GetFileTimeNow()))
        return;
    CompleteStateChange();
}

void CustomsSE::CompleteStateChange()
{
    using namespace EVEPOS::EntityState;
    switch (m_cData.state) {
        case Anchoring: {
            m_cData.state = Anchored;
        } break;
        case Onlining: {
            Upgrade();
            return;     // this entity is gone
        }
        case Unanchoring: {
            // free to scoop again, and the planet's office slot is free
            if (m_planetSE != nullptr)
                m_planetSE->SetCustomsOffice(nullptr);
            SetPlanet(0);
            m_self->SetCustomInfo("");
            m_self->SaveItem();
            m_cData.state = Unanchored;
        } break;
        case ShieldReinforce: {
            // vulnerable again (the client's bracket counts down to "Anchored")
            m_cData.state = Anchored;
            m_reinforceArmed = false;
        } break;
        default:
            return;
    }
    m_cData.timestamp = 0;
    SaveState();
    SendSlimUpdate();
}

bool CustomsSE::GMChangeState(StructureTiming::GMAction action)
{
    using namespace EVEPOS::EntityState;
    using StructureTiming::GMAction;
    // the upgrade that is running ends with the customs office:  this entity is gone after that
    if (m_cData.state == Onlining) {
        CompleteStateChange();
        return true;
    }
    bool done(false);
    if (IsInTimedState()) {
        CompleteStateChange();
        done = true;
    }
    switch (action) {
        case GMAction::Anchor:
        case GMAction::Online: {
            if (IsGantry() and (m_cData.state == Unanchored)) {
                AnchorOrbital(nullptr);
                CompleteStateChange();
                done = true;
            }
            // a gantry comes online as the customs office it builds (no upgrade materials).  this entity is gone after that
            if ((action == GMAction::Online) and IsGantry() and (m_cData.state == Anchored)) {
                Upgrade();
                return true;
            }
        } break;
        case GMAction::Unanchor: {
            if (IsGantry() and (m_cData.state == Anchored)) {
                UnanchorOrbital(nullptr);
                CompleteStateChange();
                done = true;
            }
        } break;
        default:
            // orbitals have no offline state
            break;
    }
    return done;
}

void CustomsSE::Upgrade()
{
    if (!IsGantry() or (m_planetSE == nullptr))
        return;
    uint16 constructionType = (uint16)m_self->GetAttribute(AttrconstructionType).get_int();
    if (constructionType == 0)
        constructionType = EVEDB::invTypes::PlanetaryCustomsOffice;
    PlanetSE* pPlanet(m_planetSE);
    uint32 planetID(m_oData.planetID);
    GPoint pos(GetPosition());

    ItemData idata(constructionType, m_ownerID, m_system->GetID(), flagNone, 1, itoa(planetID), false);
    StructureItemRef iRef = sItemFactory.SpawnStructure(idata);
    if (iRef.get() == nullptr) {
        _log(POS__ERROR, "CustomsSE::Upgrade() - unable to spawn the customs office for gantry %u", GetID());
        return;
    }
    iRef->SetPosition(pos);
    iRef->ChangeSingleton(true, false);
    iRef->SaveItem();

    FactionData data = FactionData();
        data.ownerID = m_ownerID;
        data.corporationID = m_corpID;
        data.allianceID = m_allyID;
        data.factionID = 0;
    CustomsSE* pCO = new CustomsSE(iRef, m_services, m_system, data);
    pCO->Init();
    pPlanet->SetCustomsOffice(pCO);
    PlanetDB::MarkCustomsOfficeSpawned(planetID);

    // the gantry goes
    _log(POS__MESSAGE, "CustomsSE::Upgrade() - gantry %u became customs office %u at %s", GetID(), iRef->itemID(), pPlanet->GetName());
    PosMgrDB::DeleteCustomsData(GetID());
    Delete();

    m_system->AddEntity(pCO);
    pCO->NotifyColonists();
    delete this;
}

void CustomsSE::NotifyColonists()
{
    if (m_oData.planetID == 0)
        return;
    DBQueryResult res;
    PlanetDB::GetCommandPinsForPlanet(m_oData.planetID, res);
    DBResultRow row;
    while (res.GetRow(row)) {
        Client* pClient(sEntityList.FindClientByCharID(row.GetUInt(2)));
        if (pClient == nullptr)
            continue;
        PyTuple* tuple = new PyTuple(2);
            tuple->SetItem(0, new PyInt(m_oData.planetID));
            tuple->SetItem(1, PyStatic.NewFalse());
        pClient->SendNotification("OnMajorPlanetStateUpdate", "clientID", &tuple);
    }
}

bool CustomsSE::CanBeAttacked()
{
    // Crucible:  CONCORD's high security offices are invulnerable, Interbus' and players' can be shot.  gantries only once anchored
    if (m_ownerID == corpCONCORD)
        return false;
    if (IsGantry() and ((m_cData.state == EVEPOS::EntityState::Unanchored) or (m_oData.planetID == 0)))
        return false;
    return !IsReinforced();
}

void CustomsSE::OnShieldDamaged()
{
    /* dev blog "Player-owned Customs Office" (18 Oct 2011):  reinforced at 25% shields, out "somewhere within the time of
     *  day specified by the owner, at least 24 hours after".  reinforcementDuration and reinforcementVariance of the type
     *  (86400 s, 3600 s on the Customs Office; Interbus offices have none and don't reinforce).  the reinforcement hour is UTC.
     */
    float capacity(m_self->GetAttribute(AttrShieldCapacity).get_float());
    if (capacity <= 0)
        return;
    float fraction(m_self->GetAttribute(AttrShieldCharge).get_float() / capacity);
    if (fraction > 0.25f) {
        m_reinforceArmed = true;
        return;
    }
    if (!m_reinforceArmed or IsGantry() or (m_cData.state != EVEPOS::EntityState::Anchored))
        return;
    if (!m_self->type().HasAttribute(AttrreinforcementDuration))
        return;
    int64 duration = (int64)m_self->GetAttribute(AttrreinforcementDuration).get_float();
    if (duration <= 0)
        return;
    int64 variance = (int64)m_self->GetAttribute(AttrreinforcementVariance).get_float();

    // the first time on the selected hour at least the reinforcement duration from now, give or take the variance
    int64 now = GetFileTimeNow();
    int64 earliest = now + duration * EvE::Time::Second;
    int64 dayStart = earliest - (earliest % EvE::Time::Day);
    int64 exit = dayStart + m_cData.selectedHour * EvE::Time::Hour;
    while (exit < earliest)
        exit += EvE::Time::Day;
    if (variance > 0)
        exit += MakeRandomInt(-variance, variance) * EvE::Time::Second;

    m_cData.state = EVEPOS::EntityState::ShieldReinforce;
    m_cData.timestamp = exit;
    m_reinforceArmed = false;
    SaveState();
    SendSlimUpdate();
    _log(POS__MESSAGE, "CustomsSE::OnShieldDamaged() - %s(%u) reinforced until %lli", GetName(), GetID(), exit);
}

PyRep* CustomsSE::GetSettingsInfo()
{   //self.selectedHour, self.taxRateValues, self.standingValue, self.allowAlliance, self.allowStandings = self.orbitalData
    /*
        self.taxRates = [util.KeyVal(key='corporation'),
         util.KeyVal(key='alliance'),
         util.KeyVal(key='standingHorrible', standing=const.contactHorribleStanding),
         util.KeyVal(key='standingBad', standing=const.contactBadStanding),
         util.KeyVal(key='standingNeutral', standing=const.contactNeutralStanding),
         util.KeyVal(key='standingGood', standing=const.contactGoodStanding),
         util.KeyVal(key='standingHigh', standing=const.contactHighStanding)]
         */
    PyDict* dict = new PyDict();
        dict->SetItemString("corporation", new PyFloat(m_cData.taxRateValues[EVEPOS::TaxValues::Corp]));
        dict->SetItemString("alliance", new PyFloat(m_cData.taxRateValues[EVEPOS::TaxValues::Alliance]));
        dict->SetItemString("standingHorrible", new PyFloat(m_cData.taxRateValues[EVEPOS::TaxValues::StandingHorrible]));
        dict->SetItemString("standingBad", new PyFloat(m_cData.taxRateValues[EVEPOS::TaxValues::StandingBad]));
        dict->SetItemString("standingNeutral", new PyFloat(m_cData.taxRateValues[EVEPOS::TaxValues::StandingNeutral]));
        dict->SetItemString("standingGood", new PyFloat(m_cData.taxRateValues[EVEPOS::TaxValues::StandingGood]));
        dict->SetItemString("standingHigh", new PyFloat(m_cData.taxRateValues[EVEPOS::TaxValues::StandingHigh]));
    PyTuple* tuple = new PyTuple(5);
        tuple->SetItem(0, new PyInt(m_cData.selectedHour));
        tuple->SetItem(1, new PyObject("util.KeyVal", dict));
        tuple->SetItem(2, new PyInt(m_cData.standingValue));    //const.contactHorribleStanding
        tuple->SetItem(3, new PyBool(m_cData.allowAlliance));
        tuple->SetItem(4, new PyBool(m_cData.allowStandings));
    return tuple;
}

void CustomsSE::UpdateSettings(int8 selectedHour, int8 standingValue, bool ally, bool standings, Call_TaxRateValuesDict& taxRateValues)
{
    m_cData.allowAlliance    = ally;
    m_cData.allowStandings   = standings;
    m_cData.selectedHour     = selectedHour;
    m_cData.standingValue    = standingValue;

    using namespace EVEPOS;
    m_cData.taxRateValues[TaxValues::Corp]              = taxRateValues.corporation;
    m_cData.taxRateValues[TaxValues::Alliance]          = taxRateValues.alliance;
    m_cData.taxRateValues[TaxValues::StandingHorrible]  = taxRateValues.standingHorrible;
    m_cData.taxRateValues[TaxValues::StandingBad]       = taxRateValues.standingBad;
    m_cData.taxRateValues[TaxValues::StandingNeutral]   = taxRateValues.standingNeutral;
    m_cData.taxRateValues[TaxValues::StandingGood]      = taxRateValues.standingGood;
    m_cData.taxRateValues[TaxValues::StandingHigh]      = taxRateValues.standingHigh;

    // save updates to db
    m_db.UpdateCustomsData(m_cData, m_oData);
}

float CustomsSE::GetTaxRate(Client* pClient)
{
    /* the rate this character pays here, -1 when they may not use the office.
     *  Crucible:  npc offices charge npcCustomsOfficeTaxRate of their type.  CONCORD's high security Customs Office
     *   has none and takes the dogma default (10%), Interbus' offices a little more (17% on the Interbus office).
     *   defaultCustomsOfficeTaxRate (5%) is a new player office's rate, not an npc one.
     *   player offices charge what their owner set for the corporation, the alliance or the character's standing with
     *   the owner, and only let in those (Player Owned Customs Office dev blog, 18 Oct 2011).  sovereignty plays no part
     */
    if (m_ownerID == corpCONCORD)
        return sPIDataMgr.GetTypeAttribute(EVEDB::invTypes::PlanetaryCustomsOffice, AttrnpcCustomsOfficeTaxRate) * sConfig.cosmic.PICONCORDTaxMultiplier;
    if (m_ownerID == corpInterbus)
        return sPIDataMgr.GetTypeAttribute(EVEDB::invTypes::InterbusCustomsOffice, AttrnpcCustomsOfficeTaxRate) * sConfig.cosmic.PIInterbusTaxMultiplier;
    if (!IsPlayerCorp(m_ownerID))
        return sPIDataMgr.GetTypeAttribute(m_self->typeID(), AttrnpcCustomsOfficeTaxRate);

    using namespace EVEPOS;
    CustomsRules::PlayerOffice office = CustomsRules::PlayerOffice();
        office.allowAlliance    = m_cData.allowAlliance;
        office.allowStandings   = m_cData.allowStandings;
        office.minStanding      = m_cData.standingValue;
        office.corporation      = m_cData.taxRateValues[TaxValues::Corp];
        office.alliance         = m_cData.taxRateValues[TaxValues::Alliance];
        office.standingHorrible = m_cData.taxRateValues[TaxValues::StandingHorrible];
        office.standingBad      = m_cData.taxRateValues[TaxValues::StandingBad];
        office.standingNeutral  = m_cData.taxRateValues[TaxValues::StandingNeutral];
        office.standingGood     = m_cData.taxRateValues[TaxValues::StandingGood];
        office.standingHigh     = m_cData.taxRateValues[TaxValues::StandingHigh];
    bool corpMember(pClient->GetCorporationID() == m_ownerID);
    bool allianceMember(IsAlliance(m_allyID) and (pClient->GetAllianceID() == m_allyID));
    // the standing lookups only when the standing rates decide
    int8 tier(CustomsRules::NeedsStanding(office, corpMember, allianceMember) ? GetStandingTier(pClient) : CustomsRules::StandingNeutral);
    return CustomsRules::PlayerOfficeRate(office, corpMember, allianceMember, tier);
}

int8 CustomsSE::GetStandingTier(Client* pClient)
{
    // the owner's contact standing towards the character, their corporation or their alliance (first found), then the owner
    //  alliance's.  no contact is neutral.  contact standings are the client's -10, -5, 0, 5, 10
    float standing = 0;
    bool found = false;
    std::vector<uint32> owners;
    owners.push_back(m_ownerID);
    if (IsAlliance(m_allyID))
        owners.push_back(m_allyID);
    for (auto owner : owners) {
        if (PosMgrDB::GetContactStanding(owner, pClient->GetCharacterID(), standing)
        or  PosMgrDB::GetContactStanding(owner, pClient->GetCorporationID(), standing)
        or  (IsAlliance(pClient->GetAllianceID()) and PosMgrDB::GetContactStanding(owner, pClient->GetAllianceID(), standing))) {
            found = true;
            break;
        }
    }
    return CustomsRules::StandingTier(found, standing);
}

bool CustomsSE::CanStore(uint16 typeID)
{
    // planetCommon.CanPutTypeInCustomsOffice
    uint8 categoryID = sPIDataMgr.GetCategoryID(typeID);
    if ((categoryID != EVEDB::invCategories::Commodity)
    and (categoryID != EVEDB::invCategories::PlanetaryResources)
    and (categoryID != EVEDB::invCategories::PlanetaryCommodities))
        return false;
    if ((categoryID == EVEDB::invCategories::Commodity) and (sPIDataMgr.GetGroupID(typeID) != EVEDB::invGroups::General))
        return false;
    return true;
}

void CustomsSE::ValidateAddItem(InventoryItemRef iRef)
{
    // a character's items going into the office's hangar:  planetary commodities only, 35000 m3 per character
    if (!CanStore(iRef->typeID())) {
        if (iRef->categoryID() == EVEDB::invCategories::Commodity)
            throw UserError("CannotPutMissionItemInCargolink").AddTypeName("typeName", iRef->typeID());
        throw CustomError ("You cannot put %s in a %s", iRef->name(), m_self->name());
    }
    double volume = iRef->type().volume() * iRef->quantity();
    double available = GetAvailableSpace(iRef->ownerID());
    if (iRef->locationID() == m_self->itemID())
        available += volume;    // moving within the office
    if (volume - available > 1e-5)
        throw UserError("NotEnoughSpace")
                .AddFormatValue("volume", new PyString(std::to_string(volume)))
                .AddFormatValue("available", new PyString(std::to_string(std::max(0.0, available))));
}

void CustomsSE::VerifyAddItem(InventoryItemRef iRef)
{
    // gantry material bays take the upgrade materials (planetary commodities too)
    if (!CanStore(iRef->typeID()))
        throw CustomError ("You cannot put %s in a %s", iRef->name(), m_self->name());
}

double CustomsSE::GetUsedSpace(uint32 ownerID)
{
    double used = 0;
    std::vector<InventoryItemRef> items;
    m_self->GetMyInventory()->GetItemsByFlag(flagHangar, items);
    for (auto cur : items)
        if (cur->ownerID() == ownerID)
            used += cur->type().volume() * cur->quantity();
    return used;
}

double CustomsSE::GetAvailableSpace(uint32 ownerID)
{
    return m_self->type().capacity() - GetUsedSpace(ownerID);
}

void CustomsSE::AddCommodity(uint32 ownerID, uint16 typeID, uint32 quantity)
{
    std::vector<InventoryItemRef> items;
    m_self->GetMyInventory()->GetItemsByFlag(flagHangar, items);
    for (auto cur : items)
        if ((cur->ownerID() == ownerID) and (cur->typeID() == typeID) and !cur->isSingleton()) {
            cur->AlterQuantity(quantity, true);
            return;
        }
    ItemData data(typeID, ownerID, locTemp, flagNone, quantity);
    InventoryItemRef iRef = sItemFactory.SpawnItem(data);
    if (iRef.get() == nullptr) {
        _log(POS__ERROR, "CustomsSE::AddCommodity() - unable to spawn %u of type %u for %u in %u", quantity, typeID, ownerID, m_self->itemID());
        return;
    }
    iRef->Move(m_self->itemID(), flagHangar, true);
}

void CustomsSE::GetEffectState(PyList& into)
{
    // not used yet - cannot use OnSpecialFX14 packet.
}

void CustomsSE::SendEffectUpdate(int16 effectID, bool active)
{
    GodmaEnvironment ge;
        ge.selfID = m_cData.itemID;
        ge.charID = m_ownerID;
        ge.shipID = m_cData.itemID;
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
        shipEff.startTime = shipEff.timeNow;    // do we need to adjust this?
        shipEff.duration = (active ? 0 : -1);
        shipEff.repeat = (active ? 1 : 0);
        shipEff.error = PyStatic.NewNone();
    PyList* events = new PyList();
        events->AddItem(shipEff.Encode());
    PyTuple* event = new PyTuple(1);
        event->SetItem(0, events);
    m_destiny->SendSingleDestinyEvent(&event);   // consumed
}

void CustomsSE::ChangeOwner(uint32 corpID, int32 allianceID)
{
    m_self->ChangeOwner(corpID, true);
    m_warID = 0;
    m_ownerID = corpID;
    m_corpID = corpID;
    m_allyID = allianceID;
    m_cData.ownerID = corpID;
    m_oData.standingOwnerID = corpID;
    SaveState();
    SendSlimUpdate();
    NotifyColonists();
}

void CustomsSE::SendSlimUpdate()
{
    if (m_destiny == nullptr)
        return;
    PyTuple* shipData = new PyTuple(2);
        shipData->SetItem(0,                            new PyLong(m_self->itemID()));
        shipData->SetItem(1,                            new PyObject("foo.SlimItem", MakeSlimItem()));
    PyTuple* sItem = new PyTuple(2);
        sItem->SetItem(0,                               new PyString("OnSlimItemChange"));
        sItem->SetItem(1,                               shipData);
    m_destiny->SendSingleDestinyUpdate(&sItem);   // consumed
}


void CustomsSE::EncodeDestiny( Buffer& into )
{
    using namespace Destiny;
    //const uint16 miniballsCount = GetMiniBalls();
    BallHeader head = BallHeader();
        head.entityID = m_cData.itemID;
        head.radius = GetRadius();
        head.posX = x();
        head.posY = y();
        head.posZ = z();
        head.mode = Ball::Mode::RIGID;
        head.flags = Ball::Flag::IsGlobal /*| Ball::Flag::IsMassive | HasMiniBalls*/;
    into.Append( head );

    RIGID_Struct main;
        main.formationID = 0xFF;
    into.Append( main );

    /* TODO  query and configure miniballs for entity
     * NOTE  MiniBalls are BROKEN!!!  DO NOT USE!
     *    into.Append( miniballsCount );
     *    MiniBall miniball;
     *    for (int16 i; i<miniballsCount; i++) {
     *        miniball.x = -7701.181;
     *        miniball.y = 8060.06;
     *        miniball.z = 27878.900;
     *        miniball.radius = 1639.241;
     *        into.Append( miniball );
     *        miniball.clear();
     *    }
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

    _log(SE__DESTINY, "CustomsSE::EncodeDestiny(): %s - id:%lli, mode:%u, flags:0x%X", GetName(), head.entityID, head.mode, head.flags);
}

PyDict *CustomsSE::MakeSlimItem() {
    _log(SE__SLIMITEM, "MakeSlimItem for CustomsSE %u", m_cData.itemID);
    _log(POS__SLIMITEM, "MakeSlimItem for CustomsSE %u", m_cData.itemID);
    /** @todo (Allan) *Timestamp will need to be set to time current state is started. */
    PyDict *slim = new PyDict();
    slim->SetItemString("name",                 new PyString(m_self->itemName()));
    slim->SetItemString("nameID",               PyStatic.NewNone());
    slim->SetItemString("itemID",               new PyLong(m_cData.itemID));
    slim->SetItemString("typeID",               new PyInt(m_self->typeID()));
    slim->SetItemString("ownerID",              new PyInt(m_ownerID));  //1000148 for interbus customs office (to be done on creation)
    slim->SetItemString("corpID",               IsCorp(m_corpID) ? new PyInt(m_corpID) : PyStatic.NewNone());
    slim->SetItemString("allianceID",           IsAlliance(m_allyID) ? new PyInt(m_allyID) : PyStatic.NewNone());
    slim->SetItemString("warFactionID",         IsFaction(m_warID) ? new PyInt(m_warID) : PyStatic.NewNone());
    slim->SetItemString("level",                new PyInt(m_oData.level));
    slim->SetItemString("orbitalTimestamp",     new PyLong(m_cData.timestamp));
    slim->SetItemString("planetID",             new PyInt(m_oData.planetID));  // planetID for this orbital
    slim->SetItemString("orbitalState",         new PyInt(m_cData.state));   // this needs to be ORBITAL state...not structure state
    PyTuple* tuple = new PyTuple(3);            // yaw, pitch, roll = getattr(slimItem, 'dunRotation', None)
        tuple->SetItem(0,                       new PyFloat(m_oData.rotation.x));
        tuple->SetItem(1,                       new PyFloat(m_oData.rotation.y));
        tuple->SetItem(2,                       new PyFloat(m_oData.rotation.z)); //MakeRandomFloat(-180, 180)
    slim->SetItemString("dunRotation", tuple);  // direction to planet
    //  dunno what these are...
    slim->SetItemString("orbitalHackerProgress",  m_oData.orbitalHackerProgress > 0 ? new PyFloat(m_oData.orbitalHackerProgress) : PyStatic.NewNone());  // packets show this as none if not value
    slim->SetItemString("orbitalHackerID",     m_oData.orbitalHackerID > 0 ? new PyInt(m_oData.orbitalHackerID) : PyStatic.NewNone());   // packets show this as none if not value

    if (is_log_enabled(POS__SLIMITEM)) {
        _log( POS__SLIMITEM, "CustomsSE::MakeSlimItem() - %s(%u)", GetName(), m_cData.itemID);
        slim->Dump(POS__SLIMITEM, "     ");
    }
    return slim;
}

bool CustomsSE::IsInvul()
{
    if (ObjectSystemEntity::IsInvul())
        return true;
    return !CanBeAttacked();
}

void CustomsSE::Killed(Damage &damage) {
    if ((m_bubble == nullptr) or (m_destiny == nullptr) or (m_system == nullptr))
        return; // make error here?

    // the planet's office slot is free (an Interbus office never comes back, players anchor their own)
    if ((m_planetSE != nullptr) and (m_planetSE->GetCustomsOffice() == this))
        m_planetSE->SetCustomsOffice(nullptr);
    PosMgrDB::DeleteCustomsData(m_self->itemID());
    NotifyColonists();

    uint32 killerID = 0;
    Client* pClient(nullptr);
    SystemEntity* killer = damage.srcSE;

    if (killer->HasPilot()) {
        pClient = killer->GetPilot();
        killerID = pClient->GetCharacterID();
    } else if (killer->IsDroneSE()) {
        pClient = sEntityList.FindClientByCharID( killer->GetSelf()->ownerID() );
        if (pClient == nullptr) {
            sLog.Error("CustomsSE::Killed()", "killer == IsDrone and pPlayer == nullptr");
        } else {
            killerID = pClient->GetCharacterID();
        }
    } else {
        killerID = killer->GetID();
    }

    std::stringstream blob;
    blob << "<items>";
    std::vector<InventoryItemRef> survivedItems;
    std::map<uint32, InventoryItemRef> deadShipInventory;
    deadShipInventory.clear();
    m_self->GetMyInventory()->GetInventoryMap(deadShipInventory);
    if (!deadShipInventory.empty()) {
        uint32 s = 0, d = 0, x = 0;
        for (auto cur : deadShipInventory) {
            d = 0;
            x = cur.second->quantity();
            s = (cur.second->isSingleton() ? 1 : 0);
            if (cur.second->categoryID() == EVEDB::invCategories::Blueprint) {
                // singleton for bpo = 1, bpc = 2.
                BlueprintRef bpRef = BlueprintRef::StaticCast(cur.second);
                s = (bpRef->copy() ? 2 : s);
            }
            blob << "<i t=" << cur.second->typeID() << " f=" << cur.second->flag() << " s=" << s ;
            // all items have 50% chance of drop, even from popped ship
            if (IsEven(MakeRandomInt(0, 100))) {
                // item survived.  check qty for drop
                if (x > 1) {
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
        data.finalCorporationID = killer->GetCorporationID();
        data.finalAllianceID = killer->GetAllianceID();
        data.finalFactionID = killer->GetWarFactionID();
        data.finalShipTypeID = killer->GetTypeID();
        data.finalWeaponTypeID = damage.weaponRef->typeID();
        data.finalSecurityStatus = 0;  /* fix this */
        data.finalDamageDone = damage.GetTotal();

        uint32 totalHP = m_self->GetAttribute(AttrHP).get_int();
            totalHP += m_self->GetAttribute(AttrArmorHP).get_int();
            totalHP += m_self->GetAttribute(AttrShieldCapacity).get_int();
        data.victimDamageTaken = totalHP;

        data.killBlob = blob.str().c_str();
        data.killTime = GetFileTimeNow();
        data.moonID = m_oData.planetID;    /* denotes moonID for POS/Structure kills */

    ServiceDB::SaveKillOrLoss(data);

    uint32 locationID = GetLocationID();
    //  log faction kill in dynamic data   -allan
    MapDB::AddKill(locationID);
    MapDB::AddFactionKill(locationID);

    if (pClient != nullptr) {
        //award kill bounty.
        //AwardBounty( pClient );
        if (m_system->GetSystemSecurityRating() > 0)
            AwardSecurityStatus(m_self, pClient->GetChar().get());  // this awards secStatusChange for npcs in empire space
    }

    GPoint wreckPosition = m_destiny->GetPosition();
    std::string wreck_name = m_self->itemName();
    wreck_name += " Wreck";
    uint32 wreckTypeID = sDataMgr.GetWreckID(m_self->typeID());
    if (!IsWreckTypeID(wreckTypeID))
        wreckTypeID = 26468;    // Wreck
    ItemData wreckItemData(wreckTypeID, killerID, locationID, flagNone, wreck_name.c_str(), wreckPosition, itoa(m_allyID));
    WreckContainerRef wreckItemRef = sItemFactory.SpawnWreckContainer( wreckItemData );
    if (wreckItemRef.get() == nullptr) {
        sLog.Error("CustomsSE::Killed()", "Creating Wreck Item Failed for %s of type %u", wreck_name.c_str(), wreckTypeID);
        return;
    }

    if (is_log_enabled(PHYSICS__TRACE))
        _log(PHYSICS__TRACE, "Ship::Killed() - Ship %s(%u) Position: %.2f,%.2f,%.2f.  Wreck %s(%u) Position: %.2f,%.2f,%.2f.", \
        GetName(), GetID(), x(), y(), z(), wreckItemRef->name(), wreckItemRef->itemID(), wreckPosition.x, wreckPosition.y, wreckPosition.z);

    DropLoot(wreckItemRef, m_self->groupID(), killerID);

    for (auto cur: survivedItems)
        cur->Move(wreckItemRef->itemID(), flagNone); // populate wreck with items that survived

    DBSystemDynamicEntity wreckEntity = DBSystemDynamicEntity();
        wreckEntity.allianceID = killer->GetAllianceID();
        wreckEntity.categoryID = EVEDB::invCategories::Celestial;
        wreckEntity.corporationID = killer->GetCorporationID();
        wreckEntity.factionID = m_warID;
        wreckEntity.groupID = EVEDB::invGroups::Wreck;
        wreckEntity.itemID = wreckItemRef->itemID();
        wreckEntity.itemName = wreck_name;
        wreckEntity.ownerID = killerID;
        wreckEntity.typeID = wreckTypeID;
        wreckEntity.position = wreckPosition;

    if (!m_system->BuildDynamicEntity(wreckEntity, m_self->itemID())) {
        sLog.Error("CustomsSE::Killed()", "Spawning Wreck Failed: typeID or typeName not supported: '%u'", wreckTypeID);
        wreckItemRef->Delete();
        return;
    }
    m_destiny->SendJettisonPacket();
}
