/*
    ------------------------------------------------------------------------------------
    LICENSE:
    ------------------------------------------------------------------------------------
    This file is part of EVEmu: EVE Online Server Emulator
    Copyright 2006 - 2021 The EVEmu Team
    For the latest information visit https://evemu.dev
    ------------------------------------------------------------------------------------
    This program is free software; you can redistribute it and/or modify it under
    the terms of the GNU Lesser General Public License as published by the Free Software
    Foundation; either version 2 of the License, or (at your option) any later
    version.

    This program is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License along with
    this program; if not, write to the Free Software Foundation, Inc., 59 Temple
    Place - Suite 330, Boston, MA 02111-1307, USA, or go to
    http://www.gnu.org/copyleft/lesser.txt.
    ------------------------------------------------------------------------------------
    Author:        Zhur
    Rewrite:    Allan
*/

#include "system/CrimeWatch.h"
#include "eve-server.h"

#include "ConsoleCommands.h"
#include "Client.h"
#include "Container.h"
#include "EVEServerConfig.h"

#include "StatisticMgr.h"
#include "account/AccountService.h"
#include "character/Character.h"
#include "fleet/FleetService.h"
#include "inventory/AttributeEnum.h"
#include "planet/CustomsOffice.h"
#include "planet/Planet.h"
#include "pos/Structure.h"
#include "standing/StandingMgr.h"
#include "system/DestinyManager.h"
#include "system/Interdiction.h"
#include "station/Station.h"
#include "system/SystemBubble.h"
#include "system/SystemEntity.h"
#include "system/SystemManager.h"



SystemEntity::SystemEntity(InventoryItemRef self, EVEServiceManager &services, SystemManager* system)
: m_self(self),
m_services(services),
m_system(system),
m_bubble(nullptr),
m_destiny(nullptr),
m_targMgr(nullptr),
m_ewar(nullptr),
m_sigMult(1.0f),
m_killed(false)
{
    assert(m_system != nullptr);
    assert(m_self.get() != nullptr);

    m_warID = 0;
    m_allyID = 0;
    m_corpID = 0;
    m_fleetID = 0;
    m_ownerID = 1;

    m_radius = m_self->GetAttribute(AttrRadius).get_double();

    m_harmonic = EVEPOS::Harmonic::Inactive;

    _log(SE__DEBUG, "Created SE for item %s (%u) with radius of %.1f.", self->name(), self->itemID(), m_radius);
}

// copy c'tor
SystemEntity::SystemEntity(const SystemEntity* oth) : m_self(oth->m_self),m_services(oth->m_services),m_system(oth->m_system),
m_bubble(oth->m_bubble),m_destiny(oth->m_destiny),m_targMgr(oth->m_targMgr),m_killed(oth->m_killed),m_warID(oth->m_warID),
m_allyID(oth->m_allyID),m_corpID(oth->m_corpID),m_fleetID(oth->m_fleetID),m_ownerID(oth->m_ownerID),m_radius(oth->m_radius),
m_harmonic(oth->m_harmonic),m_ewar(nullptr),m_sigMult(1.0f)
{
    sLog.Error("SE::SE()", "copy c'tor.");
    // wip
}


float CalcRechargeRate(float capacity, float current, float rechargeTimeMS)
{
    // dC/dt = (SQRT(C/Cmax) - C/Cmax) * 2 * Cmax / tau
    // tau = "Cap Recharge Time" / 5.0
    // prevent divide by zero.
    rechargeTimeMS = (rechargeTimeMS < 1 ? 1 : rechargeTimeMS);
    current = (current < 1 ? 1 : current);
    float Cmax = (capacity < 1 ? 1 : capacity);
    float tau = (rechargeTimeMS / 5000.0f);
    float C_Cmax = (current / Cmax);
    // charge rate in units / sec
    return (((Cmax * 2) / tau) * (std::sqrt(C_Cmax) - C_Cmax));
}

void SystemEntity::Process() {
    if (m_killed) {
        _log(SE__DEBUG, "SE::Process() - %s(%u) is dead but still in system.", m_self->name(), m_self->itemID());
        return;
    }

    if (m_destiny != nullptr)
        m_destiny->Process();

    if ((m_ewar != nullptr) and !m_ewar->Empty())
        m_ewar->Process();
}

EwarState* SystemEntity::GetEwar(bool create/*false*/)
{
    if ((m_ewar == nullptr) and create)
        m_ewar = new EwarState(this);
    return m_ewar;
}

bool SystemEntity::IsWarpScrambled()
{
    // warp core stabilizers give a negative status, CONCORD and siege a positive one
    float status(m_self->GetAttribute(AttrWarpScrambleStatus).get_float());
    if (m_ewar != nullptr)
        status += m_ewar->GetScrambleStrength();
    return (status > 0);
}

float SystemEntity::GetSignatureRadius()
{
    float sig(m_self->GetAttribute(AttrSignatureRadius).get_float() * m_sigMult);
    if (m_ewar != nullptr)
        sig *= m_ewar->GetSigRadiusMult();
    return sig;
}

double SystemEntity::LayerFraction(double used, double max)
{
    if (max <= 0)
        return 1.0;
    double fraction(1.0 - (used / max));
    if (fraction < 0)
        return 0.0;
    if (fraction > 1)
        return 1.0;
    return fraction;
}

