
 /**
  * @name Scan.cpp
  *   Scanning methods for EVEmu
  *
  * @Author:        Allan
  * @date:          7Dec15 (working)
  * @udpate:        12Mar18 (probes)
  *
  * @version:       0.42
  */

// w.i.p.

/* SCAN__ERROR
 * SCAN__WARNING
 * SCAN__MESSAGE
 * SCAN__DEBUG
 * SCAN__INFO
 * SCAN__TRACE
 * SCAN__DUMP
 * SCAN__RSPDUMP
 */

#include "eve-server.h"

#include "Client.h"
#include "EntityList.h"
#include "StatisticMgr.h"
#include "exploration/Scan.h"
#include "math/Trig.h"
#include "Probes.h"
#include "system/SystemBubble.h"
#include "system/SystemManager.h"
#include "system/cosmicMgrs/AnomalyMgr.h"

Scan::Scan(Client* pClient)
: m_client(pClient)
{
    m_busy = false;
    m_probeScan = false;
    m_probeMap.clear();
    m_activeProbeMap.clear();
}

void Scan::AddProbe(ProbeSE* pProbe)
{
    pProbe->SetScan(this);
    m_probeMap.emplace(pProbe->GetID(), pProbe);
}

void Scan::RemoveProbe(ProbeSE* pProbe)
{
    m_probeMap.erase(pProbe->GetID());
    m_activeProbeMap.erase(pProbe->GetID());
}

void Scan::AbandonProbes(bool notify/*false*/)
{
    _log(SCAN__TRACE, "Scan::AbandonProbes() - %s loses control of %u probes", m_client->GetName(), m_probeMap.size());
    for (auto cur : m_probeMap)
        cur.second->Abandon();
    m_probeMap.clear();
    m_activeProbeMap.clear();
    m_probeScan = false;
    m_busy = false;

    if (!notify)
        return;
    PyTuple* tuple = new PyTuple(0);
    m_client->SendNotification("OnScannerInfoRemoved", "charid", &tuple);
}

void Scan::ProcessScan(bool useProbe/*false*/)
{
    if (!useProbe) {
        ShipScanResult();
        m_busy = false;
        return;
    }
    if (m_probeScan) {
        ProbeScanResult();
        m_probeScan = false;
        m_busy = false;
        m_activeProbeMap.clear();
        return;
    }
    if (m_activeProbeMap.empty()) {
        // the probes went away meanwhile
        m_busy = false;
        return;
    }

    // probe scan time is the launcher's duration (10s, less with Astrometric Acquisition and hull bonuses)
    bool idle = true;
    uint32 moveTime(0), duration(0);
    for (auto cur : m_activeProbeMap) {
        duration = std::max<uint32>(duration, cur.second->GetLauncherDuration());
        if (cur.second->IsMoving()) {
            idle = false;
            moveTime = std::max<uint32>(moveTime, cur.second->GetMoveTime());
        }
    }
    _log(SCAN__TRACE, "ProcessScan() - probes - active:%lu, total:%lu, duration: %u, moving: %ums", \
                m_activeProbeMap.size(), m_probeMap.size(), duration, moveTime);
    if (!idle) {
        // check again when the last probe arrives
        m_client->SetScanTimer(moveTime + 100, true);
        return;
    }

    m_probeScan = true;
    for (auto cur : m_activeProbeMap) {
        cur.second->SendStateChange(Probe::State::Scanning);
        cur.second->StartStateTimer(duration);
    }
    SystemScanStarted(duration);
    m_client->SetScanTimer(duration, true);
}


