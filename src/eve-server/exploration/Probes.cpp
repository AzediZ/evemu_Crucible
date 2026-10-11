 /**
  * @name ProbeItems.cpp
  *     Probe Item/SE class for EVEmu
  *
  * @Author:        Allan
  * @date:          10 March 2018
  *
  */


#include "eve-server.h"

#include "Client.h"

#include "exploration/Probes.h"
#include "exploration/Scan.h"
#include "inventory/AttributeEnum.h"
#include "inventory/Inventory.h"
#include "system/SystemBubble.h"
#include "system/SystemManager.h"

/* SCAN__ERROR
 * SCAN__WARNING
 * SCAN__MESSAGE
 * SCAN__DEBUG
 * SCAN__INFO
 * SCAN__TRACE
 * SCAN__DUMP
 * SCAN__RSPDUMP
 */

ProbeItem::ProbeItem(uint32 itemID, const ItemType& _type, const ItemData& _data)
: InventoryItem(itemID, _type, _data)
{
}
/*
            Scanner_Probe = 479,
            Survey_Probe = 492,
            Warp_Disruption_Probe = 548,
            Obsolete_Probes = 972,
            */
uint32 ProbeItem::CreateItemID(ItemData& data)
{
    return InventoryItem::CreateItemID(data);
}

ProbeItemRef ProbeItem::Load(uint32 itemID)
{
    return InventoryItem::Load<ProbeItem>(itemID );
}

ProbeItemRef ProbeItem::Spawn(ItemData& data)
{
    uint32 itemID = ProbeItem::CreateItemID(data);
    if (itemID == 0 )
        return ProbeItemRef(nullptr);

    return ProbeItem::Load(itemID);
}

void ProbeItem::Delete() {
    // remove from current container's inventory.
    // this should be SolarSystem, but just in case, run tests anyway
    if (IsValidLocationID(locationID())) {
        InventoryItemRef iRef = sItemFactory.GetItemRef(locationID());
        if (iRef.get() != nullptr) {
            iRef->GetMyInventory()->RemoveItem(InventoryItemRef(this));
        } else {
            _log(ITEM__ERROR, "ProbeItem::Delete() - Cant find location %u containing %s.", locationID(), name());
        }
    } else {
        _log(ITEM__ERROR, "ProbeItem::Delete() - Location %u containing %s is invalid.", locationID(), name());
    }

    // remove from DB
    ItemDB::DeleteItem(m_itemID);
    // remove from factory cache
    sItemFactory.RemoveItem(m_itemID);
}


// abandoned probe c'tor
ProbeSE::ProbeSE(ProbeItemRef self, EVEServiceManager& services, SystemManager* system)
: DynamicSystemEntity(self, services, system),
m_scan(nullptr),
m_client(nullptr),
m_shipRef(nullptr),
m_moduleRef(nullptr),
m_secStatus(0),
m_lifeTimer(0),
m_rangeStep(0),
m_scanRange(0),
m_stateTimer(0),
m_returnTimer(0),
m_scanStrength(0),
m_scanDeviation(0),
m_rangeFactor(2),
m_scanShips(false)
{
    m_state = Probe::State::Inactive;

    if (self->HasAttribute(AttrProbeCanScanShips))
        m_scanShips = self->GetAttribute(AttrProbeCanScanShips).get_bool();

    // set probe lifetime of 5h
    m_expiry = GetFileTimeNow() + (EvE::Time::Hour *5);  // 5h abandoned lifespan
    m_lifeTimer.Start(5*60*60*1000);        // 5h to ms

    _log(SCAN__INFO, "Created Abandoned ProbeSE for %u. expiry: %lli", m_self->itemID(), m_expiry);
}

