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
    Author:     Zhur
*/

#include "eve-server.h"

#include "Client.h"
#include "StaticDataMgr.h"
#include "character/Character.h"
#include "corporation/CorporationDB.h"

// this shall be removed when i remove MulticastTarget
#include "EntityList.h"

/*
 * CORP__DB_ERROR
 * CORP__DB_WARNING
 * CORP__DB_INFO
 * CORP__DB_MESSAGE
 */

bool CorporationDB::DoesCorporationExist(uint32 corpID) {
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT corporationID FROM crpCorporation WHERE corporationID = %u", corpID))
    {
        codelog(DATABASE__ERROR, "Error in query: %s", res.error.c_str());
        return false;
    }

    return (res.GetRowCount() != 0);
}

bool CorporationDB::GetCorporationBySchool(uint32 schoolID, uint32 &corporationID) {
    DBQueryResult res;

    if (!sDatabase.RunQuery(res, "SELECT corporationID FROM chrSchools WHERE schoolID = %u", schoolID)) {
        codelog(DATABASE__ERROR, "Error in query: %S", res.error.c_str());
        return false;
    }

    DBResultRow row;
    if (!res.GetRow(row)) {
        codelog(DATABASE__ERROR, "Failed to find matching corporation for school %u", schoolID);
        return false;
    }
    corporationID = row.GetInt(0);
    return true;
}

/**
 * @todo Here should come a call to Corp??::CharacterJoinToCorp or what the heck... for now we only put it there
 */
bool CorporationDB::GetLocationCorporationByCareer(CharacterData& cdata, uint32& corporationID) {
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT "      // fixed DB Query   -allan 01/02/14  -UD 9Jul19
        "  cs.corporationID, "
        "  cs.schoolID, "
        "  co.stationID, "
        "  st.solarSystemID, "
        "  st.constellationID, "
        "  st.regionID "
        " FROM careers AS c"
        "    LEFT JOIN chrSchools AS cs USING (schoolID)"
        "    LEFT JOIN crpCorporation AS co ON cs.corporationID = co.corporationID"
        "    LEFT JOIN staStations AS st USING (stationID)"
        " WHERE c.careerID = %u", cdata.careerID))
    {
        codelog(DATABASE__ERROR, "Error in query: %s", res.error.c_str());
        return false;
    }

    DBResultRow row;
    if (!res.GetRow(row)) {
        codelog(DATABASE__ERROR, "Failed to find career %u", cdata.careerID);
        return false;
    }

    corporationID = row.GetUInt(0);
    cdata.schoolID = row.GetUInt(1);
    cdata.stationID = row.GetUInt(2);
    cdata.solarSystemID = row.GetUInt(3);
    cdata.constellationID = row.GetUInt(4);
    cdata.regionID = row.GetUInt(5);

    return true;
}

void CorporationDB::GetCorpStations(uint32 corp_id, std::vector<uint32>& stVec) {
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT stationID FROM staStations WHERE corporationID=%u", corp_id)) {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return;
    }

    DBResultRow row;
    while (res.GetRow(row))
        stVec.push_back(row.GetInt(0));

    return;
}
/*
PyObject *CorporationDB::ListStationOffices(uint32 station_id) {
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT itemID, officeFolderID, corporationID, stationID, typeID, lockDown, rentalFee, expiryDateTime"
        " FROM staOffices"
        " WHERE stationID = %u", station_id ))
    {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return nullptr;
    }

    return DBResultToRowset(res);
}
*/

PyObject *CorporationDB::ListStationCorps(uint32 station_id) {
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT "
        "   corporationID,corporationName,description,shares,graphicID,"
        "   memberCount,ceoID,stationID,raceID,corporationType,creatorID,"
        "   hasPlayerPersonnelManager,tickerName,sendCharTerminationMessage,"
        "   shape1,shape2,shape3,color1,color2,color3,typeface,memberLimit,"
        "   allowedMemberRaceIDs,url,taxRate,minimumJoinStanding,division1,"
        "   division2,division3,division4,division5,division6,division7,"
        "   allianceID,deleted,isRecruiting"
        " FROM crpCorporation"
//no idea what the criteria should be here...
        " WHERE stationID=%u",
            station_id
    ))
    {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return nullptr;
    }

    return DBResultToRowset(res);
}

PyObject *CorporationDB::ListStationOwners(uint32 stationID) {
    /** @todo  this needs work.... */
    DBQueryResult res;
  if (sDataMgr.IsStation(stationID)) {
    if (!sDatabase.RunQuery(res,
        "SELECT"
        "  s.corporationID AS ownerID,"
        "  c.corporationName AS ownerName,"
        "  c.corporationType AS typeID,"
        //"  c.corporationID AS ownerNameID,"
        "  cs.gender"
        " FROM staStations AS s"
        "  LEFT JOIN crpCorporation AS c ON cs.corporationID = c.corporationID"
        "  LEFT JOIN chrNPCCharacters AS cs ON cs.characterID = c.ceoID"
        " WHERE s.stationID = %u", stationID ))
    {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return nullptr;
    }
  } else {
    if (!sDatabase.RunQuery(res,
        "SELECT"
        "  c.corporationID AS ownerID,"
        "  c.corporationName AS ownerName,"
        "  c.corporationType AS typeID,"
        //"  c.corporationID AS ownerNameID,"
        "  cs.gender"
        " FROM entity AS e"
        "  LEFT JOIN crpCorporation AS c USING ( corporationID )"
        "  LEFT JOIN chrCharacters AS cs ON cs.characterID = c.ceoID"
        " WHERE e.itemID = %u", stationID ))
    {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return nullptr;
    }
  }

    return DBResultToRowset(res);
}

PyRep *CorporationDB::GetCorpInfo(uint32 corpID) {
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT corporationID, corporationName, description, creatorID, tickerName,"
        " url, allianceID, deleted, stationID, ceoID, shares, memberCount, taxRate"
        " FROM crpCorporation WHERE corporationID = %u", corpID))
    {
        codelog(CORP__DB_ERROR, "Error in retrieving corporation's data (%u)", corpID);
        return nullptr;
    }

    DBResultRow row;
    if (!res.GetRow(row)) {
        codelog(CORP__DB_WARNING, "Unable to find corporation's data (%u)", corpID);
        return nullptr;
    }

    //return DBResultToRowset(res);
    return DBRowToKeyVal(row);
}

PyObject *CorporationDB::GetCorporation(uint32 corpID) {
    // called by corp member. different from GetCorporations() below
    //  not sure what the difference between them is/should be
    std::string table = "crpWalletDivisons";
    if (IsNPCCorp(corpID))
        table = "crpNPCWalletDivisons";
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        " SELECT "
        "   c.corporationID,c.corporationName,c.description,c.tickerName,c.url,"
        "   c.taxRate,c.minimumJoinStanding,c.corporationType,c.hasPlayerPersonnelManager,"
        "   c.sendCharTerminationMessage,c.creatorID,c.ceoID,c.stationID,c.raceID,"
        "   c.allianceID,c.shares,c.memberCount,c.memberLimit,c.allowedMemberRaceIDs,"
        "   c.shape1,c.shape2,c.shape3,c.color1,c.color2,c.color3,c.typeface,c.deleted,c.isRecruiting,c.warFactionID,"
        "   w.division1,w.division2,w.division3,w.division4,w.division5,w.division6,"
        "   w.division7,w.walletDivision1,w.walletDivision2,w.walletDivision3,w.walletDivision4,"
        "   w.walletDivision5,w.walletDivision6,w.walletDivision7"
        " FROM crpCorporation AS c"
        "  LEFT JOIN %s AS w USING (corporationID)"
        " WHERE corporationID = %u", table.c_str(), corpID))
    {
        codelog(CORP__DB_ERROR, "Error in retrieving corporation's data (%u)", corpID);
        return nullptr;
    }

    DBResultRow row;
    if (!res.GetRow(row)) {
        codelog(CORP__DB_WARNING, "Unable to find corporation's data (%u)", corpID);
        return nullptr;
    }

    return DBRowToRow(row);
    //return DBResultToRowset(res);
}

PyRep *CorporationDB::GetCorporations(uint32 corpID) {
    // called by non-member. different from GetCorporation() above
    //  not sure what the differece between them is/should be
    std::string table = "crpWalletDivisons";
    if (IsNPCCorp(corpID))
        table = "crpNPCWalletDivisons";
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT"
        "   c.corporationID,c.corporationName,c.description,c.tickerName,c.url,"
        "   c.taxRate,c.minimumJoinStanding,c.corporationType,c.hasPlayerPersonnelManager,"
        "   c.sendCharTerminationMessage,c.creatorID,c.ceoID,c.stationID,c.raceID,"
        "   c.allianceID,c.shares,c.memberCount,c.memberLimit,c.allowedMemberRaceIDs,"
        "   c.shape1,c.shape2,c.shape3,c.color1,c.color2,c.color3,c.typeface,c.deleted,c.isRecruiting,c.warFactionID,"
        "   w.division1,w.division2,w.division3,w.division4,w.division5,w.division6,"
        "   w.division7,w.walletDivision1,w.walletDivision2,w.walletDivision3,w.walletDivision4,"
        "   w.walletDivision5,w.walletDivision6,w.walletDivision7"
        " FROM crpCorporation AS c"
        "  LEFT JOIN %s AS w USING (corporationID)"
        " WHERE corporationID = %u", table.c_str(), corpID))
    {
        codelog(CORP__DB_ERROR, "Error in retrieving corporation's data (%u)", corpID);
        return nullptr;
    }

    DBResultRow row;
    if (!res.GetRow(row)) {
        codelog(CORP__DB_WARNING, "Unable to find corporation's data (%u)", corpID);
        return nullptr;
    }

    return DBRowToPackedRow(row);
}

PyObject *CorporationDB::GetEmploymentRecord(uint32 charID) {
    DBQueryResult res;
    //do we really need this order by??
    if (!sDatabase.RunQuery(res,
        "SELECT startDate, corporationID, deleted "
        "   FROM chrEmployment "
        "   WHERE characterID = %u "
        "   ORDER BY startDate DESC", charID
        ))
    {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return nullptr;
    }

    return DBResultToRowset(res);
}

uint16 CorporationDB::CreateMedal(uint32 ownerID, uint32 creatorID, const std::string& title, const std::string& description)
{
    std::string cTitle, cDesc;
    sDatabase.DoEscapeString(cTitle, title);
    sDatabase.DoEscapeString(cDesc, description);
    uint32 medalID(0);
    DBerror err;
    sDatabase.RunQueryLID(err, medalID,
        " INSERT INTO crpMedals (ownerID, creatorID, title, description, date)"
        " VALUES (%u, %u, '%s', '%s', %lli)", ownerID, creatorID, cTitle.c_str(), cDesc.c_str(), GetFileTimeNow());

    return medalID;
}

void CorporationDB::SaveMedalData(int64 medalID, std::vector< Corp::MedalData >& dataList)
{
    // loop thru data and add to query
    std::ostringstream query;
    query << "INSERT INTO crpMedalData(medalID, part, graphic, color)";
    query << "VALUES ";

    bool first = true;
    for (auto cur : dataList) {
        if (first) {
            first = false;
        } else {
            query << ",";
        }
        query << "(" << std::to_string(medalID) << "," << std::to_string(cur.part) << ",'" << cur.graphic << "'," << std::to_string(cur.color) << ")";
    }

    if (!first ) {
        DBerror err;
        sDatabase.RunQuery(err, query.str().c_str());
    }

    //SELECT id, part, graphic, color FROM crpMedalData
}

PyRep* CorporationDB::GetRecipientsOfMedal(int32 medalID)
{
    //  SELECT recepientID, medalID, corpID, date, status FROM chrMedals WHERE recepientID = %u
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT recepientID, issuerID, date, reason, status"
        " FROM chrMedals"
        " WHERE medalID = %i", medalID))
    {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return nullptr;
    }

    return DBResultToCRowset(res);
}

PyRep* CorporationDB::GetAllCorpMedals(uint32 corpID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT medalID, creatorID, noRecepients, date, title, description FROM crpMedals WHERE ownerID = %u", corpID))
    {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return nullptr;
    }

    return DBResultToCRowset(res);
}

PyRep* CorporationDB::GetCorpMedalData(uint32 corpID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT medalID, part, graphic, color FROM crpMedalData"
        " WHERE medalID IN (SELECT medalID FROM crpMedals WHERE ownerID = %u)", corpID))
    {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return nullptr;
    }

    return DBResultToCRowset(res);
}

PyRep* CorporationDB::GetMedalsReceived(int32 charID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT chr.medalID, crp.creatorID, crp.noRecepients, crp.date, crp.title, crp.description, chr.status"
        " FROM chrMedals AS chr"
        " LEFT JOIN crpMedals AS crp USING (medalID)"
        " WHERE chr.recepientID = %i", charID))
    {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return nullptr;
    }

    return DBResultToCRowset(res);
}

PyRep* CorporationDB::GetMedalsReceivedDetails(int32 charID)
{
    // SELECT recepientID, medalID, corpID, dateTime FROM chrMedals WHERE recepientID = %u
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT medalID, part, graphic, color FROM crpMedalData"
        " WHERE medalID IN (SELECT medalID FROM chrMedals WHERE recepientID = %i)", charID))
    {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return nullptr;
    }

    return DBResultToCRowset(res);
}

PyObjectEx* CorporationDB::GetMedalDetails(int32 medalID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT medalID, ownerID, creatorID, noRecepients AS numberOfRecipients, date, title, description FROM crpMedals"
        " WHERE medalID = %i", medalID))
    {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return nullptr;
    }

    //  SELECT medalID, ownerID, creatorID, noRecepients, date, title, description FROM crpMedals WHERE medalID

    return DBResultToCRowset(res);
}

PyRep* CorporationDB::GetMedalStatuses()
{
    // this is for viewing permissions on paperdoll
    DBQueryResult res;
    sDatabase.RunQuery(res, "SELECT statusID, statusName FROM crpMedalStatus");
    return DBResultToRowset(res);
}

void CorporationDB::RemoveMedal(uint32 charID, int32 medalID)
{
    DBerror err;
    uint32 affected(0);
    if (!sDatabase.RunQuery(err, affected, "DELETE FROM chrMedals WHERE recepientID = %u AND medalID = %i", charID, medalID)) {
        codelog(CORP__DB_ERROR, "Error in query: %s", err.c_str());
        return;
    }
    if (affected > 0)
        sDatabase.RunQuery(err, "UPDATE crpMedals SET noRecepients = GREATEST(0, CAST(noRecepients AS SIGNED) - %u) WHERE medalID = %i", affected, medalID);
}

