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
    Author:     Allan
*/

#include "eve-server.h"

#include "Client.h"
#include "EVEServerConfig.h"
#include "Profiler.h"
#include "character/Character.h"
#include "inventory/AttributeEnum.h"
#include "npc/NPC.h"
#include "npc/ai/NpcMath.h"
#include "system/DestinyManager.h"
#include "ship/Missile.h"
#include "ship/Ship.h"
#include "ship/modules/GenericModule.h"
#include "system/Damage.h"
#include "system/SystemBubble.h"
#include "ship/modules/EwarModule.h"

Missile::Missile( InventoryItemRef self, EVEServiceManager& services, SystemManager* pSystem, InventoryItemRef modRef, SystemEntity* tSE, SystemEntity* pSE, GenericModule* pMod)
: DynamicSystemEntity(self, services, pSystem),
  m_modRef(modRef),
  m_targetSE(tSE),
  m_targetID(tSE->GetID()),
  m_fromSE(pSE),
  m_fromID(pSE->GetID()),
  m_lifeTimer(0),
  m_damageMod(1),
  m_alive(true),
  m_orbitingID(0),
  m_speed(0),
  m_hullHP(self->GetAttribute(AttrHP).get_int()),
  m_secStatus(0.0f),
  m_numLaunchers(1)
{
    if (pSE->HasPilot()) {
        m_ownerID = pSE->GetPilot()->GetChar()->itemID();
    } else {
        m_ownerID = self->ownerID();
    }

    m_warID = pSE->GetWarFactionID();
    m_allyID = pSE->GetAllianceID();
    m_corpID = pSE->GetCorporationID();

    // the launcher copied the loaded charge's attributes onto us (ActiveModule::LaunchMissile), so the pilot's skill,
    //  implant, hull, rig and subsystem bonuses are already in flight time, velocity, explosion and damage
    double flightTime = self->GetAttribute(AttrExplosionDelay).get_float();

    if (pSE->HasPilot()) {
        Character* pChar = pSE->GetPilot()->GetChar().get();
        // ballistic control systems multiply the character's missileDamageMultiplier
        if (pChar->HasAttribute(AttrMissileDamageMultiplier))
            m_damageMod *= pChar->GetAttribute(AttrMissileDamageMultiplier).get_float();
        m_secStatus = pChar->GetSecurityRating();
    } else if ((pMod == nullptr) and (pSE->IsNPCSE() or pSE->IsSentrySE()) and (modRef.get() != nullptr)) {
        /* npc and sentry missiles (modRef is the npc itself):  its missileDamageMultiplier (212, dgm default 1).
         *  npc missiles don't use damageMultiplier (64), that one is for turrets (npc ai plan 5.2, bug B12) */
        //  law npcs and sentries always use theirs (as their turrets do);  other npcs per npc.UseDamageMultiplier
        bool always(pSE->IsSentrySE() or (pSE->IsNPCSE() and pSE->GetNPCSE()->IsLawEnforcer()));
        if ((sConfig.npc.UseDamageMultiplier or always) and modRef->HasAttribute(AttrMissileDamageMultiplier)) {
            float mult(modRef->GetAttribute(AttrMissileDamageMultiplier).get_float());
            if (mult > 0.0f)
                m_damageMod *= mult;
        }
    }

    // overloading a launcher doesn't change missile damage.  in Crucible a launcher's overload effect (overloadRofBonus)
    //  shortens its cycle time.  overloadDamageModifier is only on turrets.

    flightTime *= sConfig.rates.missileTime;

    /* a bank fires one stacked missile (dev blog "Weapon Grouping"): damage and hp times the loaded launchers,
     *  flight time unchanged.  only online, loaded launchers fire
     */
    if ((pMod != nullptr) and pMod->IsLinked() and m_fromSE->IsShipSE()) {
        uint8 count = m_fromSE->GetShipSE()->GetShipItemRef()->GetLoadedLinkedCount(pMod);
        if (count > 1) {
            m_damageMod *= count;
            m_hullHP *= count;
            m_numLaunchers = count;
            // the stacked missile takes damage (defenders) against the combined hp
            m_self->SetAttribute(AttrHP, m_hullHP, false);
        }
    }

    m_lifeTimer.Start(flightTime);

    //_log(DAMAGE__MESSAGE, "Created Missile object for %s (%u)", self.get()->name(), self.get()->itemID());
}

