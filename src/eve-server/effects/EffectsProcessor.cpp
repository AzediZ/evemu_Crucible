/**
 * @name EffectsProcessor.cpp
 *   This file is for decoding and processing the effect data
 *   Copyright 2017  Alasiya-EVEmu Team
 *
 * @Author:    Allan
 * @date:      24 January 2017
 * @update:     19 June 2019 (revised namespaces)
 *
 */

#include "Client.h"
#include "effects/EffectsActions.h"
#include "effects/EffectsProcessor.h"
#include "inventory/InventoryItem.h"
#include "inventory/Inventory.h"
#include "character/Character.h"
#include "ship/Ship.h"
#include "ship/modules/GenericModule.h"

#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

#include "EVE_DogmaOrder.h"
#include "npc/ai/NpcMath.h"

/*
 * # Effects Logging:
 * EFFECTS=0
 * EFFECTS__ERROR=1
 * EFFECTS__WARNING=0
 * EFFECTS__MESSAGE=0
 * EFFECTS__DEBUG=0
 * EFFECTS__TRACE=0
 */

void FxProc::ParseExpression(InventoryItem* pItem, Expression expression, fxData& data, GenericModule* pMod/*nullptr*/)
{
    double profileStartTime(GetTimeUSeconds());

    if (is_log_enabled(EFFECTS__TRACE) and 0)
        _log(EFFECTS__TRACE, "FxProc::ParseExpression(): container: %s(%u) parsing %s ", \
                pItem->name(), pItem->itemID(), expression.expressionName.c_str());

    bool skill(false);
    switch (data.srcRef->categoryID()) {
        case  EVEDB::invCategories::Skill:
        case  EVEDB::invCategories::Implant: {  // cat::implant also covers grp::booster
            skill = true;
        } break;
    }

    using namespace FX;
    switch(expression.operandID) {
        // these return the given expressionValue
        case Operands::DEFBOOL:   //23  this evaulates to 'true' (Bool(1))
        case Operands::DEFINT: {  //27  this is used as  0,1,2,{raceID}
            //  seems to be called only to online/offline modules (and screws up my Online/Offline code...)
            /*
            int8 value = atoi(expression.expressionValue.c_str());
            if (module) {
                if (value == 0)
                    data.srcRef->PutOffline();
                if (value == 1)
                    data.srcRef->PutOnline(isRig);
                return; // we are done at this point
            } */
        } break;
        case Operands::DEFASSOCIATION: { //21
            data.math = GetAssociationEnum(expression.expressionValue);
            /*
            if (data.math > Math::MaxMathMethod) {
                Operand operand = sFxDataMgr.GetOperand(expression.operandID);
                _log(EFFECTS__ERROR, "FxProc::ParseExpression(): out of range mathOp: %s(%i) for operand %u (%s).", \
                        GetMathMethodName(data.math), data.math, expression.operandID, operand.operandKey.c_str());
            } */
        } break;
        case Operands::DEFENVIDX: {     //24
            data.targLoc = GetEnvironmentEnum(expression.expressionValue);
            /*
            if (data.targLoc > Target::MaxTargLocation) {
                Operand operand = sFxDataMgr.GetOperand(expression.operandID);
                _log(EFFECTS__ERROR, "FxProc::ParseExpression(): out of range targLoc: %i for operand %u (%s).", \
                        data.targAttr, expression.operandID, operand.operandKey.c_str());
            } */
        } break;
        // these provide the given expressionID (attrib/grp)
        case Operands::DEFATTRIBUTE: {  //22
            if (expression.expressionAttributeID) {
                if (data.targAttr) {  // always processed first
                    data.srcAttr = expression.expressionAttributeID;
                } else {
                    data.targAttr = expression.expressionAttributeID;
                }
            } else {
                _log(EFFECTS__ERROR, "FxProc::ParseExpression(): opATTR called with no expressionAttributeID defined");
            }
        } break;
        case Operands::DEFGROUP: {      //26
            data.fxSrc = Source::Group;
            if (expression.expressionGroupID) {
                data.grpID = expression.expressionGroupID;
            } else if (expression.expressionValue != "") {
                ;   // will have to figure out how to do this one.
                _log(EFFECTS__WARNING, "FxProc::ParseExpression(): opGROUP using expressionValue %s called by %s",\
                        expression.expressionValue.c_str(), expression.expressionName.c_str());
            } else {
                _log(EFFECTS__ERROR, "FxProc::ParseExpression(): opGROUP called with no expressionGroupID or expressionValue defined");
            }
        } break;
        case Operands::DEFTYPEID: {     //29
            if (skill)
                data.fxSrc = Source::Skill;
            if (expression.expressionTypeID) {
                data.typeID = expression.expressionTypeID;
            } else if (expression.expressionValue != "") {
                ;   // will have to figure out how to do this one.
                _log(EFFECTS__WARNING, "FxProc::ParseExpression(): opTYPEID using expressionValue %s", expression.expressionValue.c_str());
            } else {
                _log(EFFECTS__ERROR, "FxProc::ParseExpression(): opTYPEID called with no expressionTypeID or expressionValue defined");
            }
        } break;
        case Operands::GETTYPE: { //36, %(arg1)s.GetTypeID()  --used by SRLG in AORSM
            if (!data.typeID)
                data.typeID = data.srcRef->typeID();    // get items on ship that require SkillItem in srcRef
        } break;
        case Operands::GM: {    //37, %(arg1)s.GetModule(%(arg2)s)      --used by subsystems as (GetModule(Ship:201):55)
            /* only 5 subsystem effects use this, e.g. subsystemBonusAmarrEngineering2CapacitorRechargeRate is
             *  CurrentShip.ECM->rechargeRate.  the group (ECM) is a data quirk: the bonus is the ship's (cap recharge time,
             *  as the subsystem's info text says), and the client applies it to the ship.  so only arg1 (the location) is used
             */
            ParseExpression(pItem, sFxDataMgr.GetExpression(expression.arg1), data, pMod);
        } break;
        // do as stated
        case Operands::RSA: {   //64, %(arg1)s.%(arg2)s      -- used by AGRSM
            ParseExpression(pItem, sFxDataMgr.GetExpression(expression.arg1), data, pMod);
            ParseExpression(pItem, sFxDataMgr.GetExpression(expression.arg2), data, pMod);
        } break;
        case Operands::COMBINE: { //17, %(arg1)s); (%(arg2)s      --executes two statements
            ParseExpression(pItem, sFxDataMgr.GetExpression(expression.arg1), data, pMod);
            fxData data1 = fxData();
            data1.action = Action::Invalid;
            data1.srcRef = data.srcRef;
            ParseExpression(pItem, sFxDataMgr.GetExpression(expression.arg2), data1, pMod);
        } break;
        case Operands::LG: {    //48, %(arg1)s.LocationGroup.%(arg2)s  -- specify a group by grpID for a location'  used by ALGM
            ParseExpression(pItem, sFxDataMgr.GetExpression(expression.arg1), data, pMod);   //source
            ParseExpression(pItem, sFxDataMgr.GetExpression(expression.arg2), data, pMod);   //groupID
        } break;
        case Operands::SRLG: {    //49, %(arg1)s.SkillRequiredLocationGroup[%(arg2)s]  --  specify a group by skillID for a location   used by ALRSM and AORSM
            ParseExpression(pItem, sFxDataMgr.GetExpression(expression.arg1), data, pMod);   //source
            ParseExpression(pItem, sFxDataMgr.GetExpression(expression.arg2), data, pMod);   //skillID
            if (!data.fxSrc) {    // fxSrc = Self in this case.  update to remove this hack?
                //_log(EFFECTS__TRACE, "FxProc::ParseExpression(): SRLG: setting fxSrc from %s to Skill for %s.  self: %s", \
                        GetSourceName(data.fxSrc), data.srcRef->name(), (pItem == data.srcRef.get() ? "true" : "false"));
                data.fxSrc = Source::Skill;
            }
        } break;
        case Operands::ATT:     //12, %(arg1)s->%(arg2)s               --(item:attribID)
        case Operands::EFF:     //31, %(arg2)s.%(arg1)s                --define association type
        case Operands::GA:      //34, %(arg1)s.%(arg2)s                --GetAttribute      (no known uses)
        case Operands::GET:     //35, %(arg1)s.%(arg2)s()              --used a lot.  eg. Get(Ship:101) means 'get attribute 101 on ShipItem'
        case Operands::IA: {    //40, %(arg1)s                         --used by AGSM
            ParseExpression(pItem, sFxDataMgr.GetExpression(expression.arg1), data);
            if (expression.arg2)
                ParseExpression(pItem, sFxDataMgr.GetExpression(expression.arg2), data);
        } break;
        // effect function calls.
        // here is where we'll actually add the modifier data to the item's map
        case Operands::AIM:     //6,  AddItemModifier(env,%(arg1)s, %(arg2)s)
        case Operands::AGRSM:   //5,  [%(arg1)s].AGRSM(%(arg2)s)    --AddGangRequiredSkillModifier
        case Operands::AGSM: {  //3,  [%(arg1)s].AGSM(%(arg2)s)        --AddGangShipModifier
            ParseExpression(pItem, sFxDataMgr.GetExpression(expression.arg1), data, pMod);
            ParseExpression(pItem, sFxDataMgr.GetExpression(expression.arg2), data, pMod);
            pItem->AddModifier(data);
        } break;
        case Operands::ALGM:    //7,  (%(arg1)s).AddLocationGroupModifier (%(arg2)s)
        case Operands::ALM:     //8,  (%(arg1)s).AddLocationModifier (%(arg2)s)
        case Operands::ALRSM: { //9,  (%(arg1)s).AddLocationRequiredSkillModifier(%(arg2)s)
            ParseExpression(pItem, sFxDataMgr.GetExpression(expression.arg1), data, pMod);
            ParseExpression(pItem, sFxDataMgr.GetExpression(expression.arg2), data, pMod);
            if ((skill) and (!data.fxSrc))      // fxSrc = Self in this case.  update to remove this hack?
                data.fxSrc = Source::Skill;
            LocationSource(expression.operandID, skill, data);
            pItem->AddModifier(data);
        } break;
        case Operands::AORSM: { //11, (%(arg1)s).AddOwnerRequiredSkillModifier(%(arg2)s)
            /* items owned by the pilot that need the skill: charges (missiles, bombs), drones, probes, and fitted modules
             *  that have the attribute.  skills, implants, ship hulls, rigs and subsystems use this for missile bonuses
             */
            ParseExpression(pItem, sFxDataMgr.GetExpression(expression.arg1), data, pMod);
            ParseExpression(pItem, sFxDataMgr.GetExpression(expression.arg2), data, pMod);
            data.fxSrc = Source::Owner;
            pItem->AddModifier(data);
        } break;
        // remove modifier calls only partially enabled for modules and charges.
        // will implement for implants and boosters when those systems are written.
        case Operands::RIM:     //58, (%(arg1)s).RemoveItemModifier (%(arg2)s)
        case Operands::RGGM:    //54, [%(arg1)s].RemoveGangGroupModifier(%(arg2)s)
        case Operands::RGSM:    //55, [%(arg1)s].RemoveGangShipModifier(%(arg2)s)
        case Operands::RGORSM:  //56, [%(arg1)s].RemoveGangOwnerRequiredSkillModifier(%(arg2)s)
        case Operands::RGRSM: { //57, [%(arg1)s].RemoveGangRequiredSkillModifier(%(arg2)s)
            ParseExpression(pItem, sFxDataMgr.GetExpression(expression.arg1), data, pMod);
            ParseExpression(pItem, sFxDataMgr.GetExpression(expression.arg2), data, pMod);
            pItem->RemoveModifier(data);
        } break;
        case Operands::RLGM:    //59, (%(arg1)s).RemoveLocationGroupModifier (%(arg2)s)
        case Operands::RLM:     //60, (%(arg1)s).RemoveLocationModifier (%(arg2)s)
        case Operands::RLRSM: { //61, (%(arg1)s).RemoveLocationRequiredSkillModifier(%(arg2)s)
            ParseExpression(pItem, sFxDataMgr.GetExpression(expression.arg1), data, pMod);
            ParseExpression(pItem, sFxDataMgr.GetExpression(expression.arg2), data, pMod);
            if ((skill) and (!data.fxSrc))     // fxSrc = Self in this case.  update to remove this hack?
                data.fxSrc = Source::Skill;
            LocationSource(expression.operandID, skill, data);
            pItem->RemoveModifier(data);
        } break;
        case Operands::RORSM: { //62, (%(arg1)s).RemoveOwnerRequiredSkillModifier(%(arg2)s)
            ParseExpression(pItem, sFxDataMgr.GetExpression(expression.arg1), data, pMod);
            ParseExpression(pItem, sFxDataMgr.GetExpression(expression.arg2), data, pMod);
            data.fxSrc = Source::Owner;
            pItem->RemoveModifier(data);
        } break;
        /*
        // next 3 not used here, as they are only used by effect 16 (Online), which is covered in GenericModule class.
        case Operands::OR:     //'%(arg1)s OR %(arg2)s'       -- used with 'if' in arg2 as 'y'.   ((if x then y) OR z)  (used as "else" or elif)
        case Operands::AND:    //'(%(arg1)s) AND (%(arg2)s)'  -- used with 'if' in arg1 as 'x'.   (if (x AND y) then ...)
        case Operands::IF: {    //'If(%(arg1)s), Then (%(arg2)s)'    -- std conditional.  (if x then y)
        } break;
        // trivial attribute operations
        case Operands::ADD: {      //1, (%(arg1)s)+(%(arg2)s)
            // this isnt complete.
            fxData arg1 = fxData();
                arg1.srcRef = data.srcRef;
            ParseExpression(pItem, sFxDataMgr.GetExpression(expression.arg1), arg1, pMod);
            fxData arg2 = fxData();
                arg2.srcRef = data.srcRef;
            ParseExpression(pItem, sFxDataMgr.GetExpression(expression.arg2), arg2, pMod);
            data.result = (arg1.result + arg2.result);
        } break;
        case Operands::GTE: {  //39    %(arg1)s>=%(arg2)s
            fxData arg1 = fxData();
                arg1.srcRef = data.srcRef;
            ParseExpression(pItem, sFxDataMgr.GetExpression(expression.arg1), arg1, pMod);
            fxData arg2 = fxData();
                arg2.srcRef = data.srcRef;
            ParseExpression(pItem, sFxDataMgr.GetExpression(expression.arg2), arg2, pMod);
            //  this needs work
            //if (arg1.srcRef->GetAttribute(arg1.srcAttr) >= arg2.targLoc->GetAttribute(arg2.targAttr))
            //    data.result = true;

        } break;
        case Operands::GT: {   //38    %(arg1)s> %(arg2)s

        } break;

        case Operands::UE: {   //73    UserError(%(arg1)s)
            // not using this yet.
        } break;
        case Operands::SKILLCHECK: {   //67    SkillCheck(%(arg1)s)
            //data.result = true;
        } break;
        // module action method calls...not used.
        case Operands::ATTACK: // 13,
        case Operands::CARGOSCAN: // 14,
        case Operands::CHEATTELEDOCK: // 15,
        case Operands::CHEATTELEGATE: // 16,
        case Operands::DECLOAKWAVE: // 19,
        case Operands::ECMBURST: // 30,
        case Operands::EMPWAVE: // 32,
        case Operands::LAUNCH: // 44,
        case Operands::LAUNCHDEFENDERMISSILE: // 45,
        case Operands::LAUNCHDRONE: // 46,
        case Operands::LAUNCHFOFMISSILE: // 47,
        case Operands::MINE: // 50,
        case Operands::POWERBOOST: // 53,
        case Operands::SHIPSCAN: // 66,
        case Operands::SURVEYSCAN: // 69,
        case Operands::TARGETHOSTILES: // 70,
        case Operands::TARGETSILENTLY: // 71,
        case Operands::TOOLTARGETSKILLS: // 72,
        case Operands::SPEEDBOOST: {   //75    EVEmu-specific operand to apply modified speed attribs to destiny variables and update bubble
            data.action = expression.operandID;
           // pItem->AddModifier(data);
        } break;
        default: {              // in case the op hasnt been defined, make a note here
            if (is_log_enabled(EFFECTS__UNDEFINED)) {
                std::ostringstream ret;
                Operand operand = sFxDataMgr.GetOperand(expression.operandID);
                ret << "Operand id:" << expression.operandID << " key:" << operand.operandKey;
                if (operand.format.empty()) {
                    ret << " - has not been defined.";
                } else {                // % {'arg1': arg1, 'arg2': arg2, 'value': expression.expressionValue}
                    ret << " - should be added as " << operand.format.c_str();
                }
                _log(EFFECTS__UNDEFINED, "FxProc::ParseExpression() - %s", ret.str().c_str());
            }
        } break;
        */
    }

    if (sConfig.debug.UseProfiling)
        sProfiler.AddTime(Profile::parseFX, GetTimeUSeconds() - profileStartTime);
}

