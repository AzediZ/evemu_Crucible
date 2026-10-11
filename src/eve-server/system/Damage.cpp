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


#include "system/Damage.h"

#include "../../eve-common/EVE_Damage.h"

#include "Client.h"
#include "EntityList.h"
#include "EVEServerConfig.h"
#include "manufacturing/Blueprint.h"
#include "map/MapDB.h"
#include "npc/NPC.h"
#include "npc/NPCAI.h"
#include "npc/Drone.h"
#include "ship/Ship.h"
#include "ship/modules/GenericModule.h"
#include "system/Container.h"
#include "system/SystemBubble.h"
#include "system/cosmicMgrs/AnomalyMgr.h"
#include "system/CrimeWatch.h"
#include "npc/LawEnforcement.h"
#include "character/CharacterDB.h"
#include "StaticDataMgr.h"

/*
DAMAGE
DAMAGE__ERROR
DAMAGE__WARNING
DAMAGE__MESSAGE
DAMAGE__INFO
DAMAGE__TRACE
DAMAGE__DEBUG
*/

// this is for turrets
Damage::Damage(SystemEntity* pSE, InventoryItemRef wRef, float kin, float ther, float emp, float exp, float mod, uint16 eID)
: srcSE(pSE), effectID(eID), weaponRef(wRef), chargeRef(InventoryItemRef(nullptr)),
em(emp), kinetic(kin), thermal(ther), explosive(exp), modifier(mod)
{
}

// this is for npcs
Damage::Damage(SystemEntity* pSE, InventoryItemRef wRef, float mod, uint16 eID)
: srcSE(pSE), effectID(eID), weaponRef(wRef), chargeRef(InventoryItemRef(nullptr)),
modifier(mod),
em(wRef->GetAttribute(AttrEmDamage).get_float()),
kinetic(wRef->GetAttribute(AttrKineticDamage).get_float()),
thermal(wRef->GetAttribute(AttrThermalDamage).get_float()),
explosive(wRef->GetAttribute(AttrExplosiveDamage).get_float())
{
    _log(DAMAGE__WARNING, "Damage:C'tor - Called by source %s(%u) with weapon %s(%u).",
         srcSE->GetName(), srcSE->GetID(), wRef->name(), wRef->itemID() );
}

// this is for missiles
Damage::Damage(SystemEntity* pSE, InventoryItemRef wRef, InventoryItemRef cRef, uint16 eID)
: srcSE(pSE), effectID(eID), weaponRef(wRef), chargeRef(cRef),
modifier(1),
em(cRef->GetAttribute(AttrEmDamage).get_float()),
kinetic(cRef->GetAttribute(AttrKineticDamage).get_float()),
thermal(cRef->GetAttribute(AttrThermalDamage).get_float()),
explosive(cRef->GetAttribute(AttrExplosiveDamage).get_float())
{
    _log(DAMAGE__WARNING, "Damage:C'tor - Called by source %s(%u) with weapon %s(%u) using charge %s(%u).",
         srcSE->GetName(), srcSE->GetID(), wRef->name(), wRef->itemID(), cRef->name(), cRef->itemID() );
}

// No specific damage dealt here, just killed
Damage::Damage(SystemEntity* pSE, bool fatal_blow/*false*/)
: srcSE(pSE), effectID(EVEEffectID::targetAttack),
em(0.0f),kinetic(0.0f),thermal(0.0f),explosive(0.0f),
weaponRef(InventoryItemRef(nullptr)),
chargeRef(InventoryItemRef(nullptr))
{
    assert(fatal_blow and "Damage() fatal_blow called without 2nd param being true!");
}

float SystemEntity::Resonance(uint16 attrID)
{
    // a missing resonance means no resistance (not 100%)
    if (m_self->HasAttribute(attrID))
        return m_self->GetAttribute(attrID).get_float();
    return 1.0f;
}

float SystemEntity::LeakFraction(float available, float capacity, uint16 uniformityAttrID)
{
    // below (1 - uniformity) of the layer, the leaked share of a hit grows linearly from 0 at the threshold to 1 at empty
    if (capacity <= 0.0f)
        return 0.0f;
    float uniformity(1.0f);
    if (m_self->HasAttribute(uniformityAttrID))
        uniformity = m_self->GetAttribute(uniformityAttrID).get_float();
    float threshold(1.0f - uniformity);
    if (threshold <= 0.0f)
        return 0.0f;
    float fraction(available / capacity);
    if (fraction >= threshold)
        return 0.0f;
    return EvE::min(1.0, 1.0 - (fraction / threshold));
}

std::set<uint32> SystemEntity::s_unkillable;

void SystemEntity::SetUnkillable(uint32 itemID, bool set)
{
    if (set) {
        s_unkillable.insert(itemID);
    } else {
        s_unkillable.erase(itemID);
    }
}

bool SystemEntity::IsUnkillable(uint32 itemID)
{
    return (s_unkillable.find(itemID) != s_unkillable.end());
}

void SystemEntity::Destroy(Damage &d)
{
    if (m_killed)
        return;
    m_killed = true;

    if ((m_destiny != nullptr) and (m_bubble != nullptr))
        m_destiny->SendTerminalExplosion(m_self->itemID(), m_bubble->GetID(), isGlobal());

    Killed(d);  // this must NOT remove dead SE from system.
    SystemEntity::Killed(d);    // this removes dead SE from system then deletes itemRef and all its contents
}

