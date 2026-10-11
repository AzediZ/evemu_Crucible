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
    Author:     Zhur, Bloody.Rabbit
    Updates:        Allan
*/

#include "eve-server.h"
#include "EVE_Mail.h"
//#include "../../eve-common/EVE_Skills.h"

#include "Client.h"
#include "ConsoleCommands.h"
#include "EntityList.h"
#include "StaticDataMgr.h"
#include "StatisticMgr.h"
#include "account/AccountService.h"
#include "character/Character.h"
#include "effects/EffectsProcessor.h"
#include "fleet/FleetService.h"
#include "inventory/AttributeEnum.h"
#include "inventory/Inventory.h"
#include "mail/NotificationMgr.h"
#include "ship/Ship.h"
#include "station/StationDataMgr.h"

/*
 * CharacterTypeData
 */
CharacterTypeData::CharacterTypeData(
    const char* _bloodlineName, uint8 _race, const char* _desc, const char* _maleDesc, const char* _femaleDesc, uint32 _corporationID, uint8 _perception, uint8 _willpower, uint8 _charisma, uint8 _memory, uint8 _intelligence, const char* _shortDesc, const char* _shortMaleDesc, const char* _shortFemaleDesc)
: bloodlineName(_bloodlineName),
  race(_race),
  description(_desc),
  maleDescription(_maleDesc),
  femaleDescription(_femaleDesc),
  corporationID(_corporationID),
  perception(_perception),
  willpower(_willpower),
  charisma(_charisma),
  memory(_memory),
  intelligence(_intelligence),
  shortDescription(_shortDesc),
  shortMaleDescription(_shortMaleDesc),
  shortFemaleDescription(_shortFemaleDesc)
{
}

/*
 * CharacterType
 */
CharacterType::CharacterType(uint16 _id, uint8 _bloodlineID, const Inv::TypeData& _data, const CharacterTypeData& _charData)
: ItemType(_id, _data),
  m_bloodlineID(_bloodlineID),
  m_bloodlineName(_charData.bloodlineName),
  m_description(_charData.description),
  m_maleDescription(_charData.maleDescription),
  m_femaleDescription(_charData.femaleDescription),
  m_corporationID(_charData.corporationID),
  m_perception(_charData.perception),
  m_willpower(_charData.willpower),
  m_charisma(_charData.charisma),
  m_memory(_charData.memory),
  m_intelligence(_charData.intelligence),
  m_shortDescription(_charData.shortDescription),
  m_shortMaleDescription(_charData.shortMaleDescription),
  m_shortFemaleDescription(_charData.shortFemaleDescription)
{
    // check for consistency
    assert(_data.race == _charData.race);
}

CharacterType *CharacterType::Load(uint16 typeID) {
    return ItemType::Load<CharacterType>(typeID );
}

/*
 * CharacterAppearance
 */
void CharacterAppearance::Build(uint32 ownerID, PyDict* data)
{
    PyList* colors = data->GetItemString("colors")->AsList();
    PyList::const_iterator color_cur = colors->begin();
    for (; color_cur != colors->end(); ++color_cur) {
        if ((*color_cur)->IsObjectEx()) {
            PyObjectEx_Type2* color_obj = (PyObjectEx_Type2*)(*color_cur)->AsObjectEx();
            PyTuple* color_tuple = color_obj->GetArgs()->AsTuple();
            //color tuple data structure
            //[0] PyToken
            //[1] colorID
            //[2] colorNameA
            //[3] colorNameBC
            //[4] weight
            //[5] gloss

            m_db.SetAvatarColors(ownerID,
                                 PyRep::IntegerValue(color_tuple->GetItem(1)),
                                 PyRep::IntegerValue(color_tuple->GetItem(2)),
                                 PyRep::IntegerValue(color_tuple->GetItem(3)),
                                 color_tuple->GetItem(4)->AsFloat()->value(),
                                 color_tuple->GetItem(5)->AsFloat()->value());
        }
    }

    PyObjectEx* appearance = data->GetItemString("appearance")->AsObjectEx();
    PyObjectEx_Type2* app_obj = (PyObjectEx_Type2*)appearance;
    PyTuple* app_tuple = app_obj->GetArgs()->AsTuple();

    m_db.SetAvatar(ownerID, app_tuple->GetItem(1));

    PyList* modifiers = data->GetItemString("modifiers")->AsList();
    PyList::const_iterator modif_cur = modifiers->begin();
    for (; modif_cur != modifiers->end(); ++modif_cur) {
        if ((*modif_cur)->IsObjectEx()) {
            PyObjectEx_Type2* modif_obj = (PyObjectEx_Type2*)(*modif_cur)->AsObjectEx();
            PyTuple* modif_tuple = modif_obj->GetArgs()->AsTuple();

            //color tuple data structure
            //[0] PyToken
            //[1] modifierLocationID
            //[2] paperdollResourceID
            //[3] paperdollResourceVariation
            m_db.SetAvatarModifiers(ownerID,
                                    modif_tuple->GetItem(1),
                                    modif_tuple->GetItem(2),
                                    modif_tuple->GetItem(3));
        }
    }

    PyList* sculpts = data->GetItemString("sculpts")->AsList();
    PyList::const_iterator sculpt_cur = sculpts->begin();
    for (; sculpt_cur != sculpts->end(); sculpt_cur++) {
        if ((*sculpt_cur)->IsObjectEx()) {
            PyObjectEx_Type2* sculpt_obj = (PyObjectEx_Type2*)(*sculpt_cur)->AsObjectEx();
            PyTuple* sculpt_tuple = sculpt_obj->GetArgs()->AsTuple();

            //sculpts tuple data structure
            //[0] PyToken
            //[1] sculptLocationID
            //[2] weightUpDown
            //[3] weightLeftRight
            //[4] weightForwardBack
            m_db.SetAvatarSculpts(ownerID,
                                sculpt_tuple->GetItem(1),
                                sculpt_tuple->GetItem(2),
                                sculpt_tuple->GetItem(3),
                                sculpt_tuple->GetItem(4));
        }
    }
}


/*
 * CharacterPortrait
 */

void CharacterPortrait::Build(uint32 charID, PyDict* data)
{
    PortraitInfo info = PortraitInfo();

    info.backgroundID = PyRep::IntegerValue(data->GetItemString("backgroundID"));
    info.lightColorID = PyRep::IntegerValue(data->GetItemString("lightColorID"));
    info.lightID = PyRep::IntegerValue(data->GetItemString("lightID"));

    info.cameraFieldOfView = data->GetItemString("cameraFieldOfView")->AsFloat()->value();
    info.lightIntensity = data->GetItemString("lightIntensity")->AsFloat()->value();

    PyTuple* cameraPoi = data->GetItemString("cameraPoi")->AsTuple();
    info.cameraPoiX = cameraPoi->GetItem(0)->AsFloat()->value();
    info.cameraPoiY = cameraPoi->GetItem(1)->AsFloat()->value();
    info.cameraPoiZ = cameraPoi->GetItem(2)->AsFloat()->value();

    PyTuple* cameraPosition = data->GetItemString("cameraPosition")->AsTuple();
    info.cameraX = cameraPosition->GetItem(0)->AsFloat()->value();
    info.cameraY = cameraPosition->GetItem(1)->AsFloat()->value();
    info.cameraZ = cameraPosition->GetItem(2)->AsFloat()->value();

    PyDict* poseData = data->GetItemString("poseData")->AsDict();
    PyTuple* HeadLookTarget = poseData->GetItemString("HeadLookTarget")->AsTuple();
    info.headLookTargetX = HeadLookTarget->GetItem(0)->AsFloat()->value();
    info.headLookTargetY = HeadLookTarget->GetItem(1)->AsFloat()->value();
    info.headLookTargetZ = HeadLookTarget->GetItem(2)->AsFloat()->value();

    info.browLeftCurl = poseData->GetItemString("BrowLeftCurl")->AsFloat()->value();
    info.browLeftUpDown = poseData->GetItemString("BrowLeftUpDown")->AsFloat()->value();
    info.browLeftTighten = poseData->GetItemString("BrowLeftTighten")->AsFloat()->value();
    info.browRightCurl = poseData->GetItemString("BrowRightCurl")->AsFloat()->value();
    info.browRightUpDown = poseData->GetItemString("BrowRightUpDown")->AsFloat()->value();
    info.browRightTighten = poseData->GetItemString("BrowRightTighten")->AsFloat()->value();

    info.squintLeft = poseData->GetItemString("SquintLeft")->AsFloat()->value();
    info.smileLeft = poseData->GetItemString("SmileLeft")->AsFloat()->value();
    info.frownLeft = poseData->GetItemString("FrownLeft")->AsFloat()->value();
    info.squintRight = poseData->GetItemString("SquintRight")->AsFloat()->value();
    info.smileRight = poseData->GetItemString("SmileRight")->AsFloat()->value();
    info.frownRight = poseData->GetItemString("FrownRight")->AsFloat()->value();

    info.jawUp = poseData->GetItemString("JawUp")->AsFloat()->value();
    info.jawSideways = poseData->GetItemString("JawSideways")->AsFloat()->value();
    info.headTilt = poseData->GetItemString("HeadTilt")->AsFloat()->value();
    info.eyeClose = poseData->GetItemString("EyeClose")->AsFloat()->value();
    info.eyesLookHorizontal = poseData->GetItemString("EyesLookHorizontal")->AsFloat()->value();
    info.eyesLookVertical = poseData->GetItemString("EyesLookVertical")->AsFloat()->value();
    info.orientChar = poseData->GetItemString("OrientChar")->AsFloat()->value();
    info.portraitPoseNumber = poseData->GetItemString("PortraitPoseNumber")->AsFloat()->value();
    info.puckerLips = poseData->GetItemString("PuckerLips")->AsFloat()->value();

    m_db.SetPortraitInfo(charID, info);
}


/*
 * Character
 */
Character::Character(
    uint32 _characterID,
    // InventoryItem stuff:
    const CharacterType &_charType,
    const ItemData &_data,
    // Character stuff:
    const CharacterData &_charData,
    const CorpData &_corpData)
: InventoryItem(_characterID, _charType, _data),
  m_charData(_charData),
  m_corpData(_corpData),
  m_pClient(nullptr),
  m_inTraining(nullptr),
  m_loaded(false),
  m_fleetData(CharFleetData()),
  m_freePoints(0),
  m_loginTime(0)
{
    // enforce characters to be singletons
    assert(isSingleton());

    if (!IsAgent(m_itemID)) {
        m_loginTime = sEntityList.GetStamp();
        pInventory = new Inventory(InventoryItemRef(this));
    }
}

Character::~Character()
{
    SaveBookMarks();
    SaveFullCharacter();
    SaveCertificates();
    SafeDelete(pInventory);
}

CharacterRef Character::Load( uint32 characterID) {
    return InventoryItem::Load<Character>( characterID );
}