void FxProc::LocationSource(uint16 operandID, bool skill, fxData& data)
{
    using namespace FX;
    switch (operandID) {
        case Operands::ALM:
        case Operands::RLM: {
            /* a location modifier on the ship is on everything fitted to it:  the modules (and charges) that have the attribute.
             *  heat damage and overload bonuses (Thermodynamics, subsystems, strategic cruisers, red giants) are module attribs */
            if (data.targLoc == Target::Ship)
                data.fxSrc = Source::Ship;
        } break;
        case Operands::ALRSM:
        case Operands::RLRSM: {
            /* only skills and implants work on the character's skills.  anything else that names CurrentChar[skill] means
             *  the pilot's items that need the skill (the wolf-rayet beacons' systemStandardMissileKineticDamage) */
            if (!skill and (data.targLoc == Target::Char) and (data.fxSrc == Source::Skill) and (data.typeID != 0))
                data.fxSrc = Source::Owner;
        } break;
    }
}

bool FxProc::IsCharItemModifier(const fxData& data)
{
    using namespace FX;
    if (data.targLoc == Target::Self)
        return true;
    if (data.targLoc != Target::Char)
        return false;
    // implant/booster groups on the character, and skills that modify other skills
    if (data.fxSrc == Source::Group)
        return true;
    if ((data.fxSrc == Source::Skill) and (data.typeID != 0))
        return true;
    return false;
}