PyRep* Scan::ConeScan(Call_ConeScan args) {
    //  WORKING CODE...DONT FUCK WITH THIS!!  -allan 7Dec15

    // NOTE:  max distance is 14.4AU or maxInt (2417482647 in km)
    /* to find if a point is inside a cone.     (-allan 10Aug20)
     * U = unit vector along cone axis (sent from client, decoded as x,y,z)
     * VR = vector from cone vertex(V) to test point(R).
     * Uvr = unit vector of cone vertex to test point (normalized VR)
     * DP = Uvr dot U (dot product of Uvr and U).  this will give cosine of angle between VR and U
     * acDP = arc cosine of DP to give angle
     * test acDP < cone angle = point is inside cone.
     */
    bool test(false);
    float dot(0), acDP(0), angle(args.ScanAngle/2);
    std::vector<SystemEntity*> seVec;
    const GPoint vertex(m_client->GetShipSE()->GetPosition());
    const GPoint U(args.x, args.y, args.z);
    m_client->SystemMgr()->DScan(args.range, vertex, seVec);
    _log(SCAN__TRACE, "ConeScan() - query returned %u objects within range.  angle is %.3f", seVec.size(), angle);
    PyList* list = new PyList();
    for (auto cur : seVec ) {
        GVector VR(vertex, cur->GetPosition());
        VR.normalize();
        dot = U.dotProduct(VR);
        acDP = acos(dot);
        if (acDP < angle) {
            test = true;
            DirectionScanResult res;
            res.id         = cur->GetID();
            res.typeID     = cur->GetSelf()->typeID();
            res.groupID    = cur->GetSelf()->groupID();
            list->AddItem(res.Encode());
        }
        _log(SCAN__TRACE, "ConeScan() - tested %s(%u).  dot %.5f, acDP %.5f, result %s", cur->GetName(), cur->GetID(), dot, acDP, test?"true":"false");
        test = false;
    }

    return list;
}

void Scan::RequestScans(PyDict* dict) {
    if (IsScanning())
        throw UserError ("ScnAlreadyInProgress");

    // on-board scanner time is the ship's scanSpeed
    uint32 duration = m_client->GetShip()->GetAttribute(AttrScanSpeed).get_uint32();
    if (duration < 1000)
        duration = 8000;
    if ((dict == nullptr) or dict->empty()) {
        if (m_client->GetShip()->groupID() == EVEDB::invGroups::Capsule)
            throw UserError ("PodCannotScan");
        if (!m_probeMap.empty())
            throw UserError ("ScnShipScannerTooManyProbes");
        _log(SCAN__MESSAGE, "Scan::RequestScans() called by %s in %s using ship scanner.", \
                m_client->GetName(), m_client->GetSystemName().c_str());

        OnSystemScanStarted ossst;
            ossst.timestamp = GetFileTimeNow();
            ossst.duration = duration;
            ossst.scanProbesDict = new PyDict();
        PyTuple* ev = ossst.Encode();
        m_client->SendNotification("OnSystemScanStarted", "charid", &ev);
        m_busy = true;
        m_client->SetScanTimer(duration);
        return;
    }

    _log(SCAN__MESSAGE, "Scan::RequestScans() called by %s in %s using %u probes.",\
            m_client->GetName(), m_client->GetSystemName().c_str(), dict->size());

    uint32 probeID(0);
    PyDict::const_iterator cItr = dict->begin();
    for (; cItr != dict->end(); ++cItr) {
        // find probe in map....
        probeID = PyRep::IntegerValueU32(cItr->first);  // key
        std::map<uint32, ProbeSE*>::iterator pItr = m_probeMap.find(probeID);
        if (pItr == m_probeMap.end()) {
            _log(SCAN__ERROR, "Probe %u wasnt found in the probeMap for %s(%u)", \
                    probeID, m_client->GetName(), m_client->GetCharacterID());
            continue;  // make error here?
        }
        std::map<uint32, ProbeSE*>::iterator aItr = m_activeProbeMap.find(probeID);
        if (aItr == m_activeProbeMap.end())
            m_activeProbeMap[probeID] = pItr->second;

        Call_ProbeDataObj args;
        if (!args.Decode(cItr->second)) { // value
            _log(SERVICE__ERROR, "Scan::RequestScans::DecodeProbeData: Failed to decode arguments.");
            // make error here
            continue;
        }

        // the range comes from the range step (the client's scanRange isn't trusted)
        ProbeData data = ProbeData();
            data.state = args.state;    // do we need this?
            data.expiry = args.expiry;
            data.rangeStep = args.rangeStep;
            data.scanRange = 0;
        // set probe target
        PyObjectEx* obj = args.destination->AsObjectEx();
        PyTuple* dest = obj->header()->AsTuple()->GetItem(1)->AsTuple();
            data.dest.x = dest->GetItem(0)->AsFloat()->value();
            data.dest.y = dest->GetItem(1)->AsFloat()->value();
            data.dest.z = dest->GetItem(2)->AsFloat()->value();
        pItr->second->UpdateProbe(data);
    }

    // loop thru probe maps to see if any are disabled, then set status as 'inactive' to omit from tracking
    for (auto cur : m_probeMap) {
        std::map<uint32, ProbeSE*>::iterator itr = m_activeProbeMap.find(cur.first);
        if (itr == m_activeProbeMap.end())
            cur.second->SetState(Probe::State::Inactive);
    }
    if (m_activeProbeMap.empty())
        return;

    m_busy = true;
    // first check right after the probes' moves are set (ProcessScan waits for them)
    m_client->SetScanTimer(100, true);
}

