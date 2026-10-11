
/**
 * @name Weapon.cpp
 *   Class for POS Weapon Modules.
 *
 * @Author:         Allan
 * @date:   28 December 17
 */

/*
 * POS__ERROR
 * POS__WARNING
 * POS__MESSAGE
 * POS__DUMP
 * POS__DEBUG
 * POS__DESTINY
 * POS__SLIMITEM
 * POS__TRACE
 */


#include "pos/Weapon.h"


/*
 * 752     controlTowerProjectileOptimalBonus  NULL    50
 * 753     controlTowerProjectileFallOffBonus  NULL    50
 * 754     controlTowerProjectileROFBonus  NULL    -25
 * 872     disallowOffensiveModifiers  NULL    1
 */
WeaponSE::WeaponSE(StructureItemRef structure, EVEServiceManager& services, SystemManager* system, const FactionData& data)
: StructureSE(structure, services, system, data),
m_ai(nullptr)
{
    m_ai = new POS_AI(this);
}

WeaponSE::~WeaponSE()
{
    SafeDelete(m_ai);
}

void WeaponSE::SetOffline()
{
    m_ai->Stop();
    StructureSE::SetOffline();
}

void WeaponSE::Killed(Damage &damage)
{
    m_ai->Stop();
    StructureSE::Killed(damage);
}

void WeaponSE::ValidateAddItem(Client* pClient, InventoryItemRef iRef, EVEItemFlags toFlag)
{
    StructureSE::ValidateAddItem(pClient, iRef, toFlag);
    if (pClient == nullptr)
        return;
    // only the battery's charges, into its charge slot
    bool fits(false);
    if ((toFlag == flagHiSlot0) and (iRef->categoryID() == EVEDB::invCategories::Charge)) {
        const uint16 groupAttrs[5] = { AttrChargeGroup1, AttrChargeGroup2, AttrChargeGroup3, AttrChargeGroup4, AttrChargeGroup5 };
        for (int i = 0; i < 5; ++i) {
            EvilNumber group(EvilZero);
            if (m_self->HasAttribute(groupAttrs[i], group) and (group.get_uint32() == iRef->groupID()))
                fits = true;
        }
        EvilNumber size(EvilZero);
        if (fits and m_self->HasAttribute(AttrChargeSize, size) and iRef->HasAttribute(AttrChargeSize)
        and (iRef->GetAttribute(AttrChargeSize).get_uint32() != size.get_uint32()))
            fits = false;
    }
    if (!fits)
        throw UserError("CannotPlaceTypeInStructure")
                .AddFormatValue("container", new PyInt(GetTypeID()))
                .AddFormatValue("type", new PyInt(iRef->typeID()));
}

void WeaponSE::Init()
{
    StructureSE::Init();
}

void WeaponSE::Process()
{
    /* called by EntityList::Process on every loop */
    /*  Enable base call to Process state changes  */
    StructureSE::Process();
    // automatic defense (and manual control)
    if (!m_killed)
        m_ai->Process();
}


/*  small arty battery
 * 9   hp  100000  NULL
 * 30  power   101250  NULL
 * 50  cpu     0   NULL
 * 51  speed   15750   NULL
 * 54  maxRange    187500  NULL
 * 56  chargeRate  NULL    1
 * 64  damageMultiplier    NULL    44
 * 109     kineticDamageResonance  NULL    1
 * 110     thermalDamageResonance  NULL    1
 * 111     explosiveDamageResonance    NULL    1
 * 113     emDamageResonance   NULL    1
 * 128     chargeSize  2   NULL
 * 136     uniformity  NULL    1
 * 154     proximityRange  330000  NULL
 * 156     incapacitationRatio     NULL    0.001
 * 158     falloff     NULL    168000
 * 160     trackingSpeed   NULL    0.00704
 * 182     requiredSkill1  11584   NULL
 * 192     maxLockedTargets    1   NULL
 * 208     scanRadarStrength   NULL    200
 * 209     scanLadarStrength   NULL    200
 * 210     scanMagnetometricStrength   NULL    200
 * 211     scanGravimetricStrength     NULL    200
 * 245     gfxTurretID     NULL    488
 * 263     shieldCapacity  NULL    25000
 * 265     armorHP     375000  NULL
 * 277     requiredSkill1Level     1   NULL
 * 416     entityFlyRange  187500  NULL
 * 466     entityReactionFactor    NULL    0.5
 * 475     entityAttackDelayMin    7500    NULL
 * 476     entityAttackDelayMax    30000   NULL
 * 479     shieldRechargeRate  NULL    2500000
 * 484     shieldUniformity    NULL    0.75
 * 524     armorUniformity     NULL    0.75
 * 525     structureUniformity     NULL    1
 * 552     signatureRadius     NULL    3000
 * 556     anchoringDelay  NULL    5000
 * 564     scanResolution  NULL    250
 * 604     chargeGroup1    83  NULL
 * 620     optimalSigRadius    NULL    125
 * 676     unanchoringDelay    NULL    5000
 * 677     onliningDelay   NULL    120000
 * 691     targetSwitchDelay   60000   NULL
 * 771     ammoCapacity    NULL    10000
 * 872     disallowOffensiveModifiers  1   NULL
 * 1165    controlTowerMinimumDistance     5000    NULL
 * 1167    posPlayerControlStructure   1   NULL
 * 1768    typeColorScheme     NULL    11342
 */

