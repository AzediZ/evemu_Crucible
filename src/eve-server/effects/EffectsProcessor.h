/**
 * @name EffectsProcessor.h
 *   This file is for decoding and processing the effect data
 *   Copyright 2017  Alasiya-EVEmu Team
 *
 * @Author:    Allan
 * @date:      24 January 2017
 *
 */


#ifndef _EVE_FX_PROC_H__
#define _EVE_FX_PROC_H__

#include "effects/EffectsDataMgr.h"
#include "inventory/InventoryItem.h"
#include "effects/fxData.h"

class Character;
class GenericModule;
class ShipItem;

class FxProc
: public Singleton<FxProc>
{
public:
    FxProc()        { /* do nothing here */ }
    ~FxProc()       { /* do nothing here */ }

    // pItem is modifier container.  pass = FX::Pass  (CharItems/Rest split the character's modifiers, see IsCharItemModifier.  Fitted, see IsFittedModifier)
    void            ApplyEffects(InventoryItem* pItem, Character* pChar, ShipItem* pShip, bool update=false, int8 pass=0);
    // modifiers whose targets are the character's own items (skills, implants, boosters)
    static bool     IsCharItemModifier(const fxData& data);
    // the modifier changes the ship's fitted modules or loaded charges (a hull bonus to launcher powergrid need, ...)
    static bool     IsFittedModifier(const fxData& data);
    /* the modifier changes an attribute of the item it comes from (its source item).  ApplyEffects applies these before the
     *  modifiers of that item reading the attribute (EVE_DogmaOrder.h) */
    static bool     WritesOwnItem(const fxData& data, Character* pChar, ShipItem* pShip);
    // location modifiers:  ALM on the ship means its fitted items, and CurrentChar[skill] from a non-skill means the pilot's items
    static void     LocationSource(uint16 operandID, bool skill, fxData& data);
    /* applies the owner-required-skill modifiers (missile/probe/drone bonuses from skills, implants, boosters, the hull and
     *  fitted modules) to an item that wasn't there when the ship's effects were processed: a charge loaded in space,
     *  a launched probe or bomb.  resets the target's attributes first */
    void            ApplyOwnerModifiers(InventoryItemRef target, Character* pChar, ShipItem* pShip);
    // pItem is modifier container
    // pMod is not used
    void            ParseExpression(InventoryItem* pItem, Expression expression, fxData& data, GenericModule* pMod=nullptr);
    int8            GetEnvironmentEnum(const std::string& domain);
    int8            GetAssociationEnum(const std::string& association);

    const char*     GetSourceName(int8 id);
    const char*     GetMathMethodName(int8 id);
    const char*     GetTargLocName(int8 id);
    const char*     GetStateName(int8 id);

    EvilNumber      CalculateAttributeValue(EvilNumber val1, EvilNumber val2, /*FX::Math*/int8 method);

    /* stacking penalties (dogma).  a module's (and rig's) multiplier on an attribute that isn't stackable is penalised:
     *  PreMul, PreDiv, PostMul, PostDiv and PostPercent each have their own chain per item and attribute (a damage control's
     *  PreMul resists don't stack with the hardeners' PostPercent ones), bonuses and penalties are ranked apart, strongest
     *  first, and the n-th one counts 0.5 ^ ((n / 2.22292081)^2).  skills, implants, boosters, the hull, subsystems, charges
     *  and system effects aren't penalised.
     *  the modifier joins the item's chain for its attribute and math method, or leaves it on a remove (math is the map key,
     *  inverted by InventoryItem::RemoveModifier), and the value moves by the change of the chain's penalised product.
     *  false when the modifier isn't penalised:  it is applied as it is */
    bool            ApplyPenalized(InventoryItem* pItem, const fxData& data, int8 math, double srcValue, EvilNumber targValue,
                                   EvilNumber& newValue);

    void DecodeEffects(const uint16 fxID);
protected:
    void EvaluateExpression(const uint16 expID, const char* type);
    void DecodeExpression(Expression expression, fxData& data);

private:

};

#define sFxProc \
( FxProc::get() )

#endif  // _EVE_FX_PROC_H__

