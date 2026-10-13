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


#ifndef __CORPORATIONDB_H_INCL__
#define __CORPORATIONDB_H_INCL__

#include "ServiceDB.h"
#include "packets/CorporationPkts.h"

#include "corporation/CorpData.h"


struct OfficeData;

class Client;
class PyRep;
class PyObject;

class CorporationDB
: public ServiceDB
{
public:
    PyRep *GetCorpRoles();
    PyRep *GetCorpRoleGroups();
    PyRep *GetTitles(uint32 corpID);
    bool UpdateTitle(uint32 corpID, Call_UpdateTitleData& args, PyDict* updates);
    void DeleteTitle(uint32 corpID, uint16 titleID);
    void CreateTitleData(uint32 corpID);
    // the client's member rows (util.KeyVal) of the corp, all or only ids, ordered by characterID.  limit 0 = all
    static PyList* GetMemberRows(uint32 corpID, const std::vector<uint32>* ids, uint32 offset, uint32 limit);
    static std::string GetMemberTitle(uint32 charID);
    // titleID -> roles of the corp's titles
    static void GetTitleRoles(uint32 corpID, std::map<int64, Corp::TitleRoles>& into);
    // takes the title from every member
    static void ClearTitle(uint32 corpID, int64 titleID);
    // a CEO vote is open (the CEO's roles are revoked meanwhile)
    static bool HasOpenCEOVote(uint32 corpID);
    uint16 GetMemberCount(uint32 corpID);
    void GetMembers(uint32 corpID, DBQueryResult& res);
    void GetMembersForQuery(std::ostringstream& query, std::vector<uint32>& result);
    PyRep* GetMemberTrackingInfo(uint32 corpID);
    PyRep* GetMemberTrackingInfoSimple(uint32 corpID);
    PyRep *GetCorporations(uint32 corpID);
    PyObject *GetCorporation(uint32 corpID);
    PyObject *GetStations(uint32 corpID);
    PyObject *GetEveOwners(uint32 corpID);
    PyRep* GetBulletins(uint32 corpID);
    void DeleteBulletin(uint32 bulletinID);
    void AddBulletin(uint32 corpID, uint32 ownerID, uint32 cCharID, std::string& title, std::string& body);
    void EditBulletin(uint32 bulletinID, uint32 eCharID, int64 eDataTime, std::string& title, std::string& body);

    PyRep *GetMyApplications(uint32 charID);
    PyRep *GetApplications(uint32 corpID);

    // moves count shares of corpID from fromID to toID (crpShares keyed by corporationID, shareholderID).  false when fromID has fewer
    static bool MoveShares(uint32 corpID, uint32 fromID, uint32 toID, uint32 count, uint32& fromOld, uint32& toOld);
    // new shares into the corp's own treasury
    static void IssueShares(uint32 corpID, uint32 count);
    PyRep *GetShares(uint32 corpID);
    PyRep *GetMyShares(uint32 ownerID);
    bool HasShares(uint32 charID, uint32 corpID);
    PyRep* PyHasShares(uint32 charID, uint32 corpID);
    PyRep* GetSharesForCorp(uint32 corpID);

    PyObject *GetEmploymentRecord(uint32 charID);

    void GetCorpStations(uint32 corpID, std::vector<uint32>& stVec);

    void AddItemEvent(uint32 corpID, uint32 charID, uint16 eTypeID);
    void AddRoleHistory(uint32 corpID, uint32 charID, uint32 issuerID, int64 oldRoles, int64 newRoles, bool grantable);
    PyRep *GetItemEvents(uint32 corpID, uint32 charID, int64 fromDate, int64 toDate, uint8 rowsPerPage);
    PyRep *GetRoleHistroy(uint32 corpID, uint32 charID, int64 fromDate, int64 toDate, uint8 rowsPerPage);

    /* votes (crpVoteItems/crpVoteOptions/crpVotes) */
    // false when the voter already voted
    static bool CastVote(uint32 corpID, uint32 charID, uint32 voteCaseID, uint8 optionID, uint32 weight);
    static bool HasVoted(uint32 voteCaseID, uint32 charID);
    // optionID = row index.  returns voteCaseID (0 = failed)
    static uint32 AddVoteCase(uint32 corpID, uint32 charID, uint8 voteType, const std::string& text, const std::string& description,
                              int64 startDateTime, int64 endDateTime, const std::vector<Corp::VoteCaseOptions>& options);
    static PyRep* GetVotes(uint32 voteCaseID);
    // status 0 = all, 2 = open, 1 = closed (newest first).  maxLen 0 = no limit
    static PyRep* GetVoteItems(uint32 corpID, uint8 status=0, uint16 maxLen=0);
    static PyRep* GetVoteOptions(uint32 voteCaseID);
    // state 1 = in effect, 2 = not in effect and not expired, 0 = all
    static PyRep* GetSanctionedItems(uint32 corpID, uint8 state=0);
    // {column: (None, value)} rows for the 'add' notifications
    static PyDict* GetVoteItemChange(uint32 voteCaseID);
    static PyDict* GetSanctionChange(uint32 voteCaseID);
    static bool GetVoteCase(uint32 voteCaseID, Corp::VoteCase& into);
    static void GetVoteCaseOptions(uint32 voteCaseID, std::vector<Corp::VoteCaseOptions>& into);
    static void GetExpiredVoteCases(int64 now, std::vector<uint32>& into);
    static void CloseVoteCase(uint32 voteCaseID, int8 winningOptionID, bool inEffect, bool actedUpon, int64 timeActedUpon, int64 expires);
    // saves inEffect, actedUpon, timeActedUpon, rescended, timeRescended
    static void UpdateSanction(const Corp::VoteCase& vc);
    // the in-effect lockdown sanction of an item (0 = none)
    static uint32 GetInEffectLockdown(uint32 corpID, uint32 itemID);

    void GetAutoPay(uint32 corpID, DBQueryResult& res);
    static void SetAutoPay(uint32 corpID, bool market, bool rental, bool broker, bool war, bool alliance, bool sov, uint16 divisionID);

    //PyObject *ListStationOffices(uint32 station_id);
    PyObject *ListStationCorps(uint32 station_id);
    PyObject *ListStationOwners(uint32 station_id);

    PyRep *GetCorpInfo(uint32 corpID);

    PyRep* GetContacts(uint32 corpID);
    void AddContact(uint32 ownerID, int32 contactID, float relationshipID);
    void UpdateContact(float relationshipID, uint32 contactID, uint32 ownerID);
    void RemoveContact(uint32 contactID, uint32 ownerID);
    // contactID -> labelMask (labels themselves use the CharacterDB contact label helpers on crpLabels/crpContacts)
    static void GetContactLabelMasks(uint32 ownerID, const std::vector<int32>& contactIDs, std::map<int32, int64>& into);

    bool IsTickerTaken(std::string ticker);
    // an npc or player corporation already has this name (the db collation decides case)
    static bool IsCorpNameTaken(const std::string& name);
    bool AddCorporation(Call_AddCorporation & corpInfo, Client* pClient, uint32 & corpID);
    bool CreateCorporationChangePacket(OnCorporationChanged & cc, uint32 oldCorpID, uint32 newCorpID);
    bool CreateCorporationCreatePacket(OnCorporationChanged & cc, uint32 oldCorpID, uint32 newCorpID);

    bool FetchOfficesKeys (uint32 corporationID, DBQueryResult& res);
    bool FetchOffices(uint32 corpID, uint32 from, uint32 count, DBQueryResult& res);

    int32 GetCorpIDforChar(int32 charID);
    uint32 GetStationOwner(uint32 stationID);
    uint32 GetStationCorporationCEO(uint32 stationID);
    static uint32 GetCorporationCEO(uint32 corpID);
    uint16 GetCorpMemberCount(uint32 corpID);
    uint16 GetCorpMemberLimit(uint32 corpID);

    double GetCloneTypeCostByID(uint32 cloneTypeID);

    PyRep* GetMedalsReceived(int32 charID);
    PyRep* GetAllCorpMedals(uint32 corpID);
    PyRep* GetCorpMedalData(uint32 corpID);
    PyRep* GetMedalsReceivedDetails(int32 charID);
    PyRep* GetRecipientsOfMedal(int32 medalID);
    PyRep* GetMedalStatuses();
    PyObjectEx* GetMedalDetails(int32 medalID);
    void GiveMedalToCharacters(uint32 issuerID, uint32 corpID, int32 medalID, std::vector< uint32 >& charVec, const std::string& reason);
    uint16 CreateMedal(uint32 ownerID, uint32 creatorID, const std::string& title, const std::string& description);
    void SaveMedalData(int64 medalID, std::vector< Corp::MedalData >& dataList);
    void SetMedalStatus(uint32 charID, uint16 medalID, uint8 status);
    // the character gives back an award (SetMedalStatus 1)
    void RemoveMedal(uint32 charID, int32 medalID);

    PyRep* GetAdTypeData();
    PyRep* GetAdGroupData();
    PyRep* GetAdRegistryData(int64 typeMask=0, bool inAlliance=false, int16 minMembers=0, uint16 maxMembers=12602);
    PyRep* GetAdvert(uint16 adID);
    void DeleteAdvert(uint16 adID);

    uint32 CreateAdvert(Client* pClient, uint32 corpID, int64 typeMask, int8 days, uint16 members, std::string description,
                      uint32 channelID, std::string title);

    void UpdateAdvert(uint16 adID, uint32 corpID, int64 typeMask, int8 days, uint16 members, std::string description,
                        uint32 channelID, std::string title);

    int64 GetAdvertTime(uint16 adID, uint32 corpID);
    void AddRecruiters(uint16 adID, int32 corpID, std::vector< int32 >& charVec);
    PyRep* GetRecruiters(int32 corpID, uint16 adID);

    bool InsertApplication(Corp::ApplicationInfo& aInfo);
    bool UpdateApplication(const Corp::ApplicationInfo& aInfo);
    bool DeleteApplication(const Corp::ApplicationInfo& aInfo);
    bool GetCurrentApplicationInfo(Corp::ApplicationInfo& aInfo);
    bool CreateMemberAttributeUpdate(uint32 newCorpID, uint32 charID, MemberAttributeUpdate& attrib);

    static std::string GetCorpName(uint32 corpID);
    static std::string GetDivisionName(uint32 corpID, uint16 acctKey);
    bool UpdateDivisionNames(uint32 corpID, const Call_UpdateDivisionNames & divs, PyDict * notif);
    // isRecruiting -1 = unchanged
    bool UpdateCorporation(uint32 corpID, const std::string& description, const std::string& url, double taxRate, int8 isRecruiting, PyDict* notif);
    bool UpdateLogo(uint32 corpID, const Call_UpdateLogo & upd, PyDict * notif);

    PyRep* GetAssetInventory(uint32 corpID, EVEItemFlags locFlag, const char* flags);
    PyRep* GetAssetInventoryForLocation(uint32 corpID, uint32 locationID, const char* flags);
    // corpmgr.SearchAssets:  locationIDs holding matching corp items (0 = any category/group/type/quantity)
    PyRep* SearchAssets(uint32 corpID, EVEItemFlags locFlag, const char* flags, uint32 categoryID, uint32 groupID, uint32 typeID, uint32 qty);
    PyRep* GetKillsAndLosses(uint32 corpID, uint32 number, uint32 offset);

    PyRep* GetMktInfo(uint32 corpID);

    // gets taxrate, allyID, warID of corpID
    static void GetCorpData(CorpData& data);

    static void UpdateCorpHQ(uint32 corpID, uint32 stationID);
    static void GetMemberIDs(uint32 corpID, std::vector<uint32>& ids, bool online=true);

    static bool GetCorporationBySchool(uint32 schoolID, uint32 &corporationID);
    static bool GetLocationCorporationByCareer(CharacterData &cdata, uint32 &corporationID);
    static bool DoesCorporationExist(uint32 corpID);

    /* F21 corporation management */
    // comma separated short names of the roles in mask (crpRoles)
    static std::string GetRoleNames(int64 mask);
    static void SetCEO(uint32 corpID, uint32 ceoID);
    static int64 GetLastCEOVote(uint32 corpID);
    static void SetLastCEOVote(uint32 corpID, int64 time);
    // shares of corpID held by holderID / issued by corpID
    static uint32 GetSharesHeld(uint32 holderID, uint32 corpID);
    static uint32 GetCorpShares(uint32 corpID);
    // holderID -> shares of corpID
    static void GetShareholders(uint32 corpID, std::map<uint32, uint32>& into);
    static void GetAbilities(uint32 corpID, uint16& memberLimit, uint8& raceMask);
    static void SetAbilities(uint32 corpID, uint16 memberLimit, uint8 raceMask);
    static uint16 GetOtherRaceMemberCount(uint32 corpID, uint8 raceID);
    // marks the corp deleted, drops its shares, closes its votes and applications
    static void CloseCorporation(uint32 corpID);

    /* corp item lockdown (crpLockedItems:  itemID, ownerID, locationID = stationID, locked)  (F20 ItemLockdown) */
    static void GetLockedItemIDs(std::set<uint32>& into);
    static void AddLockedItem(uint32 itemID, uint32 corpID, uint32 stationID);
    static void RemoveLockedItem(uint32 itemID);
    // stationIDs with locked items of the corp
    static PyRep* GetLockedItemLocations(uint32 corpID);
    // CIndexedRowset by itemID:  itemID, typeID, ownerID, locationID
    static PyRep* GetLockedItemsByLocation(uint32 corpID, uint32 stationID);
};

#endif