void Scan::SystemScanStarted(uint32 duration)
{
    _log(SCAN__TRACE, "Scan::SystemScanStarted()  for %s in system %u", m_client->GetName(), m_client->GetSystemID());

    PyDict* probeDict = new PyDict();
    for (auto cur : m_activeProbeMap) {
        // probe data here...
        ScanProbesDict spd;
            spd.expiry = cur.second->GetExpiryTime();
            spd.maxDeviation = cur.second->GetMaxDeviation();
            spd.probeID = cur.first;
            spd.state = cur.second->GetState();
            spd.rangeStep = cur.second->GetRangeStep();
            spd.scanRange = cur.second->GetScanRange();
            spd.scanStrength = cur.second->GetEffectiveStrength();
            spd.typeID = cur.second->GetSelf()->typeID();
            spd.pos = MakeVector3(cur.second->GetPosition());
            PyIncRef(spd.pos);
            spd.destination = spd.pos;
        probeDict->SetItem(new PyInt(cur.first), spd.Encode());
    }

    OnSystemScanStarted ossst;
        ossst.timestamp = GetFileTimeNow();
        ossst.duration = duration;
        ossst.scanProbesDict = probeDict;
    PyTuple* ev = ossst.Encode();
    ev->Dump(SCAN__RSPDUMP, "sss-    ");
    m_client->SendNotification("OnSystemScanStarted", "charid", &ev, false);
}

PyRep* Scan::MakeVector3(const GPoint& pos)
{
    ScanResultPos srp;
        srp.x = pos.x;
        srp.y = pos.y;
        srp.z = pos.z;
    PyTuple* oed_tuple = new PyTuple(2);
        oed_tuple->SetItem(0, new PyToken("foo.Vector3"));
        oed_tuple->SetItem(1, srp.Encode());
    return new PyObjectEx(false, oed_tuple);  // oed goes here
}

void Scan::CacheResult(const CosmicSignature& sig, float certainty)
{
    // the client keeps sig/anom results with a point at >= 100% until they are listed in absentTargets
    if ((certainty < 1.0f) or !((sig.scanGroupID == Scanning::Group::Signature) or (sig.scanGroupID == Scanning::Group::Anomaly)))
        return;
    m_cachedIDs.insert(sig.sigID);
}

PyList* Scan::GetAbsentTargets()
{
    PyList* absent = new PyList();
    if (m_cachedIDs.empty())
        return absent;
    std::vector<CosmicSignature> sigs;
    m_client->SystemMgr()->GetAnomMgr()->GetAnomalyList(sigs);
    m_client->SystemMgr()->GetAnomMgr()->GetSignatureList(sigs);
    std::set<std::string> current;
    for (auto& cur : sigs)
        current.insert(cur.sigID);
    std::set<std::string>::iterator itr = m_cachedIDs.begin();
    while (itr != m_cachedIDs.end()) {
        if (current.find(*itr) == current.end()) {
            absent->AddItem(new PyString(*itr));
            itr = m_cachedIDs.erase(itr);
        } else {
            ++itr;
        }
    }
    return absent;
}