PyTuple* SystemEntity::MakeDamageState() {
    /* wrecks, containers, missiles and other things with structure only send (shield, armor, structure).
     *  the client shows no shield for a non-tuple shield entry (michelle.GetDamageState)
     */
    if (!IsStaticEntity()
    and (m_self->GetAttribute(AttrShieldCapacity) <= 0)
    and (m_self->GetAttribute(AttrArmorHP) <= 0)
    and (m_self->GetAttribute(AttrHP) > 0)) {
        DoDestinyDamageState3 ddds;
            ddds.shield = 0;
            ddds.armor = 0;
            ddds.structure = LayerFraction(m_self->GetAttribute(AttrDamage).get_double(), m_self->GetAttribute(AttrHP).get_double());
        return ddds.Encode();
    }
    DoDestinyDamageState ddds;
    MakeDamageState(ddds);
    return ddds.Encode();
}

void SystemEntity::MakeDamageState(DoDestinyDamageState &into) {
    into.shield = 1;
    into.recharge = 110000;
    into.armor = 1;
    into.structure = 1;
    into.timestamp = GetFileTimeNow();
}

void SystemEntity::FillDamageState(DoDestinyDamageState &into) {
    // real fractions from the item's attributes.  a layer it doesn't have shows full
    into.shield = 1.0;
    if (m_self->GetAttribute(AttrShieldCapacity) > 0)
        into.shield = 1.0 - LayerFraction(m_self->GetAttribute(AttrShieldCharge).get_double(), m_self->GetAttribute(AttrShieldCapacity).get_double());
    into.recharge = m_self->GetAttribute(AttrShieldRechargeRate).get_double();
    into.timestamp = GetFileTimeNow();
    into.armor = LayerFraction(m_self->GetAttribute(AttrArmorDamage).get_double(), m_self->GetAttribute(AttrArmorHP).get_double());
    into.structure = LayerFraction(m_self->GetAttribute(AttrDamage).get_double(), m_self->GetAttribute(AttrHP).get_double());
}

PyDict* SystemEntity::MakeSlimItem() {
    _log(SE__SLIMITEM, "MakeSlimItem for SE %s(%u)", GetName(), m_self->itemID());
    PyDict *slim = new PyDict();
        slim->SetItemString("typeID",       new PyInt(m_self->typeID()));
        slim->SetItemString("ownerID",      new PyInt(m_ownerID));
        slim->SetItemString("itemID",       new PyLong(m_self->itemID()));
    return slim;
}

void SystemEntity::EncodeDestiny( Buffer& into )
{
    using namespace Destiny;

    BallHeader head = BallHeader();
        head.entityID = m_self->itemID();
        head.mode = Ball::Mode::RIGID;
        head.radius = m_radius;
        head.posX = x();
        head.posY = y();
        head.posZ = z();
        head.flags = Ball::Flag::IsGlobal;
    into.Append( head );
    RIGID_Struct main;
        main.formationID = 0xFF;
    into.Append( main );
    _log(SE__DESTINY, "SE::EncodeDestiny(): %s - id:%lli, mode:%u, flags:0x%X", GetName(), head.entityID, head.mode, head.flags);
}

void SystemEntity::Killed(Damage& damage)
{
    if (m_targMgr != nullptr) {
        // loop thru list of all modules targeting this entity and let them know it has been killed.
        m_targMgr->Destroyed();
        // remove TargMgr here to avoid redundant calls upon object deletion
        SafeDelete(m_targMgr);
    }
    Delete();
}

void SystemEntity::Delete()
{
    if (m_targMgr != nullptr)
        m_targMgr->ClearFromTargets(); //OnTarget(nullptr, TargMgr::Mode::Clear, TargMgr::Msg::Deleted);
    if (m_system != nullptr)
        m_system->RemoveEntity(this);
    // containers have additional calls to process and calls this.  calling here will create infinite loop
    if (!IsContainerSE())
        m_self->Delete();
}

double SystemEntity::DistanceTo2(const SystemEntity* other) {
    if (other->m_bubble == nullptr)
        return 1000000.0;
    return GetPosition().distance(other->GetPosition());
}

PyRep* SystemEntity::MakeDamageDetails() {
     DamageDetails dmgState;
        dmgState.shield = 1.0;
        if (m_self->GetAttribute(AttrShieldCapacity) > 0)
            dmgState.shield = 1.0 - LayerFraction(m_self->GetAttribute(AttrShieldCharge).get_double(), m_self->GetAttribute(AttrShieldCapacity).get_double());
        dmgState.recharge = m_self->GetAttribute(AttrShieldRechargeRate).get_double();
        dmgState.timestamp = GetFileTimeNow();
        dmgState.armor = LayerFraction(m_self->GetAttribute(AttrArmorDamage).get_double(), m_self->GetAttribute(AttrArmorHP).get_double());
        dmgState.structure = LayerFraction(m_self->GetAttribute(AttrDamage).get_double(), m_self->GetAttribute(AttrHP).get_double());
    _log(DAMAGE__MESSAGE, "%s(%u): DamageUpdate - S:%f A:%f H:%f.", \
            m_self->name(), m_self->itemID(), dmgState.shield, dmgState.armor, dmgState.structure);
    return dmgState.Encode();
}

void SystemEntity::SendDamageStateChanged() {  //working 24Apr15
     OnDamageStateChange dmgChange;
        dmgChange.entityID = m_self->itemID();
        dmgChange.state = MakeDamageState();
    PyTuple *up = dmgChange.Encode();
    if (m_targMgr != nullptr)
        m_targMgr->QueueUpdate(&up);
    PySafeDecRef(up);

    // fleet members watching this pilot (fleet window watch list)
    if (IsShipSE() and HasPilot() and GetPilot()->InFleet())
        sFltSvc.SendWatchlistDamage(GetPilot());
}

