#include "admin/CommandHelper.h"


// finds the nth noneq argument
// this function is 1 indexed meaning n = 1 would return the FIRST occurence
// the command name is arg 0, so it is the first occurence.  n = 2 is the first argument after it
// if not found returns -1
// this function returns the index of the args
int cmd_find_nth_noneq(const Seperator &args, int n) {
    int c = 0;
    for (int i = 0; i < args.argCount(); i++) {
        std::string arg = args.arg(i);
        size_t pos = arg.find("=");

        // Didn't find = in string
        if (pos == std::string::npos) {
            c++;
            if (c == n) {
                return i;
            }
        }
    }
    return -1;
}

// name should be like "name="
std::string cmd_parse_eq_arg(const Seperator &args, const char *name) {
    for (int i = 1; i < args.argCount(); i++) {
        std::string arg = args.arg(i);
        // name= must start the argument
        if (arg.compare(0, strlen(name), name) != 0) {
            continue;
        }
        if (strlen(name) >= arg.size()) {
            continue;
        }
        _log(COMMAND__MESSAGE, "cmd_parse_eq_arg: '%s' returning '%s'", name, arg.c_str() + strlen(name));
        return std::string(arg.c_str() + strlen(name));
    }

    return std::string();
}

Client* cmd_find_client(const Seperator& args, size_t index, Client* self) {
    if (args.argCount() <= index)
        return nullptr;
    const std::string& arg = args.arg(index);
    if (arg == "me")
        return self;
    if (args.isNumber(index)) {
        uint32 id(strtoul(arg.c_str(), nullptr, 10));
        if (IsCharacterID(id))
            return sEntityList.FindClientByCharID(id);
        // a ship in space with a pilot
        if ((self != nullptr) and (self->SystemMgr() != nullptr)) {
            SystemEntity* pSE(self->SystemMgr()->GetSE(id));
            if ((pSE != nullptr) and pSE->HasPilot())
                return pSE->GetPilot();
        }
        // any other loaded item: its owner, when online
        if (IsPlayerItem(id)) {
            InventoryItemRef iRef(sItemFactory.GetItemRefFromID(id, false));
            if ((iRef.get() != nullptr) and IsCharacterID(iRef->ownerID()))
                return sEntityList.FindClientByCharID(iRef->ownerID());
        }
        return nullptr;
    }
    Client* pClient(sEntityList.FindClientByName(arg.c_str()));
    if (pClient != nullptr)
        return pClient;
    std::vector<Client*> clients;
    sEntityList.GetClients(clients);
    for (auto cur : clients)
        if ((cur->GetChar().get() != nullptr) and (strcasecmp(cur->GetName(), arg.c_str()) == 0))
            return cur;
    return nullptr;
}

uint32 cmd_find_char_id(CommandDB* db, const Seperator& args, size_t index, Client* self) {
    if (args.argCount() <= index)
        return 0;
    const std::string& arg = args.arg(index);
    if (arg == "me")
        return (self != nullptr ? self->GetCharacterID() : 0);
    if (args.isNumber(index)) {
        uint32 id(strtoul(arg.c_str(), nullptr, 10));
        if (IsCharacterID(id))
            return id;
        Client* pClient(cmd_find_client(args, index, self));
        return (pClient != nullptr ? pClient->GetCharacterID() : 0);
    }
    Client* pClient(cmd_find_client(args, index, self));
    if (pClient != nullptr)
        return pClient->GetCharacterID();
    return db->GetCharacterID(arg);
}

InventoryItemRef cmd_find_item(Client* self, const std::string& arg) {
    if ((arg == "me") or (arg == "myship"))
        return self->GetShip();
    if (arg == "char")
        return self->GetChar();
    if (!IsNumber(arg))
        return InventoryItemRef(nullptr);
    uint32 id(strtoul(arg.c_str(), nullptr, 10));
    if (IsCharacterID(id)) {
        Client* pClient(sEntityList.FindClientByCharID(id));
        if (pClient != nullptr)
            return pClient->GetChar();
        return sItemFactory.GetItemRef(id);
    }
    if (IsPlayerItem(id))
        return sItemFactory.GetItemRef(id);
    // NPCs, asteroids, drones, structures and the like.  only the ones in your system
    if (self->SystemMgr() != nullptr) {
        SystemEntity* pSE(self->SystemMgr()->GetSE(id));
        if (pSE != nullptr)
            return pSE->GetSelf();
    }
    return sItemFactory.GetItemRefFromID(id, false);
}

uint16 cmd_find_attribute(CommandDB* db, const std::string& arg) {
    if (arg.empty())
        return 0;
    if (IsNumber(arg)) {
        uint32 id(strtoul(arg.c_str(), nullptr, 10));
        if ((id == 0) or (id > 0xFFFF))
            return 0;
        return (db->GetAttributeName(id).empty() ? 0 : id);
    }
    int id(db->GetAttributeID(arg.c_str()));
    return (id > 0 ? id : 0);
}

bool cmd_parse_distance(const std::string& text, double& metres) {
    if (text.empty())
        return false;
    char* end(nullptr);
    double value(strtod(text.c_str(), &end));
    if (end == text.c_str())
        return false;
    std::string unit(end);
    // trim spaces around the unit
    while (!unit.empty() and isspace(static_cast<unsigned char>(unit.front())))
        unit.erase(0, 1);
    while (!unit.empty() and isspace(static_cast<unsigned char>(unit.back())))
        unit.pop_back();
    if (unit.empty() or (strcasecmp(unit.c_str(), "m") == 0)) {
        metres = value;
    } else if (strcasecmp(unit.c_str(), "km") == 0) {
        metres = value * 1000.0;
    } else if (strcasecmp(unit.c_str(), "au") == 0) {
        metres = value * ONE_AU_IN_METERS;
    } else {
        return false;
    }
    return true;
}