/** @todo  list of possible improvements
 * populate data.typeID with the skillID of skills that affect modules on location(ship)  (better conditional)
 *
 *
 *
 */

/*
            if operand.operandID in (
             const.operandALRSM,
             const.operandRLRSM,
             const.operandALGM,
             const.operandRLGM,
             const.operandAORSM,
             const.operandRORSM):
                return 'dogmaLM.%s(%s, %s, %s, %s, itemID, %s)' % (funcName,
                 arg1[0],
                 arg1[1][0][0],
                 arg1[1][0][1],
                 arg1[1][1],
                 arg2)
            if operand.operandID in (const.operandAGRSM, const.operandRGRSM):
                return 'dogmaLM.%s(shipID, %s, %s, %s, itemID, %s)' % (funcName,
                 arg1[0],
                 arg1[1][0],
                 arg1[1][1],
                 arg2)
            if operand.operandID in (const.operandAGIM, const.operandRGIM):
                return 'dogmaLM.%s(shipID, %s, %s, itemID, %s)' % (funcName,
                 arg1[0],
                 arg1[1],
                 arg2)
            if operand.operandID in (const.operandALM, const.operandRLM):
                return 'dogmaLM.%s(%s, %s, %s, itemID, %s)' % (funcName,
                 arg1[0],
                 arg1[1][0],
                 arg1[1][1],
                 arg2)

*/

/*
    def AddOwnerRequiredSkillModifier(self, env, arg1, arg2):
        affectingModuleID = env.itemID
        affectingAttributeID = arg2
        affectedOwnerID = arg1[1][0][0]
        affectedSkillID = arg1[1][0][1]
        affectedAttributeID = arg1[1][1]
        affectType = arg1[0]

    def AddLocationRequiredSkillModifier(self, env, arg1, arg2):
        affectingModuleID = env.itemID
        affectingAttributeID = arg2
        affectedLocationID = arg1[1][0][0]
        affectedSkillID = arg1[1][0][1]
        affectedAttributeID = arg1[1][1]
        affectType = arg1[0]

    def AddGangRequiredSkillModifier(self, env, arg1, arg2):
        affectingModuleID = env.itemID
        affectingAttributeID = arg2
        affectingCharID = env.charID
        affectingShipID = env.shipID
        affectedSkillID = arg1[1][0]
        affectedAttributeID = arg1[1][1]
        affectType = arg1[0]

    def AddGangGroupModifier(self, env, arg1, arg2):
        affectingModuleID = env.itemID
        affectingAttributeID = arg2
        affectingCharID = env.charID
        affectingShipID = env.shipID
        affectedGroupID = arg1[1][0]
        affectedAttributeID = arg1[1][1]
        affectType = arg1[0]

    def AddGangShipModifier(self, env, arg1, arg2):
        affectingModuleID = env.itemID
        affectingAttributeID = arg2
        affectingCharID = env.charID
        affectingShipID = env.shipID
        affectedAttributeID = arg1[1]
        affectType = arg1[0]

    def AddItemModifier(self, env, arg1, arg2):
        affectingModuleID = env.itemID
        if not arg2:
            raise RuntimeError('Expression is wrong.  AIM(%s, %s) - NULL/None value is not allowed' % (arg1, arg2))
        affectingAttributeID = arg2
        affectedModuleID = arg1[1][0]
        affectedAttributeID = arg1[1][1]
        affectType = arg1[0]
        if affectedModuleID == 0:
            return 1
        if affectedModuleID is None:
            self.LogWarn('AffectedModuleID is None.  Env:', env)
            return

    def AddLocationModifier(self, env, arg1, arg2):
        affectingModuleID = env.itemID
        affectingAttributeID = arg2
        affectedLocationID = arg1[1][0]
        affectedAttributeID = arg1[1][1]
        affectType = arg1[0]

    def AddLocationGroupModifier(self, env, arg1, arg2):
        affectingModuleID = env.itemID
        affectingAttributeID = arg2
        affectedLocationID = arg1[1][0][0]
        affectedGroupID = arg1[1][0][1]
        affectedAttributeID = arg1[1][1]
        affectType = arg1[0]

*/