bool Character::_Load() {
    if (m_loaded)
        return true;
    if (IsAgent(m_itemID))
        return true;

    if (!pInventory->LoadContents()) {
        sLog.Error("Character::_Load","LoadContents returned false for char %u", m_itemID);
        return (m_loaded = false);
    }
    if (!m_db.LoadSkillQueue(m_itemID, m_skillQueue)) {
        sLog.Error("Character::_Load","LoadSkillQueue returned false for char %u", m_itemID);
        return (m_loaded = false);
    }
    if (!m_skillQueue.empty()) {
        SkillRef sRef = GetCharSkillRef(m_skillQueue.front().typeID);
        if (sRef.get() != nullptr) {
            sRef->SetFlag(flagSkillInTraining, false);
            m_inTraining = sRef.get();
        }
    } else {
        ClearSkillFlags();
    }

    m_loaded = InventoryItem::_Load();

    if (m_loaded) {
        m_certificates.clear();
        if (!m_cdb.LoadCertificates(m_itemID, m_certificates)) {
            sLog.Warning("Character::_Load","LoadCertificates returned false for char %u", m_itemID);
            return (m_loaded = false);
        }
        LoadBoosters();
        // implant and booster ability bonuses are never saved; add them to the loaded attribs
        m_abilityBonus.clear();
        ApplyAbilityBonuses(false);
    }

    // load char personal bookmarks and folders ... corp shit will be done ???
    LoadBookmarks();

    m_charData.loginTime = GetFileTimeNow();

    return m_loaded;
}

void Character::VerifySP()
{
    std::vector<InventoryItemRef> skills;
    pInventory->GetItemsByFlag(flagSkill, skills);
    for (auto cur : skills) {
        SkillRef::StaticCast(cur)->VerifyAttribs();
        SkillRef::StaticCast(cur)->VerifySP();
    }
}

CharacterRef Character::Spawn( CharacterData& charData, CorpData& corpData) {
    // make sure it's a character
    const CharacterType *ct = sItemFactory.GetCharacterType(charData.typeID);
    if (ct == nullptr)
        return CharacterRef(nullptr);

    uint32 characterID(CharacterDB::NewCharacter(charData, corpData));
    if (!IsCharacterID(characterID)) {
        _log(CHARACTER__ERROR, "Failed to get itemID for new character.");
        return CharacterRef(nullptr);
    }

    return Character::Load(characterID);
}

void Character::LogOut()
{
    SaveFullCharacter();
    m_db.SetLogOffTime(m_itemID);
    if (!sConsole.IsShutdown())
        if (IsFleetID(m_fleetData.fleetID))
            sFltSvc.LeaveFleet(m_pClient);

    pInventory->Unload();

    if (!m_pClient->IsCharCreation()) {
        sItemFactory.RemoveItem(m_itemID);
        if (sDataMgr.IsStation(m_charData.locationID))
            ;   // do we need to do anything here?
    }
}

void Character::Delete() {
    // delete contents
    pInventory->DeleteContents();
    // delete character record
    m_db.DeleteCharacter(m_itemID);
    // let the parent care about the rest
    InventoryItem::Delete();
}

double Character::balance(uint8 type)
{
    if (type == Account::CreditType::ISK) {
        return m_charData.balance;
    } else if (type == Account::CreditType::AURUM) {
        return m_charData.aurBalance;
    } else {
        _log(ACCOUNT__ERROR, "Character::balance() - invalid type %u", type);
    }
    return 0.0;
}

bool Character::AlterBalance(double amount, uint8 type) {
    if (amount == 0)
        return true;

    // amount can be negative.  check for funds to remove, if applicable
    if ((balance(type) + amount) < 0) {
        throw UserError ("NotEnoughMoney")
                .AddISK ("amount", -amount)
                .AddISK ("balance", balance (type));
    }

    //adjust balance and send notification of change
    OnAccountChange ac;
    ac.ownerid = m_itemID;
    if (type == Account::CreditType::ISK) {
        m_charData.balance += amount;
        // cap isk balance at one trillion
        if (m_charData.balance > 1000000000000)
            m_charData.balance = 1000000000000;
        ac.balance = m_charData.balance;
        ac.accountKey = "cash";
    } else if (type == Account::CreditType::AURUM) {
        m_charData.aurBalance += amount;
        ac.balance = m_charData.aurBalance;
        ac.accountKey = "AURUM";
    }

    PyTuple *answer = ac.Encode();
    m_pClient->SendNotification("OnAccountChange", "cash", &answer, false);

    SaveCharacter();
    return true;
}

void Character::SetLocation(uint32 stationID, SystemData& data) {
    m_charData.locationID = (stationID == 0 ? data.systemID : stationID);
    m_charData.stationID = stationID;
    m_charData.solarSystemID = data.systemID;
    m_charData.constellationID = data.constellationID;
    m_charData.regionID = data.regionID;
    SaveCharacter();
}

void Character::SetDescription(const char *newDescription) {
    m_charData.description = newDescription;
    SaveCharacter();
}

void Character::JoinCorporation(const CorpData &data) {
    // Add new employment history record    -allan  25Mar14   update 20Jan15   update again 23May19
    CharacterDB::AddEmployment(m_itemID, data.corporationID, m_corpData.corporationID);
    m_corpData = data;
    m_pClient->UpdateCorpSession(m_corpData);
    /** @todo remove this in favor of updating data when changed */
    m_db.SaveCorpData(m_itemID, m_corpData);
}

void Character::SetAccountKey(int32 accountKey)
{
    m_corpData.corpAccountKey = accountKey;
    /** @todo remove this in favor of updating data when changed */
    m_db.SaveCorpData(m_itemID, m_corpData);
    m_pClient->UpdateCorpSession(m_corpData);
}

void Character::SetBaseID(uint32 baseID)
{
    m_corpData.baseID = baseID;
    /** @todo remove this in favor of updating data when changed */
    m_db.SaveCorpData(m_itemID, m_corpData);
    m_pClient->UpdateCorpSession(m_corpData);
}

void Character::UpdateCorpData(CorpData& data)
{
    m_corpData = data;
    m_pClient->UpdateCorpSession(m_corpData);
}

uint32 Character::PickAlternateShip(uint32 locationID)
{
    return m_db.PickAlternateShip(m_itemID, locationID);
}

void Character::SetFleetData(CharFleetData& fleet)
{
    m_fleetData = fleet;
    //if ((fleet.joinTime) and (m_fleetJoinTime != fleet.joinTime))
        //  m_fleetJoinTime = fleet.joinTime;

    m_pClient->UpdateFleetSession(m_fleetData);
}


void Character::ResetModifiers()
{
    _log(EFFECTS__TRACE, "Character::ResetModifiers()");
    ClearModifiers();
    ResetAttributes();
    std::vector<InventoryItemRef> allSkills;
    pInventory->GetItemsByFlag(flagSkill, allSkills);
    for (auto curSkill : allSkills) {
        curSkill->ClearModifiers();
        curSkill->ResetAttributes();
    }
    // the reset loaded the saved (base) ability attribs.  put the implant and booster bonuses back
    m_abilityBonus.clear();
    ApplyAbilityBonuses(false);
}

void Character::ProcessEffects(ShipItem* pShip)
{
    _log(EFFECTS__TRACE, "Character::ProcessEffects()");
    //ResetModifiers();

    // implant and booster attribs are worked out again every time (set bonuses multiply and must not compound)
    std::vector<InventoryItemRef> augs;
    GetImplants(augs);
    GetBoosters(augs);
    for (auto cur : augs)
        ResetAugmentation(cur);

    //  427 total skills.  this should be fairly fast...it is.
    std::vector<InventoryItemRef> allSkills;
    pInventory->GetItemsByFlag(flagSkill, allSkills);
    // the skill in training has its trained levels too
    pInventory->GetItemsByFlag(flagSkillInTraining, allSkills);

    Effect curEffect = Effect();
    std::vector<TypeEffects> typeFx;
    for (auto curSkill : allSkills) {
        typeFx.clear();
        sFxDataMgr.GetTypeEffect(curSkill->typeID(), typeFx);
        for (auto curFx : typeFx) {
            curEffect = sFxDataMgr.GetEffect(curFx.effectID);
            fxData data = fxData();
            data.action = FX::Action::Invalid;
            data.srcRef = curSkill;
            sFxProc.ParseExpression(this, sFxDataMgr.GetExpression(curEffect.preExpression), data);
        }
    }
    // implant and booster effects, parsed like skills
    for (auto cur : augs)
        ParseAugmentation(this, cur, true);

    /* apply processed char effects.  the modifiers on the char's own items (skill self multipliers, implant set bonuses,
     *  booster skill reductions) go first, so the ones reading those items' attribs get the final values */
    sFxProc.ApplyEffects(this, this, pShip, false, FX::Pass::CharItems);
    sFxProc.ApplyEffects(this, this, pShip, false, FX::Pass::Rest);
}

/*
 * implants and boosters  (F15)
 *  both are items located in the character (flagImplant / flagBooster).  their ability bonuses (charismaBonus..willpowerBonus)
 *  are added to the live ability attribs and never saved; the character saves and respecs its base values.
 */
static const uint16 abilityAttrs[5] = { AttrCharisma, AttrIntelligence, AttrMemory, AttrPerception, AttrWillpower };

uint16 Character::AbilityBonusAttr(uint16 attrID)
{
    switch (attrID) {
        case AttrCharisma:      return AttrCharismaBonus;
        case AttrIntelligence:  return AttrIntelligenceBonus;
        case AttrMemory:        return AttrMemoryBonus;
        case AttrPerception:    return AttrPerceptionBonus;
        case AttrWillpower:     return AttrWillpowerBonus;
    }
    return 0;
}

void Character::GetImplants(std::vector<InventoryItemRef>& into) const
{
    pInventory->GetItemsByFlag(flagImplant, into);
}

void Character::GetBoosters(std::vector<InventoryItemRef>& into) const
{
    pInventory->GetItemsByFlag(flagBooster, into);
}

InventoryItemRef Character::GetImplantInSlot(uint8 slot) const
{
    std::vector<InventoryItemRef> implants;
    GetImplants(implants);
    for (auto cur : implants)
        if (cur->GetAttribute(AttrImplantness).get_uint32() == slot)
            return cur;
    return InventoryItemRef(nullptr);
}

InventoryItemRef Character::GetBoosterInSlot(uint8 slot) const
{
    std::vector<InventoryItemRef> boosters;
    GetBoosters(boosters);
    for (auto cur : boosters)
        if (cur->GetAttribute(AttrBoosterness).get_uint32() == slot)
            return cur;
    return InventoryItemRef(nullptr);
}