void Missile::Process() {
    // shot down by a defender (or anything else that destroyed it):  it goes without hitting
    if (IsDead())
        m_alive = false;
    if (!m_alive) {
        Delete();
        return;
    }
    if (m_lifeTimer.Check(false)) {
        EndOfLife();
        return;
    }
    double profileStartTime(GetTimeUSeconds());
    /*  Enable base call to Process Targeting and Movement  */
    SystemEntity::Process();

    // a missile whose shooter has gone (left the system, died, cloaked or left the missile's bubble) does no damage
    if (!ShooterPresent()) {
        EndOfLife();
        return;
    }

    // the impact is found by the movement simulation, on the tick the missile's path reaches its target
    if (m_destiny->HasMissileHit()) {
        m_targetSE = SystemMgr()->GetSE(m_targetID);
        if (m_targetSE == nullptr) {
            EndOfLife();
            return;
        }
        HitTarget();
    }
    if (sConfig.debug.UseProfiling)
        sProfiler.AddTime(Profile::missile, GetTimeUSeconds() - profileStartTime);
}

void Missile::EncodeDestiny( Buffer& into )
{
    using namespace Destiny;
    BallHeader head = BallHeader();
        head.entityID = GetID();
        head.mode = m_destiny->GetState();
        head.radius = GetRadius();
        head.posX = x();
        head.posY = y();
        head.posZ = z();
        head.flags = Ball::Flag::IsFree | (m_destiny->IsMassive() ? Ball::Flag::IsMassive : 0);
    into.Append( head );
    MassSector mass = MassSector();
        mass.mass = m_destiny->GetMass();
        mass.cloak = 0;
        mass.harmonic = m_harmonic;
        mass.corporationID = m_corpID;
        mass.allianceID = (IsAlliance(m_allyID) ? m_allyID : -1);
    into.Append( mass );
    m_destiny->EncodeDestiny( into );

    _log(SE__DESTINY, "Missile::EncodeDestiny(): %s - id:%lli, mode:%u, flags:0x%X", GetName(), head.entityID, head.mode, head.flags);
}

PyDict* Missile::MakeSlimItem() {
    _log(SE__SLIMITEM, "MakeSlimItem for Missile %u", m_self->itemID());
    PyDict *slim = new PyDict();
        slim->SetItemString("itemID",                   new PyLong(m_self->itemID()));
        slim->SetItemString("typeID",                   new PyInt(m_self->typeID()));
        slim->SetItemString("groupID",                  new PyInt(m_self->groupID()));
        slim->SetItemString("categoryID",               new PyInt(m_self->categoryID()));
        slim->SetItemString("name",                     new PyString(m_self->itemName()));
        slim->SetItemString("sourceModuleID",           new PyInt(m_modRef->itemID()));
        slim->SetItemString("sourceShipID",             new PyInt(m_fromID));
        slim->SetItemString("corpID",                   IsCorp(m_corpID) ? new PyInt(m_corpID) : PyStatic.NewNone());
        slim->SetItemString("allianceID",               IsAlliance(m_allyID) ? new PyInt(m_allyID) : PyStatic.NewNone());
        slim->SetItemString("warFactionID",             IsFaction(m_warID) ? new PyInt(m_warID) : PyStatic.NewNone());
        slim->SetItemString("securityStatus",           new PyFloat(m_secStatus));
        slim->SetItemString("ownerID",                  new PyInt(m_ownerID));
        slim->SetItemString("numLaunchers",             new PyInt(m_numLaunchers));
    return(slim);
}


