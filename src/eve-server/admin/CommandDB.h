/*
  ------------------------------------------------------------------------------------
  LICENSE:
  ------------------------------------------------------------------------------------
  This file is part of EVEmu: EVE Online Server Emulator
  Copyright 2006 - 2021 The EVEmu Team
  For the latest information visit https://evemu.dev
  ------------------------------------------------------------------------------------
  This program is free software; you can redistribute it and/or modify it under
  the terms of the GNU Lesser General Public License as published by the Free Software
  Foundation; either version 2 of the License, or (at your option) any later
  version.

  This program is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
  FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License along with
  this program; if not, write to the Free Software Foundation, Inc., 59 Temple
  Place - Suite 330, Boston, MA 02111-1307, USA, or go to
  http://www.gnu.org/copyleft/lesser.txt.
  ------------------------------------------------------------------------------------
  Author:        Zhur
*/
#ifndef __COMMANDDB_H_INCL__
#define __COMMANDDB_H_INCL__

#include "ServiceDB.h"

class CommandDB
: public ServiceDB
{
 public:
    typedef std::pair<uint32, int32> skillStateDescriptor;
    typedef std::vector<skillStateDescriptor> charSkillStates;

    bool ItemSearch(const char *query, std::map<uint32, std::string> &into);
    bool ItemSearch(uint32 typeID, uint32 &actualTypeID, std::string &actualTypeName,
                    uint32 &actualGroupID, uint32 &actualCategoryID, double &actualRadius);
    int GetAttributeID(const char *attributeName);
    int GetAccountID(std::string name);
    // the account of a character (0 if unknown)
    uint32 GetAccountIDByChar(uint32 charID);
    // a character by exact name (the column's collation ignores case).  0 if unknown
    uint32 GetCharacterID(const std::string& name);
    // the dgmAttributeTypes name of an attribute (empty if unknown)
    std::string GetAttributeName(uint16 attributeID);

    // /whois:  a character's account, corp, location and status, online or not
    struct WhoisData {
        uint32 charID, accountID, corpID, allianceID, stationID, solarSystemID, shipID;
        int64 role, logonDateTime, logoffDateTime, skillPoints;
        double balance;
        float securityRating;
        bool online, banned;
        std::string name, accountName, corpName, allianceName, systemName, stationName;
    };
    bool GetWhoisData(uint32 charID, WhoisData& into);
    bool FullSkillList(std::vector<uint32> &skillList);
    static bool NotFullyLearnedSkillList(charSkillStates &skillList, uint32 charID);
    uint32_t GetStation(const char *name);
    uint32_t GetSolarSystem(const char *name);

private:
    static void PushNonPublishedSkills(std::vector<uint32> &skillList);
};


#endif