ProbeSE::ProbeSE(ProbeItemRef self, EVEServiceManager& services, SystemManager* system, InventoryItemRef moduleRef, ShipItemRef shipRef)
: DynamicSystemEntity(self, services, system),
m_scan(nullptr),
m_state(Probe::State::Idle),
m_client(shipRef->GetPilot()),
m_shipRef(shipRef),
m_moduleRef(moduleRef),
m_lifeTimer(0),
m_rangeStep(0),
m_scanRange(0),
m_stateTimer(0),
m_returnTimer(0),
m_rangeFactor(2),
m_scanStrength(0),
m_scanDeviation(0),
m_scanShips(false)
{
    m_warID = m_client->GetWarFactionID();
    m_allyID = m_client->GetAllianceID();
    m_corpID = m_client->GetCorporationID();
    m_ownerID = m_client->GetCharacterID();

    m_secStatus = m_client->GetSecurityRating();

    if (self->HasAttribute(AttrProbeCanScanShips))
        m_scanShips = self->GetAttribute(AttrProbeCanScanShips).get_bool();

    // set probe lifetime
    m_expiry = GetFileTimeNow() + (self->GetAttribute(AttrExplosionDelay).get_float() *10000); // ms to filetime
    if (m_expiry < GetFileTimeNow()) {
        m_expiry = GetFileTimeNow() + (EvE::Time::Minute *30);  // 30m default lifespan
        m_lifeTimer.Start(30*60*1000);        // 30m to ms
    } else {
        m_lifeTimer.Start(self->GetAttribute(AttrExplosionDelay).get_uint32());
    }

    CalcStats();
    SetRangeStep(5);
    m_state = Probe::State::Idle;

    _log(SCAN__INFO, "Created ProbeSE for %u. timeNow: %.0f, expiry: %lli, scan Str: %.4f, deviation: %.5f, ship: %s", \
            m_self->itemID(), GetFileTimeNow(), m_expiry, m_scanStrength, m_scanDeviation, m_scanShips?"true":"false");
}