void Scan::ShipScanResult() {
    //  WORKING CODE...DONT FUCK WITH THIS!!  -allan 11Dec15
    // the on-board scanner finds cosmic anomalies only (ScnShipScannerDeadspaceOnly)
    std::vector<CosmicSignature> anom;
    if (m_client->IsShowall()) {
        m_client->SystemMgr()->GetAllEntities(anom);
        // bubble centers only populate when bubble markers are enabled.
        sBubbleMgr.GetBubbleCenterMarkers(m_client->SystemMgr()->GetID(), anom);
    } else {
        m_client->SystemMgr()->GetAnomMgr()->GetAnomalyList(anom);
    }

    PyList* resultList = new PyList();
    for (auto anoms : anom) {
        SystemScanResult ssr;
            ssr.typeID = anoms.sigTypeID;
            ssr.scanGroupID = anoms.scanGroupID;
            ssr.groupID = anoms.sigGroupID;
            ssr.strengthAttributeID = anoms.scanAttributeID;
            ssr.dungeonName = anoms.sigName;
            ssr.id = anoms.sigID;
            ssr.deviation = 0;     /* for scan probes */
            ssr.degraded = false;  /* dunno what this does.  have only seen 'false' in packets */
            ssr.probeID = new PyInt(m_client->GetShipID());
            ssr.certainty = anoms.sigStrength;
            ssr.pos = PyStatic.NewNone();
            ssr.data = MakeVector3(anoms.position);
        resultList->AddItem(ssr.Encode());
        CacheResult(anoms, anoms.sigStrength);
    }

    OnSystemScanStopped osss;
        osss.systemScanResult = resultList;
        // probeDict is empty when using ship sensors to scan.
        osss.scanProbesDict = new PyDict();
        osss.absentTargets = GetAbsentTargets();
    PyTuple* ev = osss.Encode();
    ev->Dump(SCAN__RSPDUMP, "ssr-    ");
    m_client->SendNotification("OnSystemScanStopped", "charid", &ev);
}

void Scan::ProbeScanResult()
{
    _log(SCAN__TRACE, "Scan::ProbeScanResult()  for %s in system %u", m_client->GetName(), m_client->GetSystemID());

    PyList* resultList = new PyList();
    std::vector<CosmicSignature> sig, anom;

    // anomalies need no probes.  they are always found, at 100%
    m_client->SystemMgr()->GetAnomMgr()->GetAnomalyList(anom);
    for (auto anoms : anom) {
        SystemScanResult ssr;
            ssr.typeID = anoms.sigTypeID;
            ssr.scanGroupID = anoms.scanGroupID;
            ssr.groupID = anoms.sigGroupID;
            ssr.strengthAttributeID = anoms.scanAttributeID;
            ssr.dungeonName = anoms.sigName;
            ssr.id = anoms.sigID;
            ssr.deviation = 0;     /* 0 for anomalies */
            ssr.degraded = false;
            ssr.probeID = new PyInt(m_client->GetShipID());
            ssr.certainty = anoms.sigStrength;
            ssr.pos = PyStatic.NewNone();
            ssr.data = MakeVector3(anoms.position);
        resultList->AddItem(ssr.Encode());
        CacheResult(anoms, anoms.sigStrength);
    }

    // signatures, ships, drones/probes and structures
    m_client->SystemMgr()->GetAnomMgr()->GetSignatureList(sig);
    for (auto& sigs : sig) {
        SignalData data = SignalData();
        if (!CalcSignal(sigs, data))
            continue;
        AddResultGeometry(sigs, data, resultList);
    }

    PyDict* probeDict = new PyDict();
    for (auto cur : m_activeProbeMap) {
        ScanProbesDict spd;
            spd.expiry = cur.second->GetExpiryTime();
            spd.maxDeviation = cur.second->GetMaxDeviation();
            spd.pos = MakeVector3(cur.second->GetPosition());
            PyIncRef(spd.pos);
            spd.destination = spd.pos;
            spd.probeID = cur.first;
            spd.state = cur.second->GetState();
            spd.rangeStep = cur.second->GetRangeStep();
            spd.scanRange = cur.second->GetScanRange();
            spd.scanStrength = cur.second->GetEffectiveStrength();
            spd.typeID = cur.second->GetSelf()->typeID();
        probeDict->SetItem(new PyInt(cur.first), spd.Encode());
    }

    OnSystemScanStopped osssp;
        osssp.scanProbesDict = probeDict;
        osssp.systemScanResult = resultList;
        osssp.absentTargets = GetAbsentTargets();
    PyTuple* ev = osssp.Encode();
    ev->Dump(SCAN__RSPDUMP, "psr-    ");
    m_client->SendNotification("OnSystemScanStopped", "charid", &ev);
}

