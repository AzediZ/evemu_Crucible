
/**
 * @name Battery.cpp
 *   Class for POS Battery Modules.
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


#include "pos/Battery.h"


BatterySE::BatterySE(StructureItemRef structure, EVEServiceManager& services, SystemManager* system, const FactionData& data)
: StructureSE(structure, services, system, data),
m_ai(nullptr)
{
    m_ai = new POS_AI(this);
}

BatterySE::~BatterySE()
{
    SafeDelete(m_ai);
}

void BatterySE::SetOffline()
{
    m_ai->Stop();
    StructureSE::SetOffline();
}

void BatterySE::Killed(Damage &damage)
{
    m_ai->Stop();
    StructureSE::Killed(damage);
}

void BatterySE::ValidateAddItem(Client* pClient, InventoryItemRef iRef, EVEItemFlags toFlag)
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

void BatterySE::Init()
{
    StructureSE::Init();
}

void BatterySE::Process()
{
    /* called by EntityList::Process on every loop */
    /*  Enable base call to Process state changes  */
    StructureSE::Process();
    // automatic defense (and manual control)
    if (!m_killed)
        m_ai->Process();
}
