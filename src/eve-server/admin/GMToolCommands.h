//no macroguard on purpose
/* commands the Crucible client's GM menus send that the server didn't have.  see GMToolCommands.cpp */
COMMAND( loop, Acct::Role::VIP,
         "(count) (command...) - runs a slash command count times (max 100).  e.g. /loop 3 /fit me 2048" )
COMMAND( gag, Acct::Role::GML,
         "(charID|name) [\"reason\"] [minutes] - stops a character talking in chat channels (default 30 minutes)" )
COMMAND( ungag, Acct::Role::GML,
         "(charID|name) - lets a gagged character talk again" )
COMMAND( gagiskspammer, Acct::Role::LEGIONEER,
         "(charID) - gags an ISK spammer for 30 days" )
COMMAND( baniskspammer, Acct::Role::GML,
         "(charID) - gags an ISK spammer and bans their account" )
COMMAND( nukem, Acct::Role::ADMIN,
         "- kills every NPC in your bubble (same as /killallnpcs)" )
COMMAND( entity, Acct::Role::SPAWN,
         "deploy (count) (typeID|\"type name\") - spawns NPCs next to you" )
COMMAND( crime, Acct::Role::GML,
         "(me|charID|entityID) [victimID] - commits a crime for testing CONCORD.  a pilot attacks the victim (default: you,"
         " or a civilian when it's you) through the normal crime rules.  an NPC or other entity is made a criminal" )
COMMAND( missioncomplete, Acct::Role::GML,
         "(agentID) [charID] - completes the kill objective of your (or charID's) accepted mission with the agent" )
COMMAND( heat, Acct::Role::VIP,
         "[hi|med|low|all] [percent] [me|charID] - shows or sets a ship's rack heat.  no arguments shows yours, (me|charID) shows theirs" )
COMMAND( untrash, Acct::Role::GML,
         "(itemID) | list (me|charID|corpID) - restores a trashed item to where it was trashed from, or lists the newest 50 trashed items of an owner" )
