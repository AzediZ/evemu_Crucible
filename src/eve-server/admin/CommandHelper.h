#ifndef __COMMAND_HELPER_H__
#define __COMMAND_HELPER_H__

#include "ConsoleCommands.h"
#include "admin/CommandDB.h"
#include "inventory/InventoryItem.h"

int cmd_find_nth_noneq(const Seperator &args, int n);
std::string cmd_parse_eq_arg(const Seperator &args, const char *name);

// an online character: "me" -> self, a charID, a shipID/itemID (-> its pilot or owner if online),
// or a name (exact, then case-insensitive).  nullptr when not found/offline
Client* cmd_find_client(const Seperator& args, size_t index, Client* self);
// the character ID for "me", a charID or a name, online or not (0 when unknown)
uint32 cmd_find_char_id(CommandDB* db, const Seperator& args, size_t index, Client* self);
// an item to read/write attributes on: "me"/"myship" -> active ship, "char" -> character,
// a player item / charID -> sItemFactory, anything else -> SystemEntity in the caller's system (NPCs, asteroids, structures)
InventoryItemRef cmd_find_item(Client* self, const std::string& arg);
// an attribute by ID or dgmAttributeTypes name (0 if unknown)
uint16 cmd_find_attribute(CommandDB* db, const std::string& arg);
// "12.5km", "3au", "500" (m), "500m" -> metres.  false on parse error
bool cmd_parse_distance(const std::string& text, double& metres);

#endif