void CorporationDB::GiveMedalToCharacters(uint32 issuerID, uint32 corpID, int32 medalID, std::vector< uint32 >& charVec, const std::string& reason)
{
    // INSERT INTO chrMedals(recepientID, medalID, corpID, issuerID, reason, date, status) VALUES ()
    std::string eReason;
    sDatabase.DoEscapeString(eReason, reason);

    // loop thru data and add to query
    std::ostringstream query;
    query << "INSERT INTO chrMedals(recepientID, medalID, corpID, issuerID, date, reason)";
    query << "VALUES ";

    bool first = true;
    for (auto cur : charVec) {
        if (first) {
            first = false;
        } else {
            query << ",";
        }
        query << "(" << std::to_string(cur) << "," << std::to_string(medalID) << "," << std::to_string(corpID) << "," << std::to_string(issuerID) << ",";
        query << std::to_string(GetFileTimeNow()) << ",'" << eReason.c_str() << "')";
    }

    if (!first ) {
        DBerror err;
        sDatabase.RunQuery(err, query.str().c_str());
        sDatabase.RunQuery(err, "UPDATE crpMedals SET noRecepients=noRecepients + %u WHERE medalID = %u", (uint32)charVec.size(), medalID );
    }
}

void CorporationDB::SetMedalStatus(uint32 charID, uint16 medalID, uint8 status)
{
    DBerror err;
    sDatabase.RunQuery(err, "UPDATE chrMedals SET status = %u WHERE recepientID = %u AND medalID = %u", status, charID, medalID);
}

bool CorporationDB::IsTickerTaken(std::string ticker) {
    DBQueryResult res;
    sDatabase.RunQuery(res, " SELECT corporationID FROM crpCorporation WHERE tickerName = '%s'", ticker.c_str());
    return (res.GetRowCount() != 0);
}

static std::string _IoN( PyRep* r )
{
    if ( !r->IsInt() )
        return "NULL";
    return std::to_string(PyRep::IntegerValueU32(r));
}

bool CorporationDB::AddCorporation(Call_AddCorporation & corpInfo, Client* pClient, uint32 & corpID) {
    std::string cName, cDesc, cTick, cURL;
    sDatabase.DoEscapeString(cName, corpInfo.corpName);
    sDatabase.DoEscapeString(cDesc, corpInfo.description);
    sDatabase.DoEscapeString(cTick, corpInfo.corpTicker);
    sDatabase.DoEscapeString(cURL, corpInfo.url);

    Character* pChar = pClient->GetChar().get();
    uint32 charID = pClient->GetCharacterID();
    uint8 raceID = (uint8)pChar->race();

    // member limit from the creator's skills (corporationMemberBonus per level); CorpMgr::UpdateAbilities sets the races after creation
    uint16 mLimit = (pChar->GetSkillLevel(EvESkill::CorporationManagement) * 10);
    mLimit += (pChar->GetSkillLevel(EvESkill::MegacorpManagement) * 50);
    mLimit += (pChar->GetSkillLevel(EvESkill::EmpireControl) * 200);
    mLimit += (pChar->GetSkillLevel(EvESkill::Sovereignty) * 1000);
    DBerror err;
    if (!sDatabase.RunQueryLID(err, corpID,
        " INSERT INTO crpCorporation ( "
        "   corporationName, description, tickerName, url, taxRate, corporationType, hasPlayerPersonnelManager, "
        "   creatorID, ceoID, stationID, raceID, shares, memberLimit, allowedMemberRaceIDs,"
        "   graphicID, color1, color2, color3, shape1, shape2, shape3, "
        "   isRecruiting, allianceMemberStartDate ) "
        " VALUES "
        "   ('%s', '%s', '%s', '%s', %f, 2, 1, "
        "    %u, %u, %u, %u, 1000, %u, %u,"
        "    0, %s, %s, %s, %s, %s, %s, "
        "    %u, 0) ",
        cName.c_str(), cDesc.c_str(), cTick.c_str(), cURL.c_str(), corpInfo.taxRate,
        charID, charID, pClient->GetStationID(), raceID, mLimit, raceID,
	_IoN(corpInfo.color1).c_str(),
	_IoN(corpInfo.color2).c_str(),
	_IoN(corpInfo.color3).c_str(),
	_IoN(corpInfo.shape1).c_str(),
	_IoN(corpInfo.shape2).c_str(),
	_IoN(corpInfo.shape3).c_str(),
	corpInfo.applicationEnabled))
    {
        codelog(CORP__DB_ERROR, "Error in AddCorporation query: %s", err.c_str());
        return false;
    }

    // dont care if these fail....
    // create default wallet info
    sDatabase.RunQuery(err, "INSERT INTO crpWalletDivisons (corporationID) VALUES (%u)", corpID);

    // create default bill auto-pay
    sDatabase.RunQuery(err, "INSERT INTO crpAutoPay (corporationID) VALUES (%u)", corpID);

    // create corp-owned shares
    sDatabase.RunQuery(err, "INSERT INTO crpShares (corporationID, shareholderID, shares, shareholderCorporationID)"
                            " VALUES (%u, %u, 1000, %u)", corpID, corpID, corpID);

    // It has to go into the eveStaticOwners too
    sDatabase.RunQuery(err, " INSERT INTO eveStaticOwners (ownerID,ownerName,typeID) VALUES (%u, '%s', 2)", corpID, cName.c_str());

    return true;
}

#define _NI(a, b) if (row.IsNull(b)) { cc.a = PyStatic.NewNone(); } else { cc.a = new PyInt(row.GetUInt(b)); }
#define _NS(a, b) if (row.IsNull(b)) { cc.a = PyStatic.NewNone(); } else { cc.a = new PyString(row.GetText(b)); }

bool CorporationDB::CreateCorporationChangePacket(OnCorporationChanged & cc, uint32 oldCorpID, uint32 newCorpID) {
    std::string table = "crpWalletDivisons";
    if (IsNPCCorp(newCorpID))
        table = "crpNPCWalletDivisons";
    DBQueryResult res;
    DBResultRow row;
    if (!sDatabase.RunQuery(res,
        " SELECT "
        "  c.corporationID, c.corporationName, c.description, c.tickerName, c.allianceID, c.warFactionID, c.url, c.taxRate,"
        "  c.minimumJoinStanding, c.corporationType, c.hasPlayerPersonnelManager, c.sendCharTerminationMessage, c.creatorID, c.ceoID, c.stationID, c.raceID, "
        "  c.shares, c.memberCount, c.memberLimit, c.allowedMemberRaceIDs, c.shape1, c.shape2, c.shape3, c.color1, c.color2, c.color3,"
        "  c.typeface, w.division1, w.division2, w.division3, w.division4, w.division5, w.division6, w.division7, w.walletDivision1, w.walletDivision2,"
        "  w.walletDivision3, w.walletDivision4, w.walletDivision5, w.walletDivision6, w.walletDivision7, c.deleted, c.isRecruiting "
        " FROM crpCorporation AS c"
        "  LEFT JOIN %s AS w USING (corporationID)"
        " WHERE corporationID = %u ",table.c_str(), newCorpID
        ))
    {
        codelog(CORP__DB_ERROR, "Error in retrieving new corporation's data (%u)", newCorpID);
        return false;
    }

    if (!res.GetRow(row)) {
        codelog(CORP__DB_WARNING, "Unable to find new corporation's data (%u)", newCorpID);
        return false;
    }

    cc.corporationIDNew = row.GetUInt(0);
    cc.corporationNameNew = row.GetText(1);
    cc.descriptionNew = row.GetText(2);
    cc.tickerNameNew = row.GetText(3);
    _NI(allianceIDNew, 4);
    _NI(warFactionIDNew, 5);
    cc.urlNew = row.GetText(6);
    cc.taxRateNew = row.GetDouble(7);
    cc.minimumJoinStandingNew = row.GetDouble(8);
    cc.corporationTypeNew = row.GetUInt(9);
    cc.hasPlayerPersonnelManagerNew = row.GetUInt(10);
    cc.sendCharTerminationMessageNew = row.GetUInt(11);
    cc.creatorIDNew = row.GetUInt(12);
    cc.ceoIDNew = row.GetUInt(13);
    cc.stationIDNew = row.GetUInt(14);
    _NI(raceIDNew, 15);
    cc.sharesNew = row.GetInt64(16);
    cc.memberCountNew = row.GetUInt(17);
    cc.memberLimitNew = row.GetUInt(18);
    cc.allowedMemberRaceIDsNew = row.GetUInt(19);
    _NI(shape1New, 20);
    _NI(shape2New, 21);
    _NI(shape3New, 22);
    _NI(color1New, 23);
    _NI(color2New, 24);
    _NI(color3New, 25);
    _NI(typefaceNew, 26);
    _NS(division1New, 27);
    _NS(division2New, 28);
    _NS(division3New, 29);
    _NS(division4New, 30);
    _NS(division5New, 31);
    _NS(division6New, 32);
    _NS(division7New, 33);
    _NS(walletDivision1New, 34);
    _NS(walletDivision2New, 35);
    _NS(walletDivision3New, 36);
    _NS(walletDivision4New, 37);
    _NS(walletDivision5New, 38);
    _NS(walletDivision6New, 39);
    _NS(walletDivision7New, 40);
    cc.deletedNew = row.GetUInt(41);
    cc.isRecruitingNew = row.GetUInt(42);

    if (IsPlayerCorp(oldCorpID))
        table = "crpWalletDivisons";
    if (IsNPCCorp(oldCorpID))
        table = "crpNPCWalletDivisons";

    if (!sDatabase.RunQuery(res,
        " SELECT "
        "  c.corporationID, c.corporationName, c.description, c.tickerName, c.allianceID, c.warFactionID, c.url, c.taxRate,"
        "  c.minimumJoinStanding, c.corporationType, c.hasPlayerPersonnelManager, c.sendCharTerminationMessage, c.creatorID, c.ceoID, c.stationID, c.raceID, "
        "  c.shares, c.memberCount, c.memberLimit, c.allowedMemberRaceIDs, c.shape1, c.shape2, c.shape3, c.color1, c.color2, c.color3,"
        "  c.typeface, w.division1, w.division2, w.division3, w.division4, w.division5, w.division6, w.division7, w.walletDivision1, w.walletDivision2,"
        "  w.walletDivision3, w.walletDivision4, w.walletDivision5, w.walletDivision6, w.walletDivision7, c.deleted, c.isRecruiting "
        " FROM crpCorporation AS c"
        "  LEFT JOIN %s AS w USING (corporationID)"
        " WHERE corporationID = %u ", table.c_str(), oldCorpID
        ))
    {
        codelog(CORP__DB_ERROR, "Error in retrieving old corporation's data (%u)", oldCorpID);
        return false;
    }

    if (!res.GetRow(row)) {
        codelog(CORP__DB_WARNING, "Unable to find old corporation's data (%u)", oldCorpID);
        return false;
    }

    cc.corporationIDOld = new PyInt(row.GetInt(0));
    cc.corporationNameOld = new PyString(row.GetText(1));
    cc.descriptionOld = new PyString(row.GetText(2));
    cc.tickerNameOld = new PyString(row.GetText(3));
    _NI(allianceIDOld, 4);
    _NI(warFactionIDOld, 5);
    cc.urlOld = new PyString(row.GetText(6));
    cc.taxRateOld = new PyFloat(row.GetDouble(7));
    cc.minimumJoinStandingOld = new PyFloat(row.GetDouble(8));
    cc.corporationTypeOld = new PyInt(row.GetInt(9));
    cc.hasPlayerPersonnelManagerOld = new PyInt(row.GetInt(10));
    cc.sendCharTerminationMessageOld = new PyInt(row.GetInt(11));
    cc.creatorIDOld = new PyInt(row.GetInt(12));
    cc.ceoIDOld = new PyInt(row.GetInt(13));
    cc.stationIDOld = new PyInt(row.GetInt(14));
    _NI(raceIDOld, 15);
    cc.sharesOld = new PyLong(row.GetInt64(16));
    cc.memberCountOld = new PyInt(row.GetInt(17));
    cc.memberLimitOld = new PyInt(row.GetInt(18));
    cc.allowedMemberRaceIDsOld = new PyInt(row.GetInt(19));
    _NI(shape1Old, 20);
    _NI(shape2Old, 21);
    _NI(shape3Old, 22);
    _NI(color1Old, 23);
    _NI(color2Old, 24);
    _NI(color3Old, 25);
    _NI(typefaceOld, 26);
    _NS(division1Old, 27);
    _NS(division2Old, 28);
    _NS(division3Old, 29);
    _NS(division4Old, 30);
    _NS(division5Old, 31);
    _NS(division6Old, 32);
    _NS(division7Old, 33);
    _NS(walletDivision1Old, 34);
    _NS(walletDivision2Old, 35);
    _NS(walletDivision3Old, 36);
    _NS(walletDivision4Old, 37);
    _NS(walletDivision5Old, 38);
    _NS(walletDivision6Old, 39);
    _NS(walletDivision7Old, 40);
    cc.deletedOld = new PyInt(row.GetUInt(41));
    cc.isRecruitingOld = new PyInt(row.GetUInt(42));

    return true;
}