void Character::ApplyAbilityBonuses(bool notify)
{
    std::vector<InventoryItemRef> augs;
    GetImplants(augs);
    GetBoosters(augs);
    for (auto attrID : abilityAttrs) {
        uint16 bonusAttr(AbilityBonusAttr(attrID));
        int32 bonus(0);
        for (auto cur : augs)
            bonus += cur->GetAttribute(bonusAttr).get_int();
        int32 old(0);
        std::map<uint16, int32>::iterator itr = m_abilityBonus.find(attrID);
        if (itr != m_abilityBonus.end())
            old = itr->second;
        if (bonus == old)
            continue;
        EvilNumber base(GetAttribute(attrID) - EvilNumber(old));
        SetAttribute(attrID, base + EvilNumber(bonus), notify);
        m_abilityBonus[attrID] = bonus;
        _log(CHARACTER__INFO, "%s(%u): %s bonus %i -> %i.", name(), m_itemID, sDataMgr.GetAttrName(attrID), old, bonus);
    }
}

EvilNumber Character::GetBaseAttribute(uint16 attrID)
{
    return GetPersistentAttribute(attrID, GetAttribute(attrID));
}

void Character::SetBaseAttribute(uint16 attrID, EvilNumber value)
{
    int32 bonus(0);
    std::map<uint16, int32>::iterator itr = m_abilityBonus.find(attrID);
    if (itr != m_abilityBonus.end())
        bonus = itr->second;
    SetAttribute(attrID, value + EvilNumber(bonus), true);
}

EvilNumber Character::GetPersistentAttribute(uint16 attrID, const EvilNumber& live) const
{
    std::map<uint16, int32>::const_iterator itr = m_abilityBonus.find(attrID);
    if ((itr == m_abilityBonus.end()) or (itr->second == 0))
        return live;
    return live - EvilNumber(itr->second);
}

void Character::RefreshSkillQueueTimes()
{
    if ((m_inTraining == nullptr) or m_skillQueue.empty())
        return;
    // m_pClient isn't cleared at logoff, so ask the entity list
    if ((m_pClient == nullptr) or (sEntityList.FindClientByCharID(m_itemID) != m_pClient))
        return;
    // pausing saves the trained SP at the old rate; the restart works out the end times at the new rate
    uint16 typeID(m_skillQueue.front().typeID);
    PauseSkillQueue();
    LoadPausedSkillQueue(typeID);
    PyTuple* tmp = new PyTuple(0);
    m_pClient->SendNotification("OnSkillQueueForciblyUpdated", "charid", &tmp, false);
}

// the five ability bonus effects (charismaBonus..willpowerBonus ModAdd on the char) are ApplyAbilityBonuses()' job
static bool IsAbilityBonusEffect(uint16 effectID)
{
    switch (effectID) {
        case 302:   // empathyCharismaBonusModAddCharismaLocationChar
        case 304:   // instantRecallMemoryBonusModAddMemoryLocationChar
        case 306:   // analyticalMindIntelligenceBonusModAddIntelligenceLocationChar
        case 308:   // ironWillWillpowerBonusModAddWillpowerLocationChar
        case 310:   // spatialAwarenessPerceptionBonusModAddPerceptionLocationChar
            return true;
    }
    return false;
}

bool Character::IsAugmentationEffectActive(InventoryItem* pItem, const Effect& effect) const
{
    if (pItem == nullptr)
        return false;
    bool booster(pItem->flag() == flagBooster);
    if (!booster and (pItem->flag() != flagImplant))
        return true;
    if (IsAbilityBonusEffect(effect.effectID))
        return false;
    // booster side effects use boosterEffectChance1..5 and only work when they fired at consumption
    if ((effect.fittingUsageChanceAttributeID < AttrBoosterEffectChance1) or (effect.fittingUsageChanceAttributeID > AttrBoosterEffectChance5))
        return true;
    if (!booster)
        return false;
    std::map<uint32, BoosterState>::const_iterator itr = m_boosters.find(pItem->itemID());
    if (itr == m_boosters.end())
        return false;
    return ((itr->second.sideEffectMask & (1 << (effect.fittingUsageChanceAttributeID - AttrBoosterEffectChance1))) != 0);
}

void Character::ResetAugmentation(InventoryItemRef iRef)
{
    iRef->ClearModifiers();
    iRef->ResetAttributes();
    std::map<uint32, BoosterState>::iterator itr = m_boosters.find(iRef->itemID());
    if (itr != m_boosters.end())
        iRef->SetAttribute(AttrExpiryTime, itr->second.expiryTime, false);
}

void Character::ParseAugmentation(InventoryItem* pContainer, InventoryItemRef iRef, bool add)
{
    std::vector<TypeEffects> typeFx;
    sFxDataMgr.GetTypeEffect(iRef->typeID(), typeFx);
    for (auto curFx : typeFx) {
        Effect effect(sFxDataMgr.GetEffect(curFx.effectID));
        if (!IsAugmentationEffectActive(iRef.get(), effect))
            continue;
        uint16 expressionID(add ? effect.preExpression : effect.postExpression);
        if (expressionID == 0)
            continue;
        fxData data = fxData();
        data.action = FX::Action::Invalid;
        data.srcRef = iRef;
        sFxProc.ParseExpression(pContainer, sFxDataMgr.GetExpression(expressionID), data);
    }
}

void Character::ApplyAugmentationEffects(InventoryItemRef iRef, bool add)
{
    /* docked, the ship gets the pilot's effects again on undock (ShipItem::ResetEffects).  in space, the effects of this one
     *  item are put on or taken off the ship without touching module states.
     *  @note  a set implant plugged in or out in space changes the set bonus of the other set implants only at the next
     *   full effects run (dock, undock, ship change)
     */
    // m_pClient isn't cleared at logoff, so ask the entity list
    if ((m_pClient == nullptr) or (sEntityList.FindClientByCharID(m_itemID) != m_pClient))
        return;
    if (!m_pClient->IsInSpace() or (m_pClient->GetShipSE() == nullptr))
        return;
    ShipItemRef sRef(m_pClient->GetShip());
    if ((sRef.get() == nullptr) or (sRef->GetPilot() != m_pClient))
        return;

    if (add) {
        /* the new item gets what the character's other items do to its group: skills (Biology, Neurotoxin Recovery,
         *  Nanite Control) and implant set bonuses.  parsed into the item's scratch map, only those entries are used
         */
        ResetAugmentation(iRef);
        std::vector<InventoryItemRef> sources;
        pInventory->GetItemsByFlag(flagSkill, sources);
        pInventory->GetItemsByFlag(flagSkillInTraining, sources);
        GetImplants(sources);
        GetBoosters(sources);
        std::vector<TypeEffects> typeFx;
        for (auto src : sources) {
            typeFx.clear();
            sFxDataMgr.GetTypeEffect(src->typeID(), typeFx);
            for (auto curFx : typeFx) {
                Effect effect(sFxDataMgr.GetEffect(curFx.effectID));
                if (!IsAugmentationEffectActive(src.get(), effect))
                    continue;
                fxData data = fxData();
                data.action = FX::Action::Invalid;
                data.srcRef = src;
                sFxProc.ParseExpression(iRef.get(), sFxDataMgr.GetExpression(effect.preExpression), data);
            }
        }
        for (auto cur : iRef->GetModifiers()) {
            if ((cur.second.fxSrc != FX::Source::Group) or (cur.second.targLoc != FX::Target::Char))
                continue;
            if ((cur.second.grpID != iRef->groupID()) or (cur.second.srcRef.get() == nullptr))
                continue;
            EvilNumber targValue(iRef->GetAttribute(cur.second.targAttr));
            switch (cur.first) {
                case FX::Math::PreMul:
                case FX::Math::PostMul:
                case FX::Math::PreDiv:
                case FX::Math::PostDiv: {
                    if (targValue == EvilZero)
                        targValue = EvilOne;
                } break;
            }
            iRef->SetAttribute(cur.second.targAttr, sFxProc.CalculateAttributeValue(targValue, cur.second.srcRef->GetAttribute(cur.second.srcAttr), cur.first), false);
        }
        iRef->ClearModifiers();
    }

    ParseAugmentation(iRef.get(), iRef, add);
    if (add) {
        sFxProc.ApplyEffects(iRef.get(), this, sRef.get(), true, FX::Pass::CharItems);
        sFxProc.ApplyEffects(iRef.get(), this, sRef.get(), true, FX::Pass::Rest);
    } else {
        // take the ship's modifiers off before the ones on the char's items they were worked out from
        std::multimap<int8, fxData> mods(iRef->GetModifiers());
        iRef->ClearModifiers();
        for (auto cur : mods)
            if (!FxProc::IsCharItemModifier(cur.second))
                iRef->GetModifiers().emplace(cur.first, cur.second);
        sFxProc.ApplyEffects(iRef.get(), this, sRef.get(), true);
        for (auto cur : mods)
            if (FxProc::IsCharItemModifier(cur.second))
                iRef->GetModifiers().emplace(cur.first, cur.second);
        sFxProc.ApplyEffects(iRef.get(), this, sRef.get(), true);
    }
    iRef->ClearModifiers();

    // capacities may have changed (booster penalties), and speed or mass
    sRef->ClampDamageState();
    sRef->UpdateDestinyMass();
}

void Character::AugmentationAdded(InventoryItemRef iRef)
{
    std::map<uint16, int32> oldBonus(m_abilityBonus);
    ApplyAbilityBonuses(true);
    ApplyAugmentationEffects(iRef, true);
    if (oldBonus != m_abilityBonus)
        RefreshSkillQueueTimes();
}

void Character::DestroyAugmentation(InventoryItemRef iRef, bool update)
{
    if (update)
        ApplyAugmentationEffects(iRef, false);
    if (m_boosters.erase(iRef->itemID()) > 0)
        CharacterDB::DeleteBooster(iRef->itemID());
    _log(CHARACTER__INFO, "%s(%u): %s(%u) destroyed.", name(), m_itemID, iRef->name(), iRef->itemID());
    iRef->Delete();
    if (!update)
        return;
    std::map<uint16, int32> oldBonus(m_abilityBonus);
    ApplyAbilityBonuses(true);
    if (oldBonus != m_abilityBonus)
        RefreshSkillQueueTimes();
}

void Character::CheckRequiredSkills(InventoryItemRef iRef) const
{
    PyList* missing = new PyList();
    for (auto cur : iRef->type().GetRequiredSkills())
        if (!HasSkillTrainedToLevel(cur.first, cur.second))
            missing->AddItem(new PyInt(cur.first));
    if (missing->empty()) {
        PyDecRef(missing);
        return;
    }
    int count(missing->size());
    throw UserError("ImplantHasSkillPrerequisites")
            .AddFormatValue("item", new PyInt(iRef->typeID()))
            .AddAmount("skillCount", count)
            .AddTypeList("requiredSkills", missing);
}