void ProbeSE::CalcStats()
{
    /* Crucible probe strength and deviation.  every bonus is a PostPercent on the probe's baseSensorStrength (effect 4162
     *  and the hull/subsystem effects) or baseMaxScanDeviation (effect 4161), all from the data:
     *    skills:  Astrometric Rangefinding +10%/lvl strength, Astrometric Pinpointing -10%/lvl deviation
     *    launcher:  scanStrengthBonus (Core/Expanded II +5, Sisters +10)
     *    rigs:  Gravity Capacitor Upgrade +10/+15
     *    hull:  Magnate/Heron/Imicus/Probe +5%/frigate lvl, covert ops +10%/lvl, T3 electronic subsystems +x%/lvl
     *  the probe item is fresh (no dogma), so this is done here from type values.  only one hull bonus applies per hull
     */
    Character* pChar(m_client->GetChar().get());
    m_rangeFactor = m_self->type().GetAttribute(AttrRangeFactor).get_float();
    if (m_rangeFactor < 1)
        m_rangeFactor = 2;
    double strength(m_self->type().GetAttribute(AttrBaseSensorStrength).get_double());
    double deviation(m_self->type().GetAttribute(AttrBaseMaxScanDeviation).get_double());

    // skills
    std::vector<InventoryItemRef> skills;
    pChar->GetSkillsList(skills);
    for (auto cur : skills) {
        uint8 level(cur->GetAttribute(AttrSkillLevel).get_uint32());
        if (level < 1)
            continue;
        if (cur->type().HasEffect(4162))    // baseSensorStrengthModifierRequiringAstrometrics
            strength *= (1.0 + 0.01 * cur->type().GetAttribute(AttrScanStrengthBonus).get_double() * level);
        if (cur->type().HasEffect(4161))    // baseMaxScanDeviationModifierRequiringAstrometrics
            deviation *= (1.0 + 0.01 * cur->type().GetAttribute(AttrMaxScanDeviationModifier).get_double() * level);
    }

    // launcher
    if ((m_moduleRef.get() != nullptr) and m_moduleRef->type().HasEffect(4162))
        strength *= (1.0 + 0.01 * m_moduleRef->type().GetAttribute(AttrScanStrengthBonus).get_double());

    // rigs and subsystems
    std::vector<InventoryItemRef> modules;
    m_shipRef->GetModuleManager()->GetModuleListOfRefsAsc(modules);
    for (auto cur : modules) {
        if ((cur->flag() >= flagRigSlot0) and (cur->flag() <= flagRigSlot7)) {
            if (cur->type().HasEffect(4162))
                strength *= (1.0 + 0.01 * cur->type().GetAttribute(AttrScanStrengthBonus).get_double());
            if (cur->type().HasEffect(4161))
                deviation *= (1.0 + 0.01 * cur->type().GetAttribute(AttrMaxScanDeviationModifier).get_double());
        } else if ((cur->flag() >= flagSubSystem0) and (cur->flag() <= flagSubSystem7)) {
            struct SubBonus { uint16 effectID; uint16 attr; uint16 skill; };
            static const SubBonus subs[] = {
                { 4389, AttrSubsystemBonusAmarrElectronic,     EvESkill::AmarrElectronicSystems },
                { 4390, AttrSubsystemBonusCaldariElectronic,   EvESkill::CaldariElectronicSystems },
                { 4391, AttrSubsystemBonusGallenteElectronic,  EvESkill::GallenteElectronicSystems },
                { 4392, AttrSubsystemBonusMinmatarElectronic,  EvESkill::MinmatarElectronicSystems }
            };
            for (auto& sub : subs)
                if (cur->type().HasEffect(sub.effectID))
                    strength *= (1.0 + 0.01 * cur->type().GetAttribute(sub.attr).get_double() * pChar->GetSkillLevel(sub.skill));
        }
    }

    // hull
    struct HullBonus { uint16 effectID; uint16 attr; uint16 skill; };
    static const HullBonus hulls[] = {
        { 4164, AttrShipBonusAF,          EvESkill::AmarrFrigate },        // Magnate
        { 4165, AttrShipBonus2CF,         EvESkill::CaldariFrigate },      // Heron
        { 4166, AttrShipBonus2MF,         EvESkill::MinmatarFrigate },     // Probe
        { 4167, AttrShipBonus2GF,         EvESkill::GallenteFrigate },     // Imicus
        { 4163, AttrEliteBonusCoverOps2,  EvESkill::CovertOps },           // covert ops
        { 4168, AttrEliteBonusCoverOps2,  EvESkill::CovertOps }
    };
    const ItemType& sType(m_shipRef->type());
    for (auto& hull : hulls)
        if (sType.HasEffect(hull.effectID))
            strength *= (1.0 + 0.01 * sType.GetAttribute(hull.attr).get_double() * pChar->GetSkillLevel(hull.skill));

    /** @todo F15 (implants):  implants with effect 4162/4161 (Virtue set, PPH/PPF hardwirings) multiply here like the
     *   rigs, once Character exposes its fitted implants.  the Virtue set bonus is F15's */

    m_scanStrength = strength * sConfig.exploring.ProbeStrengthScale;
    m_scanDeviation = deviation;
}

    /*
    AttrScanAnalyzeCount = 791,     (no db data)
    AttrScanProbeStrength = 1116,         (no db data)
    AttrScanStrengthSignatures = 1117,         (no db data)
    AttrScanStrengthDronesProbes = 1118,         (no db data)
    AttrScanStrengthScrap = 1119,         (no db data)
    AttrScanStrengthShips = 1120,         (no db data)
    AttrScanStrengthStructures = 1121,         (no db data)
    AttrMaxScanGroups = 1122,         (no db data)
    AttrScanDuration = 1123,     How long this probe has to scan until it can obtain results. (no db data)
    AttrScanFrequencyResult = 1161,             (no db data)
    AttrProbeCanScanShips = 1413,       boolean

    // these are char attribs
    AttrScanStrengthBonus = 846,        in %
    AttrMaxScanDeviationModifier = 1156,

    // for moon goo
    AttrScanRange = 765,
    AttrMinScanDeviation = 787,
    AttrMaxScanDeviation = 788,

    // new shit from rhea
    AttrScanStrengthBonusModule = 1907,
    */

ProbeSE::~ProbeSE() {
    if (m_scan != nullptr)
        m_scan->RemoveProbe(this);
}