bool FxProc::IsFittedModifier(const fxData& data)
{
    using namespace FX;
    switch (data.fxSrc) {
        case Source::Group:     return (data.targLoc != Target::Char);
        case Source::Owner:
        case Source::Ship:      return true;
        case Source::Skill:     return (((data.targLoc == Target::Ship) and (data.typeID != 0)) or (data.targLoc == Target::Charge));
    }
    return false;
}

bool FxProc::WritesOwnItem(const fxData& data, Character* pChar, ShipItem* pShip)
{
    using namespace FX;
    if (data.srcRef.get() == nullptr)
        return false;
    // group, owner, location and module-by-skill modifiers go on other items
    if ((data.fxSrc == Source::Group) or (data.fxSrc == Source::Owner) or (data.fxSrc == Source::Ship))
        return false;
    if ((data.fxSrc == Source::Skill) and (data.targLoc != Target::Self))
        return false;
    switch (data.targLoc) {
        case Target::Self:
            return true;
        case Target::Ship:
            return ((pShip != nullptr) and (static_cast<InventoryItem*>(pShip) == data.srcRef.get()));
        case Target::Char:
            return ((pChar != nullptr) and (static_cast<InventoryItem*>(pChar) == data.srcRef.get()));
    }
    return false;
}

void FxProc::ApplyEffects(InventoryItem* pItem, Character* pChar, ShipItem* pShip, bool update/*false*/, int8 pass/*FX::Pass::All*/)
{
    double profileStartTime(GetTimeUSeconds());
    using namespace FX;
    //uint8 action = Action::dgmActInvalid;
    // the modifiers in math order, then made to read their own item's attributes after those are changed (EVE_DogmaOrder.h)
    std::vector<std::pair<int8, fxData>> mods;
    std::vector<DogmaOrder::Modifier> nodes;
    mods.reserve(pItem->GetModifiers().size());
    nodes.reserve(pItem->GetModifiers().size());
    for (auto& cur : pItem->GetModifiers()) {
        if (pass == Pass::Fitted) {
            if (!IsFittedModifier(cur.second))
                continue;
        } else if (pass != Pass::All) {
            if (IsCharItemModifier(cur.second) != (pass == Pass::CharItems))
                continue;
        }
        mods.push_back(cur);
        DogmaOrder::Modifier node = DogmaOrder::Modifier();
        if (cur.second.srcRef.get() != nullptr) {
            node.srcItem = cur.second.srcRef->itemID();
            node.srcAttr = cur.second.srcAttr;
            node.targAttr = cur.second.targAttr;
            node.writesSrc = WritesOwnItem(cur.second, pChar, pShip);
            node.remove = cur.second.remove;
        }
        nodes.push_back(node);
    }
    for (std::size_t idx : DogmaOrder::Order(nodes)) {
        std::pair<int8, fxData> cur(mods[idx]);  // k,v of assoc, data<math, src, targLoc, targAttr, srcAttr, grpID, typeID>
        /*
        if (cur.second.action) {
            action = cur.second.action;
            continue;
        } */
        //InventoryItemRef srcItemRef = cur.second.srcRef;

        std::vector<InventoryItemRef> itemRefVec;
        // affected target depends on source.  get source and target(s) here.
        switch (cur.second.fxSrc) {
            case Source::Group: {     // not a source per se, but defines effect's target selection requirements
                if (cur.second.targLoc == Target::Char) {
                    // implants and boosters of groupID defined in 'grpID' (implant set bonuses, booster skills)
                    if (pChar == nullptr)
                        continue;
                    std::vector<InventoryItemRef> augs;
                    pChar->GetImplants(augs);
                    pChar->GetBoosters(augs);
                    for (auto aug : augs)
                        if (aug->groupID() == cur.second.grpID)
                            itemRefVec.push_back(aug);
                    if (itemRefVec.empty())
                        continue;   // no implant or booster of that group.  nbd
                    break;
                }
                if (pShip == nullptr)
                    continue;
                // this is to apply modifiers to ship's modules of groupID defined in 'grpID'
                std::vector<InventoryItemRef> moduleList;
                pShip->GetModuleManager()->GetModuleListOfRefsAsc(moduleList);
                for (auto mod : moduleList)
                    if (mod->groupID() == cur.second.grpID)
                        itemRefVec.push_back(mod);
            } break;
            case Source::Skill: {    // source of this effect is skill, implant, or booster
                if (cur.second.typeID == EVEDB::invTypes::Invalid) {    //invalid
                    _log(EFFECTS__WARNING, "FxProc::ApplyEffects(): Source::Skill - typeID is invalid");
                    continue;  // make error here
                }
                switch (cur.second.targLoc) {
                    //  apply the modifier to ...
                    case Target::Self: {
                        // ... item itself
                        itemRefVec.push_back(cur.second.srcRef);
                    } break;
                    case Target::Ship:  {
                        if (cur.second.typeID) {
                            // ... ship's modules that require skillID defined in "typeID"
                            pShip->GetModuleManager()->GetModuleListByReqSkill(cur.second.typeID, itemRefVec);
                        } else {
                            // ... ship that require skill in 'srcRef'
                            if (pShip->HasReqSkill(cur.second.srcRef->typeID()))
                                itemRefVec.push_back(static_cast<InventoryItemRef>(pShip));
                        }
                    } break;
                    case Target::Char: {
                        if (cur.second.typeID) {
                            // ... char skills that require skill in 'srcRef' or defined in 'typeID'
                            std::vector<InventoryItemRef> allSkills;
                            pChar->GetSkillsList(allSkills);
                            for (auto curSkill : allSkills)
                                if (curSkill->HasReqSkill(cur.second.typeID))
                                    itemRefVec.push_back(curSkill);
                        } else {
                            // ... character itself
                            itemRefVec.push_back(static_cast<InventoryItemRef>(pChar));
                        }
                    } break;
                    case Target::Other: {
                        // ... ship from 'core' pilot skills (electronics, mechanics, navigation, etc)
                        itemRefVec.push_back(static_cast<InventoryItemRef>(pShip));
                    } break;
                    case Target::Charge: {
                        // ... charges
                        // will need more testing to verify this.
                        std::map<EVEItemFlags, InventoryItemRef> charges;
                        pShip->GetModuleManager()->GetLoadedCharges(charges);
                        for (auto mod : charges)
                            if (mod.second->HasReqSkill(cur.second.typeID))
                                itemRefVec.push_back(mod.second);
                    } break;
                    case Target::Target: {
                        // ... current target (focused, volatile...removed on 'invalid target')
                        itemRefVec.push_back(pShip->GetTargetRef());
                    } break;
                    case Target::Invalid: {   // null
                        _log(EFFECTS__WARNING, "FxProc::ApplyEffects(): Source::Skill target location invalid.");
                        continue;
                    } break;
                    case Target::Area:          //not used
                    case Target::PowerCore:     //defined but not used
                    default: {
                        _log(EFFECTS__ERROR, "FxProc::ApplyEffects(): Source::Skill target undefined - %s.", GetTargLocName(cur.second.targLoc));
                    } break;
                }
            } break;
            case Source::Self: {  // source is module or charge
                //  apply the modifier to ...
                switch (cur.second.targLoc) {
                    case Target::Char: {
                        // ... character itself
                        itemRefVec.push_back(static_cast<InventoryItemRef>(pChar));
                    } break;
                    case Target::Ship:  {
                        // ... the ship the calling item is located in/on
                        itemRefVec.push_back(static_cast<InventoryItemRef>(pShip));
                    } break;
                    case Target::Self: {
                        // ... item itself
                        itemRefVec.push_back(cur.second.srcRef);
                    } break;
                    case Target::Charge: {
                        // ... charge on src item (from module)
                        if (cur.second.srcRef->flag() == flagNone) {
                            _log(EFFECTS__ERROR, "FxProc::ApplyEffects(): SourceItem.flag is flagNone but need actual flag to acquire module.");
                            _log(EFFECTS__ERROR, "FxProc::ApplyEffects(): Item Data for %s(%u) - src(%s:%u)  targ(%s:%u) .", \
                                    cur.second.srcRef->name(), cur.second.srcRef->itemID(), GetSourceName(cur.second.fxSrc), cur.second.srcAttr,  \
                                    GetTargLocName(cur.second.targLoc), cur.second.targAttr);
                            EvE::traceStack();
                            continue;
                        }
                        itemRefVec.push_back(pShip->GetModuleManager()->GetLoadedChargeOnModule(cur.second.srcRef->flag()));
                    } break;
                    case Target::Other: {
                        // ... module containing the src item (from charge)
                        if (cur.second.srcRef->flag() == flagNone) {
                            _log(EFFECTS__ERROR, "FxProc::ApplyEffects(): SourceItem.flag is flagNone but need actual flag to acquire module.");
                            _log(EFFECTS__ERROR, "FxProc::ApplyEffects(): Item Data for %s(%u) - src(%s:%u)  targ(%s:%u) .", \
                                    cur.second.srcRef->name(), cur.second.srcRef->itemID(), GetSourceName(cur.second.fxSrc), cur.second.srcAttr,  \
                                    GetTargLocName(cur.second.targLoc), cur.second.targAttr);
                            EvE::traceStack();
                            continue;
                        }
                        // a charge's modifier whose charge isn't in a module any more (unloaded, used up) has nothing to modify
                        GenericModule* pOther(pShip->GetModuleManager()->GetModule(cur.second.srcRef->flag()));
                        if (pOther == nullptr) {
                            _log(EFFECTS__ERROR, "FxProc::ApplyEffects(): %s(%u) in %s is not in a module.", \
                                    cur.second.srcRef->name(), cur.second.srcRef->itemID(), sDataMgr.GetFlagName(cur.second.srcRef->flag()));
                            continue;
                        }
                        itemRefVec.push_back(pOther->GetSelf());
                    } break;
                    case Target::Target: {
                        // ... current target (focused, volatile...removed on 'invalid target')
                        itemRefVec.push_back(pShip->GetTargetRef());
                    } break;
                    case Target::Invalid: {
                        _log(EFFECTS__ERROR, "FxProc::ApplyEffects(): Source::Self target invalid.");
                    } break;
                    case Target::Area:          // not used
                    case Target::PowerCore:     //defined but not used
                    default: {
                        _log(EFFECTS__ERROR, "FxProc::ApplyEffects(): Source::Self target undefined - %s.", GetTargLocName(cur.second.targLoc));
                    } break;
                }
            } break;
            case Source::Owner: {     // owner-required-skill modifier
                /* fitted modules and loaded charges that need the skill in 'typeID' and have the attribute.
                 *  (the attribute test keeps missile bonuses off the launchers.)  launched missiles copy the loaded charge;
                 *  charges loaded in space, probes and bombs get these from ApplyOwnerModifiers()
                 */
                if (cur.second.typeID == 0)
                    continue;
                std::vector<InventoryItemRef> moduleList;
                pShip->GetModuleManager()->GetModuleListByReqSkill(cur.second.typeID, moduleList);
                for (auto mod : moduleList)
                    if (mod->HasAttribute(cur.second.targAttr))
                        itemRefVec.push_back(mod);
                std::map<EVEItemFlags, InventoryItemRef> charges;
                pShip->GetModuleManager()->GetLoadedCharges(charges);
                for (auto charge : charges)
                    if ((charge.second.get() != nullptr)
                    and charge.second->HasReqSkill(cur.second.typeID)
                    and charge.second->HasAttribute(cur.second.targAttr))
                        itemRefVec.push_back(charge.second);
                if (itemRefVec.empty())
                    continue;   // nothing fitted or loaded uses this skill.  nbd
            } break;
            /** @todo this needs more work */
            case Source::Gang: {      // source is a gang leader skill
                switch (cur.second.targLoc) {
                //  apply the modifier to ...
                    case Target::Self: {
                        // ... item itself
                        itemRefVec.push_back(cur.second.srcRef);
                    } break;
                /** @note  these are processed and applied in fleet code
                    case Target::Ship:  {
                        // ... ship of member to apply leader's skill bonuses to
                        itemRefVec.push_back(static_cast<InventoryItemRef>(pShip));
                    } break;
                    case Target::Char:  {
                        // ... our character
                        itemRefVec.push_back(static_cast<InventoryItemRef>(pChar));
                    } break;
                    */
                    default: {
                        _log(EFFECTS__ERROR, "FxProc::ApplyEffects(): Source::Gang target undefined - %s.", GetTargLocName(cur.second.targLoc));
                    } break;
                }
            } break;
            case Source::Invalid: {
                _log(EFFECTS__ERROR, "FxProc::ApplyEffects(): source location invalid.");
                continue;
            } break;
            case Source::Ship: {    // location modifier on the ship (LocationSource())
                if (pShip == nullptr)
                    continue;
                std::vector<InventoryItemRef> moduleList;
                pShip->GetModuleManager()->GetModuleListOfRefsAsc(moduleList);
                for (auto mod : moduleList)
                    if (mod->HasAttribute(cur.second.targAttr))
                        itemRefVec.push_back(mod);
                std::map<EVEItemFlags, InventoryItemRef> charges;
                pShip->GetModuleManager()->GetLoadedCharges(charges);
                for (auto charge : charges)
                    if ((charge.second.get() != nullptr) and charge.second->HasAttribute(cur.second.targAttr))
                        itemRefVec.push_back(charge.second);
                if (itemRefVec.empty())
                    continue;   // nothing fitted has the attribute.  nbd
            } break;
            // the parser never produces this (subsystem effects are Self/Skill/Group, target effects use Target::Target)
            case Source::Target:
            default: {
                _log(EFFECTS__DEBUG, "FxProc::ApplyEffects(): %s uses source location %s.  ignored.", cur.second.srcRef->name(), GetSourceName(cur.second.fxSrc));
                continue;
            } break;
        }

        if (itemRefVec.empty())
            if ((cur.second.typeID == 0)
            and (cur.second.grpID == 0)) {
                // only concerned when typeID/grpID is 0.  when either are populated, ship dont have that module.  nbd
                _log(EFFECTS__WARNING, "FxProc::ApplyEffects(%i): %s(%u): target item vector empty.", \
                        cur.first, cur.second.srcRef->name(), cur.second.srcRef->itemID());
                _log(EFFECTS__WARNING, "Data:  src(%s:%u), targ(%s:%u), grpID:%u, typeID:%u, math:%s.", \
                        GetSourceName(cur.second.fxSrc), cur.second.srcAttr, \
                        GetTargLocName(cur.second.targLoc), cur.second.targAttr, \
                        cur.second.grpID, cur.second.typeID, GetMathMethodName(cur.first));
                continue;
            }

        // get srcAttr
        EvilNumber srcValue = cur.second.srcRef->GetAttribute(cur.second.srcAttr);
        /*
        // check for inf/nan and then reset?  this will fuck up all previous fx processing on this value.
        if (srcValue.isNaN() or srcValue.isInf()) {
            srcValue = cur.second.srcRef->GetDefaultAttribute(cur.second.srcAttr);
            _log(EFFECTS__ERROR, "FxProc::ApplyEffects(): srcValue isInf or isNaN.  Data: %s(%u) - src(%s:%u) set to %.3f.", \
            cur.second.srcRef->name(), cur.second.srcRef->itemID(), GetSourceName(cur.second.fxSrc), cur.second.srcAttr, srcValue.get_float());
        } */

        /* modifiers that can't be undone by math (assignments, x0, -100%) save the target's value first.
         *  removing such a modifier puts that value back.  RemoveModifier() has already inverted the math of removes,
         *  so any remove looks for a snapshot first
         */
        bool irreversible(false);
        switch (cur.first) {
            case Math::PreAssignment:
            case Math::PostAssignment: {
                irreversible = true;
            } break;
            case Math::PostPercent: {
                irreversible = (srcValue == -100);
            } break;
            case Math::PreMul:
            case Math::PostMul: {
                irreversible = (srcValue == EvilZero);
            } break;
        }
        uint32 srcID(cur.second.srcRef->itemID());

        // set target attr to modified value
        EvilNumber targValue(EvilZero), newValue(EvilZero);
        for (auto item : itemRefVec) {
            if (item.get() == nullptr)  // still occasional nulls in the vector (segfaults)
                continue;
            if (cur.second.remove) {
                EvilNumber snapshot(EvilZero);
                if (item->TakeFxSnapshot(cur.second.targAttr, srcID, snapshot)) {
                    _log(EFFECTS__MESSAGE, "FxProc::ApplyEffects(%i): %s(%u) removed.  %s on %s restored to %.3f.", \
                        cur.first, cur.second.srcRef->name(), srcID, sDataMgr.GetAttrName(cur.second.targAttr), item->name(), snapshot.get_float());
                    item->SetAttribute(cur.second.targAttr, snapshot, update);
                    continue;
                }
            } else if (irreversible) {
                item->SaveFxSnapshot(cur.second.targAttr, srcID, item->GetAttribute(cur.second.targAttr));
            }
            // get targAttr
            targValue = item->GetAttribute(cur.second.targAttr);
            // check for inf/nan and then reset?  this will fuck up all previous fx processing on this value.
            // but it will allow continuing w/o error in subsequent processing
            if (targValue.isNaN() or targValue.isInf()) {
                targValue = item->GetDefaultAttribute(cur.second.targAttr);
                _log(EFFECTS__ERROR, "FxProc::ApplyEffects(): targValue isInf or isNaN.  Data: %s(%u) - src(%s:%u) %.3f <%s> targ(%s:%u) set targ to %.3f.", \
                        cur.second.srcRef->name(), cur.second.srcRef->itemID(), GetSourceName(cur.second.fxSrc), cur.second.srcAttr, srcValue.get_float(), \
                        GetMathMethodName(cur.first), GetTargLocName(cur.second.targLoc), cur.second.targAttr, targValue.get_float());
            }

            switch (cur.first) {
                case Math::PreMul:
                case Math::PostMul:
                case Math::PreDiv:
                case Math::PostDiv:
                case Math::PostPercent:
                case Math::RevPostPercent: {
                    /* dogma:  an attribute the item doesn't have is the attribute's default (damageMultiplier 1, the damage
                     *  types 0).  one it has at 0 stays 0, so a multiplier can be taken off again (rockets' missing damage
                     *  types, a smartbomb's other damage types) */
                    if (!item->HasAttribute(cur.second.targAttr)) {
                        targValue = sDataMgr.GetAttributeDefault(cur.second.targAttr);
                        if (targValue == EvilZero)
                            continue;   // still the default:  nothing to write
                    }
                } break;
            }

            // send data to calculator
            if (!ApplyPenalized(item.get(), cur.second, cur.first, srcValue.get_double(), targValue, newValue))
                newValue = CalculateAttributeValue(targValue, srcValue, cur.first);

            if (is_log_enabled(EFFECTS__MESSAGE))
                _log(EFFECTS__MESSAGE, "FxProc::ApplyEffects(%i): %s(%u) - src(%s:%s[%u])=%.3f <%s> targ(%s:%s[%u]) set %s from %.3f to %.3f.", \
                    cur.first, cur.second.srcRef->name(), cur.second.srcRef->itemID(), \
                    GetSourceName(cur.second.fxSrc), sDataMgr.GetAttrName(cur.second.srcAttr), cur.second.srcAttr, srcValue.get_float(), GetMathMethodName(cur.first), \
                    GetTargLocName(cur.second.targLoc), sDataMgr.GetAttrName(cur.second.targAttr), cur.second.targAttr, item->name(), targValue.get_float(), newValue.get_float());

            // set new calculated value for target attribute
            // update is used to send attrib changes to client when changing module states while in space, but NOT for pilot login. (client acts funky)
            item->SetAttribute(cur.second.targAttr, newValue, update);
            newValue = EvilZero;
        }
    }
    /*  not used
    if (action)
        sFxAct.DoAction(action, pShip->GetPilot()->GetShipSE());   // this MUST be called AFTER all active effects are applied, as it uses those modified values
     */

    // the CharItems pass leaves the map for the Rest pass, and the Fitted pass leaves the modifiers it didn't apply
    if (pass == Pass::Fitted) {
        std::multimap<int8, fxData>& modifiers(pItem->GetModifiers());
        for (auto itr = modifiers.begin(); itr != modifiers.end(); )
            if (IsFittedModifier(itr->second)) {
                itr = modifiers.erase(itr);
            } else {
                ++itr;
            }
    } else if (pass != Pass::CharItems) {
        pItem->ClearModifiers();
    }

    if (sConfig.debug.UseProfiling)
        sProfiler.AddTime(Profile::applyFX, GetTimeUSeconds() - profileStartTime);
}

