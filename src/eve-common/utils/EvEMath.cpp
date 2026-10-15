//////////////////////////////////////////////////////////////////////////////////////////
//
// EVE Math Equations for in-game features
// (pulled directly from client code and http://wiki.eve-id.net/Equations)
//
// Latest Update:       Allan  13Nov20
//
//////////////////////////////////////////////////////////////////////////////////////////

#include "../eve-common.h"

#include <cmath>

double EvEMath::Units::MetersToLightYears(double meters) {
    return meters / 9460730472580800;
}

// skill Equations
uint32 EvEMath::Skill::PointsAtLevel(uint8 level, float rank)
{
    // client skillUtil.GetSPForLevelRaw: 250 * rank * 2^(2.5 * (level - 1)), rounded up.  level 0 is 0 sp
    if (level < 1)
        return 0;
    if (level > EvESkill::MAXSKILLLEVEL)
        level = EvESkill::MAXSKILLLEVEL;
    double ret(double(rank) * EvESkill::skillPointMultiplier * std::pow(2.0, 2.5 * (level - 1)));
    return (uint32)std::ceil(ret);
}

uint8 EvEMath::Skill::LevelForPoints(uint32 currentSP, float rank)
{
    /* the highest level whose PointsAtLevel() the sp reach.  this used to be log(sp / (250 * rank)) / DIVCONSTANT + 1
     *  with an integer division and float rounding, which came out a level low at (and just past) most level boundaries
     *  (rank 1: 1415 sp gave level 1, 8000 gave 2, 45255 gave 3) */
    if (rank <= 0)
        return 0;
    for (uint8 level = EvESkill::MAXSKILLLEVEL; level > 0; --level)
        if (currentSP >= PointsAtLevel(level, rank))
            return level;
    return 0;
}

float EvEMath::Skill::PointsPerMinute(float pAttr, float sAttr)
{
    return (pAttr + (sAttr / 2.0f));
}

int64 EvEMath::Skill::StartTime(uint32 currentSP, uint32 nextSP, float SPMin, int64 timeNow)
{
    if ((SPMin <= 0) or (currentSP >= nextSP))
        return timeNow;
    return (timeNow - (int64)(double(nextSP - currentSP) / SPMin * EvE::Time::Minute));
}

int64 EvEMath::Skill::EndTime(uint32 currentSP, uint32 nextSP, float SPMin, int64 timeNow)
{
    if (currentSP >= nextSP)
        return 0;
    if (SPMin <= 0)
        return timeNow;
    return (timeNow + (int64)(double(nextSP - currentSP) / SPMin * EvE::Time::Minute));
}


//RAM Equations
int32 EvEMath::RAM::ProductionTime(uint32 BaseTime, float bpProductivityModifier, float ProductionLevel, float TimeModifier/*1*/ )
{
    float PE_Factor(0.0f);
    if (ProductionLevel >= 0.0f)
        PE_Factor = (ProductionLevel / (1.0f + ProductionLevel));
    else
        PE_Factor = (ProductionLevel - 1.0f);

    float effModifier(1.0f);
    effModifier -= ((bpProductivityModifier / BaseTime) * PE_Factor);
    return (BaseTime * effModifier * TimeModifier);
}

int32 EvEMath::RAM::ResearchTime(uint32 BaseTime, int32 Runs, float CharModifier/*1*/, float SlotModifier/*1*/)
{
    /* pre-Crius ME and PE research:  every level takes the blueprint's base time (Crius made later levels take longer).
     *  CharModifier is the character's mineralNeedResearchSpeed (ME) or manufacturingTimeResearchSpeed (PE):  Metallurgy or
     *  Research take 5% a level off, Beancounter implants more */
    return ClampTime((double)BaseTime * CharModifier * SlotModifier * Runs);
}

int32 EvEMath::RAM::CopyTime(uint32 BaseTime, uint32 CopyRuns, uint32 MaxRuns, float CharModifier/*1*/, float SlotModifier/*1*/)
{
    /* a copy's time is in proportion to its runs, the base time being for a copy of the blueprint's max runs.  CharModifier is the
     *  character's copySpeedPercent (2, Science takes 5% a level off, Beancounter implants more) */
    if (MaxRuns < 1)
        MaxRuns = 1;
    return ClampTime((double)BaseTime * CharModifier * SlotModifier * CopyRuns / MaxRuns);
}

int32 EvEMath::RAM::ClampTime(double seconds)
{
    // research and copy times of high levels or many runs pass int32 (RamMethods::ProductionTimeCheck turns them down)
    if (seconds > 2147483647.0)
        return 2147483647;
    if (seconds < 0.0)
        return 0;
    return (int32)seconds;
}

int32 EvEMath::RAM::InventionTime(uint32 BaseTime, float SlotModifier/*1*/, float ImplantModifier/*1*/ )
{
    // no skill shortens invention (Advanced Laboratory Operation only adds a research slot)
     return BaseTime * SlotModifier * ImplantModifier;
}

