/*
 * Asteroid.cpp
 *
 * asteroid item and entity classes for EVEmu
 *
 * Original file/code by Zhur
 * Rewrite:     Allan
 */


#include "eve-server.h"

#include "EVEServerConfig.h"
#include "system/DestinyManager.h"
#include "system/Asteroid.h"
#include "system/SystemManager.h"
#include "system/cosmicMgrs/BeltMgr.h"
#include "system/SystemBubble.h"
#include "system/sov/SovereigntyDataMgr.h"
#include "system/cosmicMgrs/IncursionMgr.h"


AsteroidItem::AsteroidItem(const ItemType& type, const ItemData& idata, const AsteroidData& adata)
: InventoryItem(adata.itemID, type, idata),
m_data(adata)
{
    _log(ITEM__TRACE, "Created AsteroidItem for %s(%u).", adata.itemName.c_str(), adata.itemID);
}

AsteroidItemRef AsteroidItem::Load( uint32 asteroidID)
{
    return InventoryItem::Load<AsteroidItem>(asteroidID );
}

AsteroidItemRef AsteroidItem::Spawn(ItemData& idata, AsteroidData& adata) {
    const ItemType *type = sItemFactory.GetType(adata.typeID);
    if (type == nullptr)
        return AsteroidItemRef(nullptr);

    idata.name = type->name();
    adata.itemName = type->name();

    ManagerDB::CreateRoidItemID(idata, adata);
    if (adata.itemID == 0)
        return AsteroidItemRef(nullptr);

    AsteroidItemRef roidRef = AsteroidItemRef(new AsteroidItem(*type, idata, adata));
    roidRef->SetAttribute(AttrRadius,    adata.radius);
    roidRef->SetAttribute(AttrQuantity,  adata.quantity);
    roidRef->SetAttribute(AttrVolume,    type->volume());
    roidRef->SetAttribute(AttrMass,      type->mass() * adata.quantity);

    return roidRef;
}

AsteroidItemRef AsteroidItem::SpawnTemp(ItemData& idata, AsteroidData& adata) {
    const ItemType *type = sItemFactory.GetType(adata.typeID);
    if (type == nullptr)
        return AsteroidItemRef(nullptr);

    idata.name = type->name();
    adata.itemName = type->name();
    adata.itemID = sItemFactory.GetNextTempID();

    AsteroidItemRef roidRef = AsteroidItemRef(new AsteroidItem(*type, idata, adata));
    roidRef->SetAttribute(AttrRadius,    adata.radius);
    roidRef->SetAttribute(AttrQuantity,  adata.quantity);
    roidRef->SetAttribute(AttrVolume,    type->volume());
    roidRef->SetAttribute(AttrMass,      type->mass() * adata.quantity);

    return roidRef;
}

//iRef = InventoryItem::SpawnItem(sItemFactory.GetNextTempID(), iData);

AsteroidSE::AsteroidSE(InventoryItemRef self, EVEServiceManager& services, SystemManager* system)
: ObjectSystemEntity(self, services, system),
m_beltMgr(nullptr),
m_beltID(0),
m_origQuantity(self->GetAttribute(AttrQuantity).get_double())
{
}

void AsteroidSE::Process() {
    /* called by EntityList::Process on every loop */
    /*  Enable base call to Process Targeting and Movement  */
    SystemEntity::Process();
    // regrowth is run by BeltMgr for idle belts
}

void AsteroidSE::EncodeDestiny( Buffer& into )
{
    using namespace Destiny;

    BallHeader head = BallHeader();
        head.entityID = GetID();
        head.mode = Ball::Mode::RIGID;
        head.radius = GetRadius();
        head.posX = x();
        head.posY = y();
        head.posZ = z();
        head.flags = 0;
    into.Append( head );
    RIGID_Struct main;
        main.formationID = 0xFF;
    into.Append( main );

    _log(SE__DESTINY, "AsteroidSE::EncodeDestiny(): %s - id:%lli, mode:%u, flags:0x%X", GetName(), head.entityID, head.mode, head.flags);
}

void AsteroidSE::MakeDamageState(DoDestinyDamageState &into) {
    into.shield = 1.0;
    into.recharge = 30000;
    into.timestamp = GetFileTimeNow();
    into.armor = 1.0;
    into.structure = 1.0;
}

void AsteroidSE::SendDamageStateChanged() {  //working 24Apr15
    DoDestinyDamageState dmgState;
    MakeDamageState(dmgState);
    OnDamageStateChange dmgChange;
    dmgChange.entityID = m_self->itemID();
    dmgChange.state = dmgState.Encode();
    PyTuple *up = dmgChange.Encode();
    if (m_targMgr != nullptr)
        m_targMgr->QueueUpdate(&up);
    PySafeDecRef(up);
}