void Character::PlugInImplant(InventoryItemRef iRef)
{
    if ((iRef->categoryID() != EVEDB::invCategories::Implant) or (iRef->groupID() == EVEDB::invGroups::Booster))
        throw CustomError("%s is not an implant.", iRef->name());
    uint8 slot(iRef->GetAttribute(AttrImplantness).get_uint32());
    if (slot == 0)
        throw CustomError("%s can't be plugged in.", iRef->name());
    // Crucible: no implants while a skill trains
    if (sConfig.character.requirePausedTraining and IsTraining())
        throw UserError("FailedPlugInImplant");
    if (GetImplantInSlot(slot).get() != nullptr)
        throw UserError("OnlyOneImplantActive")
                .AddTypeName("typeName", iRef->typeID());
    CheckRequiredSkills(iRef);
    uint16 prereqTypeID(iRef->GetAttribute(AttrPrereqimplant).get_uint32());
    if (prereqTypeID > 0) {
        bool found(false);
        std::vector<InventoryItemRef> implants;
        GetImplants(implants);
        for (auto cur : implants)
            if (cur->typeID() == prereqTypeID)
                found = true;
        if (!found)
            throw UserError("PrereqImplantMissing")
                    .AddTypeName("typeName", prereqTypeID);
    }

    // implants come off a stack one at a time
    if (iRef->quantity() > 1) {
        iRef = iRef->Split(1);
        if (iRef.get() == nullptr)
            throw CustomError("Unable to split the stack of implants.");
    }
    iRef->ChangeSingleton(true);
    iRef->Move(m_itemID, flagImplant, true);
    _log(CHARACTER__INFO, "%s(%u) plugged in %s(%u) in slot %u.", name(), m_itemID, iRef->name(), iRef->itemID(), slot);

    AugmentationAdded(iRef);
}

void Character::UnplugImplant(InventoryItemRef iRef)
{
    if ((iRef->ownerID() != m_itemID) or (iRef->locationID() != m_itemID) or (iRef->flag() != flagImplant))
        throw CustomError("That implant isn't plugged in.");
    // the same Crucible rule as plugging in
    if (sConfig.character.requirePausedTraining and IsTraining())
        throw UserError("FailedPlugInImplant");
    // unplugged implants are destroyed (ConfirmUnPlugInImplant)
    DestroyAugmentation(iRef, true);
}

// a skill's per-level bonus attribute (from the skill's type) times the character's level, in percent
static float SkillBonusPercent(const Character* pChar, uint16 skillTypeID, uint16 attrID)
{
    const ItemType* pType(sItemFactory.GetType(skillTypeID));
    if (pType == nullptr)
        return 0.0f;
    return pType->GetAttribute(attrID).get_float() * pChar->GetSkillLevel(skillTypeID);
}

// booster duration in ms: the type's boosterDuration (or the configured fallback) +20%/level Biology
static double BoosterDurationMs(const Character* pChar, InventoryItem* pBooster)
{
    double duration(pBooster->GetDefaultAttribute(AttrBoosterDuration).get_double());
    if (duration <= 0)
        duration = sConfig.character.boosterFallbackDurationHours * 3600000.0;
    return duration * (1.0 + SkillBonusPercent(pChar, EvESkill::Biology, AttrDurationBonus) / 100.0);
}

void Character::ConsumeBooster(InventoryItemRef iRef)
{
    if ((iRef->groupID() != EVEDB::invGroups::Booster) or (iRef->GetAttribute(AttrBoosterness).get_uint32() == 0))
        throw CustomError("%s is not a booster.", iRef->name());
    uint8 slot(iRef->GetAttribute(AttrBoosterness).get_uint32());
    // Crucible: no boosters while a skill trains
    if (sConfig.character.requirePausedTraining and IsTraining())
        throw UserError("FailedBoosterConsume");
    CheckRequiredSkills(iRef);
    // Cerebral Accelerator: only for young characters
    EvilNumber maxAge(iRef->GetAttribute(AttrBoosterMaxCharAgeHours));
    if (maxAge > EvilZero)
        if (((int64)GetFileTimeNow() - createDateTime()) / EvE::Time::Hour > maxAge.get_int())
            throw UserError("BoosterPastMaxCharAge");
    InventoryItemRef oldRef(GetBoosterInSlot(slot));
    if (oldRef.get() != nullptr)
        throw UserError("OnlyOneBoosterActive")
                .AddTypeName("typeName", iRef->typeID())
                .AddTypeName("typeName2", oldRef->typeID());

    double duration(BoosterDurationMs(this, iRef.get()));
    int64 expiry((int64)GetFileTimeNow() + (int64)(duration / 1000.0 * EvE::Time::Second));

    // side effects fire independently, each with its boosterEffectChance -5%/level Neurotoxin Recovery
    float chanceMod(1.0f + SkillBonusPercent(this, EvESkill::NeurotoxinRecovery, AttrBoosterChanceBonus) / 100.0f);
    uint8 mask(0);
    std::vector<TypeEffects> typeFx;
    sFxDataMgr.GetTypeEffect(iRef->typeID(), typeFx);
    for (auto curFx : typeFx) {
        Effect effect(sFxDataMgr.GetEffect(curFx.effectID));
        if ((effect.fittingUsageChanceAttributeID < AttrBoosterEffectChance1) or (effect.fittingUsageChanceAttributeID > AttrBoosterEffectChance5))
            continue;
        float chance(iRef->GetAttribute(effect.fittingUsageChanceAttributeID).get_float() * chanceMod);
        if (MakeRandomFloat(0, 1) < chance)
            mask |= (1 << (effect.fittingUsageChanceAttributeID - AttrBoosterEffectChance1));
    }

    // boosters come off a stack one at a time
    if (iRef->quantity() > 1) {
        iRef = iRef->Split(1);
        if (iRef.get() == nullptr)
            throw CustomError("Unable to split the stack of boosters.");
    }
    iRef->ChangeSingleton(true);
    // state first, so the move's item info already has the expiry and the working effects
    BoosterState state = BoosterState();
        state.expiryTime = expiry;
        state.sideEffectMask = mask;
    m_boosters[iRef->itemID()] = state;
    CharacterDB::AddBooster(iRef->itemID(), m_itemID, expiry, mask);
    iRef->SetAttribute(AttrExpiryTime, expiry, false);
    iRef->Move(m_itemID, flagBooster, true);
    iRef->SetAttribute(AttrExpiryTime, expiry, true);
    _log(CHARACTER__INFO, "%s(%u) consumed %s(%u) in slot %u for %.0f min, side effects 0x%x.", \
            name(), m_itemID, iRef->name(), iRef->itemID(), slot, duration / 60000.0, mask);

    AugmentationAdded(iRef);
    if (m_pClient != nullptr)
        m_pClient->SetBoosterTimer(GetNextBoosterExpiry());
}

void Character::LoadBoosters()
{
    /* booster rows hold the expiry and the side effects.  boosters expire in real time, also while logged off:
     *  expired ones (and boosters without a row) are given a past expiry here and go on the first client tick,
     *  so their removal reaches the client
     */
    m_boosters.clear();
    std::map<uint32, std::pair<int64, uint8>> rows;
    CharacterDB::LoadBoosters(m_itemID, rows);
    std::vector<InventoryItemRef> boosters;
    GetBoosters(boosters);
    for (auto cur : boosters) {
        BoosterState state = BoosterState();
            state.expiryTime = 1;
            state.sideEffectMask = 0;
        std::map<uint32, std::pair<int64, uint8>>::iterator itr = rows.find(cur->itemID());
        if (itr != rows.end()) {
            state.expiryTime = itr->second.first;
            state.sideEffectMask = itr->second.second;
            rows.erase(itr);
        }
        m_boosters[cur->itemID()] = state;
        cur->SetAttribute(AttrExpiryTime, state.expiryTime, false);
    }
    // rows of boosters that are gone
    for (auto cur : rows)
        CharacterDB::DeleteBooster(cur.first);
}

int64 Character::GetNextBoosterExpiry() const
{
    int64 next(0);
    for (auto cur : m_boosters)
        if ((next == 0) or (cur.second.expiryTime < next))
            next = cur.second.expiryTime;
    return next;
}

void Character::ProcessBoosterExpiry()
{
    int64 now(GetFileTimeNow());
    std::vector<uint32> expired;
    for (auto cur : m_boosters)
        if (cur.second.expiryTime <= now)
            expired.push_back(cur.first);
    for (auto cur : expired)
        RemoveBooster(cur);
    // m_pClient isn't cleared at logoff, so ask the entity list
    if ((m_pClient != nullptr) and (sEntityList.FindClientByCharID(m_itemID) == m_pClient))
        m_pClient->SetBoosterTimer(GetNextBoosterExpiry());
}

void Character::RemoveBooster(uint32 itemID)
{
    InventoryItemRef iRef(pInventory->GetByID(itemID));
    if ((iRef.get() == nullptr) or (iRef->flag() != flagBooster)) {
        m_boosters.erase(itemID);
        CharacterDB::DeleteBooster(itemID);
        return;
    }
    DestroyAugmentation(iRef, true);
}

void Character::RemoveAllBoosters()
{
    // boosters live in the body: pod death and clone jumps end them
    std::vector<InventoryItemRef> boosters;
    GetBoosters(boosters);
    for (auto cur : boosters)
        DestroyAugmentation(cur, false);
    for (auto cur : m_boosters)
        CharacterDB::DeleteBooster(cur.first);
    std::map<uint16, int32> oldBonus(m_abilityBonus);
    m_boosters.clear();
    ApplyAbilityBonuses(true);
    if (oldBonus != m_abilityBonus)
        RefreshSkillQueueTimes();
    if ((m_pClient != nullptr) and (sEntityList.FindClientByCharID(m_itemID) == m_pClient))
        m_pClient->SetBoosterTimer(0);
}

void Character::PopulateBoosterEffects(InventoryItem* pBooster, Rsp_CommonGetInfo_Entry& result)
{
    std::map<uint32, BoosterState>::iterator itr = m_boosters.find(pBooster->itemID());
    if (itr == m_boosters.end())
        return;
    double duration(BoosterDurationMs(this, pBooster));
    std::vector<TypeEffects> typeFx;
    sFxDataMgr.GetTypeEffect(pBooster->typeID(), typeFx);
    for (auto curFx : typeFx) {
        Effect effect(sFxDataMgr.GetEffect(curFx.effectID));
        if (!IsAbilityBonusEffect(effect.effectID) and !IsAugmentationEffectActive(pBooster, effect))
            continue;   // a side effect that didn't fire
        EntityEffectState es;
            es.env_itemID = pBooster->itemID();
            es.env_charID = m_itemID;
            es.env_shipID = m_charData.shipID;
            es.env_target = m_itemID;
            es.env_other = PyStatic.NewNone();
            es.env_area = PyStatic.NewNone();
            es.env_effectID = effect.effectID;
            es.startTime = itr->second.expiryTime - (int64)(duration / 1000.0 * EvE::Time::Second);
            es.duration = -1;
            es.repeat = 0;
            es.randomSeed = PyStatic.NewNone();
        result.activeEffects[es.env_effectID] = es.Encode();
    }
}