void Missile::HitTarget() {
    // Create Damage object:
    Damage d(m_fromSE, m_modRef, m_self, EVEEffectID::missileLaunching);

    /*  this is damage formula for missiles
     * Damage = D * MIN(1, Sr/Er, (Ev/V * Sr/Er)^(ln(DRF) / ln(DRS)) )
     *
     * D = base damage of the missile,
     * Sr = signature radius of the target,
     * Er = Explosion radius of the missile,
     * Ev = Explosion Velocity of the missile,
     * V = velocity of the target ship,
     * DRF = damage reduction factor of the missile.
     * DRS = damage reduction sensitivity of the missile.
     * MIN being a function that chooses the lower of the given vaules,
     * ln is natural logarithm.
     */
    double Sr = m_targetSE->GetSignatureRadius();    // includes target painters
    double Er = m_self->GetAttribute(AttrAoeCloudSize).get_float(); // Explosion Radius
    double Ev = m_self->GetAttribute(AttrAoeVelocity).get_float(); // Explosion Velocity
    double DRF = m_self->GetAttribute(AttrAoeDamageReductionFactor).get_float(); // Damage Reduction Factor
    double DRS = m_self->GetAttribute(AttrAoeDamageReductionSensitivity).get_float(); // Damage Reduction Sensitivity

    GPoint Vel = m_targetSE->GetVelocity();
    double V = Vel.length();
    if (V <= 0)
        V = 1;

    // apply damage modifier from char skills, if applicable
    d *= m_damageMod;
    // a damaged missile does less damage (its share of the bank's launchers is gone)
    float missileDamage(m_self->GetAttribute(AttrDamage).get_float());
    if ((missileDamage > 0) and (m_hullHP > 0))
        d *= EvE::max(1.0 - (missileDamage / m_hullHP));
    // apply missile damage formula to computed total damage (shared, guarded:  npc/ai/NpcMath)
    d *= NpcMath::MissileDamageFactor(Sr, Er, Ev, V, DRF, DRS);

    m_targetSE->ApplyDamage(d);
    m_alive = false;
}

/* Crucible's destiny (server side of Evolve, after moving a missile) looks up the missile's owner each tick.
 *  if the owner is gone, cloaked, or in a different bubble (when the missile is in one), the missile collides with
 *  itself and is removed without hitting anything.  this is why missiles stop doing damage once their shooter warps off.
 */
bool Missile::ShooterPresent() {
    m_fromSE = SystemMgr()->GetSE(m_fromID);
    if ((m_fromSE == nullptr) or m_fromSE->IsDead())
        return false;
    if ((m_fromSE->DestinyMgr() != nullptr) and m_fromSE->DestinyMgr()->IsCloaked())
        return false;
    if ((SysBubble() != nullptr) and (m_fromSE->SysBubble() != SysBubble()))
        return false;
    return true;
}

void Missile::EndOfLife() {
    m_alive = false;
    Delete();
}

void Missile::Delete() {
    //  cleanup here
    if (m_alive)
        return;
    // do we need to do anything else here?
    SystemEntity::Delete();
}


/* bombs (Missile.h).  the launcher is the bomb's 'target' too:  destiny's LaunchMissile can't follow a ball that is its own
 *  launcher, so it sends the bomb along the launcher's velocity (DestinyManager::ApplyLaunchMissile) */
namespace {
    const uint16 BombECMGroup = 863;        // Lockbreaker Bomb
    const uint16 BombEnergyGroup = 864;     // Void Bomb
}

bool Bomb::IsBombGroup(uint16 groupID)
{
    return ((groupID == EVEDB::invGroups::Bomb) or (groupID == BombECMGroup) or (groupID == BombEnergyGroup));
}

Bomb::Bomb(InventoryItemRef self, EVEServiceManager& services, SystemManager* pSystem, InventoryItemRef modRef, SystemEntity* pLauncher, GenericModule* pMod)
: Missile(self, services, pSystem, modRef, pLauncher, pLauncher, pMod)
{
    // ballistic control systems are for missiles.  the bomb's own (skill-modified) damage is all it does
    m_damageMod = 1.0f;
}

void Bomb::Process()
{
    // destroyed by another bomb:  it goes without a blast
    if (IsDead())
        m_alive = false;
    if (!m_alive) {
        Delete();
        return;
    }
    if (m_lifeTimer.Check(false)) {
        Detonate();
        return;
    }
    SystemEntity::Process();
}

