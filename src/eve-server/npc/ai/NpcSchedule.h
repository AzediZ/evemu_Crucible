 /**
  * @name NpcSchedule.h
  *   millisecond timestamps for everything an npc does on a cycle (PLAN.md 6.3, bug B13)
  *
  *   each activity has a 'next at' time.  Due(now) is now >= nextAt (the global Timer uses a strict > and restarts from
  *   'now', which lost up to a tick per cycle).  periodic activities advance by their period, so they don't drift:
  *   a 2500ms gun on the 1Hz tick fires at 3, 5, 8, 10, 13s... and averages its real rate of fire.
  *   at most one activation per activity per call;  when we fall more than two periods behind (lag, a long stall)
  *   the next one is reset to now + period instead of firing a burst.
  *   header only and dependency free (also used by eve-test)
  */

#ifndef _EVE_NPC_AI_NPCSCHEDULE_H_
#define _EVE_NPC_AI_NPCSCHEDULE_H_

#include <cstdint>

namespace NpcAct {
    enum : uint8_t {
        Think       = 0,    // target search and other periodic decisions (1s)
        Turret,             // turret cycle (speed, attribute 51)
        Missile,            // missile cycle (missileLaunchDuration, 506)
        ShieldRep,          // self repair:  shield booster cycle
        ArmorRep,           // self repair:  armor repairer cycle
        RemoteArmor,        // remote armor repair cycle
        RemoteShield,       // remote shield boost cycle
        ChaseDelay,         // chase cycle:  stalling at cruise speed
        ChaseBoost,         // chase cycle:  boosting at max velocity
        SwitchEval,         // level-one target re-evaluation
        ThreatDecay,        // level-one threat decay
        DroneRoll,          // standard ai drone aggro roll
        Leash,              // returning home after the leash pulled us back
        WarpOut,            // idle spawn moves to another belt
        Flee,               // haulers and commanders leaving (phase 2)
        Reaction,           // reaction delay before the first activation (phase 2)
        Count
    };
}

class NpcSchedule {
public:
    NpcSchedule()                                       { StopAll(); }

    // first due at now + delayMs.  periodMs > 0 repeats it every periodMs after that, 0 = once
    void Start(uint8_t act, int64_t now, int64_t delayMs, int64_t periodMs = 0) {
        if (act >= NpcAct::Count)
            return;
        m_next[act] = now + (delayMs > 0 ? delayMs : 0);
        m_period[act] = (periodMs > 0 ? periodMs : 0);
        m_active[act] = true;
    }
    // every periodMs, the first one periodMs from now
    void StartPeriodic(uint8_t act, int64_t now, int64_t periodMs) {
        if (periodMs <= 0) {
            Stop(act);
            return;
        }
        Start(act, now, periodMs, periodMs);
    }
    // starts it only when it isn't running already
    void Ensure(uint8_t act, int64_t now, int64_t delayMs, int64_t periodMs = 0) {
        if (!Active(act))
            Start(act, now, delayMs, periodMs);
    }
    void Stop(uint8_t act) {
        if (act < NpcAct::Count)
            m_active[act] = false;
    }
    void StopAll() {
        for (uint8_t i = 0; i < NpcAct::Count; ++i) {
            m_active[i] = false;
            m_next[i] = 0;
            m_period[i] = 0;
        }
    }

    bool Active(uint8_t act) const                      { return ((act < NpcAct::Count) and m_active[act]); }
    // due now, without using it up
    bool IsDue(uint8_t act, int64_t now) const          { return (Active(act) and (now >= m_next[act])); }
    /* due now:  uses it up.  a one-shot activity stops, a periodic one moves on by its period (drift free) */
    bool Due(uint8_t act, int64_t now) {
        if (!IsDue(act, now))
            return false;
        if (m_period[act] <= 0) {
            m_active[act] = false;
            return true;
        }
        int64_t lag(now - m_next[act]);
        if (lag > (2 * m_period[act])) {
            m_next[act] = now + m_period[act];
        } else {
            m_next[act] += m_period[act];
        }
        return true;
    }
    /* due, but it can't happen now (no lock, out of range):  it stays due without piling up, so it happens on the first
     *  tick it can and the period counts from there (no burst of catch-up activations afterwards) */
    void Defer(uint8_t act, int64_t now) {
        if (Active(act) and (m_next[act] < now))
            m_next[act] = now;
    }
    // changes the period of a running activity (from its next activation on)
    void SetPeriod(uint8_t act, int64_t periodMs) {
        if (act < NpcAct::Count)
            m_period[act] = (periodMs > 0 ? periodMs : 0);
    }

    int64_t NextAt(uint8_t act) const                   { return (Active(act) ? m_next[act] : 0); }
    int64_t Period(uint8_t act) const                   { return (act < NpcAct::Count ? m_period[act] : 0); }
    // ms until it's due (0 when due or not running)
    int64_t Remaining(uint8_t act, int64_t now) const {
        if (!Active(act) or (now >= m_next[act]))
            return 0;
        return m_next[act] - now;
    }

    static const char* Name(uint8_t act) {
        switch (act) {
            case NpcAct::Think:         return "think";
            case NpcAct::Turret:        return "turret";
            case NpcAct::Missile:       return "missile";
            case NpcAct::ShieldRep:     return "shieldRep";
            case NpcAct::ArmorRep:      return "armorRep";
            case NpcAct::RemoteArmor:   return "remoteArmor";
            case NpcAct::RemoteShield:  return "remoteShield";
            case NpcAct::ChaseDelay:    return "chaseDelay";
            case NpcAct::ChaseBoost:    return "chaseBoost";
            case NpcAct::SwitchEval:    return "switchEval";
            case NpcAct::ThreatDecay:   return "threatDecay";
            case NpcAct::DroneRoll:     return "droneRoll";
            case NpcAct::Leash:         return "leash";
            case NpcAct::WarpOut:       return "warpOut";
            case NpcAct::Flee:          return "flee";
            case NpcAct::Reaction:      return "reaction";
        }
        return "unknown";
    }

private:
    int64_t m_next[NpcAct::Count];
    int64_t m_period[NpcAct::Count];
    bool m_active[NpcAct::Count];
};

#endif  // _EVE_NPC_AI_NPCSCHEDULE_H_