void Character::DestroyImplants()
{
    std::map<uint16, int32> oldBonus(m_abilityBonus);
    std::vector<InventoryItemRef> implants;
    GetImplants(implants);
    for (auto cur : implants)
        DestroyAugmentation(cur, false);
    ApplyAbilityBonuses(true);
    if (oldBonus != m_abilityBonus)
        RefreshSkillQueueTimes();
}

void Character::StoreImplants(std::vector<uint16>& typeIDs)
{
    // the body we leave keeps its implants (jump clone rows hold their typeIDs).  docked, so no ship effects to take off
    std::map<uint16, int32> oldBonus(m_abilityBonus);
    std::vector<InventoryItemRef> implants;
    GetImplants(implants);
    for (auto cur : implants) {
        typeIDs.push_back(cur->typeID());
        DestroyAugmentation(cur, false);
    }
    ApplyAbilityBonuses(true);
    if (oldBonus != m_abilityBonus)
        RefreshSkillQueueTimes();
}

void Character::InstallImplants(const std::vector<uint16>& typeIDs)
{
    // the body we wake up in
    std::map<uint16, int32> oldBonus(m_abilityBonus);
    for (auto cur : typeIDs) {
        ItemData data(cur, m_itemID, locTemp, flagNone);
        InventoryItemRef iRef(sItemFactory.SpawnItem(data));
        if (iRef.get() == nullptr) {
            _log(CHARACTER__ERROR, "%s(%u): unable to spawn implant type %u for the clone.", name(), m_itemID, cur);
            continue;
        }
        iRef->ChangeSingleton(true);
        iRef->Move(m_itemID, flagImplant, true);
    }
    ApplyAbilityBonuses(true);
    if (oldBonus != m_abilityBonus)
        RefreshSkillQueueTimes();
}

void Character::ApplyClonePenalty(uint16& skillTypeID, uint32& spLost)
{
    /* Crucible: a clone saves up to its skillPointsSaved.  podded with more, the pilot loses cloneSkillLossPercent (5%) of
     *  the total from the skill with the most SP, at most cloneSkillLossCap (50%) of that skill  (removed in Rhea, 2014)
     */
    skillTypeID = 0;
    spLost = 0;
    if (!sConfig.character.cloneSkillLoss)
        return;
    uint32 cloneTypeID(0);
    if (!m_db.GetActiveCloneType(m_itemID, cloneTypeID))
        return;
    const ItemType* pClone(sItemFactory.GetType(cloneTypeID));
    if (pClone == nullptr)
        return;
    double cloneSP(pClone->GetAttribute(AttrSkillPointsSaved).get_double());

    std::vector<InventoryItemRef> skills;
    pInventory->GetItemsByFlag(flagSkill, skills);
    pInventory->GetItemsByFlag(flagSkillInTraining, skills);
    double total(0);
    uint32 maxSP(0);
    InventoryItemRef pick(nullptr);
    for (auto cur : skills) {
        uint32 sp(cur->GetAttribute(AttrSkillPoints).get_uint32());
        total += sp;
        if ((pick.get() == nullptr) or (sp > maxSP)
        or ((sp == maxSP) and (cur->GetAttribute(AttrSkillTimeConstant) > pick->GetAttribute(AttrSkillTimeConstant)))) {
            pick = cur;
            maxSP = sp;
        }
    }
    if ((total <= cloneSP) or (pick.get() == nullptr) or (maxSP == 0))
        return;

    double loss(std::min(total * sConfig.character.cloneSkillLossPercent / 100.0, maxSP * sConfig.character.cloneSkillLossCap / 100.0));
    if (loss < 1)
        return;
    skillTypeID = pick->typeID();
    spLost = LoseSkillLevel(skillTypeID, EvESkill::Event::ClonePenalty, (uint32)loss);
    if (spLost == 0)
        skillTypeID = 0;
    _log(SKILL__INFO, "%s(%u) podded with %.0f SP in a clone saving %.0f: lost %u SP of %s.", \
            name(), m_itemID, total, cloneSP, spLost, pick->name());
}

void Character::PodKilled()
{
    // m_pClient isn't cleared at logoff, so ask the entity list
    bool online((m_pClient != nullptr) and (sEntityList.FindClientByCharID(m_itemID) == m_pClient));

    uint16 skillID(0);
    uint32 spLost(0);
    ApplyClonePenalty(skillID, spLost);
    // implants and boosters were in the body
    DestroyImplants();
    RemoveAllBoosters();
    ResetClone();

    uint32 homeStationID(0);
    CharacterDB::GetCharHomeStation(m_itemID, homeStationID);
    if (online) {
        PyTuple* tmp = new PyTuple(1);
            tmp->SetItem(0, new PyInt(itemCloneAlpha));
        m_pClient->SendNotification("OnCloneTypeUpdated", "charid", &tmp, false);
    }

    // "So you died? Well you have a {cloneTypeID} at {corpStationID} station..."  (notificationUtil PramCloneActivationNotification)
    PyDict* data = new PyDict();
        data->SetItemString("cloneStationID", new PyInt(homeStationID));
        data->SetItemString("corpStationID", new PyInt(homeStationID));
        data->SetItemString("cloneTypeID", new PyInt(itemCloneAlpha));
        data->SetItemString("skillPointsLost", (spLost > 0) ? (PyRep*)new PyInt(spLost) : PyStatic.NewNone());
        data->SetItemString("skillID", (skillID > 0) ? (PyRep*)new PyInt(skillID) : PyStatic.NewNone());
        // not tracked
        data->SetItemString("lastCloned", PyStatic.NewNone());
        data->SetItemString("cloneBought", PyStatic.NewNone());
    NotificationMgr::Send(m_itemID, Notify::Types::CloneActivation, stDataMgr.GetOwnerID(homeStationID), data);
}

/*
 * SKILL__ERROR
 * SKILL__WARNING
 * SKILL__MESSAGE
 * SKILL__INFO
 * SKILL__DEBUG
 * SKILL__TRACE
 * SKILL__QUEUE
 */

void Character::GetSkillsList(std::vector<InventoryItemRef> &skills) const {
    pInventory->GetItemsByFlag(flagSkill, skills);
}

bool Character::HasSkill(uint16 skillTypeID) const {
    return (GetCharSkillRef(skillTypeID).get() != nullptr);
}

PyRep* Character::GetSkillHistory() {
    return m_db.GetSkillHistory(m_itemID);
}

void Character::SaveSkillQueue() {
    _log(SKILL__QUEUE, "%s(%u):  Saving skill queue.", name(), m_itemID );
    m_db.SaveSkillQueue( m_itemID, m_skillQueue );
}

void Character::ClearSkillFlags()
{
    std::vector<InventoryItemRef> skills;
    pInventory->GetItemsByFlag(flagSkill, skills);
    for (auto cur : skills)
        cur->SetFlag(flagSkill, false);
}

uint8 Character::GetSPPerMin(Skill* skill)
{
    uint8 primary = GetAttribute(skill->GetAttribute(AttrPrimaryAttribute).get_uint32()).get_uint32();
    uint8 secondary = GetAttribute(skill->GetAttribute(AttrSecondaryAttribute).get_uint32()).get_uint32();
    return EvEMath::Skill::PointsPerMinute(primary, secondary);
}

SkillRef Character::GetCharSkillRef(uint16 skillTypeID) const
{
    InventoryItemRef skill = pInventory->GetByTypeFlag( skillTypeID, flagSkill );
    if (skill.get() == nullptr)
        skill = pInventory->GetByTypeFlag( skillTypeID, flagSkillInTraining );

    return SkillRef::StaticCast( skill );
}

int8 Character::GetSkillLevel(uint16 skillTypeID, bool zeroForNotInjected /*true*/) const {
    SkillRef requiredSkill = GetCharSkillRef( skillTypeID );
    // First, check for existence of skill trained or in training:
    if (requiredSkill.get() == nullptr)
        return (zeroForNotInjected ? 0 : -1);

    return (int8)requiredSkill->GetAttribute(AttrSkillLevel).get_uint32() ;
}

bool Character::HasSkillTrainedToLevel(uint16 skillTypeID, uint8 skillLevel) const {
    SkillRef requiredSkill = GetCharSkillRef( skillTypeID );
    // First, check for existence of skill
    if (requiredSkill.get() == nullptr)
        return false;
    // Second, check for required minimum level of skill, note it must already be trained to this level:
    if (requiredSkill->GetAttribute(AttrSkillLevel) < skillLevel)
        return false;
    return true;
}

PyRep* Character::GetRAMSkills()
{
    /*  this queries RAM skills and is used to display blueprints tab (S&I -> Blueprints)
     *      called by RamProxy::GetRelevantCharSkills()
     *
     *            skillLevels, attributeValues = sm.GetService('manufacturing').GetRelevantCharSkills()
     *            maxManufacturingJobCount = int(attributeValues[const.attributeManufactureSlotLimit])    -AttrManufactureSlotLimit = 196,
     *            maxResearchJobCount = int(attributeValues[const.attributeMaxLaborotorySlots])           -AttrMaxLaborotorySlots = 467,
     *
     *            skillLevels <<  this is a dict of max remote ram jobs
     *            attributeValues  << this is a dict of max ram jobs
     */

    PyDict* skillLevels = new PyDict();
        skillLevels->SetItem(new PyInt(EVEDB::invTypes::ScientificNetworking), new PyInt(GetSkillLevel(EvESkill::ScientificNetworking)));
        skillLevels->SetItem(new PyInt(EVEDB::invTypes::SupplyChainManagement), new PyInt(GetSkillLevel(EvESkill::SupplyChainManagement)));

    uint8 mLab = 1 + GetSkillLevel(EvESkill::LaboratoryOperation) + GetSkillLevel(EvESkill::AdvancedLaboratoryOperation);
    uint8 mSlot = 1 + GetSkillLevel(EvESkill::MassProduction) + GetSkillLevel(EvESkill::AdvancedMassProduction);
    PyDict* attributeValues = new PyDict();
        attributeValues->SetItem(new PyInt(AttrMaxLaborotorySlots), new PyInt(mLab));
        attributeValues->SetItem(new PyInt(AttrManufactureSlotLimit), new PyInt(mSlot));

    PyTuple* tuple = new PyTuple(2);
        tuple->SetItem(0, skillLevels);
        tuple->SetItem(1, attributeValues);
    return tuple;
}

int64 Character::GetEndOfTraining() {
    if (m_inTraining == nullptr)
        return 0;

    // sanity check for skill training
    if (m_skillQueue.empty()) {
        sLog.Error("SkillQueue", "%s(%u) flagged as training but queue empty", \
                m_inTraining->name(), m_inTraining->itemID());
        m_inTraining->SetFlag(flagSkill, true);
        m_inTraining = nullptr;
        m_pClient->SetTrainingEndTime(0);
        return 0;
    }

    m_pClient->SetTrainingEndTime(m_skillQueue.front().endTime);
    return m_skillQueue.front().endTime;
}