bool CorporationDB::CreateCorporationCreatePacket(OnCorporationChanged & cc, uint32 oldCorpID, uint32 newCorpID) {
    DBQueryResult res;
    DBResultRow row;
    if (!sDatabase.RunQuery(res,
        " SELECT "
        "  c.corporationID, c.corporationName, c.description, c.tickerName, c.allianceID, c.warFactionID, c.url, c.taxRate,"
        "  c.minimumJoinStanding, c.corporationType, c.hasPlayerPersonnelManager, c.sendCharTerminationMessage, c.creatorID, c.ceoID, c.stationID, c.raceID, "
        "  c.shares, c.memberCount, c.memberLimit, c.allowedMemberRaceIDs, c.shape1, c.shape2, c.shape3, c.color1, c.color2,c. color3,"
        "  c.typeface, w.division1, w.division2, w.division3, w.division4, w.division5, w.division6, w.division7, w.walletDivision1, w.walletDivision2,"
        "  w.walletDivision3, w.walletDivision4, w.walletDivision5, w.walletDivision6, w.walletDivision7, c.deleted, c.isRecruiting "
        " FROM crpCorporation AS c"
        "  LEFT JOIN crpWalletDivisons AS w USING (corporationID)"
        " WHERE corporationID = %u ", newCorpID
        ))
    {
        codelog(CORP__DB_ERROR, "Error in retrieving new corporation's data (%u)", newCorpID);
        return false;
    }

    if (!res.GetRow(row)) {
        codelog(CORP__DB_WARNING, "Unable to find corporation's data (%u)", newCorpID);
        return false;
    }

    cc.corporationIDNew = row.GetUInt(0);
    cc.corporationNameNew = row.GetText(1);
    cc.descriptionNew = row.GetText(2);
    cc.tickerNameNew = row.GetText(3);
    _NI(allianceIDNew, 4);
    _NI(warFactionIDNew, 5);
    cc.urlNew = row.GetText(6);
    cc.taxRateNew = row.GetDouble(7);
    cc.minimumJoinStandingNew = row.GetDouble(8);
    cc.corporationTypeNew = row.GetUInt(9);
    cc.hasPlayerPersonnelManagerNew = row.GetUInt(10);
    cc.sendCharTerminationMessageNew = row.GetUInt(11);
    cc.creatorIDNew = row.GetUInt(12);
    cc.ceoIDNew = row.GetUInt(13);
    cc.stationIDNew = row.GetUInt(14);
    _NI(raceIDNew, 15);
    cc.sharesNew = row.GetInt64(16);
    cc.memberCountNew = row.GetUInt(17);
    cc.memberLimitNew = row.GetUInt(18);
    cc.allowedMemberRaceIDsNew = row.GetUInt(19);
    _NI(shape1New, 20);
    _NI(shape2New, 21);
    _NI(shape3New, 22);
    _NI(color1New, 23);
    _NI(color2New, 24);
    _NI(color3New, 25);
    _NI(typefaceNew, 26);
    _NS(division1New, 27);
    _NS(division2New, 28);
    _NS(division3New, 29);
    _NS(division4New, 30);
    _NS(division5New, 31);
    _NS(division6New, 32);
    _NS(division7New, 33);
    _NS(walletDivision1New, 34);
    _NS(walletDivision2New, 35);
    _NS(walletDivision3New, 36);
    _NS(walletDivision4New, 37);
    _NS(walletDivision5New, 38);
    _NS(walletDivision6New, 39);
    _NS(walletDivision7New, 40);
    cc.deletedNew = row.GetUInt(41);
    cc.isRecruitingNew = row.GetUInt(42);

    /** @todo these need to be updated */
    cc.allianceIDOld = PyStatic.NewNone();
    cc.warFactionIDOld = PyStatic.NewNone();
    cc.allowedMemberRaceIDsOld = PyStatic.NewNone();
    cc.ceoIDOld = PyStatic.NewNone();
    cc.color1Old = PyStatic.NewNone();
    cc.color2Old = PyStatic.NewNone();
    cc.color3Old = PyStatic.NewNone();
    cc.corporationIDOld = PyStatic.NewNone();
    cc.corporationNameOld = PyStatic.NewNone();
    cc.corporationTypeOld = PyStatic.NewNone();
    cc.creatorIDOld = PyStatic.NewNone();
    cc.deletedOld = PyStatic.NewNone();
    cc.descriptionOld = PyStatic.NewNone();
    cc.division1Old = PyStatic.NewNone();
    cc.division2Old = PyStatic.NewNone();
    cc.division3Old = PyStatic.NewNone();
    cc.division4Old = PyStatic.NewNone();
    cc.division5Old = PyStatic.NewNone();
    cc.division6Old = PyStatic.NewNone();
    cc.division7Old = PyStatic.NewNone();
    cc.walletDivision1Old = PyStatic.NewNone();
    cc.walletDivision2Old = PyStatic.NewNone();
    cc.walletDivision3Old = PyStatic.NewNone();
    cc.walletDivision4Old = PyStatic.NewNone();
    cc.walletDivision5Old = PyStatic.NewNone();
    cc.walletDivision6Old = PyStatic.NewNone();
    cc.walletDivision7Old = PyStatic.NewNone();
    cc.hasPlayerPersonnelManagerOld = PyStatic.NewNone();
    cc.memberCountOld = PyStatic.NewNone();
    cc.memberLimitOld = PyStatic.NewNone();
    cc.minimumJoinStandingOld = PyStatic.NewNone();
    cc.raceIDOld = PyStatic.NewNone();
    cc.sendCharTerminationMessageOld = PyStatic.NewNone();
    cc.shape1Old = PyStatic.NewNone();
    cc.shape2Old = PyStatic.NewNone();
    cc.shape3Old = PyStatic.NewNone();
    cc.sharesOld = PyStatic.NewNone();
    cc.stationIDOld = PyStatic.NewNone();
    cc.taxRateOld = PyStatic.NewNone();
    cc.tickerNameOld = PyStatic.NewNone();
    cc.typefaceOld = PyStatic.NewNone();
    cc.urlOld = PyStatic.NewNone();
    cc.isRecruitingOld = PyStatic.NewNone();

    return true;
}

PyList* CorporationDB::GetMemberRows(uint32 corpID, const std::vector<uint32>* ids, uint32 offset, uint32 limit)
{
    // the client's member row (corp.members:  util.KeyVal per member, edited in place by OnCorporationMemberChanged)
    std::ostringstream query;
    query << "SELECT characterID, corporationID, title, corpRole | rolesAtAll, grantableRoles, startDateTime, baseID,"
             " rolesAtHQ, grantableRolesAtHQ, rolesAtBase, grantableRolesAtBase, rolesAtOther, grantableRolesAtOther,"
             " titleMask, corpAccountKey, blockRoles + 0"
             " FROM chrCharacters WHERE corporationID = " << corpID;
    if (ids != nullptr) {
        if (ids->empty())
            return new PyList();
        query << " AND characterID IN (";
        bool first = true;
        for (auto cur : *ids) {
            if (!first)
                query << ",";
            query << cur;
            first = false;
        }
        query << ")";
    }
    query << " ORDER BY characterID";
    if (limit > 0)
        query << " LIMIT " << offset << "," << limit;

    DBQueryResult res;
    if (!sDatabase.RunQuery(res, query.str().c_str())) {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return new PyList();
    }

    PyList* list = new PyList();
    DBResultRow row;
    while (res.GetRow(row)) {
        PyDict* dict = new PyDict();
            dict->SetItemString("characterID",           new PyInt(row.GetUInt(0)));
            dict->SetItemString("corporationID",         new PyInt(row.GetUInt(1)));
            dict->SetItemString("divisionID",            new PyInt(0));
            dict->SetItemString("squadronID",            new PyInt(0));
            dict->SetItemString("title",                 new PyWString(std::string(row.GetText(2))));
            dict->SetItemString("roles",                 new PyLong(row.GetInt64(3)));
            dict->SetItemString("grantableRoles",        new PyLong(row.GetInt64(4)));
            dict->SetItemString("startDateTime",         new PyLong(row.GetInt64(5)));
            dict->SetItemString("baseID",                (row.GetInt(6) > 0 ? (PyRep*)new PyInt(row.GetInt(6)) : PyStatic.NewNone()));
            dict->SetItemString("rolesAtHQ",             new PyLong(row.GetInt64(7)));
            dict->SetItemString("grantableRolesAtHQ",    new PyLong(row.GetInt64(8)));
            dict->SetItemString("rolesAtBase",           new PyLong(row.GetInt64(9)));
            dict->SetItemString("grantableRolesAtBase",  new PyLong(row.GetInt64(10)));
            dict->SetItemString("rolesAtOther",          new PyLong(row.GetInt64(11)));
            dict->SetItemString("grantableRolesAtOther", new PyLong(row.GetInt64(12)));
            dict->SetItemString("titleMask",             new PyLong(row.GetInt64(13)));
            dict->SetItemString("accountKey",            new PyInt(row.GetInt(14)));
            dict->SetItemString("rowDate",               new PyLong(row.GetInt64(5)));
            dict->SetItemString("blockRoles",            new PyInt(row.GetInt(15)));
        list->AddItem(new PyObject("util.KeyVal", dict));
    }

    return list;
}

std::string CorporationDB::GetMemberTitle(uint32 charID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT title FROM chrCharacters WHERE characterID = %u", charID)) {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return "";
    }

    DBResultRow row;
    if (res.GetRow(row))
        return row.GetText(0);
    return "";
}

void CorporationDB::GetTitleRoles(uint32 corpID, std::map<int64, Corp::TitleRoles>& into)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT titleID, roles, grantableRoles, rolesAtHQ, grantableRolesAtHQ, rolesAtBase, grantableRolesAtBase, rolesAtOther, grantableRolesAtOther"
        " FROM crpRoleTitles WHERE corporationID = %u", corpID))
    {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return;
    }

    DBResultRow row;
    while (res.GetRow(row)) {
        Corp::TitleRoles roles = Corp::TitleRoles();
            roles.roles                 = row.GetInt64(1);
            roles.grantableRoles        = row.GetInt64(2);
            roles.rolesAtHQ             = row.GetInt64(3);
            roles.grantableRolesAtHQ    = row.GetInt64(4);
            roles.rolesAtBase           = row.GetInt64(5);
            roles.grantableRolesAtBase  = (row.IsNull(6) ? 0 : row.GetInt64(6));
            roles.rolesAtOther          = row.GetInt64(7);
            roles.grantableRolesAtOther = row.GetInt64(8);
        into[row.GetInt64(0)] = roles;
    }
}

void CorporationDB::ClearTitle(uint32 corpID, int64 titleID)
{
    DBerror err;
    sDatabase.RunQuery(err, "UPDATE chrCharacters SET titleMask = titleMask & ~%lli WHERE corporationID = %u", titleID, corpID);
}

bool CorporationDB::HasOpenCEOVote(uint32 corpID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT voteCaseID FROM crpVoteItems WHERE corporationID = %u AND voteType = %u AND status = 2 LIMIT 1",
                corpID, Corp::VoteType::CEO))
    {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return false;
    }
    return (res.GetRowCount() > 0);
}

void CorporationDB::GetMembers(uint32 corpID, DBQueryResult& res)
{
    if (!sDatabase.RunQuery(res,
        " SELECT "
        "  characterID, "
        "  corporationID,"
        "  corpAccountKey AS divisionID,"
        //"  0 AS squadronID,"
        "  title, "
        "  rolesAtAll, "
        "  grantableRoles, "
        "  startDateTime, "
        "  baseID,"
        "  rolesAtHQ, "
        "  grantableRolesAtHQ, "
        "  rolesAtBase, "
        "  grantableRolesAtBase,"
        "  rolesAtOther, "
        "  grantableRolesAtOther, "
        "  titleMask,"
        "  corpAccountKey,"
        //"  %f AS rowDate,"
        "  blockRoles,"
        "  characterName"
        " FROM chrCharacters"
        " WHERE corporationID = %u", corpID))
    {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
    }
}

void CorporationDB::GetMembersForQuery(std::ostringstream& query, std::vector< uint32 >& result)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, query.str().c_str())) {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return;
    }

    _log(DATABASE__RESULTS, "CorporationDB::GetMembersForQuery '%s' returned %lu items", query.str().c_str(), res.GetRowCount());

    DBResultRow row;
    while (res.GetRow(row))
        result.push_back(row.GetInt(0));
}

uint16 CorporationDB::GetMemberCount(uint32 corpID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT COUNT(characterID) FROM chrCharacters WHERE corporationID = %u", corpID )) {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return 0;
    }

    DBResultRow row;
    if (res.GetRow(row))
        return (row.GetInt(0));

    return 0;
}

void CorporationDB::GetMemberIDs(uint32 corpID, std::vector< uint32 >& ids, bool online/*true*/)
{
    ids.clear();
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT characterID FROM chrCharacters WHERE corporationID = %u AND online = %u", corpID, (online?1:0) )) {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return;
    }

    DBResultRow row;
    while (res.GetRow(row))
        ids.push_back(row.GetInt(0));
}

PyRep* CorporationDB::GetCorpRoles()
{
    DBQueryResult res;
    if (!sDatabase.RunQuery( res, "SELECT roleID, roleName, shortDescriptionID, descriptionID, roleIID FROM crpRoles")) {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return nullptr;
    }

    return DBResultToCRowset(res);
}

PyRep* CorporationDB::GetCorpRoleGroups()
{
    DBQueryResult res;
    if (!sDatabase.RunQuery( res,
        "SELECT roleGroupID, roleGroupName, roleGroupNameID, roleMask, appliesTo, appliesToGrantable, isLocational, isDivisional"
        " FROM crpRoleGroups"))
    {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return nullptr;
    }

    return DBResultToCRowset(res);
}

void CorporationDB::DeleteTitle(uint32 corpID, uint16 titleID)
{
    DBerror err;
    sDatabase.RunQuery(err, "DELETE FROM crpRoleTitles WHERE corporationID = %u AND titleID = %u", corpID, titleID);
    std::string title = "Untitled ";
    title += std::to_string(log2(titleID) + 1);
    sDatabase.RunQuery(err, "INSERT INTO crpRoleTitles (corporationID, titleID, titleName) VALUES (%u,%u,'%s')", corpID, titleID, title.c_str());
}

bool CorporationDB::UpdateTitle(uint32 corpID, Call_UpdateTitleData& args, PyDict* updates)
{
    if (IsNPCCorp(corpID))
        return false;

    DBQueryResult res;
    if (!sDatabase.RunQuery( res,
        "SELECT titleName, roles, grantableRoles, rolesAtHQ, grantableRolesAtHQ, rolesAtBase, grantableRolesAtBase, rolesAtOther, grantableRolesAtOther "
        " FROM crpRoleTitles WHERE corporationID = %u AND titleID = %u", corpID, args.titleID))
    {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return false;
    }

    DBResultRow row;
    if (!res.GetRow(row)) {
        _log(CORP__DB_WARNING, "No title data for titleID %u in corpID %u.", args.titleID, corpID);
        return false;
    }

    std::vector<std::string> dbQ;
    ProcessStringChange("titleName",            row.GetText(0), args.titleName,             updates,   dbQ);
    ProcessLongChange("roles",                 row.GetInt64(1), args.roles,                 updates,   dbQ);
    ProcessLongChange("grantableRoles",        row.GetInt64(2), args.grantableRoles,        updates,   dbQ);
    ProcessLongChange("rolesAtHQ",             row.GetInt64(3), args.rolesAtHQ,             updates,   dbQ);
    ProcessLongChange("grantableRolesAtHQ",    row.GetInt64(4), args.grantableRolesAtHQ,    updates,   dbQ);
    ProcessLongChange("rolesAtBase",           row.GetInt64(5), args.rolesAtBase,           updates,   dbQ);
    ProcessLongChange("grantableRolesAtBase",  row.GetInt64(6), args.grantableRolesAtBase,  updates,   dbQ);
    ProcessLongChange("rolesAtOther",          row.GetInt64(7), args.rolesAtOther,          updates,   dbQ);
    ProcessLongChange("grantableRolesAtOther", row.GetInt64(8), args.grantableRolesAtOther, updates,   dbQ);

    std::string query = "UPDATE crpRoleTitles SET ";

    int N = dbQ.size();
    for (int i = 0; i < N; ++i) {
        query += dbQ[i];
        if (i < N - 1)
            query += ", ";
    }

    query += " WHERE corporationID = ";
    query += std::to_string(corpID);
    query += " AND titleID = ";
    query += std::to_string(args.titleID);

    _log(CORP__DB_MESSAGE, "DB Query: %s", query.c_str());

    DBerror err;
    // We are here, so something must have changed...
    if (N < 1)
    return false;

    if (!sDatabase.RunQuery(err, query.c_str())) {
        codelog(CORP__DB_ERROR, "Error in query: %s", err.c_str());
        return false;
    }
    return true;
}

