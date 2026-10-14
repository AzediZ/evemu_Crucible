
 /**
  * @name EVE_Customs.h
  *     planetary customs office tax rules (Crucible)
  *
  * these are pure functions, so the unit tests (eve-test) can check them
  *
  *  the client works the money out itself (common/script/planet/entities/spaceportPin.py):
  *   import  = sum(qty * importTax of the launchpad (0.5) * importTaxMultiplier of the commodity) * rate
  *   export  = sum(qty * exportTax of the launchpad (1.0) * importTaxMultiplier of the commodity) * rate
  *   launch  = sum(qty * exportTax of the command center (3.0) * importTaxMultiplier of the commodity) * 0.05
  *  the rate comes from planetOrbitalRegistryBroker.GetTaxRate(officeID), None when the office doesn't let the
  *  character in.  a launch never asks:  CommandCenterContainer shows GetExportTax(payload) at its default rate.
  *
  *  npc offices charge npcCustomsOfficeTaxRate (1780) of their type:  0.17 on the Interbus Customs Office, the
  *  dogma default 0.1 on the Customs Office that CONCORD owns in high-sec.  defaultCustomsOfficeTaxRate (1781, 0.05,
  *  const.defaultCustomsOfficeTaxRate) is what a new player office starts at and what a launch costs.
  *  sovereignty plays no part in any of it.
  */


#ifndef EVE_CUSTOMS_H
#define EVE_CUSTOMS_H

#include <cstdint>


namespace CustomsRules {

    // const.defaultCustomsOfficeTaxRate:  launches from a command center, and a new player office's rates
    static constexpr double LaunchTaxRate = 0.05;

    // the contact standing levels (const.contactHorribleStanding .. contactHighStanding), as EVEPOS::StandingValues
    static constexpr int8_t StandingHorrible = -10;
    static constexpr int8_t StandingBad      = -5;
    static constexpr int8_t StandingNeutral  = 0;
    static constexpr int8_t StandingGood     = 5;
    static constexpr int8_t StandingHigh     = 10;

    // a standing (the owner's towards the character), as the level the orbital configuration window offers.
    //  no standing at all is neutral
    inline int8_t StandingTier(bool found, double standing)
    {
        if (!found)
            return StandingNeutral;
        if (standing >= 10)
            return StandingHigh;
        if (standing >= 5)
            return StandingGood;
        if (standing > -5)
            return StandingNeutral;
        if (standing > -10)
            return StandingBad;
        return StandingHorrible;
    }

    // what the owner set in the orbital configuration window (planetOrbitalRegistryBroker.UpdateSettings)
    struct PlayerOffice {
        bool allowAlliance;     // the alliance rate for the owner's alliance, else alliance members go by standing
        bool allowStandings;    // the standing rates for everyone else, else only the corporation (and alliance) get in
        int8_t minStanding;     // the lowest level let in
        float corporation;
        float alliance;
        float standingHorrible;
        float standingBad;
        float standingNeutral;
        float standingGood;
        float standingHigh;
    };

    // a character's standing only matters when neither the corporation nor the alliance rate applies
    inline bool NeedsStanding(const PlayerOffice& office, bool ownerCorpMember, bool ownerAllianceMember)
    {
        if (ownerCorpMember or (office.allowAlliance and ownerAllianceMember))
            return false;
        return office.allowStandings;
    }

    /* the rate a character pays at a player-owned office, -1 when it doesn't let them in (PortStandingCheckFail).
     *  the owner's corporation, then its alliance (if allowed), then the character's standing tier (if allowed and at
     *  least the minimum).  the same in claimed, unclaimed and npc null-sec, low-sec and w-space
     */
    inline float PlayerOfficeRate(const PlayerOffice& office, bool ownerCorpMember, bool ownerAllianceMember, int8_t tier)
    {
        if (ownerCorpMember)
            return office.corporation;
        if (office.allowAlliance and ownerAllianceMember)
            return office.alliance;
        if (!office.allowStandings or (tier < office.minStanding))
            return -1;
        if (tier >= StandingHigh)
            return office.standingHigh;
        if (tier >= StandingGood)
            return office.standingGood;
        if (tier >= StandingNeutral)
            return office.standingNeutral;
        if (tier >= StandingBad)
            return office.standingBad;
        return office.standingHorrible;
    }

    // the client refuses to launch an orbital for the corporation in high-sec (menusvc LaunchForCorp,
    //  not (checkIfOrbital and checkIfInHighSec)):  gantries go up in low-sec, null-sec and w-space only
    inline bool GantryAllowed(bool highSec)
    {
        return !highSec;
    }
}

#endif  // EVE_CUSTOMS_H