void Character::RemoveFromQueue(SkillRef sRef)
{
    SkillQueue::iterator itr = m_skillQueue.begin();
    while (itr != m_skillQueue.end()) {
        if (sRef->typeID() == itr->typeID)
            if (sRef->GetAttribute(AttrSkillLevel).get_uint32() >= itr->level)
                itr = m_skillQueue.erase(itr);
    }
    SkillQueueLoop();
}

void Character::ClearSkillQueue(bool update/*false*/)
{
    CancelSkillInTraining(update);
    m_skillQueue.clear();
    _log(SKILL__QUEUE, "%s(%u) Skill Queue Cleared", name(), m_itemID);
}

PyTuple *Character::SendSkillQueue() {
    // get current skill queue
    PyList *list = new PyList();
    for (auto cur : m_skillQueue) {
        SkillQueue_Element el;
        el.typeID = cur.typeID;
        el.level = cur.level;
        list->AddItem( el.Encode() );
    }

    // and encapsulate it in a tuple with the free points
    PyTuple *tuple = new PyTuple(2);
        tuple->SetItem(0, list);
        tuple->SetItem(1, new PyInt(m_freePoints));
    return tuple;
}

uint32 Character::GetTotalSP() {
    // Loop through all skills trained and calculate total SP this character has trained
    //  this will also update charData for current SP
    m_charData.skillPoints = 0;
    std::vector<InventoryItemRef> skills;
    pInventory->GetItemsByFlag(flagSkill, skills);
    for (auto cur : skills)
        m_charData.skillPoints += cur->GetAttribute( AttrSkillPoints ).get_uint32();    // much cleaner and more accurate    -allan

    return m_charData.skillPoints;
}

uint32 Character::LoseSkillLevel(uint16 skillTypeID, uint16 eventID, uint32 spAmount/*0*/)
{
    SkillRef sRef(GetCharSkillRef(skillTypeID));
    if (sRef.get() == nullptr)
        return 0;

    /* the skill's queue entries make no sense once it has lost a level.  if it is queued or training, training stops and the
     *  queue is rebuilt without it (a logged off character's queue is just cleared: AddToSkillQueue needs a client)
     */
    // m_pClient isn't cleared at logoff, so ask the entity list
    bool online((m_pClient != nullptr) and (sEntityList.FindClientByCharID(m_itemID) == m_pClient));
    bool queued((m_inTraining != nullptr) and (m_inTraining->typeID() == skillTypeID));
    std::vector<std::pair<uint16, uint8>> keep;
    for (auto cur : m_skillQueue) {
        if (cur.typeID == skillTypeID) {
            queued = true;
        } else {
            keep.push_back(std::make_pair(cur.typeID, cur.level));
        }
    }
    if (queued) {
        CancelSkillInTraining(online);
        m_skillQueue.clear();
    }

    uint8 level(sRef->GetAttribute(AttrSkillLevel).get_uint32());
    if ((level < 1) and (spAmount == 0)) {
        if (queued and online)
            for (auto cur : keep)
                AddToSkillQueue(cur.first, cur.second);
        return 0;
    }

    uint32 currentSP(sRef->GetAttribute(AttrSkillPoints).get_uint32());
    uint32 lost(spAmount);
    if (lost == 0)
        lost = sRef->GetSPForLevel(level) - sRef->GetSPForLevel(level - 1);
    if (lost > currentSP)
        lost = currentSP;
    uint32 newSP(currentSP - lost);
    uint8 newLevel(sRef->GetLevelForSP(newSP));
    if (newLevel > level)
        newLevel = level;
    if ((spAmount == 0) and (newLevel >= level))
        newLevel = level - 1;

    sRef->SetAttribute(AttrSkillLevel, newLevel, online);
    sRef->SetAttribute(AttrSkillPoints, newSP, online);
    sRef->SaveItem();
    SaveSkillHistory(eventID, GetFileTimeNow(), m_itemID, skillTypeID, newLevel, newSP);
    GetTotalSP();

    _log(SKILL__INFO, "%s(%u): %s lost %u skill points (level %u -> %u).", name(), m_itemID, sRef->name(), lost, level, newLevel);

    if (queued) {
        if (online) {
            for (auto cur : keep)
                AddToSkillQueue(cur.first, cur.second);
            UpdateSkillQueueEndTime();
            PyTuple* tmp = new PyTuple(0);
            m_pClient->SendNotification("OnSkillQueueForciblyUpdated", "charid", &tmp, false);
        } else {
            m_db.UpdateSkillQueueEndTime(0, m_itemID);
            SaveSkillQueue();
        }
    }

    return lost;
}

void Character::SaveSkillHistory(uint16 eventID, double logDate, uint32 characterID, uint16 skillTypeID, uint8 skillLevel, uint32 absolutePoints)
{
    if (absolutePoints < 1)
        return;
    if (logDate < 1)
        return;
    if (!sDataMgr.IsSkillTypeID(skillTypeID))
        return;
    m_db.SaveSkillHistory(eventID, logDate, characterID, skillTypeID, skillLevel, absolutePoints);
}

uint8 Character::InjectSkillIntoBrain(SkillRef skill) {
    if (m_pClient == nullptr)
        return 0;

    // returns
    // 1=success, 2=prereqs, 3=already known, 4=split fail, 5=load fail

    SkillRef oldSkill(GetCharSkillRef(skill->typeID()));
    if (oldSkill.get() != nullptr) {
        /** @todo: build and send proper UserError for CharacterAlreadyKnowsSkill. */
        m_pClient->SendNotifyMsg("You already know this skill.");
        return 3;
    }

    /** @todo config option (later) to check if another character on this account is training a skill.
     *  If so, send error, cancel inject and return. (flagID=61).
     */

    if (!skill->SkillPrereqsComplete(*this)) {
        /** @todo need to send back a response to the client.  need packet specs. */
        _log(SKILL__DEBUG, "%s(%u): Requested to inject %s (%u/%u) but prereq not complete.", \
                name(), m_itemID, skill->name(), skill->typeID(), skill->itemID());
        m_pClient->SendNotifyMsg("Injection failed.  Skill prerequisites incomplete.");
        return 2;
    }

    // are we injecting from a stack of skills?
    if (skill->quantity() > 1) {
        // split the stack to obtain single item
        skill = SkillRef::StaticCast(skill->Split(1));
        if (skill.get() == nullptr ) {
            _log(ITEM__ERROR, "%s (%u): Unable to split stack of %s (%u).", name(), m_itemID, skill->name(), skill->itemID());
            return 4;
        }
    }

    skill->ChangeSingleton(true);
    skill->Move(m_itemID, flagSkill, true);
    skill->SetAttribute(AttrSkillPoints, EvilZero);
    skill->SetAttribute(AttrSkillLevel, EvilZero);

    skill->VerifyAttribs();

    // 'EvESkill::Event::SkillInjected' shows as "Unknown" in PD>Skill>History
    SaveSkillHistory(EvESkill::Event::SkillInjected, GetFileTimeNow(), m_itemID, skill->typeID(), 0, 0);
    _log(SKILL__MESSAGE, "%s(%u) has injected %s(%u)", name(), m_itemID, skill->name(), skill->itemID());

    return 1;
}

void Character::PauseSkillQueue() {
    m_db.SavePausedSkillQueue(m_itemID, m_skillQueue);

    CancelSkillInTraining(true);

    m_skillQueue.clear();
    UpdateSkillQueueEndTime();
    _log(SKILL__QUEUE, "%s(%u)  Skill Queue Paused", name(), m_itemID);
}

void Character::LoadPausedSkillQueue(uint16 typeID)
{
    m_db.LoadPausedSkillQueue(m_itemID, m_skillQueue);

    if (m_skillQueue.empty())
        return;

    Skill* skill(GetCharSkillRef(typeID).get());
    if (typeID != m_skillQueue.front().typeID) {
        // skill to start != first skill in queue...do we just start removing skills till we find this typeID?
        _log(SKILL__WARNING, "LoadPausedSkillQueue() - type sent (%u) does not match first in queue (%u)",
             typeID, m_skillQueue.front().typeID);
    }

    // queue was paused.  all endTimes are off, so reset using now as start time for first skill.
    uint8 nextLvl(0);
    uint32 currentSP(0), nextSP(0);
    int64 startTime(GetFileTimeNow());
    for (SkillQueue::iterator itr = m_skillQueue.begin(); itr != m_skillQueue.end(); ++itr) {
        skill = GetCharSkillRef(itr->typeID).get();
        if (skill == nullptr)
            continue;
        nextLvl = skill->GetAttribute(AttrSkillLevel).get_uint32() + 1;
        if (nextLvl > EvESkill::MAXSKILLLEVEL)
            nextLvl = EvESkill::MAXSKILLLEVEL;

        if (itr->level == nextLvl) {
            currentSP = skill->GetCurrentSP(this, startTime);
        } else {
            currentSP = skill->GetSPForLevel(itr->level - 1);
        }

        nextSP = skill->GetSPForLevel(itr->level);
        itr->startTime = startTime;
        itr->endTime = EvEMath::Skill::EndTime(currentSP, nextSP, GetSPPerMin(skill), startTime);
        skill->SaveItem();
        startTime = itr->endTime;
    }

    // get first skill, add start history and send begin training packet
    skill = GetCharSkillRef(m_skillQueue.front().typeID).get();
    skill->SetFlag(flagSkillInTraining, true);
    skill->SaveItem();

    OnSkillStartTraining osst;
        osst.itemID = skill->itemID();
        osst.endOfTraining = m_skillQueue.front().endTime;
    PyTuple* tmp = osst.Encode();
    m_pClient->QueueDestinyEvent(&tmp);

    //  do we save/show cancel/restart training for pause/resume?
    SaveSkillHistory(EvESkill::Event::TrainingStarted, GetFileTimeNow(), m_itemID, skill->itemID(), m_skillQueue.front().level, skill->GetSPForLevel(m_skillQueue.front().level));

    UpdateSkillQueueEndTime();

    _log(SKILL__QUEUE, "%s(%u)  Paused Skill Queue Loaded and restarted", name(), m_itemID);
}

