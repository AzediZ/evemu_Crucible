 /**
  * @name EVE_StructureTiming.h
  *     anchoring, onlining and unanchoring timers of starbase and sovereignty structures as the Crucible client shows them
  *
  * these are pure functions, so the unit tests (eve-test) can check them
  *
  *  the client counts a structure's state change down from its slim item (client pwn.GetStructureState,
  *  parklife/playerOwned.py:111, drawn by bracket.StructureProgress, ui/inflight/bracket.py:769):
  *   - posState is the state the structure is going to (posConst.py:8):  1 (anchored) while anchoring, 2 (onlining) while
  *     onlining, 0 (unanchored) while unanchoring.
  *   - posTimestamp is when that change started (FILETIME, 100ns).  None means "done".
  *   - the length is the type's own anchoringDelay / onliningDelay / unanchoringDelay (ms) from the client's dogma data.
  *   - the client shows 'anchoring' / 'onlining' / 'unanchoring' with a countdown while now < posTimestamp + delay, and
  *     'anchored' / 'online' / 'unanchored' after that.  onlining ends when the server sends posState 4 (online).
  *  there is no offlining delay:  offline is posState 1 with a timestamp that is already done.
  *  this goes for every structure the client calls a structure (util.IsStructure:  categories Structure and
  *  Sovereignty Structure).  orbitals (customs offices, gantries) use orbitalState/orbitalTimestamp instead, where the
  *  timestamp is the end of the state (bracket.OrbitalProgress), see CustomsSE.
  */


#ifndef EVE_STRUCTURE_TIMING_H
#define EVE_STRUCTURE_TIMING_H

#include <cctype>
#include <cstdint>
#include <vector>


namespace StructureTiming {

    // FILETIME units in a millisecond
    static constexpr int64_t FileTimeMs = 10000;

    // posState values (EVEPOS::StructureState, client posConst.py)
    static constexpr int8_t StateUnanchored = 0;
    static constexpr int8_t StateAnchored   = 1;
    static constexpr int8_t StateOnlining   = 2;
    static constexpr int8_t StateOnline     = 4;

    // the timed change a posState stands for, and so which delay attribute the client counts with
    enum class Change : uint8_t {
        None        = 0,
        Anchoring   = 1,    // anchoringDelay
        Onlining    = 2,    // onliningDelay
        Unanchoring = 3     // unanchoringDelay
    };
    inline Change ChangeFor(int8_t posState)
    {
        switch (posState) {
            case StateUnanchored:   return Change::Unanchoring;
            case StateAnchored:     return Change::Anchoring;
            case StateOnlining:     return Change::Onlining;
        }
        return Change::None;
    }

    // a stamp the client can count from.  0 is never set (a structure that was never anchored):  the slim item says None
    inline bool HasStamp(int64_t stamp)
    {
        return (stamp > 0);
    }

    // ms left of a change of delayMs started at stamp (<= 0:  done)
    inline int64_t RemainingMs(int64_t stamp, int64_t delayMs, int64_t now)
    {
        return ((stamp - now) / FileTimeMs + delayMs);
    }

    // the stamp of a change of delayMs that is done right now:  the client shows the final state, without a countdown
    inline int64_t SettledStamp(int64_t now, int64_t delayMs)
    {
        return (now - delayMs * FileTimeMs);
    }

    // what the client's bracket does with this slim item:  true while it counts down (pwn.GetStructureState)
    inline bool ClientCountsDown(int8_t posState, bool hasStamp, int64_t stamp, int64_t delayMs, int64_t now)
    {
        if ((ChangeFor(posState) == Change::None) or !hasStamp)
            return false;
        return ((now - stamp) < delayMs * FileTimeMs);
    }

    /* after a server restart:  the timer (ms) that finishes a change that was running, 0 for none.
     *  anchoring and unanchoring that are past their time were finished before (their stamp is settled);  onlining always
     *  finishes, since the state only changes to online when it does
     */
    inline int64_t ResumeTimerMs(int8_t posState, int64_t stamp, int64_t delayMs, int64_t now)
    {
        Change change(ChangeFor(posState));
        if ((change == Change::None) or !HasStamp(stamp))
            return 0;
        int64_t left(RemainingMs(stamp, delayMs, now));
        if (left > 0)
            return left;
        return ((change == Change::Onlining) ? 1 : 0);
    }

    /* GM commands (/sov, /pos):  what to do, at once and without the delays */
    enum class GMAction : uint8_t {
        Invalid  = 0,
        Anchor   = 1,
        Online   = 2,
        Offline  = 3,
        Unanchor = 4,
        Complete = 5    // only finish the change that is running
    };
    inline bool SameWord(const char* a, const char* b)
    {
        for (; (*a != 0) and (*b != 0); ++a, ++b)
            if (std::tolower(static_cast<unsigned char>(*a)) != std::tolower(static_cast<unsigned char>(*b)))
                return false;
        return ((*a == 0) and (*b == 0));
    }
    inline GMAction ParseGMAction(const char* word)
    {
        if (word == nullptr)
            return GMAction::Invalid;
        if (SameWord(word, "anchor"))
            return GMAction::Anchor;
        if (SameWord(word, "online"))
            return GMAction::Online;
        if (SameWord(word, "offline"))
            return GMAction::Offline;
        if (SameWord(word, "unanchor"))
            return GMAction::Unanchor;
        if (SameWord(word, "complete"))
            return GMAction::Complete;
        return GMAction::Invalid;
    }

    // the steps of a GM action from a structure whose running change is already finished
    enum class Step : uint8_t {
        Anchor   = 1,
        Online   = 2,
        Offline  = 3,
        Unanchor = 4
    };
    inline std::vector<Step> GMPlan(int8_t posState, GMAction action)
    {
        std::vector<Step> steps;
        // incapacitated (-1) and unknown states are left alone
        if (posState < StateUnanchored)
            return steps;
        bool anchored(posState >= StateAnchored);
        bool powered(posState >= StateOnlining);    // onlining, online, reinforced, operating, (in)vulnerable
        switch (action) {
            case GMAction::Anchor: {
                if (!anchored)
                    steps.push_back(Step::Anchor);
            } break;
            case GMAction::Online: {
                if (!anchored)
                    steps.push_back(Step::Anchor);
                if (!powered)
                    steps.push_back(Step::Online);
            } break;
            case GMAction::Offline: {
                if (powered)
                    steps.push_back(Step::Offline);
            } break;
            case GMAction::Unanchor: {
                if (powered)
                    steps.push_back(Step::Offline);
                if (anchored)
                    steps.push_back(Step::Unanchor);
            } break;
            default:
                break;
        }
        return steps;
    }
}

#endif  // EVE_STRUCTURE_TIMING_H