/*  cruise missile battery
 * 9   hp  100000  NULL
 * 30  power   40000   NULL
 * 50  cpu     40  NULL
 * 109     kineticDamageResonance  NULL    1
 * 110     thermalDamageResonance  NULL    1
 * 111     explosiveDamageResonance    NULL    1
 * 113     emDamageResonance   NULL    1
 * 136     uniformity  NULL    1
 * 154     proximityRange  350000  NULL
 * 156     incapacitationRatio     NULL    0.001
 * 182     requiredSkill1  11584   NULL
 * 192     maxLockedTargets    1   NULL
 * 208     scanRadarStrength   NULL    200
 * 209     scanLadarStrength   NULL    200
 * 210     scanMagnetometricStrength   NULL    200
 * 211     scanGravimetricStrength     NULL    200
 * 212     missileDamageMultiplier     NULL    1
 * 263     shieldCapacity  NULL    25000
 * 265     armorHP     375000  NULL
 * 277     requiredSkill1Level     1   NULL
 * 416     entityFlyRange  175000  NULL
 * 466     entityReactionFactor    NULL    0.5
 * 475     entityAttackDelayMin    7500    NULL
 * 476     entityAttackDelayMax    30000   NULL
 * 479     shieldRechargeRate  NULL    2500000
 * 484     shieldUniformity    NULL    0.75
 * 506     missileLaunchDuration   NULL    24000
 * 524     armorUniformity     NULL    0.75
 * 525     structureUniformity     NULL    1
 * 552     signatureRadius     NULL    3000
 * 556     anchoringDelay  NULL    5000
 * 564     scanResolution  NULL    250
 * 604     chargeGroup1    386     NULL
 * 645     missileEntityVelocityMultiplier     NULL    2
 * 646     missileEntityFlightTimeMultiplier   NULL    2.5
 * 676     unanchoringDelay    NULL    5000
 * 677     onliningDelay   NULL    120000
 * 691     targetSwitchDelay   60000   NULL
 * 771     ammoCapacity    NULL    10000
 * 858     missileEntityAoeCloudSizeMultiplier     NULL    0.25
 * 859     missileEntityAoeVelocityMultiplier  NULL    1.5
 * 872     disallowOffensiveModifiers  1   NULL
 * 1165    controlTowerMinimumDistance     5000    NULL
 * 1167    posPlayerControlStructure   1   NULL
 */

/*  torpedo battery
 * 9   hp  100000  NULL
 * 30  power   80000   NULL
 * 50  cpu     80  NULL
 * 109     kineticDamageResonance  NULL    1
 * 110     thermalDamageResonance  NULL    1
 * 111     explosiveDamageResonance    NULL    1
 * 113     emDamageResonance   NULL    1
 * 136     uniformity  NULL    1
 * 154     proximityRange  350000  NULL
 * 156     incapacitationRatio     NULL    0.001
 * 182     requiredSkill1  11584   NULL
 * 192     maxLockedTargets    1   NULL
 * 208     scanRadarStrength   NULL    200
 * 209     scanLadarStrength   NULL    200
 * 210     scanMagnetometricStrength   NULL    200
 * 211     scanGravimetricStrength     NULL    200
 * 212     missileDamageMultiplier     NULL    2.5
 * 263     shieldCapacity  NULL    50000
 * 265     armorHP     750000  NULL
 * 277     requiredSkill1Level     1   NULL
 * 416     entityFlyRange  175000  NULL
 * 466     entityReactionFactor    NULL    0.5
 * 475     entityAttackDelayMin    7500    NULL
 * 476     entityAttackDelayMax    30000   NULL
 * 479     shieldRechargeRate  NULL    5000000
 * 484     shieldUniformity    NULL    0.75
 * 506     missileLaunchDuration   NULL    24000
 * 524     armorUniformity     NULL    0.75
 * 525     structureUniformity     NULL    1
 * 552     signatureRadius     NULL    3000
 * 556     anchoringDelay  NULL    5000
 * 564     scanResolution  NULL    75
 * 604     chargeGroup1    89  NULL
 * 645     missileEntityVelocityMultiplier     NULL    2.5
 * 646     missileEntityFlightTimeMultiplier   NULL    5
 * 676     unanchoringDelay    NULL    5000
 * 677     onliningDelay   NULL    120000
 * 691     targetSwitchDelay   60000   NULL
 * 771     ammoCapacity    NULL    10000
 * 858     missileEntityAoeCloudSizeMultiplier     NULL    0.75
 * 859     missileEntityAoeVelocityMultiplier  NULL    1.5
 * 872     disallowOffensiveModifiers  1   NULL
 * 1165    controlTowerMinimumDistance     5000    NULL
 * 1167    posPlayerControlStructure   1   NULL
 */