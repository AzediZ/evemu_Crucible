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
    Author:        AknorJaden
    Rewrite:       EVEmu Team (Crucible doomsday, Oct 2026)
*/

#include "eve-server.h"

#include "Client.h"
#include "ship/Ship.h"
#include "ship/modules/SuperWeapon.h"
#include "system/Damage.h"
#include "system/TargetManager.h"

SuperWeapon::SuperWeapon(ModuleItemRef mRef, ShipItemRef sRef)
: ActiveModule(mRef, sRef)
{
}

bool SuperWeapon::CanActivate()
{
    /* Crucible: "Titan super weapons can now only fire at other capital ships.  It can no longer fire its superweapon at any structures."
     *  ActiveModule::Activate() has set m_targetSE.  the module must be cleared before throwing
     */
    try {
        if (m_targetSE == nullptr)
            throw UserError("DeniedActivateTargetNotPresent");
        // the target must be locked
        Client* pClient(m_shipRef->GetPilot());
        if ((pClient == nullptr) or (pClient->GetShipSE() == nullptr)
        or (pClient->GetShipSE()->TargetMgr()->GetTarget(m_targetSE->GetID(), true) == nullptr))
            throw UserError("DeniedActivateTargetNotPresent");

        InventoryItemRef targRef(m_targetSE->GetSelf());
        // npcs flagged immune (AI_ImmuneToSuperWeapon)
        if (targRef->GetAttribute(AttrAI_ImmuneToSuperWeapon) > EvilZero)
            throw UserError("CantActivateDoomsDayOnImmuneShips")
                    .AddFormatValue("targetName", new PyString(m_targetSE->GetName()))
                    .AddFormatValue("moduleName", new PyString(m_modRef->name()));
        // ships (players' or npcs') only
        if (!m_targetSE->IsShipSE() and !m_targetSE->IsNPCSE())
            throw UserError("CantActivateDoomsDayOnNonShip")
                    .AddFormatValue("module", new PyInt(typeID()));
        // capitals only (titan, supercarrier, carrier, dreadnought, freighter, jump freighter, rorqual)
        if (targRef->GetAttribute(AttrIsCapitalSize) < EvilOne)
            throw UserError("CannotActivateSuperWeaponOnNonCapitalShip")
                    .AddFormatValue("moduleTypeID", new PyInt(typeID()));
    } catch (...) {
        Clear();
        throw;
    }

    // disallowInEmpireSpace, isotopes and capacitor are checked in the base class
    return ActiveModule::CanActivate();
}

void SuperWeapon::Activate(uint16 effectID, uint32 targetID/*0*/, int16 repeat/*0*/)
{
    ActiveModule::Activate(effectID, targetID, repeat);

    if ((m_ModuleState != Module::State::Activated) or !m_shipRef->HasPilot() or (m_targetSE == nullptr))
        return;

    // no warp for effectDeactivationDelay (ms), no jump drive, portal or cloak for jumpDelayDuration (s)
    ShipSE* pShip(m_shipRef->GetPilot()->GetShipSE());
    int64 now(GetFileTimeNow());
    pShip->SetWarpLockout(now + (GetAttribute(AttreffectDeactivationDelay).get_int() * (EvE::Time::Second / 1000)));
    pShip->SetJumpLockout(now + (GetAttribute(AttrJumpDelayDuration).get_int() * EvE::Time::Second));

    // the damage lands at once (no build-up delay).  the cycle that follows is the rate of fire
    ApplySuperWeaponDamage();
}

void SuperWeapon::ApplySuperWeaponDamage()
{
    ShipSE* pShip(m_shipRef->GetPilot()->GetShipSE());
    // Doomsday Operation raises damageMultiplier through dogma
    float mult(HasAttribute(AttrDamageMultiplier) ? GetAttribute(AttrDamageMultiplier).get_float() : 1.0f);
    if (mult <= 0.0f)
        mult = 1.0f;
    Damage d(pShip, m_modRef,
             GetAttribute(AttrKineticDamage).get_float() * mult,
             GetAttribute(AttrThermalDamage).get_float() * mult,
             GetAttribute(AttrEmDamage).get_float() * mult,
             GetAttribute(AttrExplosiveDamage).get_float() * mult,
             1.0f, m_effectID);

    _log(MODULE__MESSAGE, "%s fired %s(%u) at %s(%u) for %.0f damage", pShip->GetName(), m_modRef->name(), itemID(),
            m_targetSE->GetName(), m_targetSE->GetID(), d.GetTotal());

    // the offensive act was recorded by ActiveModule::CrimeCheck() on activation
    m_targetSE->ApplyDamage(d);
}
