 /**
  * @name SimFacilityMgr.h
  *   market simulation:  the server's facility capacity (PLAN 6.6, phase 5) as the singleton sSimFacilities.  the sim's
  *   jobs take npc station assembly lines (ramAssemblyLines.nextFreeTime, so players see them busy);  no ramJobs rows
  *   (the world keeps its jobs in mktSimJob).  players always win a line:  RamMethods quotes them the line's time without
  *   the sim's job, and RamProxyService::InstallJob moves the sim's job off the line they install on (FacilityBook).
  *   the world books through Capacity() (a Prod::ICapacity, World::SetCapacity)
  *
  *   persistence:  the lines' times are written in the transaction of whoever flushes.  attached to the market sim
  *   (AttachToSim), that is the sim's own flush, with mktSimJob and the stock the jobs used and made (WritePending()
  *   inside it, PendingWritten() after it committed);  on its own (GM test jobs), Process() flushes
  *
  * @date:          15Oct26
  *
  */

#ifndef _EVE_SERVER_MARKET_SIM_FACILITY_MGR_H__
#define _EVE_SERVER_MARKET_SIM_FACILITY_MGR_H__

#include <memory>

#include "eve-server.h"

#include "market/sim/SimFacilities.h"
#include "market/sim/SimLineCapacity.h"

class Client;
class Seperator;

class SimFacilityMgr
: public Singleton<SimFacilityMgr>,
  public MktSim::FacilityBook
{
public:
    SimFacilityMgr();

    // reads <marketSim>
    int Initialize();
    // 1 Hz:  checks the lines of the jobs restored at boot, ends due GM test jobs, and flushes when no market sim does it
    void Process();
    // writes what is pending (when no market sim does it)
    void Close();

    bool IsLoaded() const                               { return m_loaded; }
    // the sim's time (the sim's own calls tell how far '/mktsim step' put it ahead of the wall clock)
    int64 SimNow() const;

    /* players (RamMethods::Calculate, RamProxyService::InstallJob) */
    // the next free time a player is quoted for a line:  FactoryDB::GetNextFreeTime's value without the sim's job
    int64 PlayerNextFreeTime(uint32 lineID, int64 nextFreeTime) const;
    // a player installs a job on a line, busy until endTime (FILETIME):  a sim job there moves to another line, or goes on
    //  without one
    void PlayerInstalls(uint32 lineID, int64 endTime);

    /* the world's capacity on these lines.  a node's jobs run at the stations of its constellation that have lines (its
     *  own first).  probeInstall/probePerHour:  the cost of a job the planner prices before booking it (LineInstallCost,
     *  LineCostPerHour) */
    MktSim::LineCapacity* Capacity(const std::vector<MktSim::NodeData>& nodes, double probeInstall, double probePerHour);

    /* persistence */
    // the market sim writes the lines in its own flush from now on
    void AttachToSim(bool attached)                     { m_attached = attached; }
    bool AttachedToSim() const                          { return m_attached; }
    // the pending line times (inside the caller's transaction).  false:  a statement failed
    bool WritePending();
    // its own transaction (WritePending, PendingWritten after the commit)
    bool Flush();

    // /simfacility
    std::string Command(Client* who, const Seperator& args);

protected:
    bool LoadLines(uint32_t stationID, uint8_t activity, int64_t now, std::vector<MktSim::FacilityLine>& into) override;
    int64_t RawFor(int64_t end, int64_t now) const override;
    bool CheckLines(const std::vector<uint32_t>& lineIDs, std::map<uint32_t, std::pair<int64_t, bool>>& into) override;

private:
    void  ReadParams();
    int64 WallNow() const;              // unix seconds

    std::unique_ptr<MktSim::LineCapacity> m_capacity;
    int64 m_offset;                     // sim time - wall time, as the sim's last call showed
    int64 m_lastFlush;
    bool  m_loaded;
    bool  m_attached;
};

//Singleton
#define sSimFacilities \
( SimFacilityMgr::get() )

#endif  // _EVE_SERVER_MARKET_SIM_FACILITY_MGR_H__