void FxProc::ApplyOwnerModifiers(InventoryItemRef target, Character* pChar, ShipItem* pShip)
{
    /* an item that wasn't there when the ship's effects were processed (a charge loaded in space, a launched probe or bomb)
     *  gets the owner-required-skill modifiers (AORSM) of everything that gives them: the pilot's skills, implants and
     *  boosters, the ship hull, and the fitted modules (rigs, subsystems, online and active modules).
     *  the sources' expressions are parsed into the target's (scratch) modifier map, and only the Owner entries that apply
     *  to the target are used.  source values are read as they are now (skill levels and other bonuses already applied)
     */
    if ((target.get() == nullptr) or (pChar == nullptr) or (pShip == nullptr))
        return;
    double profileStartTime(GetTimeUSeconds());

    // the quantity attribute carries the loaded charge's stack size to the client (OMAC), keep it
    EvilNumber quantity(EvilZero);
    bool hasQuantity(target->HasAttribute(AttrQuantity, quantity));
    target->ResetAttributes();
    if (hasQuantity)
        target->SetAttribute(AttrQuantity, quantity, false);
    target->ClearModifiers();

    std::vector<std::pair<InventoryItemRef, std::vector<int8>>> sources;
    std::vector<int8> all = {FX::State::Passive, FX::State::Active, FX::State::Target, FX::State::Online, FX::State::Overloaded};
    std::vector<int8> passive = {FX::State::Passive};
    std::vector<InventoryItemRef> items;
    pChar->GetSkillsList(items);
    pChar->GetMyInventory()->GetItemsByFlag(flagImplant, items);
    pChar->GetMyInventory()->GetItemsByFlag(flagBooster, items);
    for (auto cur : items)
        sources.push_back(std::make_pair(cur, passive));
    sources.push_back(std::make_pair(static_cast<InventoryItemRef>(pShip), all));
    // the system's effects (w-space phenomena:  missile damage and velocity)
    if (pShip->GetSystemFx().get() != nullptr) {
        std::vector<int8> system = {FX::State::System};
        sources.push_back(std::make_pair(pShip->GetSystemFx(), system));
    }
    items.clear();
    pShip->GetModuleManager()->GetModuleListOfRefsAsc(items);
    for (auto cur : items) {
        GenericModule* pMod(pShip->GetModuleManager()->GetModule(cur->flag()));
        if ((pMod == nullptr) or !pMod->isOnline())
            continue;
        std::vector<int8> states = {FX::State::Passive, FX::State::Online};
        if (pMod->IsActive())
            states.push_back(FX::State::Active);
        sources.push_back(std::make_pair(cur, states));
    }

    for (auto& src : sources) {
        for (auto state : src.second) {
            std::map<uint16, Effect> effectMap;
            src.first->type().GetEffectMap(state, effectMap);
            for (auto& fx : effectMap) {
                // boosters' side effects that didn't fire  (F15)
                if (!pChar->IsAugmentationEffectActive(src.first.get(), fx.second))
                    continue;
                fxData data = fxData();
                data.action = FX::Action::Invalid;
                data.srcRef = src.first;
                ParseExpression(target.get(), sFxDataMgr.GetExpression(fx.second.preExpression), data);
            }
        }
    }

    // apply in math order, as ApplyEffects() does
    for (auto cur : target->GetModifiers()) {
        if (cur.second.fxSrc != FX::Source::Owner)
            continue;
        if ((cur.second.typeID == 0) or !target->HasReqSkill(cur.second.typeID) or !target->HasAttribute(cur.second.targAttr))
            continue;
        EvilNumber srcValue(cur.second.srcRef->GetAttribute(cur.second.srcAttr));
        EvilNumber targValue(target->GetAttribute(cur.second.targAttr));
        EvilNumber newValue(EvilZero);
        if (!ApplyPenalized(target.get(), cur.second, cur.first, srcValue.get_double(), targValue, newValue))
            newValue = CalculateAttributeValue(targValue, srcValue, cur.first);
        if (is_log_enabled(EFFECTS__MESSAGE))
            _log(EFFECTS__MESSAGE, "FxProc::ApplyOwnerModifiers(%i): %s(%u) - src %s=%.3f <%s> set %s on %s from %.3f to %.3f.", \
                cur.first, cur.second.srcRef->name(), cur.second.srcRef->itemID(), sDataMgr.GetAttrName(cur.second.srcAttr), \
                srcValue.get_float(), GetMathMethodName(cur.first), sDataMgr.GetAttrName(cur.second.targAttr), target->name(), \
                targValue.get_float(), newValue.get_float());
        target->SetAttribute(cur.second.targAttr, newValue, false);
    }
    target->ClearModifiers();

    if (sConfig.debug.UseProfiling)
        sProfiler.AddTime(Profile::applyFX, GetTimeUSeconds() - profileStartTime);
}

