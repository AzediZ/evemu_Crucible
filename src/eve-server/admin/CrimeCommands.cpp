/**
 * @name CrimeCommands.cpp
 *     GM commands for crime and CONCORD
 *
 * @date:          11 October 2026
 *
 */

#include "eve-server.h"

#include "Client.h"
#include "EntityList.h"
#include "EVEServerConfig.h"
#include "admin/AllCommands.h"
#include "admin/CommandDB.h"
#include "npc/LawEnforcement.h"
#include "npc/NPC.h"
#include "system/CrimeWatch.h"
#include "system/SystemManager.h"

/* /crime (me|charID|entityID) [victimID]
 *  starts CONCORD the way the game does, from the first act:
 *   a pilot: an attack on the victim, the same call a weapon cycle makes (CrimeWatch::OffensiveAct), so the normal rules
 *     decide whether it's a crime.  with no victim the victim is you, or a civilian (nobody, never a legal target) if
 *     the pilot is you.  a crime gives the criminal flag, the security status hit, and CONCORD in high-sec.
 *   an NPC or any other entity: NPCs never commit crimes, so it's marked a criminal as if it had attacked the victim.
 *     CONCORD comes for it in high-sec, and sentries shoot it in empire space.
 */
PyResult Command_crime(Client* pClient, CommandDB* db, EVEServiceManager &services, const Seperator& args)
{
    const char* usage("Correct Usage: /crime (me|charID|entityID) [victimID]");
    if (!sConfig.crime.Enabled)
        throw CustomError ("CrimeWatch is disabled (<crime><Enabled> in the server config).");
    if (args.argCount() < 2)
        throw CustomError (usage);

    Client* pAggressor(nullptr);
    SystemEntity* pEntity(nullptr);
    if (args.arg(1) == "me") {
        pAggressor = pClient;
    } else if (args.isNumber(1)) {
        uint32 id(atoi(args.arg(1).c_str()));
        if (IsCharacterID(id)) {
            pAggressor = sEntityList.FindClientByCharID(id);
            if (pAggressor == nullptr)
                throw CustomError ("Character %u is not online.", id);
        } else if (pClient->SystemMgr() != nullptr) {
            pEntity = pClient->SystemMgr()->GetSE(id);
            if (pEntity == nullptr)
                throw CustomError ("%u is not in space in your system.", id);
            if (pEntity->IsStaticEntity())
                throw CustomError ("%s can't commit crimes.", pEntity->GetName());
            // a ship with a pilot is the pilot's crime
            if (pEntity->HasPilot())
                pAggressor = pEntity->GetPilot();
        }
    } else {
        throw CustomError (usage);
    }

    SystemManager* pSystem(pAggressor != nullptr ? pAggressor->SystemMgr() : pEntity->SystemMgr());
    if (pSystem == nullptr)
        throw CustomError ("The aggressor isn't in a solar system.");

    SystemEntity* pVictim(nullptr);
    if (args.argCount() > 2) {
        if (!args.isNumber(2))
            throw CustomError (usage);
        pVictim = pSystem->GetSE(atoi(args.arg(2).c_str()));
        if (pVictim == nullptr)
            throw CustomError ("Victim %s is not in space in the aggressor's system.", args.arg(2).c_str());
    } else if ((pAggressor != pClient) and (pClient->GetShipSE() != nullptr) and (pClient->SystemMgr() == pSystem)) {
        pVictim = pClient->GetShipSE();
    }

    float trueSec(pSystem->GetSystemSecurityRating());
    uint8 secClass(CrimeWatch::SecurityClass(trueSec));
    std::string where(pSystem->GetNameStr() + " (" + std::to_string(CrimeWatch::SecurityLevel(trueSec)).substr(0, 3) + ")");
    if (secClass == Crime::SecClass::Null)
        throw CustomError ("%s is null-sec.  There are no crimes there.", where.c_str());

    std::string who, outcome;
    bool criminal(false);
    if (pAggressor != nullptr) {
        who = pAggressor->GetName();
        if (pVictim != nullptr) {
            sCrimeWatch.OffensiveAct(pAggressor, pVictim);
        } else {
            sCrimeWatch.SimulateCrime(pAggressor);
        }
        criminal = sCrimeWatch.IsCriminal(pAggressor->GetCharacterID());
        if (!criminal)
            outcome = " The attack on " + std::string(pVictim != nullptr ? pVictim->GetName() : "the victim") + " was legal, so it's no crime.";
    } else {
        who = pEntity->GetName();
        sCrimeWatch.EntityCrime(pEntity, pVictim);
        criminal = sCrimeWatch.IsCriminal(pEntity->GetID());
    }

    char buf[256];
    if (!criminal) {
        snprintf(buf, sizeof(buf), "%s is not a criminal.%s", who.c_str(), outcome.c_str());
    } else if ((secClass == Crime::SecClass::High) and sConfig.crime.Concord) {
        snprintf(buf, sizeof(buf), "%s is now a criminal in %s.  CONCORD arrives in about %us.", who.c_str(), where.c_str(),
                pSystem->GetLaw()->ConcordDelay());
    } else {
        snprintf(buf, sizeof(buf), "%s is now a criminal in %s.  No CONCORD in low-sec, but the sentries will shoot.", who.c_str(), where.c_str());
    }
    return new PyString(buf);
}
