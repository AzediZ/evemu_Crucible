
/**
 * @name DungeonCommands.h
 *     Dungeon Command system for EVEmu
 *
 * @Author:        Allan
 * @date:          20 August 2019
 *
 */




COMMAND( savedungeon, Acct::Role::CONTENT,
         " - saves dungeon <dungeonID> to server database")
COMMAND( printdungeon, Acct::Role::CONTENT,
         " - not sure what this is supposed to do yet")
COMMAND( spawndungeon, Acct::Role::CONTENT,
         " - spawns dungeon <dungeonID> in your system, with BM to location (outdated)")
COMMAND( removedungeon, Acct::Role::CONTENT,
         " - removes identified dungeon(unimplemented)")
COMMAND( ded, Acct::Role::CONTENT,
         "[list] | spawn rating|dungeonID - list the DED complexes, or put one about 20 km from your ship (a rating picks this region's pirates where they have one)")


