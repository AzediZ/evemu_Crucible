

COMMAND( create, Acct::Role::VIP,
         "(itemID) [count] - Create count or 1 of the specified item." )
COMMAND( load, Acct::Role::VIP,
         "me (itemID) [count] - Create count or 1 of the specified item." )
COMMAND( createitem, Acct::Role::ADMIN,
         "(itemID) [count] - Create count or 1 of the specified item.(from Insider)" )
COMMAND( goto, Acct::Role::VIP,
         "(x) (y) (z) - Jump to the specified position in space. Stopped." )
COMMAND( translocate, Acct::Role::VIP,
         "(entityID) - Translocate to the specified entity" )
COMMAND( tr, Acct::Role::VIP,
         "(who) [where] [offset=x,y,z|offset=randvec(dist)] - translocate; '/tr help' for details" )
COMMAND( halt, Acct::Role::VIP,
         "- Immediately stops ship, setting Destiny::State = dstball_halt.")
COMMAND( location, Acct::Role::PLAYER,
         "- Gives you back your current location in space." )
COMMAND( syncloc, Acct::Role::PLAYER,
         "- Synchronizes your location in client with location on server." )
COMMAND( syncpos, Acct::Role::PLAYER,
         "- Synchronizes locations of all dynamic entities in current bubble with location on server for all players in callers bubble." )
COMMAND( update, Acct::Role::PLAYER,
         "- Sets Current Position according to Server's DestinyManager, then Sends Bubble AddBalls and Destiny SetState. (resets spaceview with current server data)" )
COMMAND( sendstate, Acct::Role::CONTENT,
         "- Sends DoDestinyUpdate SetState." )
COMMAND( addball, Acct::Role::PLAYER,
         "- Sends BubbleManager AddBalls.")
COMMAND( addball2, Acct::Role::PLAYER,
         "- Sends BubbleManager AddBalls2.")
COMMAND( kill, Acct::Role::VIP,
         "(itemID|charID|me) - destroys a ship, NPC, drone, structure or container in your system.  charID kills their ship")
COMMAND( unkillable, Acct::Role::VIP,
         "(itemID|charID|me) [on|off] - toggles whether something can be destroyed.  unkillable things stop at 1 structure hp")
COMMAND( killallnpcs, Acct::Role::ADMIN,
         " - insta-pops all NPC ships in the current bubble")
COMMAND( cloak, Acct::Role::VIP,
         " - instantly and unconditionally toggles cloak state of your vessel")
COMMAND( sov, Acct::Role::CONTENT,
         "(anchor|online|offline|unanchor|complete) (itemID) | tenure (level) [solarSystemID] - anchors, onlines, offlines or unanchors a structure at once, without the delays (sovereignty structures, starbases, platforms, customs offices).  online anchors first, unanchor offlines first.  complete finishes the running anchoring/onlining/unanchoring now.  tenure sets the claim's strategic (Sovereignty) index level 0-5 by backdating its claim time")
COMMAND( pos, Acct::Role::PROGRAMMER,
         "(anchor|online|offline|unanchor|complete) (itemID) | fuel [towerID] [hours] - the same as /sov (any structure, at once), or fills a control tower's fuel and strontium bays")
COMMAND( hop, Acct::Role::CONTENT,
         "(distance)[m|km|au] - moves your ship that far along its heading")
