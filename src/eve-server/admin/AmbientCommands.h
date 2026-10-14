/**
 * @name AmbientCommands.h
 *     ambient npc observability and tests (doc/code_and_design_notes/planning/ambient-npc-plan.md 4.5)
 */

//no macroguard on purpose


COMMAND( ambient, Acct::Role::GMH,
         "[profile [systemID] | spawn <hauler|courier|freighter|navy|concord|miner|criminal> [count] | clear | global] - ambient npcs.  "
         "no argument:  this system's state (busyness, rates, watched grids, ships by role, off-grid travellers, last events).  "
         "profile = the busyness terms of a system.  spawn = force ambient ships on your grid.  clear = remove every ambient ship "
         "here silently.  global = totals over all systems" )
