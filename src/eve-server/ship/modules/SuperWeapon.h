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

#ifndef __SUPERWEAPON_H__
#define __SUPERWEAPON_H__

#include "ship/modules/ActiveModule.h"

/* titan doomsday (Crucible):  one capital ship target, damage applied once on activation, one cycle (the 10 minute rate of fire).
 *  null-sec only (disallowInEmpireSpace, checked by ActiveModule).  after firing, the titan can't warp for effectDeactivationDelay (30s)
 *  and can't jump, bridge or cloak for jumpDelayDuration (10 minutes) */
class SuperWeapon
: public ActiveModule
{
public:
    SuperWeapon(ModuleItemRef mRef, ShipItemRef sRef);
    virtual ~SuperWeapon()                              { /* do nothing here */ }

    virtual SuperWeapon*        GetSuperWeapon()        { return this; }
    virtual bool                IsSuperWeapon() const   { return true; }

    /* ActiveModule overrides */
    virtual void                Activate(uint16 effectID, uint32 targetID=0, int16 repeat=0);
    virtual bool                CanActivate();

private:
    void                        ApplySuperWeaponDamage();
};

#endif
