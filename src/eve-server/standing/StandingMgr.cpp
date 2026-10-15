
 /**
  * @name StandingMgr.cpp
  *   memory object caching system for managing and saving standings data
  *   methods and functions relating to manipulation of standings
  *
  * @Author:        Allan
  * @date:      14 Novemeber 2018
  *
  */


#include "EVEServerConfig.h"
#include "StandingMgr.h"

/*
 * STANDING__ERROR
 * STANDING__WARNING
 * STANDING__MESSAGE
 * STANDING__DEBUG
 * STANDING__INFO
 * STANDING__TRACE
 * STANDING__DUMP
 * STANDING__RSPDUMP
 */


StandingMgr::StandingMgr()
: m_factionStandings(nullptr)
{

}

StandingMgr::~StandingMgr()
{

}

void StandingMgr::Clear()
{
    PySafeDecRef(m_factionStandings);
}

int StandingMgr::Initialize()
{
    Populate();
    sLog.Blue("      StandingMgr", "Standings Manager Initialized.");
    return 1;
}

void StandingMgr::GetInfo()
{

}

void StandingMgr::Populate()
{
    m_factionStandings = StandingDB::GetFactionStandings();
    if (m_factionStandings == nullptr)
        sLog.Error("      StandingMgr", "m_factionStandings is null");

    m_factionRelations.clear();
    StandingDB::GetFactionRelations(m_factionRelations);
}

void StandingMgr::UpdateStandings(uint32 fromID, uint32 toID, uint16 eventType, double amount, std::string msg)
{
    StandingDB::UpdateStanding(fromID, toID, amount);
    StandingDB::SaveStandingChanges(fromID, toID, eventType, amount, msg);
}

void StandingMgr::ModifyStanding(uint32 fromID, uint32 toID, uint16 eventType, double change, const std::string& msg,
                                 PyList* notify/*nullptr*/, float minAbs/*-10*/, float maxAbs/*10*/)
{
    if (change == 0.0)
        return;
    StandingDB::ModifyStanding(fromID, toID, change, minAbs, maxAbs);
    StandingDB::SaveStandingChanges(fromID, toID, eventType, change, msg);
    if (notify == nullptr)
        return;
    PyTuple* row = new PyTuple(5);
        row->SetItem(0, new PyInt(fromID));
        row->SetItem(1, new PyInt(toID));
        row->SetItem(2, new PyFloat(change));
        row->SetItem(3, new PyFloat(minAbs / 10.0));
        row->SetItem(4, new PyFloat(maxAbs / 10.0));
    notify->AddItem(row);
}

void StandingMgr::ModifyFactionStanding(uint32 factionID, uint32 toID, uint16 eventType, double change, const std::string& msg,
                                        PyList* notify/*nullptr*/)
{
    if (change == 0.0)
        return;
    ModifyStanding(factionID, toID, eventType, change, msg, notify);
    if (!sConfig.standings.DerivedStandings)
        return;

    auto range = m_factionRelations.equal_range(factionID);
    for (auto itr = range.first; itr != range.second; ++itr) {
        uint32 otherID(itr->second.first);
        float relation(itr->second.second);
        if ((otherID == factionID) or (relation == 0.0f))
            continue;
        // a loss with this faction doesn't please its enemies
        if ((change < 0.0) and (relation < 0.0f))
            continue;
        double derived(change * relation / 10.0);
        std::string dMsg("Derived from ");
        dMsg += msg;
        if (derived > 0.0) {
            // no higher than how they stand towards this faction
            ModifyStanding(otherID, toID, Standings::DerivedModificationPleased, derived, dMsg, notify, -10.0f, relation);
        } else if (change > 0.0) {
            // an enemy:  no lower than how it stands towards this faction
            ModifyStanding(otherID, toID, Standings::DerivedModificationDispleased, derived, dMsg, notify, relation, 10.0f);
        } else {
            // a friend of a faction we lost standing with:  as far down as it stands up
            ModifyStanding(otherID, toID, Standings::DerivedModificationDispleased, derived, dMsg, notify, -relation, 10.0f);
        }
    }
}