namespace {
    // the penalised product of a chain:  bonuses and penalties are ranked apart
    double PenalizedProduct(const std::map<int64, double>& chain)
    {
        std::vector<double> up, down;
        for (auto& cur : chain)
            (cur.second > 1.0 ? up : down).push_back(cur.second);
        return NpcMath::StackedProduct(up.data(), up.size()) * NpcMath::StackedProduct(down.data(), down.size());
    }
}

bool FxProc::ApplyPenalized(InventoryItem* pItem, const fxData& data, int8 math, double srcValue, EvilNumber targValue,
                            EvilNumber& newValue)
{
    if ((pItem == nullptr) or (data.srcRef.get() == nullptr))
        return false;
    // the modifier's own math method (a remove's is inverted)
    int8 base(math);
    if (data.remove) {
        switch (math) {
            case FX::Math::PreDiv:          base = FX::Math::PreMul;        break;
            case FX::Math::PreMul:          base = FX::Math::PreDiv;        break;
            case FX::Math::PostDiv:         base = FX::Math::PostMul;       break;
            case FX::Math::PostMul:         base = FX::Math::PostDiv;       break;
            case FX::Math::RevPostPercent:  base = FX::Math::PostPercent;   break;
            default:                        return false;
        }
    }
    double multiplier(0.0);
    switch (base) {
        case FX::Math::PreMul:
        case FX::Math::PostMul:     multiplier = srcValue;                                      break;
        case FX::Math::PreDiv:
        case FX::Math::PostDiv:     multiplier = ((srcValue == 0.0) ? 0.0 : (1.0 / srcValue));  break;
        case FX::Math::PostPercent: multiplier = 1.0 + (srcValue / 100.0);                      break;
        default:                    return false;
    }
    // modules and rigs only, on attributes that aren't stackable
    if ((data.srcRef->categoryID() != EVEDB::invCategories::Module) or sDataMgr.IsAttributeStackable(data.targAttr))
        return false;

    std::map<int64, double>& chain(pItem->GetPenalizedChain(data.targAttr, base));
    int64 key(((int64)data.srcRef->itemID() << 16) | data.srcAttr);
    std::map<int64, double>::iterator itr(chain.find(key));
    if (data.remove) {
        // one that wasn't applied through the chain (x0, -100%:  undone from its snapshot) is removed as it was applied
        if (itr == chain.end())
            return false;
    } else if (!(multiplier > 0.0) or !std::isfinite(multiplier)) {
        return false;
    }

    double before(PenalizedProduct(chain));
    if (data.remove) {
        chain.erase(itr);
    } else {
        chain[key] = multiplier;
    }
    double after(PenalizedProduct(chain));
    newValue = targValue * EvilNumber(after / before);
    if (is_log_enabled(EFFECTS__DEBUG))
        _log(EFFECTS__DEBUG, "FxProc::ApplyPenalized(): %s(%u) %s x%.4f on %s %s(%u):  chain of %u, penalised product %.4f -> %.4f, %.4f -> %.4f", \
                data.srcRef->name(), data.srcRef->itemID(), data.remove ? "removes" : "adds", multiplier, sDataMgr.GetAttrName(data.targAttr), \
                pItem->name(), pItem->itemID(), (uint32)chain.size(), before, after, targValue.get_double(), newValue.get_double());
    return true;
}