bool SystemEntity::ApplyDamage(Damage &d) {
    double profileStartTime(GetTimeUSeconds());

    // invulnerable things (highsec customs offices, outposts, reinforced structures, gm invul) take nothing.  shots may still be in flight
    if (IsInvul())
        return false;

    // aggressive drones and drones guarding us respond to this
    DroneSE::OnAggression(this, d.srcSE);

    // a pilot shot at by NPCs keeps their ship in space a little longer if they log off
    if (HasPilot() and (d.srcSE != nullptr) and (d.srcSE->IsNPCSE() or d.srcSE->IsSentrySE()))
        sCrimeWatch.NPCAggression(GetPilot()->GetCharacterID());

    if (is_log_enabled(DAMAGE__MESSAGE)) {
        if (d.srcSE->IsNPCSE()) {
            _log(DAMAGE__MESSAGE, "%s(%u): Initializing %.2f damage from NPC %s(%u) with K:%.3f, T:%.3f, EM:%.3f, E:%.3f",\
                    GetName(), GetID(), d.GetTotal(), d.srcSE->GetName(), d.srcSE->GetID(), \
                    d.GetKinetic(), d.GetThermal(), d.GetEM(), d.GetExplosive() );
        } else if (d.srcSE->IsDroneSE()){
            _log(DAMAGE__MESSAGE, "%s(%u): Initializing %.2f damage from Drone %s(%u) with K:%.3f, T:%.3f, EM:%.3f, E:%.3f",\
                    GetName(), GetID(), d.GetTotal(), d.srcSE->GetName(), d.srcSE->GetID(), \
                    d.GetKinetic(), d.GetThermal(), d.GetEM(), d.GetExplosive() );
        } else if (d.srcSE->HasPilot()) {
            _log(DAMAGE__MESSAGE, "%s(%u): Initializing %.2f damage from %s's %s(%u) using %s(%u) %s with K:%.3f, T:%.3f, EM:%.3f, E:%.3f",\
                    GetName(), GetID(), d.GetTotal(), d.srcSE->GetPilot()->GetName(), d.srcSE->GetName(), d.srcSE->GetID(), \
                    d.weaponRef->name(), d.weaponRef->itemID(), (d.chargeRef ? d.chargeRef->name() : ""), \
                    d.GetKinetic(), d.GetThermal(), d.GetEM(), d.GetExplosive() );
        } else {
            _log(DAMAGE__MESSAGE, "%s(%u): Initializing %.2f damage from unknown source.", GetName(), GetID(), d.GetTotal());
        }
    }

    int8 damageID(0);
    switch (d.weaponRef->groupID()) {
        case EVEDB::invGroups::Missile_Launcher_Assault:
        case EVEDB::invGroups::Missile_Launcher_Bomb:       // not sure here
        case EVEDB::invGroups::Missile_Launcher_Citadel:
        case EVEDB::invGroups::Missile_Launcher_Cruise:
        case EVEDB::invGroups::Missile_Launcher_Defender:   // not sure here
        case EVEDB::invGroups::Missile_Launcher_Heavy:
        case EVEDB::invGroups::Missile_Launcher_Heavy_Assault:
        case EVEDB::invGroups::Missile_Launcher_Rocket:
        case EVEDB::invGroups::Missile_Launcher_Siege:
        case EVEDB::invGroups::Missile_Launcher_Standard: {
            // apply damage modifier from config
            d *= sConfig.rates.missileDamage;
            // should this be adjusted based on damage?
            damageID = 6;
        } break;
        case EVEDB::invGroups::Smart_Bomb: {
            // area damage always lands in full.  only resistances reduce it
            damageID = 6;
        } break;
        case EVEDB::invGroups::Super_Weapon: {
            // the doomsday's damage is worked out in SuperWeapon (one target, always hits)
            damageID = 5;
        } break;
        case EVEDB::invGroups::Missile_Launcher_Snowball: {
            // these dont do any damage
            //  update this to use real toHit data (once we implement them....)
            damageID = MakeRandomInt(0,8);
        } break;
        default: {
            float modifier = d.GetModifier();
            d *= modifier;
                 if (modifier == 3.0f)   { damageID = 8; }  //strikes perfectly, wrecking
            else if (modifier > 1.2501f) { damageID = 7; } //places an excellent hit
            else if (modifier > 0.9999f) { damageID = 6; } //aims well
            else if (modifier > 0.7501f) { damageID = 5; } //hits
            else if (modifier > 0.6251f) { damageID = 4; } //lightly hits
            else if (modifier > 0.4121f) { damageID = 3; } //barely scratches
            else if (modifier > 0.3751f) { damageID = 2; } //glances off
            else if (modifier > 0.2501f) { damageID = 1; } //barely misses
            else                         { damageID = 0; } //misses completely
            _log(DAMAGE__TRACE, "%s(%u): Modifier: %.3f, damageID: %u.", GetName(), GetID(), modifier, damageID);
        } break;
    }

    // apply damage modifier from config
    d *= sConfig.rates.damageRate;

    /* damage goes through shield, armor and structure in turn.  a layer the entity doesn't have (no capacity) is skipped,
     *  so wrecks and containers (hp only) take it all on structure.  missing resonances mean no resistance.
     * shield leakage (Crucible): below (1 - shieldUniformity) of its shield, part of each hit goes past the shield to armor.
     *  none at the threshold, all of it at 0 shield (linear.  CCP never published the formula).  ships have 0.75 uniformity
     *  (leaks below 25%), Tactical Shield Manipulation adds 5% per level (no leak at V).  armor to structure works the same
     *  way when server.ArmorBleedThrough is set (not confirmed for Crucible)
     */
    bool killed(false), armorDepleted(false);
    float total_damage(0.0f);

    // Shield:
    float shieldCapacity(m_self->GetAttribute(AttrShieldCapacity).get_float());
    if (shieldCapacity > 0.0f) {
        float available_shield(EvE::max(m_self->GetAttribute(AttrShieldCharge).get_float()));
        float leak(0.0f);
        if (sConfig.server.ShieldBleedThrough and (damageID > 0))
            leak = LeakFraction(available_shield, shieldCapacity, AttrShieldUniformity);
        // the leaked part skips the shield
        Damage toShield(d);
        toShield *= (1.0f - leak);
        Damage DamageToShield = toShield.MultiplyDup(
            Resonance(AttrShieldKineticDamageResonance),
            Resonance(AttrShieldThermalDamageResonance),
            Resonance(AttrShieldEmDamageResonance),
            Resonance(AttrShieldExplosiveDamageResonance) );

        float shield_damage(DamageToShield.GetTotal());
        if (shield_damage <= available_shield) {
            total_damage += shield_damage;
            float new_charge = available_shield - shield_damage;
            m_self->SetAttribute(AttrShieldCharge, new_charge);
            // only the leaked part goes on
            d *= leak;
            _log(DAMAGE__DEBUG, "%s(%u): Applying %.2f damage to shields (%.1f%% leaks through). New charge: %.2f.",
                 GetName(), GetID(), shield_damage, leak * 100, new_charge);
        } else {
            // the shield absorbs its share of the part that hit it.  the rest of that, and the leaked part, go on
            float absorbed(available_shield / shield_damage);
            d *= (leak + ((1.0f - leak) * (1.0f - absorbed)));
            total_damage += available_shield;

            if (available_shield > 0.0f) {
                _log(DAMAGE__INFO, "%s(%u): Shield depleted with %.2f damage. %.2f damage remains.",
                     GetName(), GetID(), available_shield, d.GetTotal());
                m_self->SetAttribute(AttrShieldCharge, EvilZero);
            }
        }
    }

    //Armor:
    float armorHP(m_self->GetAttribute(AttrArmorHP).get_float());
    if ((d.GetTotal() > 0.0f) and (armorHP > 0.0f)) {
        float available_armor(EvE::max(armorHP - m_self->GetAttribute(AttrArmorDamage).get_float()));
        float leak(0.0f);
        if (sConfig.server.ArmorBleedThrough and (damageID > 0))
            leak = LeakFraction(available_armor, armorHP, AttrArmorUniformity);
        Damage toArmor(d);
        toArmor *= (1.0f - leak);
        Damage DamageToArmor = toArmor.MultiplyDup(
            Resonance(AttrArmorKineticDamageResonance),
            Resonance(AttrArmorThermalDamageResonance),
            Resonance(AttrArmorEmDamageResonance),
            Resonance(AttrArmorExplosiveDamageResonance) );

        float armor_damage(DamageToArmor.GetTotal());
        if (armor_damage <= available_armor) {
            total_damage += armor_damage;
            float new_damage = m_self->GetAttribute(AttrArmorDamage).get_float() + armor_damage;
            m_self->SetAttribute(AttrArmorDamage, new_damage);
            d *= leak;
            _log(DAMAGE__DEBUG, "%s(%u): Applying %.2f damage to armor (%.1f%% leaks through). New armor damage: %.2f",
                 GetName(), GetID(), armor_damage, leak * 100, new_damage);
        } else {
            float absorbed(available_armor / armor_damage);
            d *= (leak + ((1.0f - leak) * (1.0f - absorbed)));
            total_damage += available_armor;
            armorDepleted = true;

            if (available_armor > 0) {
                _log(DAMAGE__INFO, "%s(%u): Armor depleted with %.2f damage. %.2f damage remains.",
                     GetName(), GetID(), available_armor, d.GetTotal());
                m_self->SetAttribute(AttrArmorDamage, m_self->GetAttribute(AttrArmorHP));
            }
        }
    }

    //Hull/Structure:
    //The base hp and damage attributes represent structure.
    float hp(m_self->GetAttribute(AttrHP).get_float());
    if ((d.GetTotal() > 0.0f) and (hp > 0.0f)) {
        float available_hull = hp - m_self->GetAttribute(AttrDamage).get_float();
        Damage DamageToHull = d.MultiplyDup(
            Resonance(AttrKineticDamageResonance),
            Resonance(AttrThermalDamageResonance),
            Resonance(AttrEmDamageResonance),
            Resonance(AttrExplosiveDamageResonance) );

        float hull_damage = DamageToHull.GetTotal();
        if (hull_damage < available_hull) {
            total_damage += hull_damage;
            float new_damage = m_self->GetAttribute(AttrDamage).get_float() + hull_damage;
            m_self->SetAttribute(AttrDamage, new_damage);
            _log(DAMAGE__DEBUG, "%s(%u): Applying %.2f damage to structure. New structure damage: %.2f",
                 GetName(), GetID(), hull_damage, new_damage);
        } else if (IsUnkillable(GetID())) {
            // GM: hold at 1hp
            float max_damage = hp - 1.0f;
            float old_damage = m_self->GetAttribute(AttrDamage).get_float();
            if (max_damage > old_damage) {
                total_damage += (max_damage - old_damage);
                m_self->SetAttribute(AttrDamage, max_damage);
            }
            _log(DAMAGE__INFO, "%s(%u): %.2f damage would have destroyed us, but we're unkillable.",
                 GetName(), GetID(), hull_damage);
        } else {
            total_damage += available_hull;
            //dead....
            _log(DAMAGE__INFO, "%s(%u): %.2f damage has depleted our structure. Time to explode.",
                 GetName(), GetID(), hull_damage);
            killed = true;
        }

        // module damage.
        //  after armor is gone, make damage to random module.
        if (armorDepleted and HasPilot())
            GetShipSE()->DamageRandModule(sConfig.server.ModuleDamageChance);    // config option for random module damage chance
    }

    if (killed) {
        if (m_killed)
            return true;

        // OnNotify:OnTransmission -  (235799, `You have killed this defenseless NPC, bully.  Also, you have killed this NPC and are receiving this message.`)
        Destroy(d);
    } else {
        /**
         * ALL dmg msgs working  22Apr15 (hacked - found the actual msgIDs)
         * fixed msgIDs and removed xmlp  - 15Sept19
         * @todo  still need to check/add detailed dmg msgs
         */
        if (HasPilot()) {
            //  notify player of damage received
            PyDict* dict = new PyDict();
                dict->SetItemString("source", new PyInt(d.srcSE->GetID()));
                dict->SetItemString("weapon", new PyInt((d.chargeRef.get() != nullptr ? d.chargeRef->typeID() : d.weaponRef->typeID())));
                dict->SetItemString("target", new PyInt(GetID()));
                dict->SetItemString("damage", new PyFloat(total_damage));
            PyTuple* tuple = new PyTuple(3);
                tuple->SetItem(0, new PyString("OnDamageMessage"));
                tuple->SetItem(1, new PyString(Dmg::Msg::Taken[damageID]));
                tuple->SetItem(2, dict);
            GetPilot()->QueueDestinyEvent(&tuple);
        }
        if (d.srcSE->HasPilot()) {
            //notify to player of damage done:
            PyDict* dict = new PyDict();
                dict->SetItemString("weapon", new PyInt((d.chargeRef.get() != nullptr ? d.chargeRef->typeID() : d.weaponRef->typeID())));
                dict->SetItemString("target", new PyInt(GetID()));
                dict->SetItemString("damage", new PyFloat(total_damage));
            PyTuple* tuple = new PyTuple(3);
            bool banked = false;
            tuple->SetItem(0, new PyString("OnDamageMessage"));
            if (d.weaponRef->IsModuleItem()) {
                GenericModule* pMod = d.srcSE->GetShipSE()->GetShipItemRef()->GetModule(d.weaponRef->flag());
                if (pMod != nullptr)
                    if (pMod->IsLinked())
                        banked = true;
            }
            if (banked) {
                tuple->SetItem(1, new PyString(Dmg::Msg::Banked[damageID]));
            } else {
                tuple->SetItem(1, new PyString(Dmg::Msg::Given[damageID]));
            }
            tuple->SetItem(2, dict);
            d.srcSE->GetPilot()->QueueDestinyEvent(&tuple);
        } else if (d.srcSE->IsDroneSE()) {
            // verify drone has owner set
            Client* pController(d.srcSE->GetDroneSE()->GetControllerClient());
            if (pController != nullptr) {
                //  notify the controlling pilot of damage done by their drone, the same way as their own turrets'
                PyDict* dict = new PyDict();
                    dict->SetItemString("weapon", new PyInt(d.srcSE->GetSelf()->typeID()));
                    dict->SetItemString("target", new PyInt(GetID()));
                    dict->SetItemString("damage", new PyFloat(total_damage));
                PyTuple* tuple = new PyTuple(3);
                    tuple->SetItem(0, new PyString("OnDamageMessage"));
                    tuple->SetItem(1, new PyString(Dmg::Msg::Given[damageID]));
                    tuple->SetItem(2, dict);
                pController->QueueDestinyEvent(&tuple);
            } else {
                // make error about active drone with no owner set
                _log(DRONE__WARNING, "Drone %u attacking %s with no owner set.", d.srcSE->GetID(), GetName());
            }
        }

        SendDamageStateChanged();

        // npcs start repairing, build threat and answer the attack
        if (IsNPCSE())
            GetNPCSE()->GetAIMgr()->OnDamaged(d.srcSE, total_damage);
    }

    if (sConfig.debug.UseProfiling)
        sProfiler.AddTime(Profile::damage, GetTimeUSeconds() - profileStartTime);

    return killed;
}