void SystemEntity::DropLoot(WreckContainerRef wreckRef, uint32 groupID, uint32 owner) {
    /*   allan 27Nov14    */
    /* how much the npc drops is its type's data:  entityEquipmentMin/Max rolls, entityEquipmentGroupMax drops of a group
     *  (EVE_Loot.h).  ships without them (players' ships) drop nothing here
     */
    const ItemType& type(m_self->type());
    if (!type.HasAttribute(AttrEntityEquipmentMax)) {
        _log(LOOT__INFO, "%s(%u) has no loot rolls (entityEquipmentMax)", m_self->name(), m_self->itemID());
        return;
    }
    Loot::Params params = Loot::Params();
        params.rollsMin = static_cast<int32>(type.GetAttribute(AttrEntityEquipmentMin).get_int());
        params.rollsMax = static_cast<int32>(type.GetAttribute(AttrEntityEquipmentMax).get_int());
        params.groupMax = (type.HasAttribute(AttrEntityEquipmentGroupMax)
                        ? static_cast<uint32>(std::max<int64>(type.GetAttribute(AttrEntityEquipmentGroupMax).get_int(), 0)) : 0);
    uint32 missileTypeID(type.HasAttribute(AttrEntityMissileTypeID)
                        ? static_cast<uint32>(std::max<int64>(type.GetAttribute(AttrEntityMissileTypeID).get_int(), 0)) : 0);

    std::vector<Loot::Drop> drops;
    sDataMgr.GetLoot(m_self->typeID(), groupID, params, missileTypeID, drops);
    if (drops.empty()) {
        _log(LOOT__INFO, "lootList empty for %s(%u)", m_self->name(), m_self->itemID());
        return;
    }

    for (auto& cur : drops) {
        uint32 quantity(cur.quantity);
        if (sConfig.rates.DropItem > 1)
            quantity *= sConfig.rates.DropItem;
        ItemData iLoot(cur.typeID, owner, wreckRef->itemID(), flagNone, quantity);
        InventoryItemRef iRef(sItemFactory.SpawnItem(iLoot));
        if (iRef.get() == nullptr) {
            _log(LOOT__ERROR, "unable to spawn %u of %u for %s(%u)", quantity, cur.typeID, m_self->name(), m_self->itemID());
            continue;
        }
        wreckRef->AddItem(iRef);
        _log(LOOT__INFO, "added %u of %u to list for %s(%u)", quantity, cur.typeID, m_self->name(), m_self->itemID());
    }
}

/** @todo (allan)  this doesnt need to be here */
void SystemEntity::AwardSecurityStatus(InventoryItemRef iRef, Character* pChar) {
    // security status: (10 - current) * the npc's security percentage, best npc per system per 15 minutes (CrimeWatch)
    float secAward(sCrimeWatch.OnNPCKilled(pChar, iRef, m_system));
    if (secAward <= 0)
        return;
    sLog.Magenta("SE::AwardSecurityStatus()"," %s(%u): secAward: %f.", GetName(), iRef->itemID(), secAward);
    // killing pirates costs a little standing with their faction
    std::string msg("Status Change for killing pirates in ");
    msg += m_system->GetNameStr();
    sStandingMgr.UpdateStandings(iRef->ownerID(), pChar->itemID(), Standings::CombatShipKill, -secAward, msg);

    /** @todo msg need work for details to appear correctly.  currently working, but could be better. (incomplete, but working)
     * see data in eve/common/script/util/eveFormat.py:300 for details
     *
     *
     * SE::AwardSecurityStatus():  Shadow Serpentis Port Admiral(750000006): killBonus: 0.072000.  oldSec: 0.044933.  secAward: 0.007617.
     * SE::AwardSecurityStatus():               Estamel Tharchon(750000001): killBonus: 0.244141.  oldSec: 0.000000.  secAward: 0.024414.
     *
     */
}

void SystemEntity::Abandon()
{
    m_warID = 0;
    m_allyID = 0;
    m_corpID = 0;
    m_fleetID = 0;
    m_ownerID = 1;
    m_self->ChangeOwner(1); // update this to use system owner?    not sure.  logs show this as "1" for all non-player items
    /** @todo  should this have a slimupdate or bubblecast or something?  */
}


/* Static / Non-Mobile / Non-Destructable / Celestial Objects - Suns, Planets, Moons, Belts, Gates, Stations */
StaticSystemEntity::StaticSystemEntity(InventoryItemRef self, EVEServiceManager &services, SystemManager* system)
: SystemEntity(self, services, system)
{
}

// copy c'tor
StaticSystemEntity::StaticSystemEntity(const StaticSystemEntity* oth)
: SystemEntity(oth->m_self, oth->m_services, oth->m_system)
{
    sLog.Error("SSE::SSE()", "copy c'tor.");
    // wip
}

bool StaticSystemEntity::LoadExtras() {
    return true;
}

PyDict* StaticSystemEntity::MakeSlimItem() {
    _log(SE__SLIMITEM, "MakeSlimItem for SSE %s(%u)", GetName(), m_self->itemID());
    PyDict *slim = new PyDict();
        slim->SetItemString("itemID",       new PyLong(m_self->itemID()));
        slim->SetItemString("typeID",       new PyInt(m_self->typeID()));
        slim->SetItemString("name",         new PyString(m_self->itemName()));
        slim->SetItemString("nameID",       PyStatic.NewNone());
        slim->SetItemString("ownerID",      PyStatic.NewOne());
    return slim;
}