PyRep* CorporationDB::GetTitles(uint32 corpID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery( res,
        "SELECT corporationID, titleID, titleName, roles, grantableRoles, rolesAtHQ, grantableRolesAtHQ, rolesAtBase, grantableRolesAtBase, rolesAtOther, grantableRolesAtOther "
        " FROM crpRoleTitles WHERE corporationID = %u", corpID)) {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return nullptr;
    }
    PyDict* dict = new PyDict();
    DBResultRow row;
    while (res.GetRow(row)) {
        PyDict* dict1 = new PyDict();
            dict1->SetItemString("corporationID", new PyInt(row.GetInt(0)));
            dict1->SetItemString("titleID", new PyInt(row.GetInt(1)));
            dict1->SetItemString("titleName", new PyString(row.GetText(2)));
            dict1->SetItemString("roles", new PyLong(row.GetInt64(3)));
            dict1->SetItemString("grantableRoles", new PyLong(row.GetInt64(4)));
            dict1->SetItemString("rolesAtHQ", new PyLong(row.GetInt64(5)));
            dict1->SetItemString("grantableRolesAtHQ", new PyLong(row.GetInt64(6)));
            dict1->SetItemString("rolesAtBase", new PyLong(row.GetInt64(7)));
            dict1->SetItemString("grantableRolesAtBase", new PyLong(row.GetInt64(8)));
            dict1->SetItemString("rolesAtOther", new PyLong(row.GetInt64(9)));
            dict1->SetItemString("grantableRolesAtOther", new PyLong(row.GetInt64(10)));
        dict->SetItem(new PyInt(row.GetInt(1)), new PyObject("util.KeyVal", dict1));
    }

    return dict;
}

void CorporationDB::CreateTitleData(uint32 corpID)
{
    DBerror err;
    sDatabase.RunQuery(err,
        "INSERT INTO crpRoleTitles (corporationID, titleID, titleName) VALUES"
        "(%u,1,'Untitled 1'),(%u,2,'Untitled 2'),(%u,4,'Untitled 3'),(%u,8,'Untitled 4'),(%u,16,'Untitled 5'),(%u,32,'Untitled 6'),"
        "(%u,64,'Untitled 7'),(%u,128,'Untitled 8'),(%u,256,'Untitled 9'),(%u,512,'Untitled 10'),(%u,1024,'Untitled 11'),(%u,2048,'Untitled 12'),"
        "(%u,4096,'Untitled 13'),(%u,8192,'Untitled 14'),(%u,16384,'Untitled 15'),(%u,32768,'Untitled 16')",
        corpID,corpID,corpID,corpID,corpID,corpID,corpID,corpID,corpID,corpID,corpID,corpID,corpID,corpID,corpID,corpID);
}

/* * UPDATE crpRoleTitles SET corporationID=[value-1],titleID=[value-2],titleName=[value-3],roles=[value-4],grantableRoles=[value-5],rolesAtHQ=[value-6],grantableRolesAtHQ=[value-7],rolesAtBase=[value-8],grantableRolesAtBase=[value-9],rolesAtOther=[value-10],grantableRolesAtOther=[value-11] WHERE corporationID
 */

PyRep* CorporationDB::GetContacts(uint32 corpID)
{
    _log(SOV__DEBUG, "CorporationDB::GetContacts() called...");

    DBQueryResult res;
    if (!sDatabase.RunQuery( res,
        "SELECT contactID, inWatchlist, relationshipID, labelMask"
        " FROM crpContacts WHERE ownerID = %u", corpID))
    {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return nullptr;
    }

    PyObjectEx* obj = DBResultToCIndexedRowset(res, "contactID");
    if (is_log_enabled(CORP__RSP_DUMP))
        obj->Dump(CORP__RSP_DUMP, "");

    return obj;
}

void CorporationDB::AddContact(uint32 ownerID, int32 contactID, float relationshipID)
{
    DBerror err;
    sDatabase.RunQuery(err,
        "INSERT INTO crpContacts (ownerID, contactID, relationshipID, inWatchlist, labelMask)"
        " VALUES (%u, %i, %f, 0, 0)"
        " ON DUPLICATE KEY UPDATE relationshipID = %f",
        ownerID, contactID, relationshipID, relationshipID);
}

void CorporationDB::UpdateContact(float relationshipID, uint32 contactID, uint32 ownerID)
{
    DBerror err;
    sDatabase.RunQuery(err,
        "UPDATE crpContacts SET relationshipID=%f "
        " WHERE contactID=%u AND ownerID=%u ",
         relationshipID, contactID, ownerID);
}

void CorporationDB::GetContactLabelMasks(uint32 ownerID, const std::vector<int32>& contactIDs, std::map<int32, int64>& into)
{
    if (contactIDs.empty())
        return;
    std::ostringstream ids;
    for (auto cur : contactIDs)
        ids << "," << cur;

    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT contactID, labelMask FROM crpContacts WHERE ownerID = %u AND contactID IN (0%s)",
                ownerID, ids.str().c_str()))
    {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return;
    }

    DBResultRow row;
    while (res.GetRow(row))
        into[row.GetInt(0)] = row.GetInt64(1);
}

void CorporationDB::RemoveContact(uint32 contactID, uint32 ownerID)
{
    DBerror err;
    sDatabase.RunQuery(err,
        "DELETE from crpContacts "
        " WHERE contactID=%u AND ownerID=%u ",
         contactID, ownerID);
}

// should this be cached?     ...yes
PyObject *CorporationDB::GetEveOwners(uint32 corpID) {
    DBQueryResult res;
/*
            [PyPackedRow 9 bytes]
              ["ownerID" => <90046865> [I4]]
              ["ownerName" => <Septure> [WStr]]
              ["typeID" => <1375> [I4]]
              ["gender" => <1> [Bool]]
              */

    if (!sDatabase.RunQuery( res,
        "SELECT"
        " characterID AS ownerID,"
        " characterName AS ownerName,"
        " typeID,"
        " gender"
        " FROM chrCharacters"
        " WHERE corporationID = %u", corpID
        ))
    {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return nullptr;
    }

    return DBResultToRowset(res);
}

PyObject *CorporationDB::GetStations(uint32 corpID) {
    DBQueryResult res;

    if (!sDatabase.RunQuery(res,
        " SELECT "
        " stationID, stationTypeID as typeID "
        " FROM staStations "
        " WHERE corporationID = %u ", corpID
        ))
    {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return nullptr;
    }
    return DBResultToRowset(res);
}


bool CorporationDB::FetchOfficesKeys (uint32 corporationID, DBQueryResult& res) {
    return sDatabase.RunQuery (res, "SELECT itemID FROM staOffices WHERE corporationID = %u AND impounded = 0", corporationID);
}

bool CorporationDB::FetchOffices (uint32 corpID, uint32 from, uint32 count, DBQueryResult& res) {
    return sDatabase.RunQuery(res,
        "SELECT itemID, stationID, stationTypeID, officeFolderID, typeID"
        " FROM staOffices "
        " WHERE corporationID = %u AND impounded = 0"
        " LIMIT %u, %u ",
        corpID, from, count
        );
}

//NOTE: it makes sense to push this up to ServiceDB, since others will likely need this too.
uint32 CorporationDB::GetStationOwner(uint32 stationID) {
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        " SELECT corporationID "
        " FROM staStations "
        " WHERE stationID = %u ", stationID))
    {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return 0;
    }

    DBResultRow row;
    if (!res.GetRow(row)) {
        codelog(CORP__DB_WARNING, "Missing stationID: %u", stationID);
        return 0;
    }
    return row.GetUInt(0);
}

void CorporationDB::DeleteBulletin(uint32 bulletinID)
{
    DBerror err;
    sDatabase.RunQuery(err, "DELETE FROM crpBulletins WHERE bulletinID = %u", bulletinID);
}

void CorporationDB::AddBulletin(uint32 corpID, uint32 ownerID, uint32 cCharID, std::string& title, std::string& body)
{
    DBerror err;
    sDatabase.RunQuery(err,
        "INSERT INTO crpBulletins (corporationID, ownerID, createCharacterID, createDateTime, editCharacterID, editDateTime, title, body)"
        " VALUES (%u, %u, %u, %f, %u, %f, '%s', '%s')", corpID, ownerID, cCharID, GetFileTimeNow(), cCharID, GetFileTimeNow(), title.c_str(), body.c_str());
}

void CorporationDB::EditBulletin(uint32 bulletinID, uint32 eCharID, int64 eDataTime, std::string& title, std::string& body)
{
    DBerror err;
    sDatabase.RunQuery(err,
        "UPDATE crpBulletins SET editCharacterID = %u, editDateTime = %lli, title = '%s', body = '%s'"
        " WHERE bulletinID = %u", eCharID, eDataTime, title.c_str(), body.c_str(), bulletinID);
}

PyRep* CorporationDB::GetBulletins(uint32 corpID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT corporationID, bulletinID, ownerID, createCharacterID, createDateTime, editCharacterID, editDateTime, title, body"
        " FROM crpBulletins"
        " WHERE corporationID = %u ", corpID))
    {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return nullptr;
    }
    return DBResultToCRowset(res);
}

void CorporationDB::AddRecruiters(uint16 adID, int32 corpID, std::vector< int32 >& charVec)
{
    std::ostringstream str;
    str << "INSERT INTO crpRecruiters (adID, corpID, charID) VALUES ";

    bool first = true;
    for (auto cur : charVec) {
        if (!IsCharacterID(cur))
            continue;
        if (first) {
            first = false;
        } else {
            str << ",";
        }
        str << "(" << adID << "," << corpID << "," << cur << ")";
    }

    if (!first ) {
        DBerror err;
        sDatabase.RunQuery(err, "DELETE FROM crpRecruiters WHERE corpID = %u AND adID = %u", corpID, adID);
        sDatabase.RunQuery(err, str.str().c_str());
    }
}

PyRep* CorporationDB::GetRecruiters(int32 corpID, uint16 adID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT charID FROM crpRecruiters WHERE corpID = %u AND adID = %u", corpID, adID)) {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return nullptr;
    }

    PyTuple* tuple = new PyTuple(res.GetRowCount());
    int8 i = 0;
    DBResultRow row;
    while (res.GetRow(row))
        tuple->SetItem(i++, new PyInt(row.GetInt(0)));

    return tuple;
}

PyRep* CorporationDB::GetAdTypeData()
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT typeMask, typeName, typeNameID, groupID, description, descriptionID FROM crpAdTypeData")) {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return nullptr;
    }
    return DBResultToCRowset(res);
}

PyRep* CorporationDB::GetAdGroupData()
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT groupID, groupName, groupNameID, description, descriptionID FROM crpAdGroupData")) {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return nullptr;
    }
    return DBResultToCRowset(res);
}

PyRep* CorporationDB::GetAdRegistryData(int64 typeMask/*0*/, bool inAlliance/*false*/, int16 minMembers/*0*/, uint16 maxMembers/*12602*/)
{
    // make sure this isnt 0
    if (maxMembers < 1)
        maxMembers = 12602;

    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT"
        "  adID, corporationID, allianceID, stationID, regionID, raceMask, typeMask,"
        "  createDateTime, expiryDateTime, title, description, memberCount, channelID"
        " FROM crpAdRegistry"
        "  WHERE allianceID %s 0"
        "   AND (memberCount >= %u AND memberCount < %u)",
        (inAlliance ? ">" : "="), minMembers, maxMembers))
    {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return nullptr;
    }

    return DBResultToCRowset(res);
}

PyRep* CorporationDB::GetAdvert(uint16 adID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT"
        "  adID, corporationID, allianceID, stationID, regionID, raceMask, typeMask,"
        "  createDateTime, expiryDateTime, title, description, memberCount, channelID"
        " FROM crpAdRegistry"
        "  WHERE adID = %u", adID))
    {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return nullptr;
    }

    return DBResultToCRowset(res);
}

void CorporationDB::DeleteAdvert(uint16 adID)
{
    DBerror err;
    sDatabase.RunQuery(err, "DELETE FROM crpAdRegistry WHERE adID=%u ", adID);
}

uint32 CorporationDB::CreateAdvert(Client* pClient, uint32 corpID, int64 typeMask, int8 days, uint16 members, std::string description, uint32 channelID, std::string title)
{
    uint32 adID(0);
    DBerror err;
    sDatabase.RunQueryLID(err, adID, "INSERT INTO crpAdRegistry"
    " (corporationID, allianceID, stationID, regionID, raceMask, typeMask,"
    "  createDateTime, expiryDateTime, description, title, memberCount, channelID)"
    " VALUES (%u,%u,%u,%u,%u,%lli,%f,%f,'%s','%s',%u,%u)",
        corpID, pClient->GetAllianceID(), pClient->GetStationID(), pClient->GetRegionID(), 15, typeMask, // raceMask isnt implemented yet
        GetFileTimeNow(), GetFileTimeNow() + (EvE::Time::Day * days), description.c_str(), title.c_str(), members, channelID);

    return adID;
}

void CorporationDB::UpdateAdvert(uint16 adID, uint32 corpID, int64 typeMask, int8 days, uint16 members, std::string description, uint32 channelID, std::string title)
{
    int64 time = GetFileTimeNow() + (EvE::Time::Day * days);
    DBerror err;
    sDatabase.RunQuery(err, "UPDATE crpAdRegistry"
        " SET typeMask=%lli, expiryDateTime=%lli, description='%s', title='%s', memberCount=%u, channelID=%u"
        " WHERE adID=%u ",
        typeMask, time, description.c_str(), title.c_str(), members, channelID, adID);
}

