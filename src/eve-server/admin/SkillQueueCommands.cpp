/**
 * @name SkillQueueCommands.cpp
 *   /queue:  the skill queue past the client's 24 hours
 *
 * @date:          21 October 2026
 *
 *   the Crucible client won't add a skill to a queue that runs past 24 hours (its own check, from its own estimate of the
 *   training times at Tranquility's speed).  the server has no such limit, so /queue adds there:  a skill to a level, with
 *   the skills it needs first, or everything a ship or module needs.  the skill queue window then shows the whole queue.
 */

#include "eve-server.h"

#include <cctype>
#include <ctime>
#include <set>

#include "Client.h"
#include "admin/AllCommands.h"
#include "character/Character.h"
#include "inventory/ItemFactory.h"
#include "inventory/ItemType.h"

namespace {
    const char* const Usage = "Usage:  /queue  |  /queue add (skill, ship or module) [level]  |  /queue remove (skill)  |  /queue clear";
    // a queue shown this long at most
    const size_t ShowMost = 60;

    const char* const Roman[] = { "", "I", "II", "III", "IV", "V" };

    std::string Lower(const std::string& text)
    {
        std::string out(text);
        for (char& c : out)
            c = (char)std::tolower((unsigned char)c);
        return out;
    }

    // "3d 4h", "2h 10m", "5m"
    std::string Duration(int64 fileTime)
    {
        int64 minutes(std::max<int64>(0, fileTime / EvE::Time::Minute));
        char buf[32];
        if (minutes >= 1440)
            snprintf(buf, sizeof(buf), "%lid %lih", (long)(minutes / 1440), (long)((minutes % 1440) / 60));
        else if (minutes >= 60)
            snprintf(buf, sizeof(buf), "%lih %lim", (long)(minutes / 60), (long)(minutes % 60));
        else
            snprintf(buf, sizeof(buf), "%lim", (long)minutes);
        return buf;
    }

    // "21 Oct 18:30"  (EVE time)
    std::string When(int64 fileTime)
    {
        time_t t((time_t)((fileTime - 116444736000000000LL) / EvE::Time::Second));
        struct tm utc;
        gmtime_r(&t, &utc);
        char buf[32];
        strftime(buf, sizeof(buf), "%d %b %H:%M", &utc);
        return buf;
    }

    std::string SkillName(uint16 typeID)
    {
        const ItemType* pType(sItemFactory.GetType(typeID));
        return ((pType == nullptr) ? std::to_string(typeID) : pType->name());
    }

    /* a published type by name:  a skill named exactly (skillOnly), else the best match of any type (exact, then starting with
     *  it, then containing it;  skills first, the shortest name first).  0:  none
     */
    uint16 FindType(const std::string& name, bool skillOnly, bool& isSkill)
    {
        std::string esc, like;
        sDatabase.DoEscapeString(esc, name);
        for (char c : esc) {
            if ((c == '%') or (c == '_'))
                like += '\\';
            like += c;
        }
        DBQueryResult res;
        DBResultRow row;
        if (skillOnly) {
            if (!sDatabase.RunQuery(res, "SELECT t.typeID, g.categoryID FROM invTypes AS t JOIN invGroups AS g ON g.groupID = t.groupID"
                                    " WHERE t.published = 1 AND g.categoryID = %u AND t.typeName = '%s' LIMIT 1", (uint32)EVEDB::invCategories::Skill, esc.c_str()))
                return 0;
        } else if (!sDatabase.RunQuery(res, "SELECT t.typeID, g.categoryID FROM invTypes AS t JOIN invGroups AS g ON g.groupID = t.groupID"
                                       " WHERE t.published = 1 AND t.typeName LIKE '%%%s%%'"
                                       " ORDER BY (t.typeName = '%s') DESC, (t.typeName LIKE '%s%%') DESC, (g.categoryID = %u) DESC, LENGTH(t.typeName) LIMIT 1",
                                       like.c_str(), esc.c_str(), like.c_str(), (uint32)EVEDB::invCategories::Skill)) {
            return 0;
        }
        if (!res.GetRow(row))
            return 0;
        isSkill = (row.GetUInt(1) == (uint32)EVEDB::invCategories::Skill);
        return (uint16)row.GetUInt(0);
    }

