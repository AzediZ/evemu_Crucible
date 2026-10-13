 /**
  * @name AmbientNPC.cpp
  *   one ambient ship in space (doc/code_and_design_notes/ambient-npc-plan.md 4.2, 9.1).  see AmbientNPC.h
  */

#include "eve-server.h"

#include "Client.h"
#include "EVEServerConfig.h"
#include "EntityList.h"
#include "inventory/AttributeEnum.h"
#include "inventory/InventoryItem.h"
#include "inventory/ItemFactory.h"
#include "system/Container.h"
#include "npc/ambient/AmbientMgr.h"
#include "npc/ambient/AmbientNPC.h"
#include "npc/ambient/AmbientPilot.h"
#include "packets/Destiny.h"
#include "system/CrimeWatch.h"
#include "system/Damage.h"
#include "system/DestinyManager.h"
#include "system/SystemManager.h"

AmbientNPC::AmbientNPC(InventoryItemRef self, EVEServiceManager& services, SystemManager* system, const FactionData& data,
                       const Ambient::Spec& spec)
: NPC(self, services, system, data, nullptr),
  m_spec(spec),
  m_pilot(nullptr)
{
}

AmbientNPC::~AmbientNPC()
{
    SafeDelete(m_pilot);
}

void AmbientNPC::AttachPilot(AmbientPilot* pPilot)
{
    if (m_pilot != pPilot)
        SafeDelete(m_pilot);
    m_pilot = pPilot;
}

bool AmbientNPC::IsHull() const
{
    return ((m_spec.arch != nullptr) and m_spec.arch->hull);
}

bool AmbientNPC::IsProtected() const
{
    return ((m_spec.role != Ambient::Role::Criminal) and (m_spec.role != Ambient::Role::Concord)
            and (m_spec.role != Ambient::Role::None));
}

bool AmbientNPC::Load()
{
    /* the reference hull's flight data on our own item (4.2):  npc types have no agility of their own (NPC() sets inertia 1)
     *  and absurd speeds.  an npc flies at its cruise speed, so that is the capped speed too */
    const Ambient::Archetype* pArch(m_spec.arch);
    if (pArch != nullptr) {
        m_self->SetAttribute(AttrInetia, pArch->agility, false);
        m_self->SetAttribute(AttrMaxVelocity, pArch->maxVelocity, false);
        m_self->SetAttribute(AttrEntityCruiseSpeed, pArch->maxVelocity, false);
        if (!(m_self->GetAttribute(AttrMass).get_float() > 0.0f))
            m_self->SetAttribute(AttrMass, pArch->mass, false);
    }
    // no bounty, no security status gain (10.4):  NPC::Killed() then pays and awards nothing
    m_self->SetAttribute(AttrEntityKillBounty, EvilZero, false);
    m_self->SetAttribute(AttrEntitySecurityStatusKillBonus, EvilZero, false);

    // navy ships are strong enough that attacking them is suicide (D1).  CONCORD's types are lethal already
    if (m_spec.role == Ambient::Role::Navy) {
        float scale(sConfig.ambient.MilitaryHpScale);
        if (scale > 0.0f) {
            float shield(m_self->GetAttribute(AttrShieldCapacity).get_float() * scale);
            m_self->SetAttribute(AttrShieldCapacity, shield, false);
            m_self->SetAttribute(AttrShieldCharge, shield, false);
            m_self->SetAttribute(AttrArmorHP, m_self->GetAttribute(AttrArmorHP).get_float() * scale, false);
            m_self->SetAttribute(AttrHP, m_self->GetAttribute(AttrHP).get_float() * scale, false);
            m_self->SetAttribute(AttrArmorDamage, EvilZero, false);
            m_self->SetAttribute(AttrDamage, EvilZero, false);
        }
    }
    return NPC::Load();
}

void AmbientNPC::Process()
{
    NPC::Process();
    // the itinerary (warps, jumps, docking, mining) after the ai and movement of this tick (8)
    if (!m_killed and (m_pilot != nullptr))
        m_pilot->Tick(GetSteadyTime());
}

PyDict* AmbientNPC::MakeSlimItem()
{
    // 9.1:  no charID (an npc character would show an agent's name), never a warFactionID (militia)
    PyDict* slim = new PyDict();
        slim->SetItemString("itemID",           new PyLong(m_self->itemID()));
        slim->SetItemString("typeID",           new PyInt(m_self->typeID()));
        slim->SetItemString("ownerID",          new PyInt(m_ownerID));
        slim->SetItemString("corpID",           IsCorp(m_corpID) ? new PyInt(m_corpID) : PyStatic.NewNone());
        slim->SetItemString("allianceID",       PyStatic.NewNone());
        slim->SetItemString("warFactionID",     PyStatic.NewNone());
        slim->SetItemString("categoryID",       new PyInt(m_self->categoryID()));
        slim->SetItemString("groupID",          new PyInt(m_self->groupID()));
    if (IsHull()) {
        // the client fits these as turrets (turretSet.py):  strip miner beams need a moduleID of the ship
        if (!m_spec.fakeModules.empty() and (sAmbientMgr.Catalog().MinerModuleType() != 0)) {
            PyList* list = new PyList();
            for (auto cur : m_spec.fakeModules)
                list->AddItem(new_tuple(cur, sAmbientMgr.Catalog().MinerModuleType()));
            slim->SetItemString("modules", list);
        }
    } else {
        // entities show their slim name ("Caldari Hauler").  ships without a pilot show their type name
        slim->SetItemString("name",             new PyString(m_spec.name));
    }
    return slim;
}

