//no macroguard on purpose
/* /queue:  the skill queue past the client's 24 hours.  see SkillQueueCommands.cpp */
COMMAND( queue, Acct::Role::PLAYER,
         "[add (skill, ship or module) [level] | remove (skill) | clear] - your skill queue:  shows it with its real end times;  add puts a skill"
         " (and what it needs) or everything a ship or module needs at the end, past the 24 hours the skill queue window allows" )
