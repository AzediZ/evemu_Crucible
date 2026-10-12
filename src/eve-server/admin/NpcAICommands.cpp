
/**
 * @name NpcAICommands.cpp
 *     npc ai observability (doc/code_and_design_notes/npc-ai/PLAN.md 6.9)
 *
 *   /npcai [itemID|target|near] [verbose]   an npc's (or sentry's) tier, brain, state, primary, locks with lock progress,
 *                                            top threat, next-due timers, profile values (ranges, lock time to you,
 *                                            reaction delay), group and rng seed
 *   /npcai audit [log]                       the profile validation summary over every npc type
 */

#include "eve-server.h"

#include "admin/AllCommands.h"
#include "admin/CommandDB.h"
#include "npc/NPC.h"
#include "npc/NPCAI.h"
#include "npc/Sentry.h"
#include "npc/SentryAI.h"
#include "npc/ai/NpcProfile.h"
#include "system/SystemBubble.h"
#include "system/SystemManager.h"
#include "system/TargetManager.h"

PyResult Command_npcai(Client* pClient, CommandDB* db, EVEServiceManager& services, const Seperator& args)
{
    if (pClient == nullptr)
        throw CustomError ("npcai: no caller.");

    std::string what(args.argCount() > 1 ? args.arg(1) : "target");
    bool verbose(false);
    for (size_t i = 1; i < args.argCount(); ++i)
        if ((args.arg(i) == "verbose") or (args.arg(i) == "log"))
            verbose = true;

    if (what == "audit") {
        std::string report;
        NpcProfile::Audit(report, verbose);
        pClient->SendInfoModalMsg("%s", report.c_str());
        return new PyString("npc profile audit done");
    }

    if (!pClient->IsInSpace() or (pClient->GetShipSE() == nullptr) or (pClient->SystemMgr() == nullptr))
        throw CustomError ("npcai: you have to be in space.");
    SystemEntity* pShip(pClient->GetShipSE());

    SystemEntity* pSE(nullptr);
    if ((what == "target") or (what == "verbose")) {
        pSE = pShip->TargetMgr()->GetFirstTarget(false);
        if ((pSE != nullptr) and !pSE->IsNPCSE() and !pSE->IsSentrySE())
            pSE = nullptr;
        if (pSE == nullptr)
            what = "near";
    } else if (what != "near") {
        if (!args.isNumber(1))
            throw CustomError ("Correct Usage: /npcai [itemID|target|near] [verbose]  or  /npcai audit [log]");
        pSE = pClient->SystemMgr()->GetSE(atoi(args.arg(1).c_str()));
        if (pSE == nullptr)
            throw CustomError ("npcai: %s isn't in your system.", args.arg(1).c_str());
    }
    if ((what == "near") and (pShip->SysBubble() != nullptr)) {
        double nearest(0.0);
        pShip->SysBubble()->ForEachDynamic([&](SystemEntity* cur) {
            if (!cur->IsNPCSE() and !cur->IsSentrySE())
                return;
            double distance(pShip->GetPosition().distance(cur->GetPosition()));
            if ((pSE == nullptr) or (distance < nearest)) {
                pSE = cur;
                nearest = distance;
            }
        });
    }
    if (pSE == nullptr)
        throw CustomError ("npcai: no npc found (lock one, or give its itemID).");

    std::string reply;
    if (pSE->IsNPCSE()) {
        reply = pSE->GetNPCSE()->GetAIMgr()->Dump(pShip, verbose);
    } else if (pSE->IsSentrySE()) {
        reply = pSE->GetSentrySE()->GetAIMgr()->Dump(pShip, verbose);
    } else {
        throw CustomError ("npcai: %s(%u) isn't an npc or a sentry.", pSE->GetName(), pSE->GetID());
    }
    pClient->SendInfoModalMsg("%s", reply.c_str());
    return new PyString(reply);
}