    /* the levels to queue for typeID to level:  what it needs first, then each level it doesn't have or have queued (planned:
     *  the highest level trained or queued so far, by skill).  missing:  skills it needs that the character hasn't injected
     */
    void Plan(const CharacterRef& cRef, uint16 typeID, uint8 level, std::map<uint16, uint8>& planned, std::vector<std::pair<uint16, uint8>>& into,
              std::set<uint16>& missing, int depth = 0)
    {
        if (depth > 10)
            return;
        std::map<uint16, uint8>::iterator itr = planned.find(typeID);
        if (itr == planned.end()) {
            int8 have(cRef->GetSkillLevel(typeID, false));
            if (have < 0) {
                missing.insert(typeID);
                have = 0;
            }
            uint8 queued(0);
            for (auto& cur : cRef->GetSkillQueue())
                if ((cur.typeID == typeID) and (cur.level > queued))
                    queued = cur.level;
            itr = planned.emplace(typeID, std::max<uint8>((uint8)have, queued)).first;
        }
        if (itr->second >= level)
            return;
        const ItemType* pType(sItemFactory.GetType(typeID));
        if (pType != nullptr)
            for (auto& cur : pType->GetRequiredSkills())
                Plan(cRef, cur.first, cur.second, planned, into, missing, depth + 1);
        itr = planned.find(typeID);
        for (uint8 l = itr->second + 1; l <= level; ++l)
            into.emplace_back(typeID, l);
        itr->second = level;
    }

    std::string Show(const CharacterRef& cRef)
    {
        const SkillQueue& queue(cRef->GetSkillQueue());
        if (queue.empty())
            return "Your skill queue is empty.  " + std::string(Usage);
        int64 now((int64)GetFileTimeNow());
        std::string out("Your skill queue (" + std::to_string(queue.size()) + " levels, done " + When(queue.back().endTime) + " EVE time, in "
                        + Duration(queue.back().endTime - now) + "):");
        for (size_t i = 0; (i < queue.size()) and (i < ShowMost); ++i)
            out += "<br>" + std::to_string(i + 1) + ".  " + SkillName(queue[i].typeID) + " " + Roman[std::min<uint8>(queue[i].level, 5)]
                 + ":  done " + When(queue[i].endTime) + " (in " + Duration(queue[i].endTime - now) + ")";
        if (queue.size() > ShowMost)
            out += "<br>... and " + std::to_string(queue.size() - ShowMost) + " more";
        return out;
    }

    // the queue put back without what fails keep (the skill in training stops and the first left starts, as the client's own save does)
    template <class Keep>
    uint32 Rebuild(const CharacterRef& cRef, Keep keep)
    {
        SkillQueue old(cRef->GetSkillQueue());
        cRef->ClearSkillQueue(true);
        std::map<uint16, uint8> levels;
        uint32 dropped(0);
        for (auto& cur : old) {
            // what it needs:  trained, or earlier in what's kept
            bool needs(true);
            const ItemType* pType(sItemFactory.GetType(cur.typeID));
            if (pType != nullptr)
                for (auto& req : pType->GetRequiredSkills()) {
                    std::map<uint16, uint8>::const_iterator have = levels.find(req.first);
                    if ((std::max<int8>(cRef->GetSkillLevel(req.first), 0) < req.second) and ((have == levels.end()) or (have->second < req.second)))
                        needs = false;
                }
            if (!keep(cur) or !needs) {
                ++dropped;
                continue;
            }
            cRef->AddToSkillQueue(cur.typeID, cur.level);
            levels[cur.typeID] = std::max(levels[cur.typeID], cur.level);
        }
        cRef->SkillQueueChanged();
        return dropped;
    }
}

