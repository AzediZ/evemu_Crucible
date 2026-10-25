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
Author: Bloody.Rabbit
Rewrite:    Allan
*/

#include "eve-server.h"

#include "character/Character.h"
#include "character/Skill.h"
#include "inventory/AttributeEnum.h"

/*
 * SKILL__ERROR
 * SKILL__WARNING
 * SKILL__MESSAGE
 * SKILL__INFO
 * SKILL__DEBUG
 * SKILL__TRACE
 */

Skill::Skill(uint32 _skillID, const ItemType& _type, const ItemData& _data)
: InventoryItem(_skillID, _type, _data)
{
}

SkillRef Skill::Load( uint32 skillID)
{
    return InventoryItem::Load<Skill>(skillID );
}

SkillRef Skill::Spawn( ItemData &data)
{
    uint32 skillID(InventoryItem::CreateItemID(data));
    if ( skillID == 0 )
        return SkillRef(nullptr);

    SkillRef skillRef(Skill::Load(skillID));
    if (skillRef.get() == nullptr) {
        // make error msg here for failure to load skill?
        return SkillRef(nullptr);
    }

    skillRef->SaveItem();
    return skillRef;
}

bool Skill::IsTraining() {
    return (flag() == flagSkillInTraining);
}

uint8 Skill::GetLevelForSP(uint32 currentSP) {
    return EvEMath::Skill::LevelForPoints(currentSP, GetAttribute(AttrSkillTimeConstant).get_float());
}

uint32 Skill::GetSPForLevel(uint8 level) {
    return EvEMath::Skill::PointsAtLevel(level, GetAttribute(AttrSkillTimeConstant).get_float());
}

uint32 Skill::GetCurrentSP(Character* ch, int64 startTime/*0*/)
{
    uint32 currentSP(GetAttribute(AttrSkillPoints).get_uint32());
    if (flag() == flagSkill)
        return currentSP;

    if (startTime == 0)
        return currentSP;

    if (startTime > GetFileTimeNow())
        return currentSP;

    uint8 level = GetAttribute(AttrSkillLevel).get_uint32() + 1;
    if (level > EvESkill::MAXSKILLLEVEL)
        level = EvESkill::MAXSKILLLEVEL;

    // at this point, the skill is in training.  calculate accumulated sp (client: spm * elapsed minutes, unrounded)
    double elapsedSec(double(GetFileTimeNow() - startTime) / EvE::Time::Second);
    uint32 delta((uint32)(elapsedSec * ch->GetSPPerMin(this) / 60.0));
    currentSP += delta;
    // never accrue past the level being trained
    uint32 maxSP(GetSPForLevel(level));
    if (currentSP > maxSP)
        currentSP = maxSP;

    _log(SKILL__TRACE, "Skill::GetCurrentSP() for %s is %u - delta: %u, elapsed time: %.0fs", \
            name(), currentSP, delta, elapsedSec);

    return currentSP;
}

uint32 Skill::GetRemainingSP(Character* ch, int64 curTime/*0*/)
{
    uint8 level = GetAttribute(AttrSkillLevel).get_uint32() + 1;
    if (level > EvESkill::MAXSKILLLEVEL)
        return 0;

    if (curTime == 0)
        curTime = GetFileTimeNow();

    // remaining sp = what is still to be trained at the current rate until the end of training
    double timeLeft(double(ch->GetEndOfTraining() - curTime) / EvE::Time::Second);
    if (timeLeft <= 0)
        return 0;
    uint32 remainingSP((uint32)ceil(timeLeft * ch->GetSPPerMin(this) / 60.0));
    uint32 maxRemaining(GetSPForLevel(level) - GetAttribute(AttrSkillPoints).get_uint32());
    return std::min(remainingSP, maxRemaining);
}

uint32 Skill::GetTrainingTime(Character* ch, int64 startTime/*0*/)
{
    uint8 level = GetAttribute(AttrSkillLevel).get_uint32() + 1;
    if (level > EvESkill::MAXSKILLLEVEL)
        return 0;

    float spm(ch->GetSPPerMin(this));
    if (spm <= 0)
        return 0;
    // get full sp needed for next level
    uint32 remainingSP(GetSPForLevel(level) - GetAttribute(AttrSkillPoints).get_uint32());
    // divide by spm to get minutes and convert to seconds (unrounded, like the client)
    double timeLeft(double(remainingSP) / spm * 60.0);

    if (startTime == 0)
        return (uint32)ceil(timeLeft);

    double delta(double(GetFileTimeNow() - startTime) / EvE::Time::Second);
    if (delta <= 0)
        return (uint32)ceil(timeLeft);
    if (delta >= timeLeft)
        return 0;

    // this skill is currently training.  subtract accumulated time from total and return
    return (uint32)ceil(timeLeft - delta);
}

void Skill::VerifyAttribs()
{
    if (!isSingleton())
        ChangeSingleton(true);
    /* a float 0.0 equals an int 0, so SetAttribute() kept the float:  an untrained skill (injected, or every skill a new character
     *  gets) loads its type's float 0.0, and the client's skill queue window broke on it ("integer argument expected, got float")
     */
    if (GetAttribute(AttrSkillLevel).get_type() != evil_number_int) {
        _log(SKILL__INFO, "Skill %s level type != int.  Fixing...", name());
        GetAttributeMap()->ReplaceAttribute(AttrSkillLevel, EvilNumber((int64)GetAttribute(AttrSkillLevel).get_uint32()));
    }
    if (GetAttribute(AttrSkillPoints).get_type() != evil_number_int) {
        _log(SKILL__INFO, "Skill %s sp type != int.  Fixing...", name());
        GetAttributeMap()->ReplaceAttribute(AttrSkillPoints, EvilNumber((int64)GetAttribute(AttrSkillPoints).get_uint32()));
    }
}