EvilNumber FxProc::CalculateAttributeValue(EvilNumber val1/*targ*/, EvilNumber val2/*src*/, int8 method)
{
    switch (method) {
        case FX::Math::SkillCheck:
        case FX::Math::PreAssignment:
        case FX::Math::PostAssignment:
            return val2;
        case FX::Math::PreMul:
        case FX::Math::PostMul:
            return val1 * val2;
        case FX::Math::PreDiv:
        case FX::Math::PostDiv:
            if (val2 == EvilZero)
                return val1;
            return val1 / val2;
        case FX::Math::ModAdd:
            return val1 + val2;
        case FX::Math::ModSub:
            return val1 - val2;
        case FX::Math::PostPercent:
            if (val2 == -100)
                return 0;
            return val1 * (1.0f + (val2 / 100.0f));
        case FX::Math::RevPostPercent:
            if (val2 == -100)
                return 0; //val1
            return val1 / (1.0f + (val2 / 100.0f));
        case FX::Math::Invalid:
            _log(EFFECTS__WARNING, "FxProc::CalculateNewAttributeValue() - Invalid Association used");
            return val1;
    }
    _log(EFFECTS__ERROR, "FxProc::CalculateNewAttributeValue() - Unknown Association used: %i", (int8)method);
    return val1;
}