void AsteroidSE::Grow() {
    // Crucible regenerated mined rocks at downtime.  BeltMgr calls this for belts nobody mined for a while
    if (GetQuantity() >= m_origQuantity)
        return;
    _log(MINING__TRACE, "AsteroidSE::Grow() - %s(%u) regrows from %.1f to %.1f", m_self->name(), m_self->itemID(), GetQuantity(), m_origQuantity);
    SetQuantity(m_origQuantity);
}

void AsteroidSE::SetQuantity(double quantity)
{
    m_self->SetAttribute(AttrQuantity, quantity, false);
    m_self->SetAttribute(AttrMass, m_self->type().mass() * quantity, false);

    // ice chunks keep their size (their quantity isn't derived from it)
    if ((quantity <= 0) or (m_self->groupID() == EVEDB::invGroups::Ice))
        return;
    double radius(RadiusForQuantity(m_self->categoryID(), m_self->groupID(), m_self->type().radius(), quantity));
    // skip tiny changes, every cycle would send one
    if (std::fabs(radius - GetRadius()) < 1.0)
        return;
    if (m_destiny != nullptr) {
        m_destiny->SetRadius(radius, (m_bubble != nullptr));   // SetBallRadius to the bubble
    } else {
        SetRadius(radius);
    }
}

bool AsteroidSE::RemoveQuantity(double units)
{
    if (units <= 0)
        return (GetQuantity() <= 0);
    double quantity(GetQuantity() - units);
    if (quantity < 0) {
        units += quantity;
        quantity = 0;
    }

    // ore and ice raise the system's industrial index.  gas doesn't
    if (m_self->categoryID() == EVEDB::invCategories::Asteroid)
        svDataMgr.AddIndustrialActivity(m_system->GetID(), units * m_self->GetAttribute(AttrVolume).get_double());

    SetQuantity(quantity);
    if (m_beltMgr != nullptr)
        m_beltMgr->OnMined(this, units, (quantity <= 0));
    // incursion ore (mining lasers and mining drones) can bring Sansha guards
    sIncursionMgr.OreMined(this);

    return (quantity <= 0);
}

double AsteroidSE::RadiusForQuantity(uint8 categoryID, uint16 groupID, double typeRadius, double quantity)
{
    if (quantity < 0)
        quantity = 1;
    if (categoryID == EVEDB::invCategories::Asteroid)
        return 89.675 * std::exp(4e-05 * std::min(quantity, 130000.0));
    if (groupID == EVEDB::invGroups::Harvestable_Cloud)
        return quantity * typeRadius / 10.0;
    return quantity * typeRadius;
}

double AsteroidSE::QuantityForRadius(uint8 categoryID, uint16 groupID, double typeRadius, double radius)
{
    if (categoryID == EVEDB::invCategories::Asteroid)
        return std::log(radius / 89.675) * (1.0 / 4e-05);
    if (typeRadius <= 0)
        return 0;
    if (groupID == EVEDB::invGroups::Harvestable_Cloud)
        return radius * 10.0 / typeRadius;
    return radius / typeRadius;
}

void AsteroidSE::Delete()
{
    _log(SPAWN__DEPOP, "AsteroidSE::Delete() - Removing asteroid %s(%u) from beltID %u", \
            m_self->name(), m_self->itemID(), m_beltID);
    // asteroids built outside a belt (dungeon rocks, gas clouds) have no manager
    if (m_beltMgr != nullptr)
        m_beltMgr->RemoveAsteroid(m_beltID, this);
    SystemEntity::Delete();
}

/*
 * def ComputeRadiusFromQuantity(categoryID, groupID, typeID, quantity):
 *    if quantity < 0:
 *        quantity = 1
 *    if categoryID == const.categoryAsteroid:
 *        qty = quantity
 *        if qty > 130000:
 *            qty = 130000
 *        return 89.675 * math.exp(4e-05 * qty)
 *    if groupID == const.groupHarvestableCloud:
 *        return quantity * cfg.invtypes.Get(typeID).radius / 10.0
 *    return quantity * cfg.invtypes.Get(typeID).radius
 *
 *
 * def ComputeQuantityFromRadius(categoryID, groupID, typeID, radius):
 *    if categoryID == const.categoryAsteroid:
 *        quantity = math.log(radius / 89.675) * (1.0 / 4e-05)
 *        return quantity
 *    if groupID == const.groupHarvestableCloud:
 *        quantity = radius * 10.0 / cfg.invtypes.Get(typeID).radius
 *        return quantity
 *    return radius / cfg.invtypes.Get(typeID).radius
 */