void ShipSE::Killed(Damage &fatal_blow) {
    if ((m_bubble == nullptr) or (m_destiny == nullptr) or (m_system == nullptr))
        return; // make error here?

    m_shipRef->SetPopped(true);

    /* {'messageKey': 'ShipExploded', 'dataID': 17881627, 'suppressable': True, 'bodyID': 258841, 'messageType': 'info', 'urlAudio': '', 'urlIcon': '', 'titleID': 258840, 'messageID': 1558}
     * u'ShipExplodedBody'}(u'Your ship has been destroyed by {[character]charID.name}.', None, {u'{[character]charID.name}': {'conditionalValues': [], 'variableType': 0, 'propertyName': 'name', 'args': 0, 'kwargs': {}, 'variableName': 'charID'}})
     */
    uint32 killerID = 0, locationID = GetLocationID();
    Client* pClient(nullptr);
    SystemEntity* killer(fatal_blow.srcSE);

    if (killer->HasPilot()) {
        pClient = killer->GetPilot();
        killerID = pClient->GetCharacterID();
    } else if (killer->IsDroneSE()) {
        pClient = killer->GetDroneSE()->GetControllerClient();
        if (pClient == nullptr)
            pClient = killer->GetDroneSE()->GetOwnerClient();
        if (pClient == nullptr) {
            /** @todo  make error here */
            sLog.Error("Ship::Killed()", "killer == IsDrone and pPlayer == nullptr");
            EvE::traceStack();
        } else {
            killerID = pClient->GetCharacterID();
        }
    } else {
        killerID = killer->GetID();
    }

    // AttrFwLpKill

    //  log faction kill in dynamic data   -allan
    MapDB::AddKill(locationID);
    MapDB::AddFactionKill(locationID);

    // set up basic wreck data
    GPoint wreckPosition = m_destiny->GetPosition();
    if (wreckPosition.isNaN()) {
        sLog.Error("Ship::Killed()", "Wreck Position is NaN");
        return;
    }
    uint32 wreckTypeID = sDataMgr.GetWreckID(m_self->typeID());
    if (!IsWreckTypeID(wreckTypeID)) {
        sLog.Error("Ship::Killed()", "Could not get wreckType for %s of type %u", m_self->name(), m_self->typeID());
        // default to generic frigate wreck till i get better checks and/or complete wreck data
        wreckTypeID = 26557;
    }

    std::string wreck_name = m_self->itemName() + " Wreck";

    if (!m_self->HasPilot()) {
        // a ship left in space by a pilot who logged off is still theirs
        uint32 ownerID(m_system->GetLaw()->GetLoggedOffOwner(m_self->itemID()));
        if (ownerID != 0) {
            KilledLoggedOff(fatal_blow, killer, killerID, pClient, ownerID, wreckTypeID, wreck_name, wreckPosition);
            return;
        }
        // Spawn a wreck for the Ship that was destroyed:
        ItemData wreckItemData(wreckTypeID, killerID, locationID, flagNone, wreck_name.c_str(), wreckPosition, itoa(m_allyID));
        WreckContainerRef wreckItemRef = sItemFactory.SpawnWreckContainer( wreckItemData );
        if (wreckItemRef.get() == nullptr) {
            sLog.Error("Ship::Killed()", "Creating Wreck Item Failed for %s of type %u", wreck_name.c_str(), wreckTypeID);
            return;
        }

        if (is_log_enabled(PHYSICS__TRACE))
            _log(PHYSICS__TRACE, "Ship::Killed() - Ship %s(%u) Position: %.2f,%.2f,%.2f.  Wreck %s(%u) Position: %.2f,%.2f,%.2f.", \
                    GetName(), GetID(), x(), y(), z(), wreckItemRef->name(), wreckItemRef->itemID(), wreckPosition.x, wreckPosition.y, wreckPosition.z);

        DBSystemDynamicEntity wreckEntity = DBSystemDynamicEntity();
            wreckEntity.allianceID = killer->GetAllianceID();
            wreckEntity.categoryID = EVEDB::invCategories::Celestial;
            wreckEntity.corporationID = killer->GetCorporationID();
            wreckEntity.factionID = sDataMgr.GetWreckFaction(wreckTypeID);
            wreckEntity.groupID = EVEDB::invGroups::Wreck;
            wreckEntity.itemID = wreckItemRef->itemID();
            wreckEntity.itemName = wreck_name;
            wreckEntity.ownerID = killerID;
            wreckEntity.typeID = wreckTypeID;
            wreckEntity.position = wreckPosition;

        if (!m_system->BuildDynamicEntity(wreckEntity, m_self->itemID())) {
            sLog.Error("Ship::Killed()", "Spawning Wreck Failed: typeID or typeName not supported: '%u'", wreckTypeID);
            ; /** @todo make error msg here */  //  PyException( MakeCustomError ( "Spawning Wreck Failed: typeID or typeName not supported." ) );
            wreckItemRef->Delete();
            return;
        }

        m_destiny->SendJettisonPacket();
        // wreck was created successfully.  drop loot and add to wreck.
        DropLoot(wreckItemRef, m_self->groupID(), killerID);

        return;
    }

    Client* pPilot(m_self->GetPilot());
    if (pPilot == nullptr)
        return;    //  make error here

    // a strategic cruiser destroyed with its pilot aboard costs a subsystem skill level (needs the subsystems, so before the loot)
    if (!pPilot->InPod())
        T3SkillLoss(pPilot->GetChar().get(), pPilot, fatal_blow);

    // security status penalties for everyone who attacked this pilot illegally, kill rights (CrimeWatch)
    //  a self-destruct has no killer (the pilot is the final blow on the killmail)
    sCrimeWatch.OnShipDestroyed(pPilot->GetCharacterID(), pPilot->InPod(), pPilot->GetSecurityRating(),
            ((pClient != nullptr) ? pClient->GetCharacterID() : 0), m_system, fatal_blow.selfDestruct);

    /* populate kill data for killMail and save to db  -allan 01May16  --updated 13July17 */
    KillData data = KillData();
        data.solarSystemID = m_system->GetID();
        data.victimCharacterID = pPilot->GetCharacterID();
        data.victimCorporationID = m_corpID;
        data.victimAllianceID = m_allyID;
        data.victimFactionID = m_warID;
        data.victimShipTypeID = m_self->typeID();

        data.finalCharacterID = killerID;
        data.finalCorporationID = killer->GetCorporationID();
        data.finalAllianceID = killer->GetAllianceID();
        data.finalFactionID = (killer->GetWarFactionID() > 500021 ? 500021 : killer->GetWarFactionID());
        data.finalShipTypeID = killer->GetTypeID();
        data.finalWeaponTypeID = fatal_blow.weaponRef->typeID();
        data.finalSecurityStatus = 0;   /* fix this */
        data.finalDamageDone = fatal_blow.GetTotal();

        uint32 totalHP = m_self->GetAttribute(AttrHP).get_int();
            totalHP += m_self->GetAttribute(AttrArmorHP).get_int();
            totalHP += m_self->GetAttribute(AttrShieldCapacity).get_int();
        data.victimDamageTaken = totalHP;

    std::stringstream blob;
    std::vector<InventoryItemRef> survivedItems;
    if (pPilot->InPod()) {
        blob << "<items><i t=" << data.victimShipTypeID << " f=0 s=1 d=0 x=1/></items>";
    } else {
        AbortCycle();
        // our drones lose control when this ship is removed from the system

        // remove all charges (per packet data)  ...why???
        //GetShipItemRef()->UnloadAllModules();

        blob << "<items>";
        /* killBlob contains destroyed/dropped items. u'<items><i t=3651 f=0 d=0 x=1/><i t=3634 f=0 d=0 x=1/></items>'  -allan 13July17
            " i*" tag is decoded as follows:
                t = item.typeID
                f = item.flag
                s = item.singleton
                d = item.qtyDropped
                x = item.qtyDestroyed
        */
        std::map<uint32, InventoryItemRef> deadShipInventory;
        deadShipInventory.clear();
        GetShipItemRef()->GetMyInventory()->GetInventoryMap(deadShipInventory);
        if (deadShipInventory.empty()) {
            blob << "<i t=" << data.victimShipTypeID << " f=0 s=1 d=0 x=1/>";
        } else {
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
                // all contained items have 50% chance of drop, except rigs, which do not survive
                //  dropped items keep the damage they had.  the explosion doesn't add any (Crucible)
                if (IsRigSlot(cur.second->flag())) {
                    /* just avoiding survive check */;
                } else if (IsEven(MakeRandomInt(0, 100))) {
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
    }

    data.killBlob = blob.str().c_str();
    data.killTime = GetFileTimeNow();
    data.moonID = 0;

    pPilot->GetChar()->LogKill(data);

    if (pPilot->InPod()) {
        // log podKill
        MapDB::AddPodKill(locationID);

        // no bounty for podding yourself
        if ((pClient != nullptr) and (pClient != pPilot) and !fatal_blow.selfDestruct)
            pClient->GetChar()->PayBounty(pPilot->GetChar());

        std::string corpse_name = pPilot->GetName();
        corpse_name += "'s Frozen Corpse";
        uint32 corpseTypeID = 10041; // typeID from 'invTypes' table for "Frozen Corpse"
        ItemData corpseItemData(corpseTypeID, m_ownerID, locationID, flagNone, corpse_name.c_str(), wreckPosition);
        InventoryItemRef corpseItemRef = sItemFactory.SpawnItem( corpseItemData );
        if (corpseItemRef.get() == nullptr) {
            sLog.Error("Ship::Killed()", "Creating Corpse Item Failed for %s of type %u", corpse_name.c_str(), corpseTypeID);
            DBSystemDynamicEntity corpseEntity = DBSystemDynamicEntity();
            corpseEntity.allianceID = m_allyID;
            corpseEntity.categoryID = EVEDB::invCategories::Celestial;
            corpseEntity.corporationID = m_corpID;
            corpseEntity.factionID = m_warID;
            corpseEntity.groupID = EVEDB::invGroups::Biomass;
            corpseEntity.itemID = corpseItemRef->itemID();
            corpseEntity.itemName = corpse_name;
            corpseEntity.ownerID = 1;   //would this be 'owned' by killer?
            corpseEntity.typeID = corpseTypeID;
            corpseEntity.position = wreckPosition;
            if (!m_system->BuildDynamicEntity(corpseEntity)) {
                sLog.Error("Ship::Killed()", "Spawning Corpse Failed: typeID or typeName not supported: '%u'", corpseTypeID);
            } else if (is_log_enabled(PHYSICS__TRACE)) {
                _log(PHYSICS__TRACE, "Ship::Killed() - Pod %s(%u) Position: %.2f,%.2f,%.2f.  Corpse %s(%u) Position: %.2f,%.2f,%.2f.", \
                    GetName(), GetID(), x(), y(), z(), corpseItemRef->name(), corpseItemRef->itemID(), wreckPosition.x, wreckPosition.y, wreckPosition.z);
            }
        }

        // this method will reset char variables to last clone state after being podded.  NOTE  *** NOT TESTED YET ***
        pPilot->ResetAfterPodded();
    } else {
        // since Crucible, ships destroyed by CONCORD aren't insured
        if (!(killer->IsNPCSE() and killer->GetNPCSE()->IsConcord()))
            PayInsurance();

        m_destiny->SendJettisonPacket();

        uint16 groupID = m_self->groupID();
        GPoint podPosition(wreckPosition);
        podPosition.MakeRandomPointOnSphere(GetShipItemRef()->radius() + pPilot->GetPod()->radius() + MakeRandomFloat(100, 200));
        // this resets client ship data
        pPilot->ResetAfterPopped(podPosition);

        ItemData wreckItemData(wreckTypeID, pPilot->GetCharacterID(), locationID, flagNone, wreck_name.c_str(), wreckPosition, itoa(m_allyID));
        WreckContainerRef wreckItemRef = sItemFactory.SpawnWreckContainer( wreckItemData );
        if (wreckItemRef.get() == nullptr) {
            sLog.Error("Ship::Killed()", "Creating Wreck Item Failed for %s of type %u", wreck_name.c_str(), wreckTypeID);
            return;
        }

        if (is_log_enabled(PHYSICS__TRACE))
            _log(PHYSICS__TRACE, "Ship::Killed() - Ship %s(%u) Position: %.2f,%.2f,%.2f.  Wreck %s(%u) Position: %.2f,%.2f,%.2f.", \
            GetName(), GetID(), x(), y(), z(), wreckItemRef->name(), wreckItemRef->itemID(), wreckPosition.x, wreckPosition.y, wreckPosition.z);

        DBSystemDynamicEntity wreckEntity = DBSystemDynamicEntity();
            wreckEntity.allianceID = killer->GetAllianceID();
            wreckEntity.categoryID = EVEDB::invCategories::Celestial;
            wreckEntity.corporationID = killer->GetCorporationID();
            wreckEntity.factionID = sDataMgr.GetWreckFaction(wreckTypeID);
            wreckEntity.groupID = EVEDB::invGroups::Wreck;
            wreckEntity.itemID = wreckItemRef->itemID();
            wreckEntity.itemName = wreck_name;
            wreckEntity.ownerID = pPilot->GetCharacterID();
            wreckEntity.typeID = wreckTypeID;
            wreckEntity.position = wreckPosition;

        if (!m_system->BuildDynamicEntity(wreckEntity, m_self->itemID())) {
            sLog.Error("Ship::Killed()", "Spawning Wreck Failed for typeID %u", wreckTypeID);
            wreckItemRef->Delete();
            return;
        }

        DropLoot(wreckItemRef, groupID, killerID);

        for (auto cur: survivedItems)
            cur->Move(wreckItemRef->itemID(), flagNone); // populate wreck with items that survived
    }
}

/* a ship (or pod) left in space by a pilot who logged off (logoff timer) was destroyed.  the pilot isn't here, so this
 *  does what the normal path does through the character: killmail, wreck, insurance, and a new pod in space (or a clone
 *  in their home station, for a pod).  the character item is moved out before the ship and its contents are deleted
 */
/* Crucible: a strategic cruiser destroyed with its pilot aboard costs one level's worth of skill points in one random skill
 *  required by its fitted subsystems (removed in 2021).  ejecting or self-destructing avoids it.  the client shows it on the
 *  character sheet (OnSkillPointsRemovedDueToShipLoss -> RecentSkillLossDueToT3Ship).  config server.T3SkillLoss
 */
void ShipSE::T3SkillLoss(Character* pChar, Client* pClient, Damage& fatal_blow)
{
    if (!sConfig.server.T3SkillLoss or (pChar == nullptr))
        return;
    if (m_self->groupID() != EVEDB::invGroups::StrategicCruiser)
        return;
    if (fatal_blow.selfDestruct or (fatal_blow.srcSE == this))
        return;

    std::vector<InventoryItemRef> subSystems;
    m_shipRef->GetMyInventory()->GetItemsByFlagRange(flagSubSystem0, flagSubSystem4, subSystems);
    std::vector<uint16> skills;
    for (auto cur : subSystems) {
        uint16 skillID(cur->GetAttribute(AttrRequiredSkill1).get_uint32());
        if ((skillID == 0) or (pChar->GetSkillLevel(skillID) < 1))
            continue;
        if (std::find(skills.begin(), skills.end(), skillID) == skills.end())
            skills.push_back(skillID);
    }
    if (skills.empty())
        return;

    size_t pick(MakeRandomInt(0, skills.size()));
    if (pick >= skills.size())
        pick = skills.size() - 1;
    uint16 skillID(skills[pick]);
    uint32 lost(pChar->LoseSkillLevel(skillID, EvESkill::Event::ClonePenalty));
    _log(SKILL__INFO, "%s(%u) lost %u skill points in skill %u with the %s.", pChar->name(), pChar->itemID(), lost, skillID, m_self->name());
    if ((lost == 0) or (pClient == nullptr))
        return;

    PyTuple* tmp = new PyTuple(3);
        tmp->SetItem(0, new PyInt(skillID));
        tmp->SetItem(1, new PyInt(lost));
        tmp->SetItem(2, new PyInt(m_self->typeID()));
    pClient->SendNotification("OnSkillPointsRemovedDueToShipLoss", "charid", &tmp, false);
}

void ShipSE::KilledLoggedOff(Damage& fatal_blow, SystemEntity* killer, uint32 killerID, Client* pKillerClient, uint32 ownerID,
                             uint32 wreckTypeID, const std::string& wreck_name, const GPoint& wreckPosition)
{
    m_system->GetLaw()->LoggedOffShipKilled(m_self->itemID());
    CharacterRef cRef(sItemFactory.GetCharacterRef(ownerID));
    bool pod(m_self->groupID() == EVEDB::invGroups::Capsule);
    // the pilot is still aboard a ship left in space at logoff
    if (!pod and (cRef.get() != nullptr))
        T3SkillLoss(cRef.get(), nullptr, fatal_blow);
    uint32 locationID(GetLocationID());
    sCrimeWatch.OnShipDestroyed(ownerID, pod, ((cRef.get() != nullptr) ? cRef->GetSecurityRating() : 0.0f),
            ((pKillerClient != nullptr) ? pKillerClient->GetCharacterID() : 0), m_system);

    KillData data = KillData();
        data.solarSystemID = m_system->GetID();
        data.victimCharacterID = ownerID;
        data.victimCorporationID = m_corpID;
        data.victimAllianceID = m_allyID;
        data.victimFactionID = m_warID;
        data.victimShipTypeID = m_self->typeID();
        data.finalCharacterID = killerID;
        data.finalCorporationID = killer->GetCorporationID();
        data.finalAllianceID = killer->GetAllianceID();
        data.finalFactionID = (killer->GetWarFactionID() > 500021 ? 500021 : killer->GetWarFactionID());
        data.finalShipTypeID = killer->GetTypeID();
        data.finalWeaponTypeID = ((fatal_blow.weaponRef.get() != nullptr) ? fatal_blow.weaponRef->typeID() : killer->GetTypeID());
        data.finalSecurityStatus = 0;
        data.finalDamageDone = fatal_blow.GetTotal();
        data.victimDamageTaken = m_self->GetAttribute(AttrHP).get_int() + m_self->GetAttribute(AttrArmorHP).get_int()
                               + m_self->GetAttribute(AttrShieldCapacity).get_int();
    std::stringstream blob;
    blob << "<items><i t=" << data.victimShipTypeID << " f=0 s=1 d=0 x=1/></items>";
    data.killBlob = blob.str().c_str();
    data.killTime = GetFileTimeNow();
    data.moonID = 0;
    ServiceDB::SaveKillOrLoss(data);

    if (pod) {
        MapDB::AddPodKill(locationID);
        std::string corpse_name(((cRef.get() != nullptr) ? cRef->itemName() : std::string("Unknown")) + "'s Frozen Corpse");
        ItemData corpseItemData(10041, ownerID, locationID, flagNone, corpse_name.c_str(), wreckPosition);
        InventoryItemRef corpseRef(sItemFactory.SpawnItem(corpseItemData));
        if (corpseRef.get() != nullptr) {
            DBSystemDynamicEntity corpseEntity = DBSystemDynamicEntity();
                corpseEntity.allianceID = m_allyID;
                corpseEntity.categoryID = EVEDB::invCategories::Celestial;
                corpseEntity.corporationID = m_corpID;
                corpseEntity.factionID = m_warID;
                corpseEntity.groupID = EVEDB::invGroups::Biomass;
                corpseEntity.itemID = corpseRef->itemID();
                corpseEntity.itemName = corpse_name;
                corpseEntity.ownerID = ownerID;
                corpseEntity.typeID = 10041;
                corpseEntity.position = wreckPosition;
            m_system->BuildDynamicEntity(corpseEntity);
        }
        // they wake up in their clone
        uint32 cloneStationID(0);
        if ((cRef.get() != nullptr) and CharacterDB::GetCharHomeStation(ownerID, cloneStationID) and (cloneStationID != 0)) {
            std::string podName(cRef->itemName() + "'s Capsule");
            ItemData podItem(itemTypeCapsule, ownerID, locTemp, flagNone, podName.c_str());
            ShipItemRef newPod(sItemFactory.SpawnShip(podItem));
            if (newPod.get() != nullptr) {
                newPod->ChangeSingleton(true);
                newPod->Move(cloneStationID, flagHangar);
                newPod->SaveShip();
                cRef->Move(cloneStationID, flagNone);
                cRef->SetActiveShip(newPod->itemID());
                cRef->SetActivePod(newPod->itemID());
                SystemData sysData;
                sDataMgr.GetSystemData(sDataMgr.GetStationSystem(cloneStationID), sysData);
                cRef->SetLocation(cloneStationID, sysData);
                cRef->ResetClone();
            }
        }
        return;
    }

    // since Crucible, ships destroyed by CONCORD aren't insured
    if (!(killer->IsNPCSE() and killer->GetNPCSE()->IsConcord()))
        PayInsurance();
    m_destiny->SendJettisonPacket();

    // half of what was aboard survives, rigs never do
    std::vector<InventoryItemRef> survivedItems;
    std::map<uint32, InventoryItemRef> contents;
    GetShipItemRef()->GetMyInventory()->GetInventoryMap(contents);
    for (auto cur : contents) {
        if ((cRef.get() != nullptr) and (cur.first == cRef->itemID()))
            continue;   // the pilot
        if (IsRigSlot(cur.second->flag()))
            continue;
        if (IsEven(MakeRandomInt(0, 100)))
            survivedItems.push_back(cur.second);
    }

    // they wake up in their pod, where the ship was.  the pod appears when they log in
    if (cRef.get() != nullptr) {
        std::string podName(cRef->itemName() + "'s Capsule");
        ItemData podItem(itemTypeCapsule, ownerID, locTemp, flagNone, podName.c_str());
        ShipItemRef newPod(sItemFactory.SpawnShip(podItem));
        if (newPod.get() != nullptr) {
            GPoint podPosition(wreckPosition);
            podPosition.MakeRandomPointOnSphere(GetShipItemRef()->radius() + newPod->radius() + MakeRandomFloat(100, 200));
            newPod->ChangeSingleton(true);
            newPod->Move(m_system->GetID(), flagCapsule);
            newPod->SetPosition(podPosition);
            newPod->SaveShip();
            cRef->Move(newPod->itemID(), flagPilot);
            cRef->SetActiveShip(newPod->itemID());
            cRef->SetActivePod(newPod->itemID());
            cRef->SaveCharacter();
        }
    }

    ItemData wreckItemData(wreckTypeID, ownerID, locationID, flagNone, wreck_name.c_str(), wreckPosition, itoa(m_allyID));
    WreckContainerRef wreckItemRef(sItemFactory.SpawnWreckContainer(wreckItemData));
    if (wreckItemRef.get() == nullptr)
        return;
    DBSystemDynamicEntity wreckEntity = DBSystemDynamicEntity();
        wreckEntity.allianceID = killer->GetAllianceID();
        wreckEntity.categoryID = EVEDB::invCategories::Celestial;
        wreckEntity.corporationID = killer->GetCorporationID();
        wreckEntity.factionID = sDataMgr.GetWreckFaction(wreckTypeID);
        wreckEntity.groupID = EVEDB::invGroups::Wreck;
        wreckEntity.itemID = wreckItemRef->itemID();
        wreckEntity.itemName = wreck_name;
        wreckEntity.ownerID = ownerID;
        wreckEntity.typeID = wreckTypeID;
        wreckEntity.position = wreckPosition;
    if (!m_system->BuildDynamicEntity(wreckEntity, m_self->itemID())) {
        wreckItemRef->Delete();
        return;
    }
    for (auto cur : survivedItems)
        cur->Move(wreckItemRef->itemID(), flagNone);
}
