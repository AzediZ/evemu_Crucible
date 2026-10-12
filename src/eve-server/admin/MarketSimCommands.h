//no macroguard on purpose
/* the market simulation (doc/code_and_design_notes/market-sim/PLAN.md 9.5).  see MarketSimMgr::Command */
COMMAND( mktsim, Acct::Role::GMH,
         "status | pause | resume | cell (typeID) [stationID] | actor (actorID) | actors [kind] | anchor (typeID) (price) [regionID]"
         " | shock (typeID|g:groupID|c:categoryID) (demandMult) (days) | requote (typeID|all) | seed [stationID] | audit [days]"
         " | step (ticks) | purge | purgelegacy | export (file) - the simulated market.  step needs PROGRAMMER on a test server" )
