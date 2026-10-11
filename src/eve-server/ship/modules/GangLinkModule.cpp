 /**
  * @name GangLinkModule.cpp
  *   warfare link (gang coordinator) module class
  *
  *  a link only works while its pilot is the squad, wing or fleet booster, in space, and only on members in the same system.
  *  the fleet decides who gets what (FleetService::UpdateLinks).  the module just asks for a refresh when it starts, each cycle
  *  (members jumping in or undocking) and when it stops.
  */

#include "eve-server.h"

#include "Client.h"
#include "effects/EffectsDataMgr.h"
#include "fleet/FleetService.h"
#include "ship/modules/GangLinkModule.h"


namespace {
    std::map<uint16, std::vector<LinkModifier>> s_linkModifiers;

    // the link expressions are COMBINEs of
    //  [(targetAttr).(PostPercent)].AGSM(srcAttr)                     ship bonus
    //  [(Type(skill).targetAttr).(PostPercent)].AGRSM(srcAttr)        bonus to modules requiring skill
    void ReadModifiers(uint16 exprID, std::vector<LinkModifier>& into, uint8 depth)
    {
        if ((exprID == 0) or (depth > 16))
            return;
        using namespace FX;
        Expression expr(sFxDataMgr.GetExpression(exprID));
        switch (expr.operandID) {
            case Operands::COMBINE: {
                ReadModifiers(expr.arg1, into, depth + 1);
                ReadModifiers(expr.arg2, into, depth + 1);
            } break;
            case Operands::AGSM:
            case Operands::AGRSM: {
                Expression eff(sFxDataMgr.GetExpression(expr.arg1));
                Expression assoc(sFxDataMgr.GetExpression(eff.arg1));
                Expression target(sFxDataMgr.GetExpression(eff.arg2));
                if (assoc.expressionValue.compare("PostPercent") != 0) {
                    _log(EFFECTS__WARNING, "GangLinkModule - expression %u uses %s, not PostPercent.  skipped", exprID, assoc.expressionValue.c_str());
                    break;
                }
                LinkModifier mod = LinkModifier();
                    mod.srcAttr = sFxDataMgr.GetExpression(expr.arg2).expressionAttributeID;
                if (expr.operandID == Operands::AGSM) {
                    mod.skillID = 0;
                    mod.targetAttr = sFxDataMgr.GetExpression(target.arg1).expressionAttributeID;
                } else {
                    mod.skillID = sFxDataMgr.GetExpression(target.arg1).expressionTypeID;
                    mod.targetAttr = sFxDataMgr.GetExpression(target.arg2).expressionAttributeID;
                }
                if ((mod.srcAttr == 0) or (mod.targetAttr == 0)) {
                    _log(EFFECTS__WARNING, "GangLinkModule - expression %u has no source or target attribute.  skipped", exprID);
                    break;
                }
                _log(EFFECTS__TRACE, "GangLinkModule - expression %u: skill %u, target attr %u, source attr %u", exprID, mod.skillID, mod.targetAttr, mod.srcAttr);
                into.push_back(mod);
            } break;
            default: {
                _log(EFFECTS__WARNING, "GangLinkModule - expression %u has operand %i, which links don't use.  skipped", exprID, expr.operandID);
            } break;
        }
    }
}

const std::vector<LinkModifier>& GangLinkModule::GetModifiers(uint16 effectID)
{
    std::map<uint16, std::vector<LinkModifier>>::iterator itr = s_linkModifiers.find(effectID);
    if (itr != s_linkModifiers.end())
        return itr->second;

    std::vector<LinkModifier> mods;
    Effect effect(sFxDataMgr.GetEffect(effectID));
    ReadModifiers(effect.preExpression, mods, 0);
    return s_linkModifiers.emplace(effectID, mods).first->second;
}


GangLinkModule::GangLinkModule(ModuleItemRef mRef, ShipItemRef sRef)
: ActiveModule(mRef, sRef)
{
}

void GangLinkModule::UpdateFleet()
{
    Client* pClient(m_shipRef->GetPilot());
    if ((pClient != nullptr) and pClient->InFleet())
        sFltSvc.UpdateLinks(pClient->GetFleetID());
}

void GangLinkModule::Activate(uint16 effectID, uint32 targetID/*0*/, int16 repeat/*0*/)
{
    ActiveModule::Activate(effectID, targetID, repeat);
    if (m_ModuleState == Module::State::Activated)
        UpdateFleet();
}

void GangLinkModule::DeactivateCycle(bool abort/*false*/)
{
    if (m_ModuleState < Module::State::Deactivating)
        return;
    ActiveModule::DeactivateCycle(abort);
    UpdateFleet();
}

uint32 GangLinkModule::DoCycle()
{
    uint32 cycleTime(ActiveModule::DoCycle());
    // the first cycle runs before the module counts as active.  Activate() refreshes then
    if ((cycleTime > 0) and (m_ModuleState == Module::State::Activated))
        UpdateFleet();
    return cycleTime;
}