float EvEMath::RAM::ME_EffectOnWaste( float MaterialAmount, float BaseWasteFactor, float MaterialEfficiency )
{
    float ME_Factor(0.0f);
    if (MaterialEfficiency >= 0.0f)
        ME_Factor = 1.0f / (1.0f + MaterialEfficiency);
    else
        ME_Factor = 1.0f - MaterialEfficiency;

    return (floor(0.5f + (MaterialAmount * (BaseWasteFactor / 100.0f) * ME_Factor)));
}

uint32 EvEMath::RAM::PerfectME(uint32 MaterialAmount, uint8 BaseWasteFactor)
{
    return floor(0.02f * BaseWasteFactor * MaterialAmount);
}

float EvEMath::RAM::ResearchPointsPerDay( float Multiplier, float AgentEffectiveQuality, uint8 CharSkillLevel, uint8 AgentSkillLevel )
{
     return (Multiplier * (2.0f + (AgentEffectiveQuality / 100.0f)) * pow(CharSkillLevel + AgentSkillLevel,2));
}


float EvEMath::RAM::WasteSkillBased( uint32 MaterialAmount, float ProductionEfficiency )
{
	 return (floor(0.5f + (MaterialAmount * ((25.0f - (5.0f * ProductionEfficiency)) / 100.0f))));
}

float EvEMath::RAM::InventionChance( float BaseChance, uint8 EncryptionLevel, uint8 DataCore1SkillLevel, uint8 DataCore2SkillLevel, uint8 MetaLevel, float DecryptorModifier )
{
     // pre-Phoebe (Crucible) formula, chruker.dk 'Invention Chance - Before Phoebe 1.0'
     if (MetaLevel > 4)
         MetaLevel = 4;
     return (BaseChance * (1.0f + 0.01f * EncryptionLevel)
           * (1.0f + (DataCore1SkillLevel + DataCore2SkillLevel) * (0.1f / (5.0f - MetaLevel)))
           * DecryptorModifier);
     // fuzzysteve's formula (23may13)
     // base * (1+0.01*EncryptionLevel) * (1+ (skill*(0.1/(5-metalevel)))) * max(decryptor,1)
     // skill = sum of sciences skill lvls (as required by bpc)

     // decryptor formula
     //  min(max(rounddown((inputT1BPCruns/T1MaxPerBPC)*(T2MaxPerBPC/10)),1)+decryptorRunBonus, T2MaxPerBPC)

     //  new formula (pre phoebe)
     // base * skill * decryptor
     //  skill = 1 + EncryptionLevel/40 + (DataCore1SkillLevel + DataCore2SkillLevel)/30
}

float EvEMath::RAM::ReverseEngineeringChance(float BaseChance, uint8 ReverseEngineeringLevel, uint8 DataCore1SkillLevel, uint8 DataCore2SkillLevel)
{
    // pre-Phoebe formula (chruker.dk 'Reverse Engineering Chance - Merged with Invention in Phoebe 1.0')
    return (BaseChance * (1.0f + 0.01f * ReverseEngineeringLevel) * (1.0f + 0.1f * (DataCore1SkillLevel + DataCore2SkillLevel)));
}

int16 EvEMath::RAM::InventionRuns(int16 T1Runs, uint32 T1MaxRuns, uint32 T2MaxRuns, int16 DecryptorRuns)
{
    //  min(max(rounddown((inputT1BPCruns/T1MaxPerBPC)*(T2MaxPerBPC/10)),1)+decryptorRunBonus, T2MaxPerBPC)
    if (T1MaxRuns < 1)
        T1MaxRuns = 1;
    if (T2MaxRuns < 1)
        T2MaxRuns = 1;
    int32 runs = (int32)floor(((double)T1Runs / T1MaxRuns) * (T2MaxRuns / 10.0));
    if (runs < 1)
        runs = 1;
    runs += DecryptorRuns;
    if (runs > (int32)T2MaxRuns)
        runs = T2MaxRuns;
    if (runs < 1)
        runs = 1;
    return (int16)runs;
}

float EvEMath::Refine::StationTaxesForReprocessing(float CharacterStandingWithStationOwner )
{
    return 5.0f - (0.75f * CharacterStandingWithStationOwner);
}

float EvEMath::Refine::EffectiveRefiningYield(float EquipmentYield, uint8 RefiningLevel, uint8 RefiningEfficiencyLevel/*0*/, uint8 OreProcessingLevel/*0*/)
{
    return (EquipmentYield + 0.375f * (1.0f + (RefiningLevel * 0.02f))
            * (1.0f + (RefiningEfficiencyLevel * 0.04f)) * (1.0f + (OreProcessingLevel * 0.05f)));
}


// Agent Equations
float EvEMath::Agent::EffectiveQuality(int8 AgentQuality, uint8 NegotiationLevel, float AgentPersonalStanding)
{
    return (AgentQuality + (5.0f * NegotiationLevel) + AgentPersonalStanding);
}

float EvEMath::Agent::EffectiveStanding(float YourStanding, double standingBonus)
{
    return (1.0f - (1.0f - YourStanding / 10.0f) * (1.0f - standingBonus / 10.0f)) * 10.0f;
}