int64 CorporationDB::GetAdvertTime(uint16 adID, uint32 corpID)
{
    DBQueryResult res;
    sDatabase.RunQuery(res, "SELECT expiryDateTime FROM crpAdRegistry WHERE adID=%u AND corporationID=%u", adID, corpID);
    DBResultRow row;
    if (res.GetRow(row))
        return row.GetInt64(0);

    return 0;
}

PyRep *CorporationDB::GetMyApplications(uint32 charID) {
    //    header = [applicationID, corporationID, characterID, applicationText, roles, grantableRoles, status, applicationDateTime, deleted, lastCorpUpdaterID]
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        " SELECT applicationID, corporationID, characterID, applicationText, roles, grantableRoles,"
        " status, applicationDateTime, deleted, lastCorpUpdaterID"
        " FROM crpApplications"
        " WHERE characterID = %u ", charID))
    {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return nullptr;
    }

    PyObjectEx* obj = DBResultToCRowset(res);
    if (is_log_enabled(CORP__RSP_DUMP))
        obj->Dump(CORP__RSP_DUMP, "");

    return obj;
}

PyRep *CorporationDB::GetApplications(uint32 corpID) {
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        " SELECT"
        " applicationID, characterID, corporationID, applicationText, status,"
        " applicationDateTime, lastCorpUpdaterID"
        " FROM crpApplications"
        " WHERE corporationID = %u AND deleted = 0",
        corpID))
    {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return nullptr;
    }
    PyObjectEx* obj = DBResultToCIndexedRowset(res, "characterID");
    if (is_log_enabled(CORP__RSP_DUMP))
        obj->Dump(CORP__RSP_DUMP, "");

    return obj;
}

bool CorporationDB::GetCurrentApplicationInfo(Corp::ApplicationInfo& aInfo) {
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        " SELECT"
        " applicationID, applicationText, roles, grantableRoles, status,"
        " applicationDateTime, lastCorpUpdaterID, deleted"
        " FROM crpApplications"
        " WHERE characterID = %u AND corporationID = %u AND applicationText = '%s'",
        aInfo.charID, aInfo.corpID, aInfo.appText.c_str()))
    {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        aInfo.valid = false;
        return false;
    }

    DBResultRow row;
    if (!res.GetRow(row)) {
        codelog(CORP__DB_WARNING, "There's no application matching the sent data.");
        aInfo.valid = false;
        return false;
    }

    aInfo.appID = row.GetInt(0);
    aInfo.appText = row.GetText(1);
    aInfo.role = row.GetInt64(2);
    aInfo.grantRole = row.GetInt64(3);
    aInfo.status = row.GetInt(4);
    aInfo.appTime = row.GetInt64(5);
    aInfo.lastCID = row.GetInt(6);
    aInfo.deleted = row.GetInt(7);
    aInfo.valid = true;
    return true;
}

bool CorporationDB::InsertApplication(Corp::ApplicationInfo& aInfo) {
    if (!aInfo.valid) {
        _log(CORP__DB_WARNING, "InsertApplication(): aInfo contains invalid data");
        return false;
    }

    std::string escaped;
    sDatabase.DoEscapeString(escaped, aInfo.appText);
    DBerror err;
    if (!sDatabase.RunQueryLID(err, aInfo.appID,
        " INSERT INTO crpApplications"
        " (corporationID, characterID, applicationText, applicationDateTime)"
        " VALUES (%u, %u, '%s', %lli)",
        aInfo.corpID, aInfo.charID, escaped.c_str(), aInfo.appTime))
    {
        codelog(CORP__DB_ERROR, "Error in query: %s", err.c_str());
        return false;
    }

    return true;
}

bool CorporationDB::UpdateApplication(const Corp::ApplicationInfo& aInfo) {
    if (!aInfo.valid) {
        _log(CORP__DB_WARNING, "UpdateApplication(): info contains invalid data");
        return false;
    }

    DBerror err;
    std::string escaped;
    sDatabase.DoEscapeString(escaped, aInfo.appText);
    if (!sDatabase.RunQuery(err,
        " UPDATE crpApplications"
        " SET status = %u, lastCorpUpdaterID = %u, applicationText = '%s'"
        " WHERE applicationID = %u", aInfo.status, aInfo.lastCID, escaped.c_str(), aInfo.appID))
    {
        codelog(CORP__DB_ERROR, "Error in query: %s", err.c_str());
        return false;
    }
    return true;
}

bool CorporationDB::DeleteApplication(const Corp::ApplicationInfo& aInfo) {
    DBerror err;
    if (!sDatabase.RunQuery(err,
        " UPDATE crpApplications"
        " SET status = 3, deleted = 1"
        " WHERE applicationID = %u", aInfo.appID))
    {
        codelog(CORP__DB_ERROR, "Error in query: %s", err.c_str());
        return false;
    }
    return true;
}

uint32 CorporationDB::GetStationCorporationCEO(uint32 stationID) {
    if (!sDataMgr.IsStation(stationID))
        return 0;
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        " SELECT c.ceoID "
        " FROM crpCorporation AS c"
        " LEFT JOIN staStations USING (corporationID) "
        " WHERE staStations.stationID = %u ", stationID))
    {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return 0;
    }
    DBResultRow row;
    if (!res.GetRow(row)) {
        codelog(CORP__DB_WARNING, "There's either no such station or the station has no corp owner or the corporation has no ceo.");
        return 0;
    }
    return row.GetUInt(0);
}

uint32 CorporationDB::GetCorporationCEO(uint32 corpID) {
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        " SELECT ceoID "
        " FROM crpCorporation "
        " WHERE corporationID = %u ", corpID))
    {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return 0;
    }
    DBResultRow row;
    if (!res.GetRow(row)) {
        codelog(CORP__DB_WARNING, "There's either no such corp owner or the corporation has no ceo.");
        return 0;
    }
    return row.GetUInt(0);
}

uint16 CorporationDB::GetCorpMemberCount(uint32 corpID) {
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        " SELECT memberCount"
        " FROM crpCorporation"
        " WHERE corporationID = %u ", corpID))
    {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return 0;
    }
    DBResultRow row;
    if (!res.GetRow(row)) {
        codelog(CORP__DB_WARNING, "Cannot find memberCount for corpID %u", corpID);
        return 0;
    }
    return row.GetInt(0);
}

uint16 CorporationDB::GetCorpMemberLimit(uint32 corpID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        " SELECT memberLimit"
        " FROM crpCorporation"
        " WHERE corporationID = %u ", corpID))
    {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return 0;
    }
    DBResultRow row;
    if (!res.GetRow(row)) {
        codelog(CORP__DB_WARNING, "Cannot find memberLimit for corpID %u", corpID);
        return 0;
    }
    return row.GetInt(0);
}

double CorporationDB::GetCloneTypeCostByID(uint32 cloneTypeID) {
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        " SELECT basePrice "
        " FROM invTypes "
        " WHERE typeID = %u ", cloneTypeID))
    {
        codelog(CORP__DB_ERROR, "Failed to retrieve basePrice of typeID = %u",cloneTypeID);
    }
    DBResultRow row;
    if (!res.GetRow(row)) {
        codelog(CORP__DB_WARNING, "GetCloneTypeCostByID returned no results");
        return 0;
    }
    return row.GetDouble(0);
}

bool CorporationDB::CreateMemberAttributeUpdate(uint32 newCorpID, uint32 charID, MemberAttributeUpdate& attrib) {
    // What are we doing here exactly?
    // Corporation gets a new member
    // it's new to it

    DBQueryResult res;
    DBResultRow row;
    if (!sDatabase.RunQuery(res,
        " SELECT "
        "   title, startDateTime, corporationID, "
        "   corpRole, rolesAtAll, rolesAtBase, "
        "   rolesAtHQ, rolesAtOther, titleMask, baseID "
        " FROM chrCharacters "
        " WHERE characterID = %u ", charID))
    {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return false;
    }

    if (!res.GetRow(row)) {
        codelog(CORP__DB_WARNING, "Cannot find character in database");
        return false;
    }

    // this could be stored in the db
#define PRN PyStatic.NewNone()
#define PRI(i) new PyInt(i)
#define PRL(i) new PyLong(i)
#define PRS(s) new PyString(s)
#define PRNI(i) (row.IsNull(i) ? PRU(0) : PRU(row.GetInt64(i)))
#define F(name, o, n) \
    attrib.name##Old = o; \
    attrib.name##New = n

    //element                   Old Value               New Value
    F(accountKey,               PRN,                    PRN);
    F(baseID,                   PRI(row.GetInt(9)),     PRI(0));
    F(characterID,              PRN,                    PRI(charID));
    F(corporationID,            PRI(row.GetUInt(2)),    PRI(newCorpID));
    F(divisionID,               PRN,                    PRN);
    F(roles,                    PRL(row.GetInt64(3)),   PRI(0));
    F(grantableRoles,           PRL(row.GetInt64(4)),   PRI(0));
    F(grantableRolesAtBase,     PRL(row.GetInt64(5)),   PRI(0));
    F(grantableRolesAtHQ,       PRL(row.GetInt64(6)),   PRI(0));
    F(grantableRolesAtOther,    PRL(row.GetInt64(7)),   PRI(0));
    F(squadronID,               PRN,                    PRN);
    F(startDateTime,            PRL(row.GetInt64(1)),   PRL(GetFileTimeNow()));
    F(titleMask,                PRL(row.GetInt64(8)),   PRI(0));

#undef F
#undef PRN
#undef PRI
#undef PRS
#undef PRL
#undef PRNI

    return true;
}

bool CorporationDB::UpdateDivisionNames(uint32 corpID, const Call_UpdateDivisionNames& divs, PyDict* notif) {
    //  updated to update wallet names, too.   -allan 14Jan15
    if (IsNPCCorp(corpID))
        return false; // make error here for changing npc corp data?  nah, should NEVER hit
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT"
        "  division1, division2, division3, division4, division5, division6, division7,"
        "  walletDivision2, walletDivision3, walletDivision4, walletDivision5, walletDivision6, walletDivision7"
        " FROM crpWalletDivisons"
        " WHERE corporationID = %u", corpID))
    {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return false;
    }

    DBResultRow row;
    if (!res.GetRow(row)) {
        codelog(CORP__DB_WARNING, "Corporation %u doesn't exist.", corpID);
        return false;
    }

    std::vector<std::string> dbQ;
    ProcessStringChange("division1", row.GetText(0), divs.div1, notif, dbQ);
    ProcessStringChange("division2", row.GetText(1), divs.div2, notif, dbQ);
    ProcessStringChange("division3", row.GetText(2), divs.div3, notif, dbQ);
    ProcessStringChange("division4", row.GetText(3), divs.div4, notif, dbQ);
    ProcessStringChange("division5", row.GetText(4), divs.div5, notif, dbQ);
    ProcessStringChange("division6", row.GetText(5), divs.div6, notif, dbQ);
    ProcessStringChange("division7", row.GetText(6), divs.div7, notif, dbQ);
    //ProcessStringChange("walletDivision1", row.GetText(7), divs.wallet1, notif, dbQ);  cannot change this one
    ProcessStringChange("walletDivision2", row.GetText(7), divs.wallet2, notif, dbQ);
    ProcessStringChange("walletDivision3", row.GetText(8), divs.wallet3, notif, dbQ);
    ProcessStringChange("walletDivision4", row.GetText(9), divs.wallet4, notif, dbQ);
    ProcessStringChange("walletDivision5", row.GetText(10), divs.wallet5, notif, dbQ);
    ProcessStringChange("walletDivision6", row.GetText(11), divs.wallet6, notif, dbQ);
    ProcessStringChange("walletDivision7", row.GetText(12), divs.wallet7, notif, dbQ);

    std::string query = "UPDATE crpWalletDivisons SET ";

    int N = dbQ.size();
    for (int i = 0; i < N; ++i) {
        query += dbQ[i];
        if (i < N - 1)
            query += ", ";
    }

    query += " WHERE corporationID = ";
    query += std::to_string(corpID);

    _log(CORP__DB_MESSAGE, "DB Query: %s", query.c_str());

    DBerror err;
    // We are here, so something must have changed...
    if (N > 0)
        if (!sDatabase.RunQuery(err, query.c_str())) {
            codelog(CORP__DB_ERROR, "Error in query: %s", err.c_str());
            return false;
        }

    return true;
}

std::string CorporationDB::GetCorpName(uint32 corpID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT corporationName FROM crpCorporation WHERE corporationID = %u", corpID)) {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return "Unknown";
    }

    DBResultRow row;
    if (!res.GetRow(row)) {
        _log(CORP__DB_WARNING, "Corporation %u - Corp name not found.", corpID);
        return "Unknown";
    }

    return row.GetText(0);
}

std::string CorporationDB::GetDivisionName(uint32 corpID, uint16 acctKey)
{
    std::string acctKeyName = "";
    switch (acctKey) {
        case Account::KeyType::Cash:    acctKeyName = "division1"; break;
        case Account::KeyType::Cash2:   acctKeyName = "division2"; break;
        case Account::KeyType::Cash3:   acctKeyName = "division3"; break;
        case Account::KeyType::Cash4:   acctKeyName = "division4"; break;
        case Account::KeyType::Cash5:   acctKeyName = "division5"; break;
        case Account::KeyType::Cash6:   acctKeyName = "division6"; break;
        case Account::KeyType::Cash7:   acctKeyName = "division7"; break;
    }

    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT %s FROM crpWalletDivisons WHERE corporationID = %u", acctKeyName.c_str(), corpID)) {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return "Unknown";
    }

    DBResultRow row;
    if (!res.GetRow(row)) {
        _log(CORP__DB_WARNING, "Corporation %u - division name for acctKey %u not found.", corpID, acctKey);
        return "Unknown";
    }

    return row.GetText(0);
}