void Bomb::Detonate()
{
    m_alive = false;
    SystemBubble* pBubble(SysBubble());
    SystemManager* pSys(SystemMgr());
    if ((pBubble != nullptr) and (pSys != nullptr)) {
        // the bomber, if it's still in the system.  otherwise the bomb is the source
        SystemEntity* pFrom(pSys->GetSE(m_fromID));
        if ((pFrom == nullptr) or pFrom->IsDead())
            pFrom = this;

        const GPoint at(GetPosition());
        double range(m_self->GetAttribute(AttrExplosionRange).get_double());
        if (range <= 0.0)
            range = m_self->GetAttribute(AttrEmpFieldRange).get_double();
        double cloud(m_self->GetAttribute(AttrAoeCloudSize).get_double());
        uint16 group(m_self->groupID());
        float drain(m_self->GetAttribute(AttrEnergyDestabilizationAmount).get_float());

        std::map<uint32, SystemEntity*> entities;
        pBubble->GetEntities(entities);
        std::vector<uint32> ids;
        for (auto cur : entities) {
            SystemEntity* pSE(cur.second);
            if ((pSE == this) or pSE->IsDead())
                continue;
            if ((at.distance(pSE->GetPosition()) - pSE->GetRadius()) > range)
                continue;
            ids.push_back(cur.first);
        }

        uint16 hit(0);
        for (auto id : ids) {
            SystemEntity* pSE(pSys->GetSE(id));
            if ((pSE == nullptr) or pSE->IsDead())
                continue;
            Damage d(pFrom, m_modRef, m_self, EVEEffectID::missileLaunching);
            d *= m_damageMod;
            if (pSE->IsMissileSE()) {
                if (IsBombGroup(pSE->GetSelf()->groupID()))
                    BlastBomb(pSE->GetMissileSE(), d);
                continue;
            }
            if (!pSE->IsShipSE() and !pSE->IsNPCSE() and !pSE->IsDroneSE() and !pSE->IsSentrySE())
                continue;
            ++hit;
            // Lockbreaker:  an ecm burst against the target's sensor strength
            if ((group == BombECMGroup) and (pSE->TargetMgr() != nullptr)
            and !pSE->GetSelf()->GetAttribute(AttrDisallowOffensiveModifiers).get_bool()
            and Ewar::RollJam(m_self, at, pSE, AttrExplosionRange, AttrFalloff))
                Ewar::BreakLocks(pSE);
            // Void:  a flat capacitor drain
            if ((group == BombEnergyGroup) and (drain > 0.0f))
                Ewar::DrainCapacitor(pSE, drain);
            // the blast, reduced only by the target's signature radius
            if (cloud > 0.0) {
                double sig(pSE->GetSignatureRadius());
                if (sig < cloud)
                    d *= (float)(sig / cloud);
            }
            if (pSE->IsDead())
                continue;
            pSE->ApplyDamage(d);
        }
        _log(DAMAGE__MESSAGE, "Bomb::Detonate() - %s(%u) from %u went off with %u in its %.0fm range", GetName(), GetID(), m_fromID, hit, range);

        // the client shows the blast
        if (m_destiny != nullptr)
            m_destiny->SendTerminalExplosion(GetID(), pBubble->GetID(), isGlobal());
    }
    Delete();
}

void Bomb::BlastBomb(Missile* pOther, Damage& d)
{
    if ((pOther == nullptr) or !pOther->IsAlive())
        return;
    // bombs have only armour (and a little structure).  a missing resonance is no resistance
    InventoryItemRef oRef(pOther->GetSelf());
    auto res = [&oRef](uint16 attrID) { return (oRef->HasAttribute(attrID) ? oRef->GetAttribute(attrID).get_float() : 1.0f); };
    float damage(d.GetEM() * res(AttrArmorEmDamageResonance) + d.GetThermal() * res(AttrArmorThermalDamageResonance)
               + d.GetKinetic() * res(AttrArmorKineticDamageResonance) + d.GetExplosive() * res(AttrArmorExplosiveDamageResonance));
    double cloud(m_self->GetAttribute(AttrAoeCloudSize).get_double());
    double sig(oRef->GetAttribute(AttrSignatureRadius).get_double());
    if ((cloud > 0.0) and (sig < cloud))
        damage *= (float)(sig / cloud);
    float hp(oRef->GetAttribute(AttrArmorHP).get_float() + oRef->GetAttribute(AttrHP).get_float());
    if (damage >= hp) {
        _log(DAMAGE__MESSAGE, "Bomb::BlastBomb() - %s(%u) destroyed %s(%u) with %.0f damage (%.0f hp)", GetName(), GetID(), pOther->GetName(), pOther->GetID(), damage, hp);
        pOther->Intercepted();
    }
}