int8 FxProc::GetAssociationEnum(const std::string& association)
{   // opID 21
    if (association == "PreAssignment") {
        return FX::Math::PreAssignment;
    } else if (association == "PreDiv") {
        return FX::Math::PreDiv;
    } else if (association == "PreMul") {
        return FX::Math::PreMul;
    } else if (association == "ModAdd") {
        return FX::Math::ModAdd;
    } else if (association == "ModSub") {
        return FX::Math::ModSub;
    } else if (association == "PostPercent") {
        return FX::Math::PostPercent;
    } else if (association == "PostMul") {
        return FX::Math::PostMul;
    } else if (association == "PostDiv") {
        return FX::Math::PostDiv;
    } else if (association == "PostAssignment") {
        return FX::Math::PostAssignment;
    } else if (association == "SkillCheck") {
        return FX::Math::SkillCheck;
    } else if (association == "AddRate") {
        return FX::Math::AddRate;
    } else if (association == "SubRate") {
        return FX::Math::SubRate;
    } else {
        return FX::Math::Invalid;  //throw std::bad_typeid();
    }
}

int8 FxProc::GetEnvironmentEnum(const std::string& env)
{   // opID 24
    if (env == "Self") {
        return FX::Target::Self;
    } else if (env == "Char") {
        return FX::Target::Char;
    } else if (env == "Ship") {
        return FX::Target::Ship;
    } else if (env == "Target") {
        return FX::Target::Target;
    } else if (env == "Area") {
        return FX::Target::Area;
    } else if (env == "Other") {
        return FX::Target::Other;
    } else if (env == "Charge") {
        return FX::Target::Charge;
    } else {
        return FX::Target::Invalid;  //throw std::bad_typeid();
    }
}

const char* FxProc::GetMathMethodName(int8 id)
{
    switch (id) {
        case FX::Math::PreAssignment:  return "PreAssignment";
        case FX::Math::PreDiv:         return "PreDiv";
        case FX::Math::PreMul:         return "PreMul";
        case FX::Math::ModAdd:         return "ModAdd";
        case FX::Math::ModSub:         return "ModSub";
        case FX::Math::PostPercent:    return "PostPercent";
        case FX::Math::RevPostPercent: return "RevPostPercent";
        case FX::Math::PostMul:        return "PostMul";
        case FX::Math::PostDiv:        return "PostDiv";
        case FX::Math::PostAssignment: return "PostAssignment";
        case FX::Math::SkillCheck:     return "SkillCheck";
        case FX::Math::AddRate:        return "AddRate";
        case FX::Math::SubRate:        return "SubRate";
        case FX::Math::Invalid:
        default:                       return "Invalid";
    }
}

const char* FxProc::GetSourceName(int8 id)
{
    switch (id) {
        case FX::Source::Self:         return "Self";
        case FX::Source::Skill:        return "Skill";
        case FX::Source::Ship:         return "Ship";
        case FX::Source::Owner:        return "Owner";
        case FX::Source::Gang:         return "Gang";
        case FX::Source::Group:        return "Group";
        case FX::Source::Target:       return "Target";
        case FX::Source::Invalid:
        default:                       return "Invalid";
    }
}

const char* FxProc::GetTargLocName(int8 id)
{
    switch (id) {
        case FX::Target::Self:         return "Self";
        case FX::Target::Char:         return "Char";
        case FX::Target::Ship:         return "Ship";
        case FX::Target::Target:       return "Target";
        case FX::Target::Area:         return "Area";
        case FX::Target::Other:        return "Other";
        case FX::Target::Charge:       return "Charge";
        case FX::Target::Invalid:
        default:                       return "Invalid";
    }
}