bool CorporationDB::UpdateCorporation(uint32 corpID, const std::string& description, const std::string& url, double taxRate, int8 isRecruiting, PyDict * notif) {
    DBQueryResult res;

    if (!sDatabase.RunQuery(res,
        " SELECT description, url, taxRate, isRecruiting "
        " FROM crpCorporation "
        " WHERE corporationID = %u ", corpID))
    {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return false;
    }

    DBResultRow row;
    if (!res.GetRow(row)) {
        codelog(CORP__DB_WARNING, "Corporation %u doesn't exists.", corpID);
        return false;
    }

    std::vector<std::string> dbQ;
    ProcessStringChange("description", row.GetText(0), description, notif, dbQ);
    ProcessStringChange("url", row.GetText(1), url, notif, dbQ);
    ProcessRealChange("taxRate", row.GetDouble(2), taxRate, notif, dbQ);
    if (isRecruiting >= 0)
        ProcessIntChange("isRecruiting", row.GetUInt(3), isRecruiting, notif, dbQ);

    std::string query = " UPDATE crpCorporation SET ";

    int N = dbQ.size();
    for (int i = 0; i < N; i++) {
        query += dbQ[i];
        if (i < N - 1) query += ", ";
    }

    query += " WHERE corporationID = %u";

    // only update if there is anything to update
    if ((N > 0) && (!sDatabase.RunQuery(res.error, query.c_str(), corpID))) {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return false;
    }

    return true;
}

#define NI(i) row.IsNull(i) ? 0 : row.GetInt(i)
bool CorporationDB::UpdateLogo(uint32 corpID, const Call_UpdateLogo & upd, PyDict * notif) {
    DBQueryResult res;

    /** @todo  update this  */
    if (!sDatabase.RunQuery(res,
        " SELECT shape1, shape2, shape3, color1, color2, color3, typeface "
        " FROM crpCorporation "
        " WHERE corporationID = %u ", corpID))
    {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return false;
    }

    DBResultRow row;
    if (!res.GetRow(row)) {
        codelog(CORP__DB_WARNING, "Corporation %u doesn't exists.", corpID);
        return false;
    }

    std::vector<std::string> dbQ;
    ProcessIntChange("shape1", NI(0), upd.shape1, notif, dbQ);
    ProcessIntChange("shape2", NI(1), upd.shape2, notif, dbQ);
    ProcessIntChange("shape3", NI(2), upd.shape3, notif, dbQ);

    ProcessIntChange("color1", NI(3), upd.color1, notif, dbQ);
    ProcessIntChange("color2", NI(4), upd.color2, notif, dbQ);
    ProcessIntChange("color3", NI(5), upd.color3, notif, dbQ);

    std::string query = " UPDATE crpCorporation SET ";

    int N = dbQ.size();
    for (int i = 0; i < N; i++) {
        query += dbQ[i];
        if (i < N - 1) query += ", ";
    }

    query += " WHERE corporationID = %u ";
    if ((N > 0) && (!sDatabase.RunQuery(res.error, query.c_str(), corpID))) {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return false;
    }

    return true;
}
#undef NI

PyRep* CorporationDB::GetMemberTrackingInfo(uint32 corpID)
{
    //  lastOnline(hours) needs update based on char logoffDateTime using GetElapsedHours();
    // no idea how to do that short of pulling/updating column every (x time) interval....and uh, no.
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT c.characterID, c.corporationID, c.title, c.startDateTime, c.corpRole AS roles, c.baseID, c.grantableRoles, c.blockRoles,"
        " c.logonDateTime, c.logoffDateTime, c.locationID, IFNULL(e.typeID, 0) AS shipTypeID, -1 AS lastOnline"
        " FROM chrCharacters AS c"
        "  LEFT JOIN entity AS e ON e.itemID = c.shipID"
        " WHERE c.corporationID = %u", corpID))
    {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return nullptr;
    }
    return DBResultToCRowset(res);
}

PyRep* CorporationDB::GetMemberTrackingInfoSimple(uint32 corpID)
{
    // lastOnline may need something else more accurate, without lastOnline, the member list does not work for someone is a corp member
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT c.characterID, c.corporationID, c.logoffDateTime, c.logonDateTime, c.title, c.startDateTime, c.corpRole AS roles,"
        " c.baseID, c.blockRoles, IFNULL(e.typeID, 0) AS shipTypeID, c.logonDateTime AS lastOnline"
        " FROM chrCharacters AS c"
        "  LEFT JOIN entity AS e ON e.itemID = c.shipID"
        " WHERE corporationID = %u", corpID))
    {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return nullptr;
    }
    return DBResultToCRowset(res);
}

void CorporationDB::GetAutoPay(uint32 corpID, DBQueryResult& res)
{
    sDatabase.RunQuery(res, "SELECT market, rental, broker, war, alliance, sov, divisionID FROM crpAutoPay WHERE corporationID = %u", corpID);
}

void CorporationDB::SetAutoPay(uint32 corpID, bool market, bool rental, bool broker, bool war, bool alliance, bool sov, uint16 divisionID)
{
    DBerror err;
    sDatabase.RunQuery(err, "DELETE FROM crpAutoPay WHERE corporationID = %u", corpID);
    if (!sDatabase.RunQuery(err,
        "INSERT INTO crpAutoPay (corporationID, market, rental, broker, war, alliance, sov, divisionID) VALUES (%u, %u, %u, %u, %u, %u, %u, %u)",
        corpID, (market ? 1 : 0), (rental ? 1 : 0), (broker ? 1 : 0), (war ? 1 : 0), (alliance ? 1 : 0), (sov ? 1 : 0), divisionID))
        codelog(CORP__DB_ERROR, "Error in query: %s", err.c_str());
}

void CorporationDB::AddItemEvent(uint32 corpID, uint32 charID, uint16 eTypeID)
{
    DBerror err;
    sDatabase.RunQuery(err,
        "INSERT INTO crpItemEvent (corporationID, characterID, eventTypeID, eventDateTime)"
        " VALUES (%u, %u, %u, %f)", corpID, charID, eTypeID, GetFileTimeNow());
}

PyRep* CorporationDB::GetItemEvents(uint32 corpID, uint32 charID, int64 fromDate, int64 toDate, uint8 rowsPerPage)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        " SELECT eventID, corporationID, characterID, eventTypeID, eventDateTime"
        " FROM crpItemEvent"
        " WHERE corporationID = %u AND characterID = %u AND eventDateTime > %lli AND eventDateTime <= %lli "
        " LIMIT %u", corpID, charID, fromDate, toDate, rowsPerPage))
    {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return nullptr;
    }

    return DBResultToCRowset(res);
/*
    PyList* list = new PyList();
    if (res.GetRowCount() > 0) {
        DBResultRow row;
        while (res.GetRow(row)) {
            PyDict* dict = new PyDict();
                dict->SetItemString("eventID", new PyInt(row.GetInt(0)));
                dict->SetItemString("corporationID", new PyInt(row.GetInt(1)));
                dict->SetItemString("characterID", new PyInt(row.GetInt(2)));
                dict->SetItemString("eventTypeID", new PyInt(row.GetInt(3)));
                dict->SetItemString("eventDateTime", new PyLong(row.GetInt64(4)));
            list->AddItem(dict);
        }
    } else {
        PyDict* dict = new PyDict();
        dict->SetItemString("eventID", new PyInt(0));
        dict->SetItemString("corporationID", new PyInt(0));
        dict->SetItemString("characterID", new PyInt(0));
        dict->SetItemString("eventTypeID", new PyInt(0));
        dict->SetItemString("eventDateTime", new PyInt(0));
        list->AddItem(dict);
    }

    return new PyObject("util.KeyVal", list);
    */
}

void CorporationDB::AddRoleHistory(uint32 corpID, uint32 charID, uint32 issuerID, int64 oldRoles, int64 newRoles, bool grantable)
{
    DBerror err;
    sDatabase.RunQuery(err,
        "INSERT INTO crpRoleHistroy (corporationID, characterID, issuerID, changeTime, oldRoles, newRoles, grantable)"
        " VALUES (%u, %u, %u, %f, %lli, %lli, %i)", corpID, charID, issuerID, GetFileTimeNow(), oldRoles, newRoles, (grantable ? 1 : 0));
}

PyRep* CorporationDB::GetRoleHistroy(uint32 corpID, uint32 charID, int64 fromDate, int64 toDate, uint8 rowsPerPage)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        " SELECT corporationID, characterID, issuerID, changeTime, oldRoles, newRoles, grantable"
        " FROM crpRoleHistroy"
        " WHERE corporationID = %u and characterID = %u AND changeTime > %lli AND changeTime <= %lli "
        " LIMIT %u", corpID, charID, fromDate, toDate, rowsPerPage))
    {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return nullptr;
    }

    return DBResultToCRowset(res);
/*
    PyList* list = new PyList();
    if (res.GetRowCount() > 0) {
        DBResultRow row;
        while (res.GetRow(row)) {
        PyDict* dict = new PyDict();
            dict->SetItemString("corporationID", new PyInt(row.GetInt(0)));
            dict->SetItemString("charID", new PyInt(row.GetInt(1)));
            dict->SetItemString("issuerID", new PyInt(row.GetInt(2)));
            dict->SetItemString("changeTime", new PyLong(row.GetInt64(3)));
            dict->SetItemString("oldRoles", new PyLong(row.GetInt64(4)));
            dict->SetItemString("newRoles", new PyLong(row.GetInt64(5)));
            dict->SetItemString("grantable", new PyBool(row.GetBool(6)));
        list->AddItem(dict);
        }
    } else {
        PyDict* dict = new PyDict();
            dict->SetItemString("corporationID", new PyInt(0));
            dict->SetItemString("charID", new PyInt(0));
            dict->SetItemString("issuerID", new PyInt(0));
            dict->SetItemString("changeTime", new PyInt(0));
            dict->SetItemString("oldRoles", new PyInt(0));
            dict->SetItemString("newRoles", new PyInt(0));
            dict->SetItemString("grantable", new PyBool(false));
        list->AddItem(dict);
    }

    return new PyObject("util.KeyVal", list);
    */
}

uint32 CorporationDB::AddVoteCase(uint32 corpID, uint32 charID, uint8 voteType, const std::string& text, const std::string& description,
                                  int64 startDateTime, int64 endDateTime, const std::vector<Corp::VoteCaseOptions>& options)
{
    std::string textEsc, descEsc;
    sDatabase.DoEscapeString(textEsc, text);
    sDatabase.DoEscapeString(descEsc, description);

    uint32 voteCaseID(0);
    DBerror err;
    if (!sDatabase.RunQueryLID(err, voteCaseID,
        " INSERT INTO crpVoteItems (corporationID, characterID, voteType, voteCaseText, description, inEffect, status, startDateTime, endDateTime)"
        " VALUES (%u, %u, %u, '%s', '%s', 0, 2, %lli, %lli)",
        corpID, charID, voteType, textEsc.c_str(), descEsc.c_str(), startDateTime, endDateTime))
    {
        codelog(CORP__DB_ERROR, "Error in query: %s", err.c_str());
        return 0;
    }

    for (auto cur : options) {
        std::string optEsc;
        sDatabase.DoEscapeString(optEsc, cur.optionText);
        if (!sDatabase.RunQuery(err,
            "INSERT INTO crpVoteOptions (voteCaseID, optionID, optionText, parameter, parameter1, parameter2, votesFor)"
            " VALUES (%u, %i, '%s', %i, %i, %i, 0)",
            voteCaseID, cur.optionID, optEsc.c_str(), cur.parameter, cur.parameter1, cur.parameter2))
            codelog(CORP__DB_ERROR, "Error in query: %s", err.c_str());
    }

    return voteCaseID;
}

namespace {
const char* VOTE_ITEM_COLUMNS =
    " voteCaseID, voteCaseText, description, corporationID, characterID, voteType, startDateTime, endDateTime,"
    " status, inEffect, actedUpon, timeActedUpon, rescended, timeRescended";
}

PyRep* CorporationDB::GetVoteItems(uint32 corpID, uint8 status/*0*/, uint16 maxLen/*0*/)
{
    // bco_voting:  all (cached by the client and kept by OnCorporationVoteCaseChanged), open (2), or the last maxLen closed (1)
    std::ostringstream query;
    query << "SELECT" << VOTE_ITEM_COLUMNS << " FROM crpVoteItems WHERE corporationID = " << corpID;
    if (status != 0)
        query << " AND status = " << (uint32)status;
    query << " ORDER BY " << (status == 1 ? "endDateTime DESC" : "voteCaseID");
    if (maxLen > 0)
        query << " LIMIT " << maxLen;

    DBQueryResult res;
    if (!sDatabase.RunQuery(res, query.str().c_str())) {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return nullptr;
    }
    return DBResultToCIndexedRowset(res, "voteCaseID");
}

PyDict* CorporationDB::GetVoteItemChange(uint32 voteCaseID)
{
    // an 'add' for OnCorporationVoteCaseChanged:  {column: (None, value)} over the vote case columns
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT%s FROM crpVoteItems WHERE voteCaseID = %u", VOTE_ITEM_COLUMNS, voteCaseID)) {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return nullptr;
    }

    DBResultRow row;
    if (!res.GetRow(row))
        return nullptr;

    PyDict* change = new PyDict();
    for (uint32 i = 0; i < row.ColumnCount(); ++i) {
        PyTuple* tuple = new PyTuple(2);
            tuple->SetItem(0, PyStatic.NewNone());
            tuple->SetItem(1, DBColumnToPyRep(row, i));
        change->SetItemString(row.ColumnName(i), tuple);
    }
    return change;
}

bool CorporationDB::GetVoteCase(uint32 voteCaseID, Corp::VoteCase& into)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT corporationID, characterID, voteType, status, inEffect, actedUpon, rescended, winningOptionID,"
        " startDateTime, endDateTime, expires, timeActedUpon, timeRescended, voteCaseText, description"
        " FROM crpVoteItems WHERE voteCaseID = %u", voteCaseID))
    {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return false;
    }

    DBResultRow row;
    if (!res.GetRow(row))
        return false;

    into.voteCaseID = voteCaseID;
    into.corporationID = row.GetUInt(0);
    into.characterID = row.GetUInt(1);
    into.voteType = row.GetUInt(2);
    into.status = row.GetUInt(3);
    into.inEffect = (row.GetUInt(4) != 0);
    into.actedUpon = (row.GetUInt(5) != 0);
    into.rescended = (row.GetUInt(6) != 0);
    into.winningOptionID = (row.IsNull(7) ? -1 : row.GetInt(7));
    into.startDateTime = row.GetInt64(8);
    into.endDateTime = row.GetInt64(9);
    into.expires = row.GetInt64(10);
    into.timeActedUpon = row.GetInt64(11);
    into.timeRescended = row.GetInt64(12);
    into.voteCaseText = row.GetText(13);
    into.description = row.GetText(14);
    return true;
}