bool ProbeSE::ProcessTic()
{
    // the caller (EntityList's probe loop) erases us from its map when this returns false
    if (m_lifeTimer.Check()) {
        _log(SCAN__INFO, "ProbeSE::Process() probe %u expired", m_self->itemID());
        RemoveProbe(false);
        m_self->Delete();
        delete(this);
        return false;
    }
    if (m_returnTimer.Enabled())
        if (m_returnTimer.Check()) {
            m_returnTimer.Disable();
            _log(SCAN__INFO, "ProbeSE::Process() return timer hit for probeID %u", m_self->itemID());
            if (m_state == Probe::State::Warping)
                SendWarpEnd();
            if (!CanReturn()) {
                // the ship it was returning to is gone.  the probe stays in space, lost
                if (m_client != nullptr)
                    m_client->SendRemoteMessage("ScannerProbeShipLeftReturnAborted");
                Abandon();
                return true;
            }
            if (!m_shipRef->GetMyInventory()->HasAvailableSpace(flagCargoHold, m_self)) {
                m_client->SendNotifyMsg("Your cargo hold has no room for the %s.  It waits where it is.", m_self->name());
                SendStateChange(Probe::State::Idle);
                return true;
            }
            RemoveProbe(false);
            m_self->MergeTypesInCargo(m_shipRef.get(), flagCargoHold);
            delete(this);
            return false;
        }
    if (m_stateTimer.Enabled())
        if (m_stateTimer.Check()) {
            m_stateTimer.Disable();
            _log(SCAN__INFO, "ProbeSE::Process() state timer hit for probeID %u  state: %s", \
                    m_self->itemID(), GetStateName(m_state));
            if (IsMoving())
                m_self->SetPosition(m_destination);
            if (m_state == Probe::State::Warping)
                SendWarpEnd();
            SendStateChange(Probe::State::Idle);
        }
    return true;
}

bool ProbeSE::IsMoving()
{
    switch (m_state) {
        case Probe::State::Idle:
        case Probe::State::Inactive:
        case Probe::State::Scanning:
        case Probe::State::Waiting:
            return false;
        case Probe::State::Moving:
        case Probe::State::Warping:
        case Probe::State::Returning:
            return true;
    }
    return false;
}

void ProbeSE::SetRangeStep(uint8 step)
{
    if (step < 1)
        step = 1;
    if (step > 8)       // const.scanProbeNumberOfRangeSteps
        step = 8;
    m_rangeStep = step;
    m_scanRange = m_self->type().GetAttribute(AttrBaseScanRange).get_float() * std::pow(m_rangeFactor, m_rangeStep - 1) * ONE_AU_IN_METERS;
}

void ProbeSE::UpdateProbe(ProbeData& data)
{
    SetRangeStep(data.rangeStep);
    m_destination = data.dest;

    float time(1), dist = GetPosition().distance(m_destination);
    if (dist < 100) {
        time = 0.5f;
    } else if (dist > BUBBLE_RADIUS_METERS){
        float wsm = m_self->GetAttribute(AttrWarpSpeedMultiplier).get_float() * (ONE_AU_IN_METERS /4);
        time = EvE::max(dist / wsm, 1);
        SendStateChange(Probe::State::Warping);
        SendWarpStart(time);
    } else {
        SendStateChange(Probe::State::Moving);
        uint32 mv = m_self->GetAttribute(AttrMaxVelocity).get_uint32();
        time = EvE::max(dist / mv, 1);
    }

    m_stateTimer.Start(time *1000);
    _log(SCAN__TRACE, "ProbeSE::UpdateProbe()  id:%u, state: %s, scanRange: %.2f, step: %u, dist:%.2f, time: %.2f", \
                GetID(), GetStateName(m_state), m_scanRange, m_rangeStep, dist, time );
}

bool ProbeSE::CanReturn()
{
    if ((m_client == nullptr) or (m_shipRef.get() == nullptr))
        return false;
    return (m_client->IsInSpace() and (m_client->GetShipID() == m_shipRef->itemID()) and (m_client->SystemMgr() == m_system));
}

uint32 ProbeSE::GetLauncherDuration()
{
    if (m_moduleRef.get() == nullptr)
        return 10000;
    uint32 duration(m_moduleRef->GetAttribute(AttrDuration).get_uint32());
    return (duration < 1000 ? 10000 : duration);
}