void Skill::VerifySP()
{
    uint32 spCurrent(GetAttribute(AttrSkillPoints).get_uint32());
    uint32 level(GetAttribute(AttrSkillLevel).get_uint32());
    if (is_log_enabled(SKILL__MESSAGE))
        _log(SKILL__MESSAGE, "Begin SP check for %s. level %u: CurrentSP: %u", name(), level, spCurrent);

    if (spCurrent == 0)
        return;

    if (level > EvESkill::MAXSKILLLEVEL) {
        level = EvESkill::MAXSKILLLEVEL;
        SetAttribute(AttrSkillLevel, level, false);
    }
    // sp below the saved level: the level stands, the sp come up to it
    uint32 spThisLevel(GetSPForLevel(level));
    if (spCurrent < spThisLevel) {
        _log(SKILL__WARNING, "Skill %s points low.  Updating from %u to %u", name(), spCurrent, spThisLevel);
        spCurrent = spThisLevel;
        SetAttribute(AttrSkillPoints, spCurrent, false);
    }
    // never more than level 5 sp
    uint32 spMax(GetSPForLevel(EvESkill::MAXSKILLLEVEL));
    if (spCurrent > spMax) {
        _log(SKILL__WARNING, " %s - Skillpoints high for L5. Updating SP from %u to %u.", name(), spCurrent, spMax);
        spCurrent = spMax;
        SetAttribute(AttrSkillPoints, spCurrent, false);
    }
    // sp reaching a higher level (exactly on the boundary counts) raise the level
    uint8 spLevel(GetLevelForSP(spCurrent));
    if (spLevel > level) {
        _log(SKILL__WARNING, " %s - Skillpoints high. Updating level from %u to %u (%u sp).", name(), level, spLevel, spCurrent);
        SetAttribute(AttrSkillLevel, spLevel, false);
    }
}

bool Skill::SkillPrereqsComplete(Character &ch) {
    bool test(true);
    EvilNumber skillID(0);
    if (HasAttribute(AttrRequiredSkill1, skillID)) {
        if (GetAttribute(AttrRequiredSkill1Level) > ch.GetSkillLevel(skillID.get_uint32()))
            test = false;
        if (HasAttribute(AttrRequiredSkill2, skillID)) {
            if (GetAttribute(AttrRequiredSkill2Level) > ch.GetSkillLevel(skillID.get_uint32()))
                test = false;
            if (HasAttribute(AttrRequiredSkill3, skillID)) {
                if (GetAttribute(AttrRequiredSkill3Level) > ch.GetSkillLevel(skillID.get_uint32()))
                    test = false;
                if (HasAttribute(AttrRequiredSkill4, skillID)) {
                    if (GetAttribute(AttrRequiredSkill4Level) > ch.GetSkillLevel(skillID.get_uint32()))
                        test = false;
                }
            }
        }
    }

    return test;
}

void Skill::GetMissingPrereqs(Character &ch, std::vector<std::pair<uint16, uint8>>& missing)
{
    static const uint16 reqAttrs[6][2] = {
        {AttrRequiredSkill1, AttrRequiredSkill1Level}, {AttrRequiredSkill2, AttrRequiredSkill2Level},
        {AttrRequiredSkill3, AttrRequiredSkill3Level}, {AttrRequiredSkill4, AttrRequiredSkill4Level},
        {AttrRequiredSkill5, AttrRequiredSkill5Level}, {AttrRequiredSkill6, AttrRequiredSkill6Level}
    };
    EvilNumber skillID(0);
    for (uint8 i = 0; i < 6; ++i) {
        if (!HasAttribute(reqAttrs[i][0], skillID))
            continue;
        if (skillID.get_uint32() == 0)
            continue;
        uint8 level(GetAttribute(reqAttrs[i][1]).get_uint32());
        if (ch.GetSkillLevel(skillID.get_uint32()) < level)
            missing.push_back(std::make_pair((uint16)skillID.get_uint32(), level));
    }
}

bool Skill::FitModuleSkillCheck(InventoryItemRef iRef, CharacterRef cRef) {
    bool test(true);
    EvilNumber skillID(0);
    if (iRef->HasAttribute(AttrRequiredSkill1, skillID)) {//Primary Skill
        if ( iRef->GetAttribute(AttrRequiredSkill1Level) > cRef->GetSkillLevel(skillID.get_uint32()))
            test = false;
        if (iRef->HasAttribute(AttrRequiredSkill2, skillID)) {//Secondary Skill
            if ( iRef->GetAttribute(AttrRequiredSkill2Level) > cRef->GetSkillLevel(skillID.get_uint32()))
                test = false;
            if (iRef->HasAttribute(AttrRequiredSkill3, skillID)) {//Tertiary Skill
                if ( iRef->GetAttribute(AttrRequiredSkill3Level) > cRef->GetSkillLevel(skillID.get_uint32()))
                    test = false;
                if (iRef->HasAttribute(AttrRequiredSkill4, skillID)) {//Quarternary Skill
                    if ( iRef->GetAttribute(AttrRequiredSkill4Level) > cRef->GetSkillLevel(skillID.get_uint32()))
                        test = false;
                    if (iRef->HasAttribute(AttrRequiredSkill5, skillID)) {//Quinary Skill
                        if ( iRef->GetAttribute(AttrRequiredSkill5Level) > cRef->GetSkillLevel(skillID.get_uint32()))
                            test = false;
                        if (iRef->HasAttribute(AttrRequiredSkill6, skillID)) {//Senary Skill
                            if ( iRef->GetAttribute(AttrRequiredSkill6Level) > cRef->GetSkillLevel(skillID.get_uint32()))
                                test = false;
                        }
                    }
                }
            }
        }
    }

    return test;
}