void CorporationDB::GetVoteCaseOptions(uint32 voteCaseID, std::vector<Corp::VoteCaseOptions>& into)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT optionID, IFNULL(parameter, 0), IFNULL(parameter1, 0), IFNULL(parameter2, 0), votesFor, optionText"
        " FROM crpVoteOptions WHERE voteCaseID = %u ORDER BY optionID", voteCaseID))
    {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return;
    }

    DBResultRow row;
    while (res.GetRow(row)) {
        Corp::VoteCaseOptions option = Corp::VoteCaseOptions();
            option.optionID = row.GetInt(0);
            option.parameter = row.GetInt(1);
            option.parameter1 = row.GetInt(2);
            option.parameter2 = row.GetInt(3);
            option.votesFor = row.GetDouble(4);
            option.optionText = row.GetText(5);
        into.push_back(option);
    }
}

void CorporationDB::GetExpiredVoteCases(int64 now, std::vector<uint32>& into)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT voteCaseID FROM crpVoteItems WHERE status = 2 AND endDateTime <= %lli", now)) {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return;
    }

    DBResultRow row;
    while (res.GetRow(row))
        into.push_back(row.GetUInt(0));
}

void CorporationDB::CloseVoteCase(uint32 voteCaseID, int8 winningOptionID, bool inEffect, bool actedUpon, int64 timeActedUpon, int64 expires)
{
    DBerror err;
    std::string winner = (winningOptionID < 0 ? "NULL" : std::to_string(winningOptionID));
    if (!sDatabase.RunQuery(err,
        "UPDATE crpVoteItems SET status = 1, winningOptionID = %s, inEffect = %u, actedUpon = %u, timeActedUpon = %lli, expires = %lli"
        " WHERE voteCaseID = %u",
        winner.c_str(), (inEffect ? 1 : 0), (actedUpon ? 1 : 0), timeActedUpon, expires, voteCaseID))
        codelog(CORP__DB_ERROR, "Error in query: %s", err.c_str());
}

void CorporationDB::UpdateSanction(const Corp::VoteCase& vc)
{
    DBerror err;
    if (!sDatabase.RunQuery(err,
        "UPDATE crpVoteItems SET inEffect = %u, actedUpon = %u, timeActedUpon = %lli, rescended = %u, timeRescended = %lli"
        " WHERE voteCaseID = %u",
        (vc.inEffect ? 1 : 0), (vc.actedUpon ? 1 : 0), vc.timeActedUpon, (vc.rescended ? 1 : 0), vc.timeRescended, vc.voteCaseID))
        codelog(CORP__DB_ERROR, "Error in query: %s", err.c_str());
}

uint32 CorporationDB::GetInEffectLockdown(uint32 corpID, uint32 itemID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT v.voteCaseID FROM crpVoteItems v JOIN crpVoteOptions o ON o.voteCaseID = v.voteCaseID AND o.optionID = v.winningOptionID"
        " WHERE v.corporationID = %u AND v.voteType = %u AND v.inEffect = 1 AND o.parameter = %u ORDER BY v.voteCaseID DESC LIMIT 1",
        corpID, Corp::VoteType::ItemLock, itemID))
    {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return 0;
    }

    DBResultRow row;
    if (res.GetRow(row))
        return row.GetUInt(0);
    return 0;
}

PyRep* CorporationDB::GetVoteOptions(uint32 voteCaseID)
{
    // CIndexedRowset by optionID.  votesFor is a float (the closed vote results show votesFor / totalVotes * 100)
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT o.voteCaseID, o.optionID, o.optionText, o.parameter, o.votesFor,"
        " (SELECT COUNT(*) FROM crpVotes v WHERE v.voteCaseID = o.voteCaseID AND v.optionID = o.optionID) AS votesMade,"
        " 0 AS votesProxied, o.parameter1, o.parameter2"
        " FROM crpVoteOptions o WHERE o.voteCaseID = %u ORDER BY o.optionID", voteCaseID))
    {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return nullptr;
    }

    return DBResultToCIndexedRowset(res, "optionID");
}

PyRep* CorporationDB::GetSanctionedItems(uint32 corpID, uint8 state/*0*/)
{
    // state 1:  in effect, 2:  not in effect and not expired, 0:  all
    std::string filter;
    if (state == 1) {
        filter = " AND v.inEffect = 1";
    } else if (state == 2) {
        filter = " AND v.inEffect = 0 AND v.expires > ";
        filter += std::to_string(GetFileTimeNow());
    }

    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        " SELECT v.voteCaseID, v.voteType, v.winningOptionID AS optionID, v.inEffect, v.actedUpon, v.timeActedUpon, v.timeRescended,"
        "  v.expires, IFNULL(o.parameter, 0) AS parameter, IFNULL(o.parameter1, 0) AS parameter1, IFNULL(o.parameter2, 0) AS parameter2"
        " FROM crpVoteItems v"
        "  JOIN crpVoteOptions o ON o.voteCaseID = v.voteCaseID AND o.optionID = v.winningOptionID"
        " WHERE v.corporationID = %u AND v.status = 1%s", corpID, filter.c_str()))
    {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return nullptr;
    }

    return DBResultToCRowset(res);
}

PyDict* CorporationDB::GetSanctionChange(uint32 voteCaseID)
{
    // an 'add' for OnSanctionedActionChanged over the sanctioned action columns
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        " SELECT v.voteCaseID, v.voteType, v.winningOptionID AS optionID, v.inEffect, v.actedUpon, v.timeActedUpon, v.timeRescended,"
        "  v.expires, IFNULL(o.parameter, 0) AS parameter, IFNULL(o.parameter1, 0) AS parameter1, IFNULL(o.parameter2, 0) AS parameter2"
        " FROM crpVoteItems v"
        "  JOIN crpVoteOptions o ON o.voteCaseID = v.voteCaseID AND o.optionID = v.winningOptionID"
        " WHERE v.voteCaseID = %u", voteCaseID))
    {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return nullptr;
    }

    DBResultRow row;
    if (!res.GetRow(row))
        return nullptr;

    PyDict* change = new PyDict();
    for (uint32 i = 0; i < row.ColumnCount(); ++i) {
        PyTuple* tuple = new PyTuple(2);
            tuple->SetItem(0, PyStatic.NewNone());
            tuple->SetItem(1, DBColumnToPyRep(row, i));
        change->SetItemString(row.ColumnName(i), tuple);
    }
    return change;
}

PyRep* CorporationDB::GetVotes(uint32 voteCaseID)
{
    // util.IndexRowset by characterID (bco_voting.OnCorporationVoteChanged rebuilds it with that key)
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT charID AS characterID, voteCaseID, optionID FROM crpVotes"
        " WHERE voteCaseID = %u", voteCaseID))
    {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return nullptr;
    }

    return DBResultToIndexRowset(res, "characterID");
}

bool CorporationDB::HasVoted(uint32 voteCaseID, uint32 charID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT optionID FROM crpVotes WHERE voteCaseID = %u AND charID = %u", voteCaseID, charID)) {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return true;
    }
    return (res.GetRowCount() > 0);
}

bool CorporationDB::CastVote(uint32 corpID, uint32 charID, uint32 voteCaseID, uint8 optionID, uint32 weight)
{
    DBerror err;
    if (!sDatabase.RunQuery(err,
            "INSERT INTO crpVotes (voteCaseID, charID, corpID, optionID, weight)"
            " VALUES (%u, %u, %u, %u, %u)", voteCaseID, charID, corpID, optionID, weight))
    {
        // (voteCaseID, charID) is the key:  one vote per voter
        codelog(CORP__DB_ERROR, "Error in query: %s", err.c_str());
        return false;
    }
    sDatabase.RunQuery(err, "UPDATE crpVoteItems SET votesMade = votesMade + 1 WHERE voteCaseID = %u", voteCaseID);
    sDatabase.RunQuery(err, "UPDATE crpVoteOptions SET votesFor = votesFor + %u WHERE voteCaseID = %u AND optionID = %u", weight, voteCaseID, optionID);
    return true;
}

bool CorporationDB::MoveShares(uint32 corpID, uint32 fromID, uint32 toID, uint32 count, uint32& fromOld, uint32& toOld)
{
    fromOld = GetSharesHeld(fromID, corpID);
    toOld = GetSharesHeld(toID, corpID);
    if ((count == 0) or (fromOld < count))
        return false;

    DBerror err;
    if (fromOld == count) {
        sDatabase.RunQuery(err, "DELETE FROM crpShares WHERE corporationID = %u AND shareholderID = %u", corpID, fromID);
    } else {
        sDatabase.RunQuery(err, "UPDATE crpShares SET shares = shares - %u WHERE corporationID = %u AND shareholderID = %u", count, corpID, fromID);
    }

    // shareholderCorporationID:  the holder's corp (a corp holder is its own)
    uint32 holderCorpID = (IsCorp(toID) ? toID : CharacterDB::GetCorpID(toID));
    sDatabase.RunQuery(err,
        "INSERT INTO crpShares (corporationID, shareholderID, shares, shareholderCorporationID) VALUES (%u, %u, %u, %u)"
        " ON DUPLICATE KEY UPDATE shares = shares + %u, shareholderCorporationID = %u",
        corpID, toID, count, holderCorpID, count, holderCorpID);
    return true;
}

void CorporationDB::IssueShares(uint32 corpID, uint32 count)
{
    DBerror err;
    sDatabase.RunQuery(err, "UPDATE crpCorporation SET shares = shares + %u WHERE corporationID = %u", count, corpID);
    sDatabase.RunQuery(err,
        "INSERT INTO crpShares (corporationID, shareholderID, shares, shareholderCorporationID) VALUES (%u, %u, %u, %u)"
        " ON DUPLICATE KEY UPDATE shares = shares + %u",
        corpID, corpID, count, corpID, count);
}
PyRep* CorporationDB::GetShares(uint32 corpID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        " SELECT corporationID, shareholderID, shares, shareholderCorporationID"
        " FROM crpShares"
        " WHERE corporationID = %u ", corpID))
    {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return nullptr;
    }
    return DBResultToIndexRowset(res, "shareholderID");
}

PyRep *CorporationDB::GetMyShares(uint32 ownerID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        " SELECT corporationID, shareholderID, shares, shareholderCorporationID"
        " FROM crpShares"
        " WHERE shareholderID = %u ", ownerID))
    {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return nullptr;
    }
    return DBResultToRowset(res);
}

PyRep* CorporationDB::GetSharesForCorp(uint32 corpID)
{
    // kick vote target picker:  members holding more than the average member holding; every member when nobody does
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT s.shareholderID FROM crpShares s JOIN chrCharacters c ON c.characterID = s.shareholderID AND c.corporationID = %u"
        " WHERE s.corporationID = %u AND s.shares > (SELECT AVG(s2.shares) FROM crpShares s2"
        "  JOIN chrCharacters c2 ON c2.characterID = s2.shareholderID AND c2.corporationID = %u WHERE s2.corporationID = %u)",
        corpID, corpID, corpID, corpID))
    {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return new PyList();
    }

    DBResultRow row;
    PyList* list = new PyList();
    while (res.GetRow(row))
        list->AddItem(new PyInt(row.GetInt(0)));

    if (list->empty()) {
        std::vector<uint32> ids, offline;
        GetMemberIDs(corpID, ids, true);
        GetMemberIDs(corpID, offline, false);
        for (auto cur : ids)
            list->AddItem(new PyInt(cur));
        for (auto cur : offline)
            list->AddItem(new PyInt(cur));
    }

    return list;
}

bool CorporationDB::HasShares(uint32 charID, uint32 corpID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT shares FROM crpShares WHERE shareholderID = %u AND corporationID = %u", charID, corpID))  {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return false;
    }

    if (res.GetRowCount())
        return true;

    return false;
}

PyRep* CorporationDB::PyHasShares(uint32 charID, uint32 corpID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT shares FROM crpShares WHERE shareholderID = %u AND corporationID = %u", charID, corpID))  {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return PyStatic.NewFalse();
    }

    if (res.GetRowCount())
        return PyStatic.NewTrue();

    return PyStatic.NewFalse();
}

PyRep* CorporationDB::GetAssetInventory(uint32 corpID, EVEItemFlags locFlag, const char* flags)
{
    /* this may need more code/thought to change implementation
     * currently, "property" will show all corp items located in dynamic containers that are or have corp hangars
     * this will include items located in station hangars that have corp hangars, or anything with flagHangar(4) located in an item of itemID => playerItem
     *  at this time, im not sure if this is desired action
     *
     * this will need to return unique locationIDs for all corp items (dupes cause problems)
     */
    /** @todo  this can be done better  revisit after everything is working and sorted */
    DBQueryResult res;
    switch (locFlag) {
        case flagOffice:        // in stations, using officeIDs (100m)
        case flagImpounded:
        case flagCorpMarket: {
            if (!sDatabase.RunQuery(res,
                " SELECT DISTINCT s.stationID AS locationID FROM entity AS e"
                " LEFT JOIN staOffices AS s ON (s.itemID = e.locationID)"
                " WHERE e.ownerID = %u AND e.flag IN %s"
                " AND e.locationID >= %u AND e.locationID <= %u", corpID, flags, minOffice, maxOffice))
            {
                codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
                return (PyRep*)PyStatic.NewNone();    // cannot return nullptr cause we may deref it on the return.
            }
        } break;
        case flagProperty: {  // in space...this will show items in containers, ships, POS, CO, etc.
            if (!sDatabase.RunQuery(res,
                " SELECT DISTINCT locationID FROM entity WHERE ownerID = %u AND flag IN %s"
                " AND locationID >= %u AND locationID <= %u", corpID, flags, minPlayerItem, maxPlayerItem))
            {
                codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
                return (PyRep*)PyStatic.NewNone();    // cannot return nullptr cause we may deref it on the return.
            }

            // current thought is this option will need more work, to avoid showing corp hangars in ships in stations
            // or do we want to show all possible locations that are NOT corp offices?
        } break;
        default: {
            _log(CORP__DB_WARNING, "CorporationDB::GetAssetInventory(): unhandled locFlag sent: %u", locFlag);
            return (PyRep*)PyStatic.NewNone();    // cannot return nullptr cause we may deref it on the return.
        } break;
    }

    return DBResultToCRowset(res);
}