void ProbeSE::Abandon()
{
    _log(SCAN__INFO, "ProbeSE::Abandon() - probe %u of %u is lost.", m_self->itemID(), m_ownerID);
    // no client from here on.  a warp in progress ends at its destination
    m_client = nullptr;
    m_scan = nullptr;
    m_returnTimer.Disable();
    if (m_stateTimer.Enabled()) {
        m_stateTimer.Disable();
        if (IsMoving()) {
            if (m_state == Probe::State::Warping) {
                SendWarpEnd();
            } else {
                m_self->SetPosition(m_destination);
            }
        }
    }
    m_state = Probe::State::Inactive;
}

void ProbeSE::Reconnect(Client* pClient, ShipItemRef shipRef, InventoryItemRef launcherRef)
{
    m_client = pClient;
    m_shipRef = shipRef;
    m_moduleRef = launcherRef;

    m_warID = m_client->GetWarFactionID();
    m_allyID = m_client->GetAllianceID();
    m_corpID = m_client->GetCorporationID();
    m_secStatus = m_client->GetSecurityRating();

    CalcStats();
    m_state = Probe::State::Idle;
    SendNewProbe();
    _log(SCAN__INFO, "ProbeSE::Reconnect() - probe %u reconnected to %s", m_self->itemID(), m_client->GetName());
}

void ProbeSE::RecoverProbe(PyList* list)
{
    if (!CanReturn())
        return;
    // only probes at rest come back, and only into room in the hold
    if ((m_state != Probe::State::Idle) and (m_state != Probe::State::Inactive))
        return;
    if (!m_shipRef->GetMyInventory()->HasAvailableSpace(flagCargoHold, m_self))
        return;
    m_destination = m_shipRef->position() + 250;
    float time(1), dist = GetPosition().distance(m_destination);
    if (dist > BUBBLE_RADIUS_METERS){
        float wsm = m_self->GetAttribute(AttrWarpSpeedMultiplier).get_float() * (ONE_AU_IN_METERS /4);
        time = EvE::max(dist / wsm, 1);
        //SendWarpStart(time);
    } else {
        uint32 mv = m_self->GetAttribute(AttrMaxVelocity).get_uint32();
        time = EvE::max(dist / mv, 1);
    }

    /** @todo  verify probe status and controller before adding to "recover success list" */
    // add to list if still controlled by player
    list->AddItem(new PyInt(m_self->itemID()));
    m_returnTimer.Start(time * 1000);
    SendStateChange(Probe::State::Returning);
    _log(SCAN__TRACE, "ProbeSE::RecoverProbe()  Probe %u returning.  Return time is %.2fs", \
                GetID(), time);
}

void ProbeSE::SendNewProbe()
{
    if (m_client == nullptr)
        return;
    ScanResultPos ssr_oed;
        ssr_oed.x = GetPosition().x;
        ssr_oed.y = GetPosition().y;
        ssr_oed.z = GetPosition().z;
    PyToken* token = new PyToken("foo.Vector3");
    PyTuple* oed_tuple = new PyTuple(2);
        oed_tuple->SetItem(0, token);
        oed_tuple->SetItem(1, ssr_oed.Encode());
    PyDict* newProbe = new PyDict();
        newProbe->SetItemString("probeID",      new PyLong(m_self->itemID()));
        newProbe->SetItemString("typeID",       new PyInt(m_self->typeID()));
        newProbe->SetItemString("scanRange",    new PyFloat(m_scanRange));
        newProbe->SetItemString("expiry",       new PyLong(m_expiry));
        newProbe->SetItemString("pos",          new PyObjectEx(false, oed_tuple));
    PyTuple* ev = new PyTuple(1);
        ev->SetItem(0, new PyObject("util.KeyVal", newProbe));
    m_client->SendNotification("OnNewProbe", "clientID", &ev);  // this is sequenced
}