void Character::CancelSkillInTraining(bool update/*false*/)
{
    if (m_inTraining == nullptr)
        return; //nothing to do...

    if (m_skillQueue.empty())
        return;

    QueuedSkill qs = m_skillQueue.front();
    uint8 nextLvl(m_inTraining->GetAttribute(AttrSkillLevel).get_uint32() + 1);
    if (nextLvl > EvESkill::MAXSKILLLEVEL)
        nextLvl = EvESkill::MAXSKILLLEVEL;

    int64 curTime(GetFileTimeNow());
    uint32 currentSP(0);

    // is skill in training first in queue?
    if (qs.typeID != m_inTraining->typeID()) {
        // nope
        // not sure how to do this correctly...
        currentSP = m_inTraining->GetCurrentSP(this);
        SaveSkillHistory(EvESkill::Event::TrainingCanceled, curTime, m_itemID, m_inTraining->typeID(), nextLvl, currentSP);
        // send training stopped packet
        if (update) {
            OnSkillTrainingStopped osst;
                osst.itemID = m_inTraining->itemID();
                osst.silent = true;    // silent means 'disable neocom blink event'
            PyTuple* tmp = osst.Encode();
            m_pClient->QueueDestinyEvent(&tmp);
        }

        if (is_log_enabled(SKILL__INFO))
            _log(SKILL__INFO, "%s:%s(%u/%u) Training canceled from skill not in queue.", \
                    name(), m_inTraining->name(), m_inTraining->typeID(), m_inTraining->itemID());

        m_inTraining->SetAttribute(AttrSkillPoints, currentSP, update);
        m_inTraining->SetFlag(flagSkill, update);
        m_inTraining->SaveItem();
        m_inTraining = nullptr;
        // either way, training canceled.
        return;
    }

    // at this point, skill in training is first in queue
    currentSP = m_inTraining->GetCurrentSP(this, qs.startTime);
    if ((qs.endTime < curTime) or (currentSP >= m_inTraining->GetSPForLevel(nextLvl))) {
        // this level has completed
        // send training complete packet
        if (update) {
            OnSkillTrained ost;
                ost.itemID = m_inTraining->itemID();
            PyTuple* tmp = ost.Encode();
            m_pClient->QueueDestinyEvent(&tmp);
        }

        // update attribs and save
        m_inTraining->SetAttribute(AttrSkillLevel, nextLvl, false);
        currentSP = m_inTraining->GetSPForLevel(nextLvl);
        SaveSkillHistory(EvESkill::Event::TrainingComplete, qs.endTime, m_itemID, m_inTraining->typeID(), nextLvl, currentSP);

        if (is_log_enabled(SKILL__INFO))
            _log(SKILL__INFO, "%s:%s(%u/%u) - CancelSkillInTraining - Training to level %u completed.", \
                    name(), m_inTraining->name(), m_inTraining->typeID(), m_inTraining->itemID(), nextLvl);
    } else {
        SaveSkillHistory(EvESkill::Event::TrainingCanceled, curTime, m_itemID, m_inTraining->typeID(), nextLvl, currentSP);
        // send training stopped packet
        if (update) {
            OnSkillTrainingStopped osst;
                osst.itemID = m_inTraining->itemID();
                osst.silent = true;    // silent means 'disable neocom blink event'
            PyTuple* tmp = osst.Encode();
            m_pClient->QueueDestinyEvent(&tmp);
        }

        if (is_log_enabled(SKILL__INFO))
            _log(SKILL__INFO, "%s:%s(%u/%u) - Training to level %u canceled.  CurrentSP: %u, nextSP: %u", \
                    name(), m_inTraining->name(), m_inTraining->typeID(), m_inTraining->itemID(), nextLvl, currentSP, m_inTraining->GetSPForLevel(nextLvl));
    }

    m_inTraining->SetAttribute(AttrSkillPoints, currentSP, update);
    m_inTraining->SetFlag(flagSkill, update);
    m_inTraining->SaveItem();

    // remove from queue, if applicable
    if (!m_skillQueue.empty())
        if (m_inTraining->typeID() == m_skillQueue.front().typeID)
            m_skillQueue.erase( m_skillQueue.begin() );

    m_inTraining = nullptr;
}

void Character::AddToSkillQueue(uint16 typeID, uint8 level) {
    Skill* skill(GetCharSkillRef(typeID).get());
    if (skill == nullptr) {
        //  skill not found.  cancel and return
        _log(SKILL__QUEUE, "Cannot find Skill %u.", typeID);
        m_pClient->SendErrorMsg("Cannot find skill to train.  Ref: ServerError 60723.");
        return;
    }

    _log(SKILL__INFO, "Starting checks to add %s to training queue.", skill->name());

    uint8 nextLvl(skill->GetAttribute(AttrSkillLevel).get_uint32() + 1);
    if (nextLvl > EvESkill::MAXSKILLLEVEL)
        nextLvl = EvESkill::MAXSKILLLEVEL;

    int64 curTime(GetFileTimeNow());
    uint32 currentSP(skill->GetCurrentSP(this)), nextSP(skill->GetSPForLevel(level));
    if (level < nextLvl) {
        // level to train is below current level. update client data and return.
        SaveSkillHistory(EvESkill::Event::TaskMaster, curTime, m_itemID, typeID, level, currentSP);
        skill->SetFlag(flagSkill, true);
        skill->SaveItem();

        OnSkillTrained ost;
            ost.itemID = skill->itemID();
        PyTuple* tmp = ost.Encode();
        m_pClient->QueueDestinyEvent(&tmp);

        _log(SKILL__WARNING, "Trying to add level %u but current level is %u.", level, nextLvl -1);
        return;
    }

    if (level > nextLvl)
        currentSP = skill->GetSPForLevel(level -1);

    // verify sp is below next level
    if (currentSP >= nextSP) {
        // it's not.  update client data and return.
        SaveSkillHistory(EvESkill::Event::TaskMaster, curTime, m_itemID, typeID, level, currentSP);
        skill->SetFlag(flagSkill, true);
        skill->SaveItem();

        OnSkillTrained ost;
            ost.itemID = skill->itemID();
        PyTuple* tmp = ost.Encode();
        m_pClient->QueueDestinyEvent(&tmp);

        _log(SKILL__WARNING, "Trying to add level %u at %u sp but current sp is %u.", level, nextSP, currentSP);
        return;
    }

    // current level trainable
    QueuedSkill qs = QueuedSkill();
        qs.typeID = typeID;
        qs.level = level;

    if (m_skillQueue.empty()) {
        // nothing in queue.  begin training this skill
        skill->SetFlag(flagSkillInTraining, true);
        m_inTraining = skill;
        qs.startTime = curTime;
        qs.endTime = EvEMath::Skill::EndTime(currentSP, nextSP, GetSPPerMin(skill), curTime);
        SaveSkillHistory(EvESkill::Event::TrainingStarted, curTime, m_itemID, typeID, level, nextSP);

        OnSkillStartTraining osst;
            osst.itemID = skill->itemID();
            osst.endOfTraining = qs.endTime;
        PyTuple* tmp = osst.Encode();
        m_pClient->QueueDestinyEvent(&tmp);
    } else {
        qs.startTime = m_skillQueue.back().endTime +EvE::Time::Second;
        qs.endTime = EvEMath::Skill::EndTime(currentSP, nextSP, GetSPPerMin(skill), qs.startTime);
    }

    // add to queue and save
    m_skillQueue.push_back( qs );
    skill->SaveItem();

    float timeLeft = (qs.endTime - qs.startTime) / EvE::Time::Second;
    const char* formatedTime = EvE::FormatTime(timeLeft);
    _log(SKILL__QUEUE, "Added %s Level %u to queue with %s(%.1f) to train %uSP.", \
            skill->name(), level, formatedTime, timeLeft, nextSP - currentSP);
}

void Character::UpdateSkillQueue() {
    /* cleaned up code and reworked logic  -allan 28Apr16   -- revisited 23Mar17  --updated code, logic and timers 16Nov17  -again 9jan18*/
    // finally fixed.  22Jan18  --it wasnt 24Jan18     still not right 3Dec18    -again 4jan19      -another 6Aug19
    // rewrote and FINALLY fixed.  10Aug19   --still wrong 23Aug20
    // complete revamp and rename, this kept for notes  -allan 12Oct20
}

void Character::SkillQueueLoop(bool update/*true*/)
{
    double begin(GetTimeMSeconds());
    _log(SKILL__QUEUE, "%s(%u) calling SkillQueueLoop() - update %s", name(), m_itemID, update?"true":"false");

    // anything to do here?
    if (m_skillQueue.empty())
        if (m_inTraining == nullptr)
            return;

    if (m_inTraining->typeID() != m_skillQueue.front().typeID)
        CancelSkillInTraining(update);

    // at this point, there is a skill in training, and it is front of queue
    int64 curTime(GetFileTimeNow());
    if (m_skillQueue.front().endTime > curTime) {
        float timeLeft = (m_skillQueue.front().endTime - curTime) / EvE::Time::Second;
        const char* formatedTime = EvE::FormatTime(timeLeft);
        _log(SKILL__INFO, "%s still training.  %s remaining.", m_inTraining->name(), formatedTime);
        UpdateSkillQueueEndTime();
        return;
    }

    PyList* list = new PyList();
    bool sent(false), multiple(false);
    Skill* skill(nullptr);
    while (!m_skillQueue.empty()) {
        QueuedSkill qs = m_skillQueue.front();
        skill = GetCharSkillRef( qs.typeID ).get();
        if ((qs.typeID == 0) or (skill == nullptr)) {
            _log( SKILL__WARNING, "SkillID %u to train was not found.  Erase and continue.", qs.typeID);
            m_skillQueue.erase( m_skillQueue.begin() );
            continue;
        }

        _log( SKILL__INFO, "Starting checks for %s.", skill->name());

        if (qs.endTime == 0) {
            // this should not hit at this point.
            _log(SKILL__ERROR, "endTime wasnt set.  Erase from queue and continue.");
            skill->SetFlag(flagSkill, true);
            skill->SaveItem();
            m_skillQueue.erase( m_skillQueue.begin() );
            skill = nullptr;
            m_inTraining = nullptr;
            continue;
        }

        if (qs.endTime < curTime) {
            //  skill training has completed.
            uint32 currentSP = skill->GetSPForLevel(qs.level);
            SaveSkillHistory(EvESkill::Event::QueueTrainingCompleted, qs.endTime, m_itemID, skill->typeID(), qs.level, currentSP);

            if (is_log_enabled(SKILL__INFO))
                _log(SKILL__INFO, "Queued Training completed for level: %u", qs.level);

            // update attribs and save
            skill->SetAttribute(AttrSkillLevel, qs.level, update);
            skill->SetAttribute(AttrSkillPoints, currentSP, update);
            skill->SetFlag(flagSkill, update);
            skill->SaveItem();

            // remove completed skill level from queue
            m_skillQueue.erase( m_skillQueue.begin() );

            // notify client
            if (update) {
                PyTuple* tmp(nullptr);
                if (m_skillQueue.empty()) {
                    // only one skill to update
                    OnSkillTrained ost;
                        ost.itemID = skill->itemID();
                    tmp = ost.Encode();
                } else {
                    // another skill in the works.  send combined update
                    SkillRef sref = GetCharSkillRef(m_skillQueue.front().typeID);
                    if (sref.get() == nullptr) {
                        // that shit didnt work...revert to multiple packets
                        OnSkillTrained ost;
                            ost.itemID = skill->itemID();
                        tmp = ost.Encode();
                    } else {
                        multiple = true;
                        //  queue skill switching uses the following to reduce data sent
                        //def OnSkillSwitched(self, oldSkillID, newSkillID, ETA):
                        OnSkillSwitched oss;
                            oss.oldSkillID = skill->itemID();
                            oss.newSkillID = sref->itemID();
                            oss.ETA = m_skillQueue.front().endTime;
                        tmp = oss.Encode();
                    }
                }
                m_pClient->QueueDestinyEvent(&tmp);
            } else if (m_pClient->IsLogin())
                // for login, use OnMultipleSkillsTrained[]
                list->AddItemInt(skill->itemID());

            if (m_pClient->IsInSpace() and update) {
                switch (skill->groupID()) {
                    case EVEDB::invGroups::Trade:
                    case EVEDB::invGroups::Social:
                    case EVEDB::invGroups::Planet_Management:
                    case EVEDB::invGroups::Corporation_Management: {
                        ;   // do nothing for these.
                    } break;
                    default: {
                        if (!sent) {
                            sent = true;
                            m_pClient->SendInfoModalMsg("Completed Skill Training.<br>Your ship will update to the new skill level the next time you undock.");
                        }
                    } break;
                }
            }

            // clear variables and continue
            skill = nullptr;
            m_inTraining = nullptr;
            continue;
        }

        // at this point, there is at least one skill queued, and nothing currently training.
        uint32 nextSP(skill->GetSPForLevel(qs.level));
        SaveSkillHistory(EvESkill::Event::TrainingStarted, qs.startTime, m_itemID, qs.typeID, qs.level, nextSP);
        skill->SetFlag(flagSkillInTraining, true);
        skill->SaveItem();
        m_inTraining = skill;

        if (is_log_enabled(SKILL__INFO)) {
            float timeLeft = (qs.endTime - curTime) / EvE::Time::Second;
            const char* formatedTime = EvE::FormatTime(timeLeft);
            _log(SKILL__INFO, "Training started.  %s to train %u sp for level %u", \
                    formatedTime, nextSP - skill->GetCurrentSP(this, qs.startTime), qs.level);
        }

        if (!multiple) {
            OnSkillStartTraining osst;
                osst.itemID = skill->itemID();
                osst.endOfTraining = qs.endTime;
            PyTuple* tmp = osst.Encode();
            m_pClient->QueueDestinyEvent(&tmp);
        }

        break;
    }

    UpdateSkillQueueEndTime();
    SaveCharacter();

    if (m_pClient->IsLogin()) {
        PyTuple* tmp(nullptr);
        if (!list->empty()) {
            if (list->size() > 1) {
                OnMultipleSkillsTrained omst;
                    omst.skillList = list;
                tmp = omst.Encode();
            }
        } else if (skill != nullptr) {
            OnSkillTrained ost;
                ost.itemID = skill->itemID();
            tmp = ost.Encode();
        }

        m_pClient->QueueDestinyEvent(&tmp);
    }

    _log(SKILL__QUEUE, "SkillQueueLoop() completed in %.4fms", (GetTimeMSeconds() - begin));
}