void StaticSystemEntity::EncodeDestiny( Buffer& into ) {
    using namespace Destiny;
    BallHeader head = BallHeader();
        head.entityID = m_self->itemID();
        head.mode = Ball::Mode::RIGID;
        head.posX = x();
        head.posY = y();
        head.posZ = z();
        head.radius = m_radius;
        head.flags = Ball::Flag::IsGlobal;
    into.Append( head );
    RIGID_Struct main;
        main.formationID = 0xFF;
    into.Append( main );
    _log(SE__DESTINY, "SSE::EncodeDestiny(): %s - id:%lli, mode:%u, flags:0x%X, radius:%.1f", GetName(), head.entityID, head.mode, head.flags, head.radius);
}

BeltSE::BeltSE(InventoryItemRef self, EVEServiceManager &services, SystemManager* system)
: StaticSystemEntity(self, services, system)
{
}

// copy c'tor
BeltSE::BeltSE(const BeltSE* oth)
: StaticSystemEntity(oth->m_self, oth->m_services, oth->m_system)
{
    sLog.Error("SSE::BeltSE()", "copy c'tor.");
    // wip
}

bool BeltSE::LoadExtras() {
    if (!StaticSystemEntity::LoadExtras())
        return false;

    if (m_bubble == nullptr)
        sBubbleMgr.Add(this);

    m_bubble->SetBelt(m_self);
    _log(DESTINY__BUBBLE_DEBUG, "BeltSE::LoadExtras() - IsBelt set to true for bubble %u.", m_bubble->GetID() );
    return true;
}

StargateSE::StargateSE(InventoryItemRef self, EVEServiceManager &services, SystemManager* system)
: StaticSystemEntity(self, services, system),
m_jumps(nullptr),
m_sbuSE(nullptr)
{
    m_destGates.clear();
}

// copy c'tor
StargateSE::StargateSE(const StargateSE* oth)
: StaticSystemEntity(oth->m_self, oth->m_services, oth->m_system),
m_jumps(nullptr),
m_sbuSE(oth->m_sbuSE),
m_destGates(oth->m_destGates)
{
    sLog.Error("SSE::gateSE()", "copy c'tor.");
    // wip
}

bool StargateSE::LoadExtras() {
    if (!StaticSystemEntity::LoadExtras())
        return false;

    if (m_bubble == nullptr)
        sBubbleMgr.Add(this);

    m_bubble->SetGate(true);
    _log(DESTINY__BUBBLE_DEBUG, "StargateSE::LoadExtras() - IsGate set to true for bubble %u.", m_bubble->GetID() );
    m_jumps = SystemDB::ListJumps(m_self->itemID());
    m_destGates.clear();
    SystemDB::GetJumpDestinations(m_self->itemID(), m_destGates);
    if (m_jumps != nullptr)
        return true;

    return false;
}

bool StargateSE::HasDestination(uint32 gateID) const
{
    return (std::find(m_destGates.begin(), m_destGates.end(), gateID) != m_destGates.end());
}

PyDict* StargateSE::MakeSlimItem() {
    _log(SE__SLIMITEM, "MakeSlimItem for StargateSE %s(%u)", GetName(), m_self->itemID());
    /** @todo  finish gate rotation data
    PyTuple* rotation = new PyTuple(3);
        rotation->SetItem(0, new PyFloat(0));
        rotation->SetItem(1, new PyFloat(0));
        rotation->SetItem(2, new PyFloat(0));*/
    PyDict *slim = new PyDict();
        //slim->SetItemString("dunRotation", rotation);
        slim->SetItemString("typeID",       new PyInt(m_self->typeID()));
        /** @todo (allan) make function to lookup controlling faction id for this */
        //  NOTE:  maybe not...logs show this is "1" for all items.
        slim->SetItemString("ownerID",      PyStatic.NewOne());
        slim->SetItemString("itemID",       new PyLong(m_self->itemID()));
        slim->SetItemString("name",         new PyString(m_self->itemName()));
        slim->SetItemString("nameID",       PyStatic.NewNone());
    if (m_jumps != nullptr)
        slim->SetItemString("jumps", m_jumps->Clone());
    return slim;
}


/* Non-Static / Non-Mobile / Non-Destructable / Celestial Objects - Containers, DeadSpace, ForceFields, ScanProbes */
ItemSystemEntity::ItemSystemEntity(InventoryItemRef self, EVEServiceManager &services, SystemManager* system)
: SystemEntity(self, services, system)
{
}

// copy c'tor
ItemSystemEntity::ItemSystemEntity(const ItemSystemEntity* oth)
: SystemEntity(oth->m_self, oth->m_services, oth->m_system)
{
    sLog.Error("ISE::ISE()", "copy c'tor.");
    // wip
}

PyDict* ItemSystemEntity::MakeSlimItem() {
    _log(SE__SLIMITEM, "MakeSlimItem for ISE %s(%u)", GetName(), m_self->itemID());
    PyDict *slim = new PyDict();
        slim->SetItemString("itemID",       new PyLong(m_self->itemID()));
        slim->SetItemString("typeID",       new PyInt(m_self->typeID()));
        slim->SetItemString("ownerID",      new PyInt(m_ownerID));
        // acceleration gates add their dungeon data (AccelGateSE)
    /** @todo  finish rotation data
    Large_Collidable_Structure
    Large_Collidable_Ship
    Large_Collidable_Object
    PyTuple* rotation = new PyTuple(3);
        rotation->SetItem(0, new PyFloat(0));
        rotation->SetItem(1, new PyFloat(0));
        rotation->SetItem(2, new PyFloat(0));
    slim->SetItemString("dunRotation", rotation);
    */
    return slim;
}