/* Crucible (Apocrypha - Retribution) probe scanning.  CCP never published the formula; this is the community's
 *  reverse-engineered model (forums-archive topic 223301, 2013) in a bounded form:
 *    size        = site signal band in % (1/80 = 1.25 ... 1/5 = 20), or signature radius / exploring.ProbeShipSigDivisor
 *                  for ships, drones and structures
 *    probe       = Asymptote(size * strength at range * e^-((distance/range)^2) / 4)   in %
 *    total       = strongest probe + each next probe * its spread to the stronger ones (at most 8 probes)
 *    certainty   = total / 100.  1 = warpable (needs 4+ probes:  1 probe = sphere, 2 = circle, 3 = two points)
 *  Asymptote(x) = x up to 25, then 75 - 50 e^-((x-25)/50)  (continuous, never above 75 for one probe)
 *  spread:  1 - cos(angle at the target) between two probes, normalised to the tetrahedral angle (well spread
 *  probes count fully, probes bunched on one side count little)
 */
float Scan::Asymptote(float x)
{
    if (x <= 25.0f)
        return x;
    return 75.0f - 50.0f * std::exp(-(x - 25.0f) / 50.0f);
}

bool Scan::CalcSignal(const CosmicSignature& sig, SignalData& data)
{
    SystemManager* pSys(m_client->SystemMgr());
    data.truePos = sig.position;
    data.certainty = 0;
    data.deviation = 0;
    data.hits.clear();

    bool isSite((sig.scanGroupID == Scanning::Group::Signature) or (sig.scanGroupID == Scanning::Group::Anomaly));
    float size(0.0f);
    if (isSite) {
        switch (sig.dungeonType) {
            case Dungeon::Type::Mission:        // npc mission
            case Dungeon::Type::Escalation:     // new dungeon from previous site. very limited access
            case Dungeon::Type::Rated: {        // DED rated dungeon
                return false;
            }
        }
        // band as a fraction (1/80 = 0.0125)
        size = sig.sigStrength * 100.0f;
    } else {
        // ships, drones and structures are found where they are now
        SystemEntity* pSE(pSys->GetSE(sig.sigItemID));
        if (pSE == nullptr)
            return false;
        if ((pSE->DestinyMgr() != nullptr) and pSE->DestinyMgr()->IsCloaked())
            return false;
        // our own ship, probes and drones aren't results
        if (pSE->GetID() == m_client->GetShipID())
            return false;
        if ((pSE->IsProbeSE() or pSE->IsDroneSE()) and (pSE->GetOwnerID() == m_client->GetCharacterID()))
            return false;
        data.truePos = pSE->GetPosition();
        size = pSE->GetSelf()->GetAttribute(AttrSignatureRadius).get_float() / sConfig.exploring.ProbeShipSigDivisor;
    }
    if (size <= 0)
        return false;

    for (auto cur : m_activeProbeMap) {
        ProbeSE* pProbe(cur.second);
        // core probes can't find ships, drones or structures
        if (!isSite and !pProbe->CanScanShips())
            continue;
        float range(pProbe->GetScanRange());
        float distance(pProbe->GetPosition().distance(data.truePos));
        if ((range <= 0) or (distance > range))
            continue;
        float ratio(distance / range);
        ProbeHit hit = ProbeHit();
            hit.probe = pProbe;
            hit.distance = distance;
            hit.signal = Asymptote(size * pProbe->GetEffectiveStrength() * std::exp(-(ratio * ratio)) / 4.0f);
        data.hits.push_back(hit);
    }
    if (data.hits.empty())
        return false;

    std::sort(data.hits.begin(), data.hits.end(), [](const ProbeHit& a, const ProbeHit& b) { return a.signal > b.signal; });
    if (data.hits.size() > 8)
        data.hits.resize(8);

    // each next probe adds what its position adds:  its spread to every stronger probe (tetrahedral spread = full)
    static const float fullSpread(1.0f - std::cos(EvE::Trig::Deg2Rad(109.47)));
    float total(0.0f), devSum(0.0f);
    for (uint8 k = 0; k < data.hits.size(); ++k) {
        devSum += data.hits[k].probe->GetMaxDeviation();
        float spread(1.0f);
        if (k > 0) {
            GVector vk(data.truePos, data.hits[k].probe->GetPosition());
            vk.normalize();
            for (uint8 j = 0; j < k; ++j) {
                GVector vj(data.truePos, data.hits[j].probe->GetPosition());
                vj.normalize();
                float am((1.0f - (float)vk.dotProduct(vj)) / fullSpread);
                spread = std::min(spread, std::min(1.0f, am));
            }
        }
        total += data.hits[k].signal * spread;
    }
    data.certainty = total / 100.0f;
    data.deviation = (devSum / data.hits.size()) * std::max(0.0f, 1.0f - data.certainty);

    _log(SCAN__TRACE, "Scan::CalcSignal() - %s(%s) size %.2f:  %u probes, certainty %.4f, deviation %.0fm", \
            sig.sigName.c_str(), sig.sigID.c_str(), size, data.hits.size(), data.certainty, data.deviation);
    return true;
}

