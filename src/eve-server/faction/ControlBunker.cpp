/**
 * @name ControlBunker.cpp
 *   a factional warfare system's control bunker in space.  see ControlBunker.h
 *
 * @date:          22 October 2026
 */

#include "eve-server.h"

#include "Client.h"
#include "faction/ControlBunker.h"
#include "faction/FacWarMgr.h"
#include "inventory/AttributeEnum.h"
#include "system/Damage.h"
#include "system/SystemManager.h"
#include "system/cosmicMgrs/AnomalyMgr.h"

ControlBunkerSE::ControlBunkerSE(InventoryItemRef self, EVEServiceManager& services, SystemManager* system, const FactionData& data)
: ObjectSystemEntity(self, services, system)
{
    m_warID = data.factionID;
    m_allyID = data.allianceID;
    m_corpID = data.corporationID;
    m_ownerID = data.ownerID;

    // the type's hit points, less what FacWarMgr says it has lost
    const ItemType& type(self->type());
    self->SetAttribute(AttrShieldCapacity, (type.HasAttribute(AttrShieldCapacity) ? type.GetAttribute(AttrShieldCapacity).get_double() : 0.0), false);
    self->SetAttribute(AttrArmorHP, (type.HasAttribute(AttrArmorHP) ? type.GetAttribute(AttrArmorHP).get_double() : 0.0), false);
    self->SetAttribute(AttrHP, (type.HasAttribute(AttrHP) ? type.GetAttribute(AttrHP).get_double() : 1.0), false);
    if (type.HasAttribute(AttrSignatureRadius))
        self->SetAttribute(AttrSignatureRadius, type.GetAttribute(AttrSignatureRadius), false);
    self->SetAttribute(AttrRadius, type.radius(), false);
    self->SetAttribute(AttrMass, type.mass(), false);
    double shield(1.0), armor(1.0), hull(1.0);
    if (system != nullptr)
        sFacWarMgr.GetBunkerHealth(system->GetID(), shield, armor, hull);
    SetHealth(shield, armor, hull);
}

bool ControlBunkerSE::IsInvul()
{
    if (ObjectSystemEntity::IsInvul() or (m_system == nullptr))
        return true;
    return !sFacWarMgr.BunkerVulnerable(m_system->GetID());
}

void ControlBunkerSE::SetHealth(double shield, double armor, double hull)
{
    m_self->SetAttribute(AttrShieldCharge, m_self->GetAttribute(AttrShieldCapacity).get_double() * shield, false);
    m_self->SetAttribute(AttrArmorDamage, m_self->GetAttribute(AttrArmorHP).get_double() * (1.0 - armor), false);
    m_self->SetAttribute(AttrDamage, m_self->GetAttribute(AttrHP).get_double() * (1.0 - hull), false);
    if (m_bubble != nullptr)
        SendDamageStateChanged();
}

void ControlBunkerSE::OnDamaged(SystemEntity* pAttacker)
{
    if (m_system == nullptr)
        return;
    double shieldCap(m_self->GetAttribute(AttrShieldCapacity).get_double()), armorHP(m_self->GetAttribute(AttrArmorHP).get_double()),
           hp(m_self->GetAttribute(AttrHP).get_double());
    double shield((shieldCap > 0) ? (m_self->GetAttribute(AttrShieldCharge).get_double() / shieldCap) : 0.0);
    double armor((armorHP > 0) ? (1.0 - (m_self->GetAttribute(AttrArmorDamage).get_double() / armorHP)) : 0.0);
    double hull((hp > 0) ? (1.0 - (m_self->GetAttribute(AttrDamage).get_double() / hp)) : 0.0);
    sFacWarMgr.OnBunkerDamaged(m_system->GetID(), EvE::max(shield, 0.0), EvE::max(armor, 0.0), EvE::max(hull, 0.0));
}

void ControlBunkerSE::Killed(Damage& damage)
{
    uint32 charID(0);
    if ((damage.srcSE != nullptr) and damage.srcSE->HasPilot())
        charID = damage.srcSE->GetPilot()->GetCharacterID();
    if (m_system != nullptr) {
        if (m_system->GetAnomMgr() != nullptr)
            m_system->GetAnomMgr()->RemoveSignal(GetID());
        sFacWarMgr.OnBunkerDestroyed(m_system->GetID(), charID);
    }
    ObjectSystemEntity::Killed(damage);
}