void Character::UpdateSkillQueueEndTime()
{
    if (m_skillQueue.empty()) {
        m_db.UpdateSkillQueueEndTime(0, m_itemID);
        m_pClient->SetTrainingEndTime(0);
        if (is_log_enabled(SKILL__TRACE))
            _log(SKILL__QUEUE, "%s(%u):  UpdateSkillQueueEndTime() - Queue is empty.", name(), m_itemID);
        return;
    }

    // update client timer check for skill in training
    m_pClient->SetTrainingEndTime(m_skillQueue.front().endTime);
    m_db.UpdateSkillQueueEndTime(m_skillQueue.back().endTime, m_itemID);

    SaveSkillQueue();
}

PyDict *Character::GetCharInfo() {
    // this is char, skills, implants, boosters.
    if (!pInventory->ContentsLoaded())
        if (!pInventory->LoadContents()) {
            codelog(CHARACTER__ERROR, "%s (%u): Failed to load contents for GetCharInfo", name(), m_itemID);
            return nullptr;
        }

    Rsp_CommonGetInfo_Entry entry1;
    if (!Populate(entry1))
        return nullptr;

    PyDict *result = new PyDict();
    result->SetItem(new PyInt(m_itemID), new PyObject("util.KeyVal", entry1.Encode()));

    //now encode skills...
    std::vector<InventoryItemRef> skills;
    skills.clear();
    //find all the skills contained within ourself.
    pInventory->GetItemsByFlag(flagSkill, skills );
    pInventory->GetItemsByFlag(flagSkillInTraining, skills );
    // godma builds char.implants and char.boosters from these
    GetImplants(skills);
    GetBoosters(skills);

    //encode an entry for each one.
    for (auto cur : skills) {
        Rsp_CommonGetInfo_Entry entry;
        if (cur->Populate(entry)) {
            result->SetItem(new PyInt(cur->itemID()), new PyObject("util.KeyVal", entry.Encode()));
        } else {
            codelog(CHARACTER__ERROR, "%s (%u): Failed to load character item %u for GetCharInfo", name(), m_itemID, cur->itemID());
        }
    }

    return result;
}

PyObject *Character::GetDescription() const {
    util_Row row;
        row.header.push_back("description");
        row.line = new PyList();
        row.line->AddItemString( description().c_str() );
    return row.Encode();
}

void Character::AddItem(InventoryItemRef iRef)
{
    if (iRef.get() == nullptr)
        return;

    InventoryItem::AddItem(iRef);

    _log( CHARACTER__INFO, "%s(%u) has been added to %s with flag %i.", iRef->name(), iRef->itemID(), name(), (uint8)iRef->flag() );
}

void Character::SetActiveShip(uint32 shipID)
{
    m_charData.shipID = shipID;
    m_db.SetCurrentShip(m_itemID, shipID);
}

void Character::SetActivePod(uint32 podID)
{
    m_charData.capsuleID = podID;
    m_db.SetCurrentPod(m_itemID, podID);
}

void Character::ResetClone()
{
    m_db.ChangeCloneType(m_itemID, itemCloneAlpha);
}

void Character::SaveCharacter() {
    _log( CHARACTER__INFO, "Saving character info for %u.", m_itemID );

    // update skill points before save
    GetTotalSP();
    SetLogonMinutes();
    m_db.SaveCharacter(m_itemID, m_charData);
}

void Character::SaveFullCharacter() {
    _log( CHARACTER__INFO, "Saving full character info for %u.", m_itemID );
    //GetTotalSP();
    SaveCharacter();
    m_db.SaveCorpData(m_itemID, m_corpData);
    SaveAttributes();

    if (m_inTraining != nullptr) {
        m_inTraining->SetAttribute(AttrSkillPoints, m_inTraining->GetCurrentSP(this, m_skillQueue.front().startTime), false);
        m_inTraining->SaveItem();
    }

    SaveSkillQueue();
}

void Character::PayBounty(CharacterRef cRef) {
    std::string reason = "Bounty for the killing of ";
    reason += cRef->itemName();
    AccountService::TransferFunds(corpCONCORD, m_itemID, cRef->bounty(), reason, Journal::EntryType::Bounty, cRef->itemID());
    // add data to StatisticMgr
    sStatMgr.Add(Stat::pcBounties, cRef->bounty());
}

void Character::SetLoginTime() {
    m_loginTime = sEntityList.GetStamp();
    m_db.SetLogInTime(m_itemID);
}

uint16 Character::OnlineTime()
{
    double onlineTime = m_charData.loginTime - GetFileTimeNow();
    onlineTime /= 10000000;
    onlineTime -= 11644473600;
    onlineTime /= 60;
    return (uint16)onlineTime;
}

// called on 10m timer from client
void Character::SetLogonMinutes() {
    //  get login time and set _logonMinutes       -allan
    uint16 loginMinutes = (sEntityList.GetStamp() - m_loginTime) /60;

    // some checks are done < 1m, so if this check has no minutes, keep original time and exit
    if (loginMinutes > 0) {
        m_charData.logonMinutes += loginMinutes;
        m_loginTime = sEntityList.GetStamp();
    }
}

// certificate system
bool Character::HasCertificate( uint32 certID ) const {
    CertMap::const_iterator itr = m_certificates.find(certID);
    return (itr != m_certificates.end());
}

void Character::GetCertificates( CertMap &crt ) {
    crt = m_certificates;
}

void Character::GrantCertificate( uint32 certID )
{
    CharCerts cert = CharCerts();
        cert.certificateID = certID;
        cert.grantDate = GetFileTimeNow();
        cert.visibilityFlags = 0;
    m_certificates.emplace(certID, cert);
    m_cdb.AddCertificate(m_itemID, cert);
}

void Character::UpdateCertificate( uint32 certID, bool pub ) {
    m_cdb.UpdateCertificate(m_itemID, certID, pub);
}

void Character::SaveCertificates() {
    _log( CHARACTER__INFO, "Saving Certificates of character %u", m_itemID );
    m_cdb.SaveCertificates( m_itemID, m_certificates );
}

// functions and methods for bookmark system (char mem maps)
/** @todo this will need more thought/work   */
void Character::LoadBookmarks()
{
    // not used yet
}

void Character::SaveBookMarks()
{
    // not used yet
}


// functions and methods for standings system
/** @todo  this needs to be moved to common standings code */
/** @todo  these need to use common standings methods for formulas  */
/** @todo  these need secStatus tests and calcs for negative secStatus */
float Character::GetStandingModified(uint32 fromID, uint32 toID)
{
    if (toID == 0)
        toID = m_itemID;
    float res = StandingDB::GetStanding(fromID, toID);
    if (res < 0.0f) {
        res += ((10.0f + res) * (0.04f * GetSkillLevel(EvESkill::Diplomacy)));
    } else {
        res += ((10.0f - res) * (0.04f * GetSkillLevel(EvESkill::Connections)));
    }
    return res;
}

float Character::GetNPCCorpStanding(uint32 fromID, uint32 toID) {
    if (toID == 0)
        toID = m_itemID;
    float res = StandingDB::GetStanding(fromID, toID);
    if (res < 0.0f) {
        res += ((10.0f + res) * (0.04f * GetSkillLevel(EvESkill::Diplomacy)));
    } else {
        res += ((10.0f - res) * (0.04f * GetSkillLevel(EvESkill::Connections)));
    }
    return res;
}

void Character::SetStanding(uint32 fromID, uint32 toID, float standing) {
    StandingDB::SetStanding(fromID, toID, standing);
    if (m_pClient == nullptr)
        return;
    // standingsvc.OnStandingSet(fromID, toID, standing)
    PyTuple* payload = new PyTuple(3);
        payload->SetItem(0, new PyInt(fromID));
        payload->SetItem(1, new PyInt(toID));
        payload->SetItem(2, new PyFloat(standing));
    m_pClient->SendNotification("OnStandingSet", "charid", payload, false);
}

// for map system
void Character::VisitSystem(uint32 solarSystemID) {
	m_db.VisitSystem(solarSystemID, m_itemID);
}
