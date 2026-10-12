//no macroguard on purpose


COMMAND( spawnn, Acct::Role::ADMIN,
         "(count) (spread) (typeID) - spawns count of typeID within spread meters of you (the client's WM: /Spawn this type)" )
COMMAND( spawn, Acct::Role::SPAWN,
         "(typeID) - Spawn an NPC with the specified type." )
COMMAND( unspawn, Acct::Role::SPAWN,
         "(itemID) | [range=meters] only=(groupWreck|categoryDrone|group<N>|category<N>|...) - remove and delete itemID, or everything of a group/category in range (default 500km)" )
COMMAND( search, Acct::Role::VIP,
         "(text) - Search for items matching the specified query" )
COMMAND( giveisk, Acct::Role::GMH,
         "(entityID) (amount) - Give the specified amount of cash to the specified character. 0=self." )
COMMAND( freesp, Acct::Role::GMH,
         "(charID|me) (amount) - adds (or with a negative amount takes) unallocated skill points" )
COMMAND( recustomize, Acct::Role::GMH,
         "(charID|me) (state) - sets the paperdoll state: 0 none, 1 resculpt, 3 full recustomization (bloodline), 4 forced at login" )
COMMAND( token, Acct::Role::GMH,
         "(charID|me) (typeID) [quantity] - adds an item to the redeem list of the character's account" )
COMMAND( pop, Acct::Role::ADMIN,
         "(type) (key) (value) - Send an OnRemoteMessage" )
COMMAND( setbpattr, Acct::Role::CONTENT,
         "(itemID) (copy) (materialLevel) (productivityLevel) (licensedProductionRunsRemaining) - Change blueprint's attributes." )
COMMAND( getattr, Acct::Role::VIP,
         "(itemID|me|char) (attributeID|name) - shows an item's current and type default attribute value" )
COMMAND( setattr, Acct::Role::CONTENT,
         "(itemID|me|char) (attributeID|name) (value) - sets a live attribute value until the next refit/undock" )
COMMAND( fit, Acct::Role::VIP,
        "(itemID) - Fits selected item to active ship." )
COMMAND( giveallskills, Acct::Role::ADMIN,
        "(me|charID|\"name\") - gives ALL skills to designated online character or self" )
COMMAND( giveskill, Acct::Role::VIP,
         "(me|charID|\"name\") (skillID) (level) - gives skillID to specified level." )
COMMAND( online, Acct::Role::VIP,
        "(entityID) - online all modules on the ship of the entityID. entityID=me=>online my modules" )
COMMAND( unload, Acct::Role::VIP,
        "(entityID) (itemID) - unload module itemID from entityID (itemID=all=>unload all) (entityID=me=>my modules)" )
COMMAND( repairmodules, Acct::Role::HEALSELF,
        "(entityID) (itemID) - repair the modules of the character with the entityID" )
COMMAND( giveskills, Acct::Role::ADMIN,
        "(itemID) - gives skills to character." )
COMMAND( dogma, Acct::Role::ADMIN,
        "(itemID|me) (attributeName|attributeID) = (value) - sets a live attribute value until the next refit/undock" )
COMMAND( kick, Acct::Role::ADMIN,
        "(charName) - kicks [charName] from the server")
COMMAND( ban, Acct::Role::ADMIN,
        "(charName) - bans player's account from the server")
COMMAND( unban, Acct::Role::ADMIN,
        "(charName) - removes ban on player's account")
COMMAND( givelp, Acct::Role::ADMIN,
        "('me'|<characterID>) (corporationID) (amount) - adds LP to a character for a specific corporation")

COMMAND( whois, Acct::Role::GMH,
        "(charID|\"name\") - shows account, corp, location and status of a character (online or not)")
COMMAND( chatban, Acct::Role::GML,
        "(charID|\"name\") [days|perm] [\"reason\"] - gags a character in every chat channel, online or offline (lift with /ungag)")