void ProbeSE::SendStateChange(uint8 state)
{
    if (m_state == state)
        return;

    _log(SCAN__TRACE, "ProbeSE::SendStateChange()  Probe %u changing state to %s.", GetID(), GetStateName(state));
    m_state = state;
    if (m_client == nullptr)
        return;
    PyTuple* tuple = new PyTuple(2);
        tuple->SetItem(0, new PyLong(m_self->itemID()));
        tuple->SetItem(1, new PyInt(state));
    m_client->SendNotification("OnProbeStateChanged", "clientID", &tuple);  // this is sequenced
}

void ProbeSE::RemoveProbe(bool fromEntityList/*true*/)
{
    // remove from scan map, but check for abandoned probes (no scan)
    if (m_scan != nullptr) {
        m_scan->RemoveProbe(this);
        m_scan = nullptr;
    }
    // remove from system
    m_system->RemoveEntity(this);
    // set item loc to null
    m_self->SetPosition(NULL_ORIGIN);
    // remove from entity list
    if (fromEntityList)
        sEntityList.RemoveProbe(m_self->itemID());

    if (m_client == nullptr)
        return;
    PyTuple* ev = new PyTuple(1);
        ev->SetItem(0, new PyLong(m_self->itemID()));
    m_client->SendNotification("OnRemoveProbe", "clientID", &ev);  // this is sequenced
}

void ProbeSE::SendWarpStart(float travelTime/*0*/)
{
    // remove from bubble
    m_bubble->Remove(this);

    PyToken* token = new PyToken("foo.Vector3");
    ScanResultPos posFrom;
        posFrom.x = m_self->position().x;
        posFrom.y = m_self->position().y;
        posFrom.z = m_self->position().z;
    ScanResultPos posTo;
        posTo.x = m_destination.x;
        posTo.y = m_destination.y;
        posTo.z = m_destination.z;
    PyTuple* from = new PyTuple(2);
        from->SetItem(0, token);
        from->SetItem(1, posFrom.Encode());
    PyTuple* to = new PyTuple(2);
        to->SetItem(0, token);
        to->SetItem(1, posTo.Encode());
    // OnProbeWarpStart(self, probeID, fromPos, toPos, startTime, duration)
    PyTuple* tuple = new PyTuple(5);
        tuple->SetItem(0, new PyLong(m_self->itemID()));    //probeID
        tuple->SetItem(1, new PyObjectEx(false, from));     //from
        tuple->SetItem(2, new PyObjectEx(false, to));       //to
        tuple->SetItem(3, new PyLong(GetFileTimeNow()));    //startTime
        tuple->SetItem(4, new PyFloat(travelTime));         //duration in ms
    if (m_client == nullptr) {
        PyDecRef(tuple);
        return;
    }
    m_client->SendNotification("OnProbeWarpStart", "clientID", &tuple);  // this is sequenced

    // not sure if this will work right...slimItem is removed when probe warps out
    //SendSlimChange();
}

void ProbeSE::SendWarpEnd()
{
    m_self->SetPosition(m_destination);
    sBubbleMgr.Add(this);
    if (m_client == nullptr)
        return;
    PyTuple* tuple = new PyTuple(1);
        tuple->SetItem(0, new PyLong(m_self->itemID()));
    m_client->SendNotification("OnProbeWarpEnd", "clientID", &tuple);  // this is sequenced
}

const char* ProbeSE::GetStateName(uint8 state)
{
    switch(state) {
        case Probe::State::Inactive:   return "Inactive";
        case Probe::State::Idle:       return "Idle";
        case Probe::State::Moving:     return "Moving";
        case Probe::State::Warping:    return "Warping";
        case Probe::State::Scanning:   return "Scanning";
        case Probe::State::Returning:  return "Returning";
    }

    return "Undefined";
}

float ProbeSE::GetEffectiveStrength()
{
    // strength halves each time the range doubles
    return m_scanStrength / std::pow(m_rangeFactor, (m_rangeStep > 0 ? m_rangeStep - 1 : 0));
}

