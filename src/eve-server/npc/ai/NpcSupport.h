 /**
  * @name NpcSupport.h
  *   what an npc does for its friends (doc/code_and_design_notes/npc-ai/PLAN.md 4.7, 5.8, 5.9;  phase 3).  any tier with
  *   the effect, never law enforcement.  made by NPCAIMgr only for types that have one of these:
  *
  *   remote repair (NPCRemoteArmorRepair 3852:  sleeper wardens, preservers, keepers, upholders;  NPCRemoteShieldBoost 3855:
  *    incursion logistics and capitals).  "this will repair friendly npc within the npc's group":
  *     candidates   members of our aggro group (of every group of our dungeon room), within npcAssistanceRange (1464, surface
  *                  distance), below the threshold:  armor damage / armor hp >= 1456, shield 1 - charge / capacity >= 1462
  *     order        npcAssistanceProirity (1451) high first, then under fire (damaged in the last 10s), then the lowest layer
  *     cap          up to 1501 / 1502 patients a cycle
  *     cycle        1454 / 1458 ms, each one rolls its activation chance 1453 / 1459
  *     reservation  armor repair lands at the end of the cycle;  until then it is reserved on the patient
  *                  (NPCAIMgr::ReserveArmor), so a second repairer doesn't put a cycle on a patient that is already covered.
  *                  shield boosts land at the start of the cycle
  *     when         only while we or our group fight (or were hurt in the last 10s);  otherwise we look every 5s
  *   group assists (4686-4689:  Vylade Dien and a test npc):  every duration, roll the activation chance and give the
  *    same-group npcs within npcAssistanceRange (us too) the bonus for that duration (NPCAIMgr::ApplyBuff)
  *   remote ecm (4656:  the incursion capitals):  every 1658 ms, recalculated as clamp(1661 x 1660 ^ max(0, (players - 1663)
  *    / 1662), 1659, 1661) from the players in the bubble, roll 1664;  on success every player ship in the bubble is jammed for
  *    npc.RemoteEcmJamTime
  */

#ifndef _EVE_NPC_AI_NPCSUPPORT_H_
#define _EVE_NPC_AI_NPCSUPPORT_H_

#include "eve-server.h"

#include "npc/ai/NpcMath.h"

class NPC;
class NPCAIMgr;
class NpcRng;
struct NpcProfile;

class NpcSupport {
public:
    NpcSupport(NPC* pNPC, NPCAIMgr* pAI, const NpcProfile& profile, NpcRng& rng);

    // a type with remote repair, group assists or remote ecm
    static bool Needed(const NpcProfile& profile);

    /* every tick while we're awake.  engaged:  we're fighting.  bounded (phase 5):  the group's combat state is worked out
     *  only when a repair poll or an assist is due, and once a second for the whole group (AggroGroup::AnyFighting) */
    void Process(int64 now, bool engaged);
    // we leave (warp) or die:  pending repairs are lost, the timers stop, the beams of a running cycle go off
    void StopAll();

    std::string Describe(int64 now) const;

private:
    struct RepairState {
        int64 cycleEnd;         // ms.  0 = not cycling
        int64 pollAt;           // ms.  next look for patients while not cycling
        uint32 cycles;          // for /npcai
        std::vector<uint32> beams;  // the patients of the running cycle (their beam runs for the cycle)
    };
    struct Pending {
        uint32 patientID;
        float amount;
        int64 at;
    };
    struct Patient {
        NPC* pNPC;
        NpcMath::RepairOrder order;
    };

    void ProcessRepair(bool armor, RepairState& state, int64 now, bool combat);
    // a cycle ended or a poll for patients is due
    bool RepairDue(bool armor, const RepairState& state, int64 now) const;
    // an assist runs out (or isn't running:  it starts with the fight)
    bool AssistDue(int64 now) const;
    // the beams of a running cycle go off
    void StopBeams(bool armor, RepairState& state);
    // a new cycle:  roll, pick the patients, repair (shield) or reserve (armor).  false:  nobody needs it
    bool StartCycle(bool armor, int64 now);
    // armor repairs whose cycle ended
    void LandPending(int64 now);
    // we, or a member of our group, fight (or were hurt in the last 10s)
    bool InCombat(int64 now, bool engaged);
    // fn(NPC*) for our friends:  our group, or every group of our dungeon room
    template<typename F> void ForEachFriend(const F& fn);

    void ProcessAssists(int64 now, bool combat);
    void ProcessRemoteEcm(int64 now, bool engaged);

    NPC* m_npc;
    NPCAIMgr* m_ai;
    const NpcProfile& m_profile;
    NpcRng& m_rng;

    RepairState m_armor;
    RepairState m_shield;
    std::vector<int64> m_assistAt;      // per profile.groupAssists, ms.  0 = not running
    int64 m_ecmAt;                      // ms.  0 = not running
    uint32 m_ecmBursts;

    std::vector<Pending> m_pending;
    std::vector<Patient> m_patients;    // scratch (no allocation per tick)
};

#endif  // _EVE_NPC_AI_NPCSUPPORT_H_
