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
    Author:        Allan
*/

#include "eve-server.h"


#include "system/IndexManager.h"
#include "system/sov/SovereigntyDataMgr.h"
#include "inventory/AttributeEnum.h"

IndexManager::IndexManager() :
    Service("devIndexManager", eAccessLevel_SolarSystem2)
{
    this->Add("GetAllDevelopmentIndices", &IndexManager::GetAllDevelopmentIndices);
    this->Add("GetDevelopmentIndicesForSystem", &IndexManager::GetDevelopmentIndicesForSystem);
}

PyResult IndexManager::GetAllDevelopmentIndices(PyCallArgs& call) {
    /*
        for indexInfo in sm.RemoteSvc('devIndexManager').GetAllDevelopmentIndices():
            systemToIndexMap[indexInfo.solarSystemID] = {const.attributeDevIndexMilitary: indexInfo.militaryPoints,
             const.attributeDevIndexIndustrial: indexInfo.industrialPoints,
             const.attributeDevIndexSovereignty: indexInfo.claimedFor * CLAIM_DAYS_TO_SECONDS}
     */
    return svDataMgr.GetAllDevIndices();
}

PyResult IndexManager::GetDevelopmentIndicesForSystem(PyCallArgs& call, PyInt* solarSystemID) {
    // {attributeID: KeyVal(points, increasing)} for the military and industrial indices only (see sovSvc.CanInstallUpgrade)
    return svDataMgr.GetDevIndicesForSystem(solarSystemID->value());
}

/*
developmentIndices = [attributeDevIndexMilitary, attributeDevIndexIndustrial, attributeDevIndexSovereignty]
attributeDevIndexMilitary = 1583
attributeDevIndexIndustrial = 1584
attributeDevIndexSovereignty = 1615
*/