PyDict* ProbeSE::MakeSlimItem()
{
    _log(SE__SLIMITEM, "MakeSlimItem for ProbeSE %s(%u)", GetName(), m_self->itemID());
    PyDict* slim = new PyDict();
    slim->SetItemString("itemID",                   new PyLong(m_self->itemID()));
    slim->SetItemString("typeID",                   new PyInt(m_self->typeID()));
    slim->SetItemString("ownerID",                  new PyInt(m_ownerID));
    slim->SetItemString("corpID",                   IsCorp(m_corpID) ? new PyInt(m_corpID) : PyStatic.NewNone());
    slim->SetItemString("allianceID",               IsAlliance(m_allyID) ? new PyInt(m_allyID) : PyStatic.NewNone());
    slim->SetItemString("warFactionID",             IsFaction(m_warID) ? new PyInt(m_warID) : PyStatic.NewNone());
    slim->SetItemString("numLaunchers",             PyStatic.NewOne());
    slim->SetItemString("sourceModuleID",           m_moduleRef.get() != nullptr? new PyInt(m_moduleRef->itemID()): PyStatic.NewNone());
    slim->SetItemString("securityStatus",           new PyFloat(m_secStatus));
    return slim;
}

void ProbeSE::MakeDamageState(DoDestinyDamageState &into) {
    into.shield = 1.0;
    into.recharge = 1000000;
    into.timestamp = GetFileTimeNow();
    into.armor = 1.0;
    into.structure = 1.0 - (m_self->GetAttribute(AttrDamage).get_double() / m_self->GetAttribute(AttrHP).get_double());
}

void ProbeSE::SendSlimChange()
{
    PyDict* slim = new PyDict();
        slim->SetItemString("itemID",                   new PyLong(m_self->itemID()));
        slim->SetItemString("typeID",                   new PyInt(m_self->typeID()));
        slim->SetItemString("categoryID",               new PyInt(m_self->categoryID()));
        slim->SetItemString("ownerID",                  new PyInt(m_ownerID));
        slim->SetItemString("corpID",                   IsCorp(m_corpID) ? new PyInt(m_corpID) : PyStatic.NewNone());
        slim->SetItemString("allianceID",               IsAlliance(m_allyID) ? new PyInt(m_allyID) : PyStatic.NewNone());
        slim->SetItemString("warFactionID",             IsFaction(m_warID) ? new PyInt(m_warID) : PyStatic.NewNone());
        slim->SetItemString("numLaunchers",             PyStatic.NewOne());
        slim->SetItemString("sourceModuleID",           m_moduleRef.get() != nullptr? new PyInt(m_moduleRef->itemID()):PyStatic.NewNone());
        slim->SetItemString("securityStatus",           new PyFloat(m_secStatus));
        slim->SetItemString("warpingAway",              m_state == Probe::State::Returning ? PyStatic.NewFalse() : PyStatic.NewTrue());    // this is sent when probe warps
    PyTuple* probeData = new PyTuple(2);
        probeData->SetItem(0, new PyLong(m_self->itemID()));
        probeData->SetItem(1, new PyObject("foo.SlimItem", slim));
    PyTuple* updates = new PyTuple(2);
        updates->SetItem(0, new PyString("OnSlimItemChange"));
        updates->SetItem(1, probeData);
    m_destiny->SendSingleDestinyUpdate(&updates, true);
}

/*
                      [PyString "OnSlimItemChange"]
                      [PyTuple 2 items]
                        [PyIntegerVar 9000000000000020440]
                        [PyObjectData Name: foo.SlimItem]
                          [PyDict 10 kvp]
                            [PyString "itemID"]
                            [PyIntegerVar 9000000000000020440]
                            [PyString "typeID"]
                            [PyInt 30028]
                            [PyString "allianceID"]
                            [PyNone]
                            [PyString "sourceModuleID"]
                            [PyIntegerVar 1002331157594]
                            [PyString "corpID"]
                            [PyInt 98038978]
                            [PyString "numLaunchers"]
                            [PyInt 1]
                            [PyString "warpingAway"]
                            [PyInt 1]
                            [PyString "warFactionID"]
                            [PyNone]
                            [PyString "securityStatus"]
                            [PyFloat 3.09141417479958]
                            [PyString "ownerID"]
                            [PyInt 1661059544]
                            */