float EvEMath::Agent::RequiredStanding( uint8 AgentLevel, int8 AgentQuality )
{
    return (((AgentLevel - 1.0f) * 2.0f) + (AgentQuality/20.0f));
}

float EvEMath::Agent::MissionStandingIncrease( float BaseMissionIncrease, uint8 YourSocialSkillLevel )
{
    return (BaseMissionIncrease * (1.0f + 0.05f * YourSocialSkillLevel));
}

float EvEMath::Agent::Efficiency( uint8 AgentLevel, int8 AgentQuality )
{
    return (0.01f * ((8.0f * AgentLevel) + (0.1f * AgentQuality) - 4.0f));
}

float EvEMath::Agent::AgentStandingIncrease(float CurrentStanding, float PercentIncrease)
{
    return (((10.0f - CurrentStanding) * PercentIncrease) + CurrentStanding);
}

float EvEMath::Agent::GetStandingBonus(float fromStanding, uint32 fromFactionID, uint8 ConnectionsSkillLevel, uint8 DiplomacySkillLevel, uint8 CriminalConnectionsSkillLevel)
{
    float bonus(0.0f);
    if (fromStanding < 0.0f) {
        bonus = DiplomacySkillLevel * 0.4f;
    } else if (fromStanding > 0.0f) {
        switch (fromFactionID) {
            case 500010:
            case 500011:
            case 500012:
            case 500019:
            case 500020: {
                bonus = CriminalConnectionsSkillLevel * 0.4f;
            } break;
            default: {
                bonus = ConnectionsSkillLevel * 0.4f;
            } break;
        }
    }
    return bonus;
}

float EvEMath::Market::BrokerFee(uint8 brSkillLvl, float fStanding, float cStanding, float orderValue)
{
    float wStanding = (0.7f * fStanding + 0.3f * cStanding) / 10.0f;
    float fee = 0.01f * (1.0f - (0.05f * brSkillLvl)) * pow(2, -2 * wStanding);
    return EvE::max(fee * orderValue, 100.0f);
}

float EvEMath::Market::RelistFee(float oldPrice, float newPrice, float brokerPercent/*0.01*/, float discount/*0*/)
{
    // this needs a 'Relist Discount' but no clue where to find data for it yet
    return EvE::max(brokerPercent * (newPrice -oldPrice)) + (1 -discount) *brokerPercent *newPrice;
}

float EvEMath::Market::SalesTax(float baseSalesTax, uint8 accountingLvl/*0*/, uint8 taxEvasionLvl/*0*/)
{
    // Accounting -10%/level (client quote), Tax Evasion -2%/level of the SCC tax (skill description; the client's quote leaves it out)
    float maximumTax = baseSalesTax / 100.0f;
    float tax = maximumTax * (1 - 0.1f * accountingLvl) * (1 - 0.02f * taxEvasionLvl);
    return EvE::max(EvE::min(tax, maximumTax), 0.0f);
}

void EvEMath::PI::Dijkstra(uint32 sourcePin, uint32 destinationPin)
{
    // not used yet...
}


/*
 *
 * Research Points Per Day
 * Research_Points_Per_Day = Multiplier * ((1 + (Agent_Effective_Quality / 100)) * ((Your_Skill + Agent_Skill) ^ 2))
 * Multiplier is a specific multiplier for the research field you want to do research in. Like 3x for starship engineering
 * Your_Skill is your skill level in the research field
 * Agent_Skill is the agent's skill level in the research field
 *
 *
 * Station take when refining/reprocessing
 * Station_Take = Max((5 - (0.75 * Your_Standing)), 0)
 * For the station to take 0% you need a standing to the station owner of at least: 5 / 0.75 = 6.67
 *
 *
 * Agent Effective Quality - Removed in Incursion 1.5 (2011-05-19)
 * Agent_Effective_Quality = Agent_Quality + (5 * Negotiation_Skill_Level) + Round_Down(Effective_Standing)
 * Effective_Standing is the highest effective of either personal, corp. or faction standing.
 *
 *
 * Blueprint Material Requirement - Before Crius 1.0 (2014-07-22)
 * Required_Amount = Round(Base_Amount * ((1 + (Default_Blueprint_Waste_Factor / (1 + Blueprint_Material_Level))) + (0.25 - (0.05 * Production_Efficiency_Skill_Level))), 0)
 *
 *
 * Invention Chance - Before Phoebe 1.0 (2014-11-04)
 * Invention_Chance = Base_Chance * (1 + (0.01 * Encryption_Skill_Level)) * (1 + ((Datacore_1_Skill_Level + Datacore_2_Skill_Level) * (0.1 / (5 - Meta_Level)))) * Decryptor_Modifier
 * Meta_Level of the base items used. No base items is the same as metalevel 0 = useless.
 * Decryptor_Modifier is optional :-)
 *
 *
 * Reverse Engineering Chance - Merged with Invention in Phoebe 1.0 (2014-11-04)
 * Reverse_Chance = Base_Chance * (1 + (0.01 * Reverse_Engineering_Skill_Level)) * (1 + (0.1 * (Datacore_1_Skill_Level + Datacore_2_Skill_Level)))
 */