const char* FxProc::GetStateName(int8 id)
{
    switch (id) {
        case FX::State::Passive:       return "Passive";
        case FX::State::Active:        return "Active";
        case FX::State::Target:        return "Target";
        case FX::State::Area:          return "Area";
        case FX::State::Online:        return "Online";
        case FX::State::Overloaded:    return "Overload";
        case FX::State::Dungeon:       return "Dungeon";
        case FX::State::System:        return "System";
        case FX::State::Invalid:
        default:                       return "Invalid";
    }
}

void FxProc::DecodeEffects( const uint16 fxID ) {
    sLog.Green("DecodeEffects", "fxID %u", fxID);
    EvaluateExpression(sFxDataMgr.GetEffect(fxID).preExpression, "Pre");
    EvaluateExpression(sFxDataMgr.GetEffect(fxID).postExpression, "Post");
}

void FxProc::EvaluateExpression(const uint16 expID, const char* type)
{
    fxData data = fxData();
    Expression expression = sFxDataMgr.GetExpression(expID);
    DecodeExpression(expression, data);
    std::string name(expression.description);
    if (name.empty())
        name = expression.expressionName;
    if (name.empty())
        name = "No Name";
    sLog.Green("EvaluateExpression", "expID %u: %s", expID, name.c_str());
    sLog.Cyan("EvaluateExpression", " %s - <%s> src(%s:%u) targ(%s:%u)", \
            type, GetMathMethodName(data.math), \
            GetSourceName(data.fxSrc), data.srcAttr,  \
            GetTargLocName(data.targLoc), data.targAttr);
}

void FxProc::DecodeExpression(Expression expression, fxData& data)
{
    using namespace FX;
    switch(expression.operandID) {
        // these return the given expressionValue
        case Operands::DEFBOOL:   //23  this evaulates to 'true' (Bool(1))
        case Operands::DEFINT: {  //27  this is used as  0,1,2,{raceID}
        } break;
        case Operands::DEFASSOCIATION: { //21
            data.math = GetAssociationEnum(expression.expressionValue);
        } break;
        case Operands::DEFENVIDX: {     //24
            data.targLoc = GetEnvironmentEnum(expression.expressionValue);
        } break;
        // these provide the given expressionID (attrib/grp)
        case Operands::DEFATTRIBUTE: {  //22
            if (expression.expressionAttributeID) {
                if (data.targAttr) { // always processed first
                    data.srcAttr = expression.expressionAttributeID;
                } else {
                    data.targAttr = expression.expressionAttributeID;
                }
            }
        } break;
        case Operands::DEFGROUP: {      //26
            data.fxSrc = Source::Group;
            if (expression.expressionGroupID)
                data.grpID = expression.expressionGroupID;
        } break;
        case Operands::DEFTYPEID: {     //29
            if (expression.expressionTypeID)
                data.typeID = expression.expressionTypeID;
        } break;
        case Operands::GETTYPE: { //36, %(arg1)s.GetTypeID()  --used by SRLG in AORSM
            if (!data.typeID)   // verify this isnt set yet (dont overwrite)
                data.typeID = 0; //data.srcRef->typeID();    // get items on ship that require SkillItem in srcRef
        } break;
        // do as stated
        case Operands::COMBINE: { //17, %(arg1)s); (%(arg2)s      --executes two statements
            DecodeExpression(sFxDataMgr.GetExpression(expression.arg1), data);
            fxData data1 = fxData();
            data1.action = Action::Invalid;
            data1.srcRef = data.srcRef;
            DecodeExpression(sFxDataMgr.GetExpression(expression.arg2), data1);
        } break;
        case Operands::LG: {    //48, %(arg1)s.LocationGroup.%(arg2)s  -- specify a group by grpID for a location'  used by ALGM
            DecodeExpression(sFxDataMgr.GetExpression(expression.arg1), data);   //source
            DecodeExpression(sFxDataMgr.GetExpression(expression.arg2), data);   //groupID
        } break;
        case Operands::SRLG: {    //49, %(arg1)s.SkillRequiredLocationGroup[%(arg2)s]  --  specify a group by skillID for a location   used by ALRSM and AORSM
            DecodeExpression(sFxDataMgr.GetExpression(expression.arg1), data);   //source
            DecodeExpression(sFxDataMgr.GetExpression(expression.arg2), data);   //skillID
        } break;
        case Operands::ATT:     //12, %(arg1)s->%(arg2)s               --(item:attribID)
        case Operands::EFF:     //31, %(arg2)s.%(arg1)s                --define association type
        case Operands::GA:      //34, %(arg1)s.%(arg2)s                --GetAttribute      (no known uses)
        case Operands::GET:     //35, %(arg1)s.%(arg2)s()              --used a lot.  eg. Get(Ship:101) means 'get attribute 101 on ShipItem'
        case Operands::IA: {    //40, %(arg1)s                         --used by AGSM
            DecodeExpression(sFxDataMgr.GetExpression(expression.arg1), data);
            if (expression.arg2)
                DecodeExpression(sFxDataMgr.GetExpression(expression.arg2), data);
        } break;
        // effect function calls.
        // here is where we'll actually add the modifier data to the item's map
        case Operands::AIM:     //6,  AddItemModifier(env,%(arg1)s, %(arg2)s)
        case Operands::ALGM:    //7,  (%(arg1)s).AddLocationGroupModifier (%(arg2)s)
        case Operands::ALM:     //8,  (%(arg1)s).AddLocationModifier (%(arg2)s)
        case Operands::ALRSM:   //9,  (%(arg1)s).AddLocationRequiredSkillModifier(%(arg2)s)
        case Operands::AORSM:   //11, (%(arg1)s).AddOwnerRequiredSkillModifier(%(arg2)s)
        case Operands::AGRSM:   //5,  [%(arg1)s].AGRSM(%(arg2)s)    --AddGangRequiredSkillModifier
        case Operands::AGSM:    //3,  [%(arg1)s].AGSM(%(arg2)s)        --AddGangShipModifier
        case Operands::GM:      //37, %(arg1)s.GetModule(%(arg2)s)      --used by subsystems as (GetModule(Ship:201):55)
        case Operands::RSA:     //64, %(arg1)s.%(arg2)s      -- used by AGRSM
        case Operands::RIM:     //58, (%(arg1)s).RemoveItemModifier (%(arg2)s)
        case Operands::RGGM:    //54, [%(arg1)s].RemoveGangGroupModifier(%(arg2)s)
        case Operands::RGSM:    //55, [%(arg1)s].RemoveGangShipModifier(%(arg2)s)
        case Operands::RGORSM:  //56, [%(arg1)s].RemoveGangOwnerRequiredSkillModifier(%(arg2)s)
        case Operands::RGRSM:   //57, [%(arg1)s].RemoveGangRequiredSkillModifier(%(arg2)s)
        case Operands::RLGM:    //59, (%(arg1)s).RemoveLocationGroupModifier (%(arg2)s)
        case Operands::RLM:     //60, (%(arg1)s).RemoveLocationModifier (%(arg2)s)
        case Operands::RLRSM:   //61, (%(arg1)s).RemoveLocationRequiredSkillModifier(%(arg2)s)
        case Operands::RORSM: { //62, (%(arg1)s).RemoveOwnerRequiredSkillModifier(%(arg2)s)
            DecodeExpression(sFxDataMgr.GetExpression(expression.arg1), data);
            DecodeExpression(sFxDataMgr.GetExpression(expression.arg2), data);
        } break;
    }
}
