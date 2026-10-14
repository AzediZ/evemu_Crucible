
 /**
  * @name EVE_Sov.h
  *     who the Crucible client shows as a system's sovereign, and where an alliance may hold a claim
  *
  * these are pure functions, so the unit tests (eve-test) can check them
  *
  *  the client (locationInfoSvc.UpdateSOVLocationInfo, sovSvc.GetSovOwnerID, the map's sovereignty colouring) takes the
  *  system's own factionID (mapSolarSystems.factionID) first.  only when that is None does it ask
  *  sovMgr.GetSystemSovereigntyInfo() and use its allianceID.  None there is 'Unclaimed', or 'Unclaimable' in w-space.
  *   empire (high/low-sec):  the empire faction, also in factional warfare (occupancy is a separate line)
  *   npc null-sec:           the pirate/npc faction of mapSolarSystems.factionID (Guristas Pirates in Venal...)
  *   claimable null-sec:     the alliance holding the online TCU, else unclaimed
  *   w-space:                nobody
  *  npc stations, customs offices and npc corps in a system play no part.
  *
  *  sovMgr.GetSystemSovereigntyInfo() must be None wherever no alliance holds a claim:  the sov dashboard looks up
  *  cfg.eveowners.Get(sovInfo.allianceID) and the PI/POS code treats any allianceID as a foreign sovereign.
  */


#ifndef EVE_SOV_H
#define EVE_SOV_H

#include <cstdint>

#include "EVE_Defines.h"


namespace SovRules {

    enum class SpaceKind : uint8_t {
        Empire,         // a faction system with security above 0.0 (high and low-sec)
        NpcNull,        // a faction system at 0.0 or below (pirate and npc regions)
        Claimable,      // k-space without a faction, held by alliances through TCUs
        Wormhole        // w-space, can't be claimed
    };

    // factionID and security are the system's mapSolarSystems.factionID (0 for NULL) and security
    inline SpaceKind Classify(uint32_t systemID, uint32_t factionID, double security)
    {
        if (IsWSpaceID(systemID))
            return SpaceKind::Wormhole;
        if (factionID == 0)
            return SpaceKind::Claimable;
        return (security > 0.0 ? SpaceKind::Empire : SpaceKind::NpcNull);
    }

    // only faction-free k-space can be claimed (TCUs, indices, hub upgrades)
    inline bool IsClaimable(SpaceKind kind)
    {
        return (kind == SpaceKind::Claimable);
    }

    // sovMgr.GetSystemSovereigntyInfo() sends the claim only for an alliance claim in claimable space, else None
    inline bool ReportsClaim(SpaceKind kind, uint32_t claimAllianceID)
    {
        return (IsClaimable(kind) and (claimAllianceID > 0));
    }

    // the sovereign the client shows:  the system's faction, else the claiming alliance.  0 is unclaimed/unclaimable
    inline uint32_t DisplayedHolder(SpaceKind kind, uint32_t factionID, uint32_t claimAllianceID)
    {
        if (factionID > 0)
            return factionID;
        return (ReportsClaim(kind, claimAllianceID) ? claimAllianceID : 0);
    }

    // why a sovereignty structure can't be launched in this kind of space, "" if it can
    inline const char* LaunchRefusal(SpaceKind kind)
    {
        switch (kind) {
            case SpaceKind::Empire:     return "Launching sovereignty structures is forbidden in empire space.";
            case SpaceKind::NpcNull:    return "Sovereignty cannot be claimed in space held by an NPC faction.";
            case SpaceKind::Wormhole:   return "Sovereignty cannot be claimed in wormhole space.";
            case SpaceKind::Claimable:  break;
        }
        return "";
    }
}

#endif  // EVE_SOV_H