void ItemSystemEntity::EncodeDestiny( Buffer& into )
{
    using namespace Destiny;
    BallHeader head = BallHeader();
        head.entityID = m_self->itemID();
        head.mode = Ball::Mode::RIGID;
        head.radius = m_radius;
        head.posX = x();
        head.posY = y();
        head.posZ = z();
        head.flags = 0;
    into.Append( head );
    RIGID_Struct main;
        main.formationID = 0xFF;
    into.Append( main );

    _log(SE__DESTINY, "ISE::EncodeDestiny(): %s - id:%lli, mode:%u, flags:0x%X", GetName(), head.entityID, head.mode, head.flags);
}

void ItemSystemEntity::MakeDamageState(DoDestinyDamageState &into) {
    if (m_self->groupID() == EVEDB::invGroups::Force_Field) {
        SystemEntity::MakeDamageState(into);
    } else {
        FillDamageState(into);
    }
}

FieldSE::FieldSE(InventoryItemRef self, EVEServiceManager &services, SystemManager *system, const FactionData& data)
: ItemSystemEntity(self, services, system),
m_allowCorp(false),
m_allowAlliance(false)
{
    m_warID = data.factionID;
    m_allyID = data.allianceID;
    m_corpID = data.corporationID;
    m_ownerID = data.ownerID;
}

// copy c'tor
FieldSE::FieldSE(const FieldSE* oth)
: ItemSystemEntity(oth->m_self, oth->m_services, oth->m_system),
m_allowCorp(oth->m_allowCorp),
m_allowAlliance(oth->m_allowAlliance)
{
    sLog.Error("ISE::FieldSE()", "copy c'tor.");
    // wip
}

void FieldSE::EncodeDestiny( Buffer& into )
{
    using namespace Destiny;
    BallHeader head = BallHeader();
        head.entityID = m_self->itemID();
        head.mode = (m_harmonic > EVEPOS::Harmonic::Offline ? Ball::Mode::FIELD : Ball::Mode::STOP);
        head.radius = m_radius;
        head.posX = x();
        head.posY = y();
        head.posZ = z();
        // a tuned field is solid to everyone but matching harmonics, the tower's corp and alliance (if allowed)
        head.flags = (m_harmonic > EVEPOS::Harmonic::Offline ? Ball::Flag::IsMassive : 0);
    into.Append( head );
    MassSector mass = MassSector();
        mass.mass = 10000000000;    // as seen in packets
        mass.cloak = 0;
        mass.harmonic = m_harmonic;
        mass.corporationID = GetCorpAccessID();
        mass.allianceID = GetAllianceAccessID();
    into.Append( mass );
    if (head.mode == Ball::Mode::FIELD) {
        FIELD_Struct main;
        main.formationID = 0xFF;
        into.Append( main );
    } else if (head.mode == Ball::Mode::STOP) {
        STOP_Struct main;
        main.formationID = 0xFF;
        into.Append( main );
    }

    _log(SE__DESTINY, "FSE::EncodeDestiny(): %s - id:%lli, mode:%u, flags:0x%X", GetName(), head.entityID, head.mode, head.flags);
}

PyDict *FieldSE::MakeSlimItem()
{
    return SystemEntity::MakeSlimItem();
}


/* Non-Static / Non-Mobile / Destructible / Celestial Objects - POS Structures, Outposts, Deployables, empty Ships, Asteroids */
ObjectSystemEntity::ObjectSystemEntity(InventoryItemRef self, EVEServiceManager &services, SystemManager* system)
: SystemEntity(self, services, system),
m_invul(false)
{
    m_targMgr = new TargetManager(this);
    m_destiny = new DestinyManager(this);

    assert(m_targMgr != nullptr);
    assert(m_destiny != nullptr);
}

// copy c'tor
ObjectSystemEntity::ObjectSystemEntity(const ObjectSystemEntity* oth)
: SystemEntity(oth->m_self, oth->m_services, oth->m_system)
{
    sLog.Error("OSE::OSE()", "copy c'tor.");
    // wip
}

ObjectSystemEntity::~ObjectSystemEntity()
{
    if (m_targMgr != nullptr)
        if (!sConsole.IsShutdown()) {
            m_targMgr->ClearModules();
            m_targMgr->ClearAllTargets(false);
            //m_targMgr->OnTarget(nullptr, TargMgr::Mode::Clear, TargMgr::Msg::Destroyed);
        }

    SafeDelete(m_targMgr);
    SafeDelete(m_destiny);
}

void ObjectSystemEntity::EncodeDestiny( Buffer& into )
{
    using namespace Destiny;
    BallHeader head = BallHeader();
        head.entityID = m_self->itemID();
        head.mode = Ball::Mode::RIGID;
        head.radius = m_radius;
        head.posX = x();
        head.posY = y();
        head.posZ = z();
        head.flags = Ball::Flag::IsMassive;
    into.Append( head );
    RIGID_Struct main;
        main.formationID = 0xFF;
    into.Append( main );

    _log(SE__DESTINY, "OSE::EncodeDestiny(): %s - id:%lli, mode:%u, flags:0x%X", GetName(), head.entityID, head.mode, head.flags);
}

