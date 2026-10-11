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

#include "EntityList.h"
#include "Client.h"
#include "packets/Planet.h"
#include "planet/PlanetORBBound.h"
#include "planet/CustomsOffice.h"
#include "system/SystemManager.h"
#include "services/ServiceManager.h"

PlanetORB::PlanetORB(EVEServiceManager& mgr) :
    BindableService("planetOrbitalRegistryBroker", mgr)
{
}

BoundDispatcher* PlanetORB::BindObject(Client* client, PyRep* bindParameters) {
    if (bindParameters->IsInt() == false) {
        throw CustomError("Cannot bind service");
    }

    uint32 systemID = bindParameters->AsInt()->value();
    auto it = this->m_instances.find (systemID);

    if (it != this->m_instances.end ())
        return it->second;

    PlanetORBBound* bound = new PlanetORBBound(this->GetServiceManager(), *this, systemID);

    this->m_instances.insert_or_assign (systemID, bound);

    return bound;
}

void PlanetORB::BoundReleased (PlanetORBBound* bound) {

}
PlanetORBBound::PlanetORBBound(EVEServiceManager& mgr, PlanetORB& parent, uint32 systemID) :
    EVEBoundObject(mgr, parent),
    m_systemID(systemID)
{
    this->Add("GetTaxRate", &PlanetORBBound::GetTaxRate);
    this->Add("GetSettingsInfo", &PlanetORBBound::GetSettingsInfo);
    this->Add("UpdateSettings", &PlanetORBBound::UpdateSettings);
    this->Add("GMChangeSpaceObjectOwner", &PlanetORBBound::GMChangeSpaceObjectOwner);
}

namespace {
    CustomsSE* GetCustomsOffice(uint32 systemID, uint32 itemID) {
        SystemManager* pSystem(sEntityList.FindOrBootSystem(systemID));
        SystemEntity* pSE(pSystem == nullptr ? nullptr : pSystem->GetSE(itemID));
        if ((pSE == nullptr) or (pSE->GetCOSE() == nullptr))
            throw CustomError ("Customs office %u not found", itemID);
        return pSE->GetCOSE();
    }
}

PyResult PlanetORBBound::GetTaxRate(PyCallArgs& call, PyInt* itemID)
{
    //  taxRate = moniker.GetPlanetOrbitalRegistry(session.solarsystemid).GetTaxRate(itemID)
    // NOTE:  "return PyNone()" = access denied to customs office.
    CustomsSE* pCOSE(GetCustomsOffice(m_systemID, itemID->value()));

    // corporation, alliance or standing rate.  None when the owner doesn't let this character in (PortStandingCheckFail)
    float rate = pCOSE->GetTaxRate(call.client);
    if (rate < 0)
        return PyStatic.NewNone();

    return new PyFloat(rate);
}

PyResult PlanetORBBound::GetSettingsInfo(PyCallArgs& call, PyInt* orbitalID)
{
    /*   self.orbitalData = self.remoteOrbitalRegistry.GetSettingsInfo(self.orbitalID)  << for customs offices
     *   self.selectedHour, self.taxRateValues, self.standingLevel, self.allowAlliance, self.allowStandings = self.orbitalData
     */
    CustomsSE* pCOSE(GetCustomsOffice(m_systemID, orbitalID->value()));
    if (call.client->GetCorporationID() != pCOSE->GetOwnerID())
        throw CustomError ("Only members of the owning corporation can view this customs office's settings.");
    return pCOSE->GetSettingsInfo();
}