PyRep* CorporationDB::SearchAssets(uint32 corpID, EVEItemFlags locFlag, const char* flags, uint32 categoryID, uint32 groupID, uint32 typeID, uint32 qty)
{
    std::ostringstream filter;
    if (typeID > 0)
        filter << " AND e.typeID = " << typeID;
    if (groupID > 0)
        filter << " AND t.groupID = " << groupID;
    if (categoryID > 0)
        filter << " AND g.categoryID = " << categoryID;
    if (qty > 0)
        filter << " AND e.quantity >= " << qty;

    DBQueryResult res;
    if (locFlag == flagProperty) {
        if (!sDatabase.RunQuery(res,
            " SELECT DISTINCT e.locationID FROM entity AS e"
            " LEFT JOIN invTypes AS t USING (typeID)"
            " LEFT JOIN invGroups AS g USING (groupID)"
            " WHERE e.ownerID = %u AND e.flag IN %s AND e.locationID >= %u AND e.locationID <= %u%s",
            corpID, flags, minPlayerItem, maxPlayerItem, filter.str().c_str()))
        {
            codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
            return new PyList();
        }
    } else {
        // offices, impounded and deliveries are listed by station
        if (!sDatabase.RunQuery(res,
            " SELECT DISTINCT s.stationID AS locationID FROM entity AS e"
            " JOIN staOffices AS s ON (s.itemID = e.locationID)"
            " LEFT JOIN invTypes AS t ON (t.typeID = e.typeID)"
            " LEFT JOIN invGroups AS g ON (g.groupID = t.groupID)"
            " WHERE e.ownerID = %u AND e.flag IN %s%s",
            corpID, flags, filter.str().c_str()))
        {
            codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
            return new PyList();
        }
    }

    return DBResultToCRowset(res);
}

PyRep* CorporationDB::GetAssetInventoryForLocation(uint32 corpID, uint32 locationID, const char* flags)
{
    // this will need to get full item data...locationID sent from GetAssetInventory()
    DBQueryResult res;
    if (sDataMgr.IsStation(locationID)) {    // transpose stationID to officeID for item location...should never hit
        if (!sDatabase.RunQuery(res,
            " SELECT e.itemID, e.itemName, e.typeID, e.ownerID, e.locationID, e.flag AS flagID, e.singleton,"
            " e.quantity AS stacksize, t.groupID, g.categoryID FROM entity AS e"
            " LEFT JOIN invTypes AS t USING (typeID)"
            " LEFT JOIN invGroups AS g USING (groupID)"
            " WHERE e.ownerID = %u AND e.flag IN %s AND e.locationID = (SELECT itemID FROM staOffices WHERE stationID = %u AND corporationID = %u)",
            corpID, flags, locationID, corpID)) // quantity, contraband, x, y, z, customInfo
        {
            codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
            return nullptr;
        }
    } else if (IsOfficeID(locationID)) {  // transpose officeID to stationID for item location...most oft used (corp hangars in station)
        if (!sDatabase.RunQuery(res,
            " SELECT e.itemID, e.itemName, e.typeID, e.ownerID, e.locationID, e.flag AS flagID, e.singleton,"
            " e.quantity AS stacksize, t.groupID, g.categoryID FROM entity AS e"
            " LEFT JOIN invTypes AS t USING (typeID)"
            " LEFT JOIN invGroups AS g USING (groupID)"
            " WHERE e.ownerID = %u AND e.flag IN %s AND e.locationID = (SELECT stationID FROM staOffices WHERE itemID = %u)",
            corpID, flags, locationID))
        {
            codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
            return nullptr;
        }
    } else if (IsPlayerItem(locationID)) {  // location is item with corp hangar (ship, pos, etc)
        if (!sDatabase.RunQuery(res,
            " SELECT e.itemID, e.itemName, e.typeID, e.ownerID, e.locationID, e.flag AS flagID, e.singleton,"
            " e.quantity AS stacksize, t.groupID, g.categoryID FROM entity AS e"
            " LEFT JOIN invTypes AS t USING (typeID)"
            " LEFT JOIN invGroups AS g USING (groupID)"
            " WHERE e.ownerID = %u AND e.flag IN %s AND e.locationID = %u", corpID, flags, locationID))
        {
            codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
            return nullptr;
        }
    } else {
        _log(CORP__DB_WARNING, "CorporationDB::GetAssetInventoryForLocation(): unhandled locationID sent:  %u", locationID);
        return nullptr;
    }

    return DBResultToCRowset(res);
}

PyRep* CorporationDB::GetKillsAndLosses(uint32 corpID, uint32 number, uint32 offset)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT"
        "  killID,"
        "  solarSystemID,"
        "  victimCharacterID,"
        "  victimCorporationID,"
        "  victimAllianceID,"
        "  victimFactionID,"
        "  victimShipTypeID,"
        "  finalCharacterID,"
        "  finalCorporationID,"
        "  finalAllianceID,"
        "  finalFactionID,"
        "  finalShipTypeID,"
        "  finalWeaponTypeID,"
        "  killBlob,"
        "  killTime,"
        "  victimDamageTaken,"
        "  finalSecurityStatus,"
        "  finalDamageDone,"
        "  moonID"
        " FROM chrKillTable"
        " WHERE victimCorporationID = %u OR finalCorporationID = %u", corpID, corpID))
    {
        codelog(CORP__DB_ERROR, "Error on query: %s", res.error.c_str());
        return nullptr;
    }

    _log(DATABASE__RESULTS, "CorporationDB::GetKillsAndLosses for corpID: %u returned %lu items", corpID, res.GetRowCount());

    return DBResultToCRowset(res);
}

PyRep* CorporationDB::GetMktInfo(uint32 corpID)
{
    // bid = buy order
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT sell.typeID AS typeID, sell.price AS sellPrice, sell.volRemaining AS sellQuantity, sell.issued AS sellDate, sell.stationID AS sellStationID, "
        " buy.price AS buyPrice, buy.volRemaining AS buyQuantity, buy.issued AS buyDate, buy.stationID AS buyStationID"
        " FROM mktOrders AS sell, mktOrders AS buy"
        " WHERE sell.ownerID = %u AND buy.ownerID = %u AND sell.typeID = buy.typeID AND sell.bid=0 AND buy.bid=1", corpID, corpID))    // bid=buy order
    {
        codelog(CORP__DB_ERROR, "Error on query: %s", res.error.c_str());
    }

    _log(DATABASE__RESULTS, "CorporationDB::GetMktBuyInfo for corpID: %u returned %lu items", corpID, res.GetRowCount());

    return DBResultToCRowset(res);
}

void CorporationDB::GetCorpData(CorpData& data)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT"
        "  taxRate,"
        "  stationID,"
        "  allianceID,"
        "  warFactionID,"
        "  corporationName,"
        "  tickerName"
        " FROM crpCorporation"
        " WHERE corporationID = %u", data.corporationID))
    {
        codelog(DATABASE__ERROR, "Failed to query data of corporation %u: %s.", data.corporationID, res.error.c_str());
        return;
    }

    DBResultRow row;
    if (!res.GetRow(row)) {
        _log(DATABASE__MESSAGE, "No data found for character's %u corporation.", data.corporationID);
        return;
    }

    data.taxRate = row.GetDouble(0);
    data.corpHQ = (row.IsNull(1) ? 0 : row.GetUInt(1));
    data.allianceID = (row.IsNull(2) ? 0 : row.GetUInt(2));
    data.warFactionID = (row.IsNull(3) ? 0 : row.GetUInt(3));
    data.name = row.GetText(4);
    data.ticker = row.GetText(5);
    /** @todo do we want to put shares here? */
}

void CorporationDB::UpdateCorpHQ(uint32 corpID, uint32 stationID)
{
    DBerror err;
    if (!sDatabase.RunQuery(err, "UPDATE crpCorporation SET stationID = %u WHERE corporationID = %u", stationID, corpID))
        codelog(CORP__DB_ERROR, "Error in UpdateCorpHQ query: %s", err.c_str());
}

int32 CorporationDB::GetCorpIDforChar(int32 charID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT corporationID FROM chrCharacters WHERE characterID = %u", charID)) {
        codelog(DATABASE__ERROR, "Failed to query data of corporation for chararcterID %u: %s.", charID, res.error.c_str());
        return 0;
    }

    DBResultRow row;
    if (res.GetRow(row))
        return row.GetInt(0);

    _log(DATABASE__MESSAGE, "No data found for character's %u corporation.", charID);
    return 0;
}

std::string CorporationDB::GetRoleNames(int64 mask)
{
    std::string names;
    if (mask == 0)
        return names;

    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT roleID, shortDescription FROM crpRoles ORDER BY roleIID")) {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return names;
    }

    DBResultRow row;
    while (res.GetRow(row)) {
        int64 roleID = row.GetInt64(0);
        if ((roleID == 0) or ((mask & roleID) != roleID))
            continue;
        if (!names.empty())
            names += ", ";
        names += row.GetText(1);
    }
    return names;
}

void CorporationDB::SetCEO(uint32 corpID, uint32 ceoID)
{
    DBerror err;
    if (!sDatabase.RunQuery(err, "UPDATE crpCorporation SET ceoID = %u WHERE corporationID = %u", ceoID, corpID))
        codelog(CORP__DB_ERROR, "Error in query: %s", err.c_str());
}

int64 CorporationDB::GetLastCEOVote(uint32 corpID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT lastCEOVoteDateTime FROM crpCorporation WHERE corporationID = %u", corpID)) {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return 0;
    }

    DBResultRow row;
    if (res.GetRow(row))
        return row.GetInt64(0);
    return 0;
}

void CorporationDB::SetLastCEOVote(uint32 corpID, int64 time)
{
    DBerror err;
    sDatabase.RunQuery(err, "UPDATE crpCorporation SET lastCEOVoteDateTime = %lli WHERE corporationID = %u", time, corpID);
}

uint32 CorporationDB::GetSharesHeld(uint32 holderID, uint32 corpID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT SUM(shares) FROM crpShares WHERE corporationID = %u AND shareholderID = %u", corpID, holderID)) {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return 0;
    }

    DBResultRow row;
    if (res.GetRow(row) and !row.IsNull(0))
        return row.GetUInt(0);
    return 0;
}

uint32 CorporationDB::GetCorpShares(uint32 corpID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT shares FROM crpCorporation WHERE corporationID = %u", corpID)) {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return 0;
    }

    DBResultRow row;
    if (res.GetRow(row))
        return row.GetUInt(0);
    return 0;
}

void CorporationDB::GetShareholders(uint32 corpID, std::map<uint32, uint32>& into)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT shareholderID, SUM(shares) FROM crpShares WHERE corporationID = %u GROUP BY shareholderID", corpID)) {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return;
    }

    DBResultRow row;
    while (res.GetRow(row))
        if (row.GetUInt(1) > 0)
            into[row.GetUInt(0)] = row.GetUInt(1);
}

void CorporationDB::GetAbilities(uint32 corpID, uint16& memberLimit, uint8& raceMask)
{
    memberLimit = 0;
    raceMask = 0;
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT memberLimit, allowedMemberRaceIDs FROM crpCorporation WHERE corporationID = %u", corpID)) {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return;
    }

    DBResultRow row;
    if (res.GetRow(row)) {
        memberLimit = row.GetUInt(0);
        raceMask = row.GetUInt(1);
    }
}

void CorporationDB::SetAbilities(uint32 corpID, uint16 memberLimit, uint8 raceMask)
{
    DBerror err;
    if (!sDatabase.RunQuery(err, "UPDATE crpCorporation SET memberLimit = %u, allowedMemberRaceIDs = %u WHERE corporationID = %u",
                memberLimit, raceMask, corpID))
        codelog(CORP__DB_ERROR, "Error in query: %s", err.c_str());
}

uint16 CorporationDB::GetOtherRaceMemberCount(uint32 corpID, uint8 raceID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT COUNT(characterID) FROM chrCharacters WHERE corporationID = %u AND raceID != %u", corpID, raceID)) {
        codelog(CORP__DB_ERROR, "Error in query: %s", res.error.c_str());
        return 0;
    }

    DBResultRow row;
    if (res.GetRow(row))
        return row.GetUInt(0);
    return 0;
}

void CorporationDB::CloseCorporation(uint32 corpID)
{
    DBerror err;
    sDatabase.RunQuery(err, "UPDATE crpCorporation SET deleted = 1, memberCount = 0, ceoID = 0 WHERE corporationID = %u", corpID);
    sDatabase.RunQuery(err, "DELETE FROM crpShares WHERE corporationID = %u", corpID);
    // open votes close with the corp
    sDatabase.RunQuery(err, "UPDATE crpVoteItems SET status = 1 WHERE corporationID = %u AND status = 2", corpID);
    sDatabase.RunQuery(err, "DELETE FROM crpApplications WHERE corporationID = %u", corpID);
}

void CorporationDB::GetLockedItemIDs(std::set<uint32>& into)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT itemID FROM crpLockedItems WHERE locked = 1")) {
        codelog(CORP__DB_ERROR, "Error in GetLockedItemIDs query: %s", res.error.c_str());
        return;
    }
    DBResultRow row;
    while (res.GetRow(row))
        into.insert(row.GetUInt(0));
}

void CorporationDB::AddLockedItem(uint32 itemID, uint32 corpID, uint32 stationID)
{
    DBerror err;
    if (!sDatabase.RunQuery(err,
        "REPLACE INTO crpLockedItems (itemID, ownerID, locationID, locked) VALUES (%u, %u, %u, 1)", itemID, corpID, stationID))
        codelog(CORP__DB_ERROR, "Error in AddLockedItem query for item %u: %s", itemID, err.c_str());
}

void CorporationDB::RemoveLockedItem(uint32 itemID)
{
    DBerror err;
    sDatabase.RunQuery(err, "DELETE FROM crpLockedItems WHERE itemID = %u", itemID);
}

PyRep* CorporationDB::GetLockedItemLocations(uint32 corpID)
{
    PyList* list = new PyList();
    DBQueryResult res;
    if (!sDatabase.RunQuery(res, "SELECT DISTINCT locationID FROM crpLockedItems WHERE ownerID = %u AND locked = 1", corpID)) {
        codelog(CORP__DB_ERROR, "Error in GetLockedItemLocations query for corp %u: %s", corpID, res.error.c_str());
        return list;
    }
    DBResultRow row;
    while (res.GetRow(row))
        list->AddItem(new PyInt(row.GetInt(0)));
    return list;
}

PyRep* CorporationDB::GetLockedItemsByLocation(uint32 corpID, uint32 stationID)
{
    DBQueryResult res;
    if (!sDatabase.RunQuery(res,
        "SELECT l.itemID, e.typeID, l.ownerID, l.locationID"
        " FROM crpLockedItems AS l"
        "  JOIN entity AS e ON e.itemID = l.itemID"
        " WHERE l.ownerID = %u AND l.locationID = %u AND l.locked = 1", corpID, stationID)) {
        codelog(CORP__DB_ERROR, "Error in GetLockedItemsByLocation query for corp %u: %s", corpID, res.error.c_str());
        return new PyDict();
    }
    return DBResultToCIndexedRowset(res, "itemID");
}