PyDict* ObjectSystemEntity::MakeSlimItem() {
    _log(SE__SLIMITEM, "MakeSlimItem for OSE %s(%u)", GetName(), m_self->itemID());
    PyDict *slim = new PyDict();
        slim->SetItemString("itemID",           new PyLong(m_self->itemID()));
        slim->SetItemString("typeID",           new PyInt(GetTypeID()));
        slim->SetItemString("ownerID",          new PyInt(m_ownerID));
        slim->SetItemString("categoryID",       new PyInt(m_self->categoryID()));
        slim->SetItemString("groupID",          new PyInt(m_self->groupID()));
        slim->SetItemString("name",             new PyString(m_self->itemName()));
        slim->SetItemString("corpID",           IsCorp(m_corpID) ? new PyInt(m_corpID) : PyStatic.NewNone());
        slim->SetItemString("allianceID",       IsAlliance(m_allyID) ? new PyInt(m_allyID) : PyStatic.NewNone());
        slim->SetItemString("warFactionID",     IsFaction(m_warID) ? new PyInt(m_warID) : PyStatic.NewNone());
    return slim;
}

void ObjectSystemEntity::MakeDamageState(DoDestinyDamageState &into) {
    FillDamageState(into);
}

void ObjectSystemEntity::UpdateDamage()
{
    SendDamageStateChanged();
}

void ObjectSystemEntity::Killed(Damage& damage)
{
    // do we need to make wreck items here?
    // do these structures have loot?  probably so eventually

    /** @todo  test and complete this to null current customs office for this planet ... */
    if (IsCOSE()) {
        if (GetCOSE()->GetPlanetID() > 0) {
            SystemEntity* pSE = m_system->GetSE(GetCOSE()->GetPlanetID());
            pSE->GetPlanetSE()->SetCustomsOffice(nullptr);
        }
    }
}

DeployableSE::DeployableSE(InventoryItemRef self, EVEServiceManager &services, SystemManager *system, const FactionData& data)
: ObjectSystemEntity(self, services, system),
m_state(DeployState::Unanchored),
m_stateTimer(0)
{
    m_warID = data.factionID;
    m_allyID = data.allianceID;
    m_corpID = data.corporationID;
    m_ownerID = data.ownerID;
}

void DeployableSE::Init()
{
    // anchoring/unanchoring in progress isn't saved: it comes back in the state it started from
    EvilNumber online(EvilZero);
    m_state = ((m_self->HasAttribute(AttrOnline, online) and (online > EvilZero)) ? DeployState::Anchored : DeployState::Unanchored);
    if (m_state == DeployState::Anchored)
        SetField(true);
}

void DeployableSE::EncodeDestiny( Buffer& into )
{
    // DeployableSpaceObject.SetColorBasedOnStatus(): isFree -> green, no sphere.  otherwise red with the warpScrambleRange sphere
    using namespace Destiny;
    BallHeader head = BallHeader();
        head.entityID = m_self->itemID();
        head.mode = Ball::Mode::RIGID;
        head.radius = m_radius;
        head.posX = x();
        head.posY = y();
        head.posZ = z();
        head.flags = (m_state == DeployState::Unanchored ? Ball::Flag::IsFree : 0);
    into.Append( head );
    if (m_state == DeployState::Unanchored) {
        DataSector data = DataSector();
            data.inertia = 1;
            data.velX = 0;
            data.velY = 0;
            data.velZ = 0;
            data.maxSpeed = 1;
            data.speedfraction = 0;
        into.Append( data );
    }
    RIGID_Struct main;
        main.formationID = 0xFF;
    into.Append( main );

    _log(SE__DESTINY, "DeployableSE::EncodeDestiny(): %s - id:%lli, mode:%u, flags:0x%X", GetName(), head.entityID, head.mode, head.flags);
}

void DeployableSE::Process()
{
    SystemEntity::Process();

    if (!m_stateTimer.Check(false))
        return;
    m_stateTimer.Disable();

    switch (m_state) {
        case DeployState::Anchoring: {
            m_state = DeployState::Anchored;
            m_self->SetAttribute(AttrOnline, EvilOne, false);
            m_self->SaveAttributes();
            SetField(true);
            // client: red, sphere shown
            SendAnchorFx("effects.AnchorDrop", false, 0);
            _log(SE__TRACE, "DeployableSE::Process() - %s(%u) anchored.", GetName(), GetID());
        } break;
        case DeployState::Unanchoring: {
            m_state = DeployState::Unanchored;
            m_self->SetAttribute(AttrOnline, EvilZero, false);
            m_self->SaveAttributes();
            SetField(false);
            SetBallFree sbf;
                sbf.entityID = m_self->itemID();
                sbf.is_free = true;
            PyTuple* up = sbf.Encode();
            m_destiny->SendSingleDestinyUpdate(&up);   // consumed
            // client: green, sphere hidden
            SendAnchorFx("effects.AnchorLift", false, 0);
            _log(SE__TRACE, "DeployableSE::Process() - %s(%u) unanchored.", GetName(), GetID());
        } break;
    }
}

void DeployableSE::CheckControl(Client* pClient)
{
    if ((m_ownerID != pClient->GetCharacterID()) and (m_ownerID != (uint32)pClient->GetCorporationID()))
        throw CustomError ("You do not own the %s.", GetName());
    // the client offers these within maxConfigureDistance (5km, surface to surface)
    ShipSE* pShipSE(pClient->GetShipSE());
    if ((pShipSE == nullptr) or (pShipSE->SystemMgr() != m_system)
    or ((pShipSE->GetPosition().distance(GetPosition()) - pShipSE->GetRadius() - GetRadius()) > 5000.0))
        throw CustomError ("You are too far away from the %s to do that.", GetName());
}

