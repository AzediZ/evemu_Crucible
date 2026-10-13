
 /**
  * @name EVE_Charges.h
  *     charge (ammo, crystal, script) rules for modules that load them
  *
  * these are pure functions, so the unit tests (eve-test) can check them
  *
  */


#ifndef EVE_CHARGES_H
#define EVE_CHARGES_H

#include <vector>


namespace Charges {

    /* reloadTime (attribute 1795) is only set on hybrid turrets in Crucible (5s).
     *  every other module that loads charges takes the attribute's dogma default, 10s (dgmAttributeTypes.defaultValue)
     */
    static constexpr uint32 DefaultReloadTimeMS = 10000;

    inline uint32 ReloadTimeMS(double reloadTimeAttr)
    {
        if (reloadTimeAttr < 1)
            return DefaultReloadTimeMS;
        return (uint32)(reloadTimeAttr + 0.5);
    }

    /* charges a module can still take:  its capacity over the charge's volume, less what is loaded.
     *  the client works this out the same way (int(capacity / volume - quantity + 1e-07)), the epsilon keeps 0.4/0.01 at 40
     */
    inline int32 RoomFor(double capacity, double chargeVolume, int32 loaded)
    {
        if ((capacity <= 0) or (chargeVolume <= 0))
            return 0;
        int32 room((int32)(capacity / chargeVolume - loaded + 1e-07));
        return (room > 0 ? room : 0);
    }

    /* loading a weapon bank:  the charges are split evenly over its modules, as far as each has room.
     *  what one module has no room for goes to the others.  share[i] is what room[i] gets
     */
    inline void SplitAcrossBank(int32 available, const std::vector<int32>& room, std::vector<int32>& share)
    {
        share.assign(room.size(), 0);
        if (available < 1)
            return;
        while (available > 0) {
            // modules that can still take more
            int32 open(0);
            for (size_t i = 0; i < room.size(); ++i)
                if (share[i] < room[i])
                    ++open;
            if (open == 0)
                return;
            int32 each(available / open);
            int32 extra(available % open);
            int32 given(0);
            for (size_t i = 0; i < room.size(); ++i) {
                if (share[i] >= room[i])
                    continue;
                int32 want(each);
                if (extra > 0) {
                    ++want;
                    --extra;
                }
                int32 left(room[i] - share[i]);
                if (want > left)
                    want = left;
                share[i] += want;
                given += want;
            }
            if (given < 1)
                return;
            available -= given;
        }
    }

    /* crystals don't get used up.  a crystal that gets damaged (crystalsGetDamaged on the crystal:  tech 2 and faction,
     *  not tech 1) takes crystalVolatilityDamage with crystalVolatilityChance per shot, and is destroyed at its hp (1)
     */
    inline bool CrystalBurnsOut(double damage, double hp)
    {
        return (damage >= (hp > 0 ? hp : 1.0));
    }

}

#endif  // EVE_CHARGES_H