PyResult PlanetORBBound::UpdateSettings(PyCallArgs& call, PyInt* orbitalID, PyRep* reinforceValue, PyObject* taxRateValues, PyRep* standingValue, PyRep* allowAllianceValue, PyRep* allowStandingsValue)
{
    //remoteOrbitalRegistry.UpdateSettings(self.orbitalID, reinforceValue, taxRateValues, standingValue, allowAllianceValue, allowStandingsValue)
    _log(INV__MESSAGE, "Calling PlanetORBBound::UpdateSettings()");
    call.Dump(PLANET__DUMP);

    CustomsSE* pCOSE(GetCustomsOffice(m_systemID, orbitalID->value()));
    // the owner's station managers configure the office (orbitalConfiguration)
    if ((call.client->GetCorporationID() != pCOSE->GetOwnerID()) or ((call.client->GetCorpRole() & Corp::Role::StationManager) == 0))
        throw CustomError ("You need to be a Station Manager of the owning corporation to configure this customs office.");

    PyDict* input = taxRateValues->arguments()->AsDict();
    Call_TaxRateValuesDict dict;
    if (!dict.Decode(input)) {
        codelog(SERVICE__ERROR, "PlanetORBBound: Failed to decode arguments.");
        return nullptr;
    }

    // tax rates are fractions.  the reinforcement hour is a UTC hour, the standing one of the contact levels
    auto clamp = [](double value) { return (float)std::min(1.0, std::max(0.0, value)); };
    dict.corporation = clamp(dict.corporation);
    dict.alliance = clamp(dict.alliance);
    dict.standingHorrible = clamp(dict.standingHorrible);
    dict.standingBad = clamp(dict.standingBad);
    dict.standingNeutral = clamp(dict.standingNeutral);
    dict.standingGood = clamp(dict.standingGood);
    dict.standingHigh = clamp(dict.standingHigh);
    int64 hour = std::min((int64)23, std::max((int64)0, PyRep::IntegerValue(reinforceValue)));
    double standing = (standingValue->IsFloat() ? standingValue->AsFloat()->value() : (double)PyRep::IntegerValue(standingValue));
    int8 standingLevel = EVEPOS::StandingValues::StandingHorrible;
    if (standing >= 10) {
        standingLevel = EVEPOS::StandingValues::StandingHigh;
    } else if (standing >= 5) {
        standingLevel = EVEPOS::StandingValues::StandingGood;
    } else if (standing > -5) {
        standingLevel = EVEPOS::StandingValues::StandingNeutral;
    } else if (standing > -10) {
        standingLevel = EVEPOS::StandingValues::StandingBad;
    }
    bool allowAlliance = (allowAllianceValue->IsBool() ? allowAllianceValue->AsBool()->value() : (PyRep::IntegerValue(allowAllianceValue) != 0));
    bool allowStandings = (allowStandingsValue->IsBool() ? allowStandingsValue->AsBool()->value() : (PyRep::IntegerValue(allowStandingsValue) != 0));

    pCOSE->UpdateSettings((int8)hour, standingLevel, allowAlliance, allowStandings, dict);

    return nullptr;
}

PyResult PlanetORBBound::GMChangeSpaceObjectOwner(PyCallArgs& call, PyInt* itemID, PyInt* corpID)
{
    // this is called when taking ownership of control tower
    // sends itemID, corpID
    /*
                state = slimItem.orbitalState
                if state in (entities.STATE_UNANCHORING,
                 entities.STATE_ONLINING,
                 entities.STATE_ANCHORING,
                 entities.STATE_OPERATING,
                 entities.STATE_OFFLINING,
                 entities.STATE_SHIELD_REINFORCE):
                    stateText = pos.DISPLAY_NAMES[pos.Entity2DB(state)]
                    gm.append(('End orbital state change (%s)' % stateText, self.CompleteOrbitalStateChange, (itemID,)))
                elif state == entities.STATE_ANCHORED:
                    upgradeType = sm.GetService('godma').GetTypeAttribute2(slimItem.typeID, const.attributeConstructionType)
                    if upgradeType is not None:
                        gm.append(('Upgrade to %s' % cfg.invtypes.Get(upgradeType).typeName, self.GMUpgradeOrbital, (itemID,)))
                gm.append(('GM: Take Control', self.TakeOrbitalOwnership, (itemID, slimItem.planetID)))

    def TakeOrbitalOwnership(self, itemID, planetID):
        registry = moniker.GetPlanetOrbitalRegistry(session.solarsystemid)
        registry.GMChangeSpaceObjectOwner(itemID, session.corpid)
    */
    _log(PLANET__DEBUG, "PlanetORBBound::Handle_GMChangeSpaceObjectOwner - size=%lli", call.tuple->size());
    call.Dump(PLANET__DUMP);

    if ((call.client->GetAccountRole() & (Acct::Role::GML | Acct::Role::GMH | Acct::Role::WORLDMOD)) == 0)
        throw CustomError ("Not allowed");
    CustomsSE* pCOSE(GetCustomsOffice(m_systemID, itemID->value()));
    int32 allianceID(corpID->value() == call.client->GetCorporationID() ? call.client->GetAllianceID() : 0);
    pCOSE->ChangeOwner(corpID->value(), allianceID);

    return PyStatic.NewNone();
}


/**
    def ConfigureOrbital(self, item):
        sm.GetService('planetUI').OpenConfigureWindow(item)

        */