void DeployableSE::Anchor(Client* pClient)
{
    CheckControl(pClient);
    if (m_state != DeployState::Unanchored)
        throw CustomError ("The %s is already anchored.", GetName());
    // mobile warp disruptors only work in 0.0 and w-space
    if (m_self->type().HasAttribute(AttrDisallowInEmpireSpace)
    and (m_self->type().GetAttribute(AttrDisallowInEmpireSpace) > EvilZero)
    and InterdictionMgr::IsRestricted(m_system))
        throw UserError ("CantInEmpireSpace");

    uint32 delay(m_self->GetAttribute(AttrAnchoringDelay).get_uint32());
    m_state = DeployState::Anchoring;
    m_stateTimer.Start(delay > 0 ? delay : 1);

    SetBallFree sbf;
        sbf.entityID = m_self->itemID();
        sbf.is_free = false;
    PyTuple* up = sbf.Encode();
    m_destiny->SendSingleDestinyUpdate(&up);   // consumed
    // client: yellow until the stop at the end of the delay
    SendAnchorFx("effects.AnchorDrop", true, delay);
    _log(SE__TRACE, "DeployableSE::Anchor() - %s(%u) anchoring for %ums.", GetName(), GetID(), delay);
}

void DeployableSE::Unanchor(Client* pClient)
{
    CheckControl(pClient);
    if (m_state != DeployState::Anchored)
        throw CustomError ("The %s is not anchored.", GetName());

    // anchorLift uses anchoringDelay too (durationAttributeID 556).  the field stays up until it's done
    uint32 delay(m_self->GetAttribute(AttrAnchoringDelay).get_uint32());
    m_state = DeployState::Unanchoring;
    m_stateTimer.Start(delay > 0 ? delay : 1);
    SendAnchorFx("effects.AnchorLift", true, delay);
    _log(SE__TRACE, "DeployableSE::Unanchor() - %s(%u) unanchoring for %ums.", GetName(), GetID(), delay);
}

void DeployableSE::SetField(bool on)
{
    if (m_self->groupID() != EVEDB::invGroups::Mobile_Warp_Disruptor)
        return;
    if (on) {
        m_system->GetInterdictionMgr()->AddField(this, m_self->GetAttribute(AttrWarpScrambleRange).get_double(),
                Interdiction::Source::Deployable, m_self->typeID());
    } else {
        m_system->GetInterdictionMgr()->RemoveField(GetID());
    }
}

void DeployableSE::SendAnchorFx(const char* guid, bool start, uint32 duration)
{
    if (m_destiny == nullptr)
        return;
    m_destiny->SendSpecialEffect(GetID(), GetID(), m_self->typeID(), 0, 0, guid, false, start, start, (start ? (int32)duration : 0), 0);
}

// copy c'tor
DeployableSE::DeployableSE(const DeployableSE* oth)
: ObjectSystemEntity(oth->m_self, oth->m_services, oth->m_system),
m_state(DeployState::Unanchored),
m_stateTimer(0)
{
    sLog.Error("OSE::DeployableSE()", "copy c'tor.");
    // wip
}


/* Non-Static / Mobile / Destructible / Celestial Objects - PC's, NPC's, Drones, Ships, Missiles, Wrecks  */
DynamicSystemEntity::DynamicSystemEntity(InventoryItemRef self, EVEServiceManager &services, SystemManager* system)
: SystemEntity(self, services, system),
m_invul(false),
m_frozen(false)
{
    m_targMgr = new TargetManager(this);
    m_destiny = new DestinyManager(this);

    assert(m_targMgr != nullptr);
    assert(m_destiny != nullptr);
}

// copy c'tor
DynamicSystemEntity::DynamicSystemEntity(const DynamicSystemEntity* oth)
: SystemEntity(oth->m_self, oth->m_services, oth->m_system)
{
    sLog.Error("DSE::DSE()", "copy c'tor.");
    // wip
}

DynamicSystemEntity::~DynamicSystemEntity()
{
    if (m_targMgr != nullptr)
        if (!sConsole.IsShutdown()) {
            m_targMgr->ClearModules();
            m_targMgr->ClearAllTargets(false);
            //m_targMgr->OnTarget(nullptr, TargMgr::Mode::Clear, TargMgr::Msg::Destroyed);
        }

    SafeDelete(m_targMgr);
    SafeDelete(m_destiny);
}

PyDict *DynamicSystemEntity::MakeSlimItem() {
    if (IsNPCSE())
        return SystemEntity::MakeSlimItem();

    _log(SE__SLIMITEM, "MakeSlimItem for DSE %s(%u)", GetName(), m_self->itemID());
    PyDict *slim = new PyDict();
        slim->SetItemString("itemID",           new PyLong(m_self->itemID()));
        slim->SetItemString("typeID",           new PyInt(m_self->typeID()));
        slim->SetItemString("ownerID",          new PyInt(m_ownerID));
        //slim->SetItemString("categoryID",       new PyInt(m_self->categoryID()));
        //slim->SetItemString("groupID",          new PyInt(m_self->groupID()));
        slim->SetItemString("name",             new PyString(m_self->itemName()));
        slim->SetItemString("corpID",           IsCorp(m_corpID) ? new PyInt(m_corpID) : PyStatic.NewNone());
        slim->SetItemString("allianceID",       IsAlliance(m_allyID) ? new PyInt(m_allyID) : PyStatic.NewNone());
        slim->SetItemString("warFactionID",     IsFaction(m_warID) ? new PyInt(m_warID) : PyStatic.NewNone());
    return (slim);
}

