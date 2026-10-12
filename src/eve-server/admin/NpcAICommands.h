
/**
 * @name NpcAICommands.h
 *     npc ai observability (doc/code_and_design_notes/npc-ai/PLAN.md 6.9)
 */

//no macroguard on purpose


COMMAND( npcai, Acct::Role::GMH,
         "[itemID|target|near] [verbose] | audit [log] | stats - an npc's or sentry's ai:  tier, brain, state, locks, threat, schedule, profile, rng seed.  "
         "target (default) = your locked target, near = the nearest npc.  verbose adds its last shots with every term.  "
         "audit = the npc type data problems (log:  each type to NPC__AI_WARN).  stats = this system's npc ai telemetry (NPC__AI_STATS)" )
