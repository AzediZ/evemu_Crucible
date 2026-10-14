
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
  *
  *  sovereignty blockade (Dominion rules, unchanged in Crucible):
  *   a Sovereignty Blockade Unit anchors 30-150 km from a stargate, one per gate, only where sovereignty is claimed.
  *   the claim's 'contested' (sovMgr.GetSystemSovereigntyInfo and ProcessSovStatusChanged) is the number of stargates
  *   with an online SBU, whoever owns it.  the client (sovSvc.GetContestedState) shows 'Vulnerable' when
  *   contested * 2 > the system's stargates and 'Contested' for any smaller count above 0.
  *   while more than half the gates are blockaded the claiming TCU and the claim's hub can be attacked and no TCU can be
  *   onlined (CantOnlineDisruptorsOnline).  when the count drops back to half or less they are invulnerable again.
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

    /* sovereignty blockade.  sbuGates is the number of stargates with an online SBU, gates the system's stargates */

    // more than half the stargates are blockaded:  the claim (TCU and hub) is vulnerable.  sovSvc.GetContestedState
    inline bool SbuThresholdMet(uint32_t sbuGates, uint32_t gates)
    {
        return ((sbuGates > 0) and ((uint64_t)sbuGates * 2 > (uint64_t)gates));
    }

    enum class Blockade : uint8_t {
        None,           // no stargate has an online SBU
        Contested,      // some do, half of the gates or fewer ('Contested')
        Vulnerable      // more than half do ('Vulnerable')
    };

    inline Blockade BlockadeState(uint32_t sbuGates, uint32_t gates)
    {
        if (sbuGates == 0)
            return Blockade::None;
        return (SbuThresholdMet(sbuGates, gates) ? Blockade::Vulnerable : Blockade::Contested);
    }

    // the count as sent in 'contested' and kept in mapSystemSovInfo.contested (tinyint unsigned)
    inline uint8_t ContestedValue(uint32_t sbuGates)
    {
        return (sbuGates > 255 ? 255 : (uint8_t)sbuGates);
    }

    // what the holding alliance hears when the count goes from oldGates to newGates
    struct BlockadeChange {
        bool newSbu;            // another gate has an online SBU (notificationTypeSovDisruptorMsg)
        bool vulnerable;        // the threshold was crossed upwards (notificationTypeAllStructVulnerableMsg, flag vulnerable audio)
        bool stable;            // ...and back down (notificationTypeAllStrucInvulnerableMsg, 'Sovereignty claim stable')
    };

    inline BlockadeChange CompareBlockade(uint32_t oldGates, uint32_t newGates, uint32_t gates)
    {
        BlockadeChange change = BlockadeChange();
        change.newSbu = (newGates > oldGates);
        bool was(SbuThresholdMet(oldGates, gates)), now(SbuThresholdMet(newGates, gates));
        change.vulnerable = (now and !was);
        change.stable = (was and !now);
        return change;
    }
}

#endif  // EVE_SOV_H