void AmbientNPC::EncodeDestiny(Buffer& into)
{
    using namespace Destiny;

    BallHeader head = BallHeader();
        head.entityID = GetID();
        head.mode = m_destiny->GetState();
        head.radius = GetRadius();
        head.posX = x();
        head.posY = y();
        head.posZ = z();
        head.flags = Ball::Flag::IsFree | (m_destiny->IsMassive() ? Ball::Flag::IsMassive : 0);
        // a ship without the flag is an empty ship to the client ("Board Ship", 2.6)
        if (IsHull())
            head.flags |= Ball::Flag::IsInteractive;
    into.Append(head);
    MassSector mass = MassSector();
        mass.mass = m_destiny->GetMass();
        mass.cloak = (m_destiny->IsCloaked() ? 1 : 0);
        mass.harmonic = m_harmonic;
        mass.corporationID = m_corpID;
        mass.allianceID = -1;
    into.Append(mass);
    m_destiny->EncodeDestiny(into);
}

void AmbientNPC::DropLoot(WreckContainerRef wreckRef, uint32 groupID, uint32 owner)
{
    if (wreckRef.get() == nullptr)
        return;
    std::vector<Loot::Item> items;
    switch (m_spec.role) {
        case Ambient::Role::Navy:
        case Ambient::Role::Concord:
            return;
        case Ambient::Role::Criminal:
            // a pirate's own loot data (the loot work's pool of its type and group)
            NPC::DropLoot(wreckRef, groupID, owner);
            return;
        case Ambient::Role::Miner: {
            // some of the ore it was mining:  20 - 120 m3 of it
            SystemEntity* pRock((m_pilot != nullptr) ? m_system->GetSE(m_pilot->GetLastRockID()) : nullptr);
            uint16 oreType(pRock != nullptr ? pRock->GetTypeID() : 1230);   // Veldspar
            const ItemType* pOre(sItemFactory.GetType(oreType));
            double volume((pOre != nullptr) and (pOre->volume() > 0.0f) ? pOre->volume() : 0.1);
            uint32 lo(static_cast<uint32>(std::max(1.0, 20.0 / volume)));
            uint32 hi(static_cast<uint32>(std::max(1.0, 120.0 / volume)));
            items.push_back(Loot::Item{oreType, 0, 0.8, lo, hi});
        } break;
        default:
            items = sAmbientMgr.Catalog().TradeGoods();
            break;
    }
    if (items.empty())
        return;
    // 0 - 2 rolls, one drop per group:  a wreck holds a stack or two, often nothing
    Loot::Params params = Loot::Params();
        params.rollsMin = 0;
        params.rollsMax = 2;
        params.groupMax = 0;
    std::vector<Loot::Group> pool(1, Loot::Group{0, 1.0, &items});
    std::vector<Loot::Drop> drops;
    Loot::Roll(sAmbientMgr.LootRng(), params, pool, drops);
    for (auto& cur : drops) {
        uint32 quantity(cur.quantity);
        if (sConfig.rates.DropItem > 1)
            quantity *= sConfig.rates.DropItem;
        ItemData iLoot(cur.typeID, owner, wreckRef->itemID(), flagNone, quantity);
        InventoryItemRef iRef(sItemFactory.SpawnItem(iLoot));
        if (iRef.get() == nullptr)
            continue;
        wreckRef->AddItem(iRef);
        _log(LOOT__INFO, "ambient %s(%u):  %u of %u in its wreck", GetName(), GetID(), quantity, cur.typeID);
    }
}

void AmbientNPC::Killed(Damage& damage)
{
    if (m_pilot != nullptr)
        m_pilot->OnKilled();
    // 10.4:  a pilot (or his drones) who destroys a protected ambient ship takes the ship kill penalty
    SystemEntity* pKiller(damage.srcSE);
    if (IsProtected() and (pKiller != nullptr)) {
        Client* pClient(nullptr);
        if (pKiller->HasPilot()) {
            pClient = pKiller->GetPilot();
        } else if (pKiller->IsDroneSE()) {
            pClient = sEntityList.FindClientByCharID(pKiller->GetSelf()->ownerID());
        }
        if (pClient != nullptr)
            sCrimeWatch.AmbientKilled(pClient, m_system);
    }
    // the wreck (no bounty or security gain:  Load() zeroed them)
    NPC::Killed(damage);
}
