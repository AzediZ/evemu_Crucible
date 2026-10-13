/**
 * @name CrimeCommands.h
 *     GM commands for crime and CONCORD
 *
 * @date:          11 October 2026
 *
 */




COMMAND( crime, Acct::Role::GML,
         "on|off - switches crime on or off (it starts off).  "
         "(me|charID|entityID) [victimID] - commits a crime for testing CONCORD.  a pilot attacks the victim (default: you,"
         " or a civilian when it's you) through the normal crime rules.  an NPC or other entity is made a criminal" )