void DynamicSystemEntity::EncodeDestiny( Buffer& into )
{
    using namespace Destiny;
    BallHeader head = BallHeader();
        head.entityID = m_self->itemID();
        head.mode = m_destiny->GetState();
        head.radius = m_radius;
        head.posX = x();
        head.posY = y();
        head.posZ = z();
        head.flags = Ball::Flag::IsFree;
    into.Append( head );
    MassSector mass = MassSector();
        mass.mass = m_destiny->GetMass();
        // a gate/login (session) cloak only hides the ball from everyone else;  it never reaches the pilot's own client
        //  (SendCloakBall(), UnCloak()), so its ball must not come in flagged cloaked either:  nothing would ever clear it
        mass.cloak = ((m_destiny->IsCloaked() and (m_destiny->GetCloakMode() != Destiny::Cloak::Session)) ? 1 : 0);
        mass.harmonic = m_harmonic;
        mass.corporationID = m_corpID;
        mass.allianceID = (IsAlliance(m_allyID) ? m_allyID : -1);
    into.Append( mass );
    // movement data and mode-specific part are written by destiny
    m_destiny->EncodeDestiny( into );

    _log(SE__DESTINY, "DSE::EncodeDestiny(): %s - id:%lli, mode:%u, flags:0x%X", GetName(), head.entityID, head.mode, head.flags);
}

void DynamicSystemEntity::MakeDamageState(DoDestinyDamageState &into) {
    FillDamageState(into);
}

void DynamicSystemEntity::UpdateDamage()
{
    SendDamageStateChanged();
}

void SystemEntity::AwardBounty(Client* pClient)
{
    // this will use a map{charID/BountyData} in system manager for using a bounty timer.
    double bounty = m_self->GetAttribute(AttrEntityKillBounty).get_double();
    bounty *= sConfig.rates.npcBountyMultiply;
    if (bounty < 1)
        return;

    // add data to StatisticMgr
    sStatMgr.Add(Stat::npcBounties, bounty);

    std::string reason = "Bounty for killing a pirate in ";
    reason += pClient->GetSystemName();

    BountyData data = BountyData();
    data.fromID = m_self->typeID();     // the delayed payout lists kills by type
    data.toID = pClient->GetCharacterID();
    data.refTypeID = Journal::EntryType::BountyPrize;
    data.fromKey = Account::KeyType::Cash;
    data.toKey = Account::KeyType::Cash;
    data.reason = reason;

    /* single kills are BountyPrize journal entries with referenceID -typeID (the client shows the npc's type name),
     * delayed payouts are one BountyPrizes entry per tic (SystemManager::PayBounties)
     */
    std::vector< uint32 > members;
    if (pClient->InFleet()) {
        // get fleet members onGrid and distrubute bounty
        sFltSvc.GetFleetMembersOnGrid(pClient, members);
        if (members.empty())
            members.push_back(pClient->GetCharacterID());
        // split bounty between members
        bounty /= members.size();
        data.amount = bounty;
        if (sConfig.server.BountyPayoutDelayed and sConfig.server.FleetShareDelayed) {
            for (auto cur :members)
                m_system->AddBounty(cur, data);
            return;
        }
        reason += " (FleetShare) ";
        reason += " by ";
        reason += pClient->GetName();
    } else {
        data.amount = bounty;
        if (sConfig.server.BountyPayoutDelayed) {
            m_system->AddBounty(pClient->GetCharacterID(), data);
            return;
        }
        members.push_back(pClient->GetCharacterID());
    }

    // immediate payout.  a failed transfer must not break the kill
    for (auto cur : members) {
        try {
            AccountService::TransferFunds(corpCONCORD, cur, bounty, reason.c_str(), Journal::EntryType::BountyPrize, -GetTypeID());
        } catch (...) {
            _log(CLIENT__ERROR, "AwardBounty() - unable to pay %.2f bounty for %s(%u) to %u", bounty, GetName(), GetID(), cur);
        }
    }
}

DungeonEditSE::DungeonEditSE(InventoryItemRef self, EVEServiceManager& services, SystemManager* system, Dungeon::RoomObject data)
: ObjectSystemEntity(self, services, system),
    m_data(data)
{
}

PyDict* DungeonEditSE::MakeSlimItem()
{
    _log(SE__SLIMITEM, "MakeSlimItem for DungeonEditSE %s(%u)", GetName(), m_self->itemID());
    PyDict *slim = new PyDict();
        slim->SetItemString("itemID", new PyLong(m_self->itemID()));
        slim->SetItemString("typeID", new PyInt(m_self->typeID()));
        slim->SetItemString("groupID", new PyInt(m_self->groupID()));
        slim->SetItemString("dunObjectID", new PyInt(m_self->itemID()));
        slim->SetItemString("dunRadius", new PyFloat(m_data.radius));
        slim->SetItemString("dunRoomID", new PyInt(m_data.roomID));
        slim->SetItemString("dunX", new PyFloat(m_data.x));
        slim->SetItemString("dunY", new PyFloat(m_data.y));
        slim->SetItemString("dunZ", new PyFloat(m_data.z));

    return slim;
}