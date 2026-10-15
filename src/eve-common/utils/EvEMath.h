

 //////////////////////////////////////////////////////////////////////////////////////////
 //
 // EVE Math Equations for in-game features
 // (pulled directly from http://wiki.eve-id.net/Equations)
 //
 //////////////////////////////////////////////////////////////////////////////////////////

#ifndef EVE_COMMON_UTILS_MATH_H
#define EVE_COMMON_UTILS_MATH_H

#include "../../eve-core/eve-core.h"

class EvilNumber;

namespace EvESkill {
    // skill constants
    const uint8 MAXSKILLLEVEL = 5;
    const uint8 skillPointMultiplier = 250;
    const float DIVCONSTANT = std::log(2) * 2.5;
}

namespace EvEMath {
    namespace Units {
        double MetersToLightYears(double meters);
    }
    namespace Skill {
        uint32 PointsAtLevel(uint8 level, float rank);
        // client skillUtil.GetSkillPointsPerMinute (float, no rounding)
        float PointsPerMinute(float pAttr, float sAttr);
        // highest level the sp reach (0..5), consistent with PointsAtLevel()
        uint8 LevelForPoints(uint32 currentSP, float rank);
        int64 EndTime(uint32 currentSP, uint32 nextSP, float SPMin, int64 timeNow);
        int64 StartTime(uint32 currentSP, uint32 nextSP, float SPMin, int64 timeNow);
    }

    namespace RAM {
        int32 CopyTime(uint32 BaseTime, uint32 CopyRuns, uint32 MaxRuns, float CharModifier=1, float SlotModifier=1);
        int32 InventionTime(uint32 BaseTime, float SlotModifier = 1, float ImplantModifier = 1);
        int32 ProductionTime(uint32 BaseTime, float bpProductivityModifier, float ProductionLevel, float TimeModifier=1);
        // ME or PE research, all the runs (levels)
        int32 ResearchTime(uint32 BaseTime, int32 Runs, float CharModifier=1, float SlotModifier=1);
        int32 ClampTime(double seconds);

        float ME_EffectOnWaste(float MaterialAmount, float BaseWasteFactor, float MaterialEfficiency);
        float ResearchPointsPerDay(float Multiplier, float AgentEffectiveQuality, uint8 CharSkillLevel, uint8 AgentSkillLevel );

        uint32 PerfectME(uint32 MaterialAmount, uint8 BaseWasteFactor);

        float WasteSkillBased(uint32 MaterialAmount, float ProductionEfficiency);
        float InventionChance(float BaseChance, uint8 EncryptionLevel, uint8 DataCore1SkillLevel, uint8 DataCore2SkillLevel, uint8 MetaLevel, float DecryptorModifier );
        float ReverseEngineeringChance(float BaseChance, uint8 ReverseEngineeringLevel, uint8 DataCore1SkillLevel, uint8 DataCore2SkillLevel);
        // runs of an invented T2 copy
        int16 InventionRuns(int16 T1Runs, uint32 T1MaxRuns, uint32 T2MaxRuns, int16 DecryptorRuns);
    }

    namespace Refine {
        float StationTaxesForReprocessing(float CharacterStandingWithStationOwner);
        float EffectiveRefiningYield(float EquipmentYield, uint8 RefiningLevel, uint8 RefiningEfficiencyLevel=9, uint8 OreProcessingLevel=0);
    }

    namespace Agent {
        float EffectiveQuality(int8 AgentQuality, uint8 NegotiationSkillLevel, float AgentPersonalStanding);
        float EffectiveStanding(float YourStanding, double standingBonus);
        float RequiredStanding(uint8 AgentLevel, int8 AgentQuality);
        float MissionStandingIncrease( float BaseMissionIncrease, uint8 YourSocialSkillLevel);
        float Efficiency(uint8 AgentLevel, int8 AgentQuality);
        float AgentStandingIncrease(float CurrentStanding, float PercentIncrease);
        float GetStandingBonus(float fromStanding, uint32 fromFactionID, uint8 ConnectionsSkillLevel, uint8 DiplomacySkillLevel, uint8 CriminalConnectionsSkillLevel);
    }

    namespace Market {
        float BrokerFee(uint8 brSkillLvl, float fStanding, float cStanding, float total);
        float RelistFee(float oldPrice, float newPrice, float brokerPercent=0.01, float discount=0);
        float SalesTax(float baseSalesTax, uint8 accountingLvl=0, uint8 taxEvasionLvl=0);
    }

    namespace PI {
        void Dijkstra(uint32 sourcePin, uint32 destinationPin);

        /**  @note client code for shortest path algorithm (PI shit)
         *
         *    def Dijkstra(self, sourcePin, destinationPin):
         *        D = {}
         *        P = {}
         *        Q = planetCommon.priority_dict()
         *        Q[sourcePin] = 0.0
         *        while len(Q) > 0:
         *            vPin = Q.smallest()
         *            D[vPin] = Q[vPin]
         *            if vPin == destinationPin:
         *                break
         *            Q.pop_smallest()
         *            for wDestinationID in self.colonyData.GetLinksForPin(vPin.id):
         *                wLink = self.GetLink(vPin.id, wDestinationID)
         *                wPin = self.GetPin(wDestinationID)
         *                vwLength = D[vPin] + self._GetLinkWeight(wLink, wPin, vPin)
         *                if wPin in D:
         *                    if vwLength < D[wPin]:
         *                        raise ValueError, 'Dijkstra: found better path to already-final vertex'
         *                elif wPin not in Q or vwLength < Q[wPin]:
         *                    Q[wPin] = vwLength
         *                    P[wPin] = vPin
         *
         *        return (D, P)
         */
    }

}

#endif  // EVE_COMMON_UTILS_MATH_H