PyResult Command_queue(Client* pClient, CommandDB* db, EVEServiceManager& services, const Seperator& args)
{
    CharacterRef cRef(pClient->GetChar());
    if (cRef.get() == nullptr)
        throw CustomError("No character.");
    if (args.argCount() < 2)
        return new PyString(Show(cRef));

    std::string cmd(Lower(args.arg(1)));
    if (cmd == "clear") {
        cRef->ClearSkillQueue(true);
        cRef->SkillQueueChanged();
        return new PyString("Your skill queue is cleared.");
    }
    if ((cmd != "add") and (cmd != "remove"))
        throw CustomError("%s", Usage);
    if (args.argCount() < 3)
        throw CustomError("%s", Usage);

    // the name, and a level after it (a digit, or I to V after a skill's name)
    std::vector<std::string> words;
    for (size_t i = 2; i < args.argCount(); ++i)
        words.push_back(args.arg(i));
    uint8 level(0);
    std::string last(words.back()), rest;
    for (size_t i = 0; i + 1 < words.size(); ++i)
        rest += (i ? " " : "") + words[i];
    bool isSkill(false);
    uint16 typeID(0);
    if ((cmd == "add") and (words.size() > 1)) {
        if ((last.size() == 1) and (last[0] >= '1') and (last[0] <= '5')) {
            level = (uint8)(last[0] - '0');
        } else {
            for (uint8 l = 1; l <= 5; ++l)
                if (Lower(last) == Lower(Roman[l]))
                    level = l;
            if ((level != 0) and ((typeID = FindType(rest, true, isSkill)) == 0))
                level = 0;
        }
    }
    std::string name;
    for (size_t i = 0; i < words.size() - (level ? 1 : 0); ++i)
        name += (i ? " " : "") + words[i];
    if (typeID == 0)
        typeID = FindType(name, (cmd == "remove"), isSkill);
    if (typeID == 0)
        throw CustomError("No %s called '%s'.", ((cmd == "remove") ? "skill" : "skill, ship or module"), name.c_str());
    if (cmd == "remove")
        isSkill = true;

    if (cmd == "remove") {
        uint32 dropped(Rebuild(cRef, [typeID](const QueuedSkill& q) { return (q.typeID != typeID); }));
        return new PyString("Took " + std::to_string(dropped) + " levels out of your skill queue (" + SkillName(typeID)
                            + " and what needed it).<br>" + Show(cRef));
    }

    // what to queue:  the skill to its level (the next by default), or what the ship or module needs
    std::map<uint16, uint8> planned;
    std::vector<std::pair<uint16, uint8>> plan;
    std::set<uint16> missing;
    if (isSkill) {
        if (level == 0) {
            int8 have(std::max<int8>(cRef->GetSkillLevel(typeID), 0));
            uint8 queued(0);
            for (auto& cur : cRef->GetSkillQueue())
                if ((cur.typeID == typeID) and (cur.level > queued))
                    queued = cur.level;
            level = (uint8)std::min<int>(std::max<int>(have, queued) + 1, 5);
        }
        Plan(cRef, typeID, level, planned, plan, missing);
    } else {
        const ItemType* pType(sItemFactory.GetType(typeID));
        if ((pType == nullptr) or pType->GetRequiredSkills().empty())
            return new PyString(SkillName(typeID) + " needs no skills.");
        for (auto& cur : pType->GetRequiredSkills())
            Plan(cRef, cur.first, cur.second, planned, plan, missing);
    }
    if (!missing.empty()) {
        std::string list;
        for (uint16 cur : missing)
            list += (list.empty() ? "" : ", ") + SkillName(cur);
        throw CustomError("You haven't injected %s yet.", list.c_str());
    }
    if (plan.empty())
        return new PyString("You already have" + std::string(isSkill ? "" : " every skill") + (isSkill ? " " + SkillName(typeID) + " " + Roman[level] : " " + SkillName(typeID) + " needs")
                            + ", trained or queued.");
    for (auto& cur : plan)
        cRef->AddToSkillQueue(cur.first, cur.second);
    cRef->SkillQueueChanged();

    std::string added;
    for (auto& cur : plan)
        added += (added.empty() ? "" : ", ") + SkillName(cur.first) + " " + Roman[cur.second];
    const SkillQueue& queue(cRef->GetSkillQueue());
    return new PyString("Added " + added + ".  Your queue is done " + (queue.empty() ? std::string("now") : When(queue.back().endTime) + " EVE time, in "
                        + Duration(queue.back().endTime - (int64)GetFileTimeNow())) + ".  /queue shows it.");
}
