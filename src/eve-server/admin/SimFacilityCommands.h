//no macroguard on purpose
/* the market simulation's station lines (doc/code_and_design_notes/market-sim/PLAN.md 6.6).  see SimFacilityMgr::Command */
COMMAND( simfacility, Acct::Role::GMH,
         "status | station (stationID) | reserve (stationID) (activity) (jobs) (hours) | release (jobID|test)"
         " - the market sim's use of npc station assembly lines.  reserve makes GM test jobs that hold lines like the sim's;"
         "  reserve and release need PROGRAMMER" )