void Scan::AddResultGeometry(const CosmicSignature& sig, SignalData& data, PyList* results)
{
    /* client (scanner.py):  data is a float -> sphere of that radius around pos;  a Vector3 -> a point;  else an object
     *  with point/normal/radius -> a circle.  spheres and circles never show as 100%.  three probes give two points with
     *  the same id (two possible locations), neither warpable
     */
    uint8 count(data.hits.size());
    ProbeSE* p1(data.hits[0].probe);
    GPoint pos1(p1->GetPosition());
    float certainty(data.certainty);

    PyRep* probeIDs(nullptr);
    if (count == 1) {
        probeIDs = new PyInt(p1->GetID());
    } else {
        PyTuple* tuple = new PyTuple(count);
        for (uint8 i = 0; i < count; ++i)
            tuple->SetItem(i, new PyInt(data.hits[i].probe->GetID()));
        probeIDs = tuple;
    }

    std::vector<PyRep*> shapes;     // one result per entry
    if (count == 3) {
        // the true position and its mirror across the probes' plane, each off by up to the deviation
        GPoint pos2(data.hits[1].probe->GetPosition()), pos3(data.hits[2].probe->GetPosition());
        GVector normal(GVector(pos2 - pos1).crossProduct(GVector(pos3 - pos1)));
        if (normal.length() > 1.0) {
            normal.normalize();
            GPoint mirror(data.truePos - normal * (2.0 * GVector(data.truePos - pos1).dotProduct(normal)));
            certainty = std::min(certainty, 0.99f);
            GPoint a(data.truePos), b(mirror);
            if (data.deviation > 0) {
                GPoint offset(NULL_ORIGIN);
                offset.MakeRandomPointOnSphere(MakeRandomFloat(0, data.deviation));
                a += offset;
                offset = NULL_ORIGIN;
                offset.MakeRandomPointOnSphere(MakeRandomFloat(0, data.deviation));
                b += offset;
            }
            shapes.push_back(MakeVector3(a));
            shapes.push_back(MakeVector3(b));
        } else {
            // probes in a line:  as good as two
            count = 2;
        }
    }
    if (count == 2) {
        // the circle where the two probes' spheres meet:  around the line between them, through the target
        GPoint pos2(data.hits[1].probe->GetPosition());
        GVector axis(pos2 - pos1);
        double len(axis.length());
        if (len > 1.0) {
            axis.normalize();
            GPoint center(pos1 + axis * GVector(data.truePos - pos1).dotProduct(axis));
            double radius(center.distance(data.truePos));
            if (data.deviation > 0)
                radius = std::max(0.0, radius + MakeRandomFloat(-1, 1) * data.deviation);
            if (radius > 1.0) {
                PyDict* dict = new PyDict();
                    dict->SetItemString("point", MakeVector3(center));
                    dict->SetItemString("normal", MakeVector3(GPoint(axis.x, axis.y, axis.z)));
                    dict->SetItemString("radius", new PyFloat(radius));
                shapes.push_back(new PyObject("util.KeyVal", dict));
            }
        }
        if (shapes.empty())
            count = 1;      // target on the line between them:  a sphere
    }
    if (count == 1) {
        // a sphere around the probe:  the target is about this far away
        float distance(data.hits[0].distance);
        if (data.deviation > 0)
            distance = std::max(1.0, distance + MakeRandomFloat(-1, 1) * data.deviation);
        shapes.push_back(new PyFloat(distance));
    }
    if (count >= 4) {
        // a point.  perfect at 100%
        GPoint point(data.truePos);
        if (certainty < 1.0f) {
            if (data.deviation > 0) {
                GPoint offset(NULL_ORIGIN);
                offset.MakeRandomPointOnSphere(MakeRandomFloat(0, data.deviation));
                point += offset;
            }
        } else {
            data.deviation = 0;
            if (sig.scanGroupID == Scanning::Group::Signature)
                sStatMgr.Increment(Stat::sitesScanned);
        }
        shapes.push_back(MakeVector3(point));
        CacheResult(sig, certainty);
    }

    for (uint8 i = 0; i < shapes.size(); ++i) {
        SystemScanResult ssr;
            ssr.id = sig.sigID;
            ssr.dungeonName = sig.sigName;
            ssr.typeID = sig.sigTypeID;
            ssr.groupID = sig.sigGroupID;
            ssr.scanGroupID = sig.scanGroupID;
            ssr.strengthAttributeID = sig.scanAttributeID;
            ssr.degraded = false;
            ssr.deviation = data.deviation;     // how far the shown position may be off the target
            ssr.certainty = certainty;          // "signal strength" in the scanner window
            if (i > 0)
                PyIncRef(probeIDs);
            ssr.probeID = probeIDs;
            ssr.pos = MakeVector3(pos1);
            ssr.data = shapes[i];
        results->AddItem(ssr.Encode());
    }
}

/* moon scanning (survey probes) is in ExplorationMgr.  the survey probe fx packet, for reference:
 *                      [PyString "OnSpecialFX"]
 *                      [PyTuple 14 items]
 *                        [PyIntegerVar 1002331681462]
 *                        [PyIntegerVar 1002332233248]
 *                        [PyInt 444]
 *                        [PyNone]
 *                        [PyNone]
 *                        [PyList 0 items]
 *                        [PyString "effects.SurveyScan"]
 *                        [PyBool False]
 *                        [PyInt 1]
 *                        [PyInt 1]
 *                        [PyFloat 4250]
 *                        [PyInt 0]
 *                        [PyIntegerVar 129509430135552798]
 *                        [PyNone]
 */
