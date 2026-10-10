/*
 * eve-bot: a headless test client for EVEmu.
 *
 * Usage:  eve-bot <host> <port> <script file> [name=value ...]   (name=value sets $name)
 *
 * The script is a list of commands, one per line (# starts a comment):
 *
 *   login <account> <password>        log in (the account must exist and allow plain passwords)
 *   character <name>                  select this character, creating it first if needed
 *   slash <command...>                run a GM slash command; its result is remembered as $last
 *   set <var> <value|$last|$item:typeID|$space:typeID>   store a value for later lines ($var)
 *   assemble <shipItemID>             assemble a ship in the station hangar
 *   board <shipItemID>                make that (docked) ship the active ship
 *   movetobay <shipID> <itemID> <qty>  move a stack from the hangar into that docked ship's drone bay
 *   undock                            undock the active ship and wait for the ballpark
 *   stop                              stop the ship
 *   launch <itemID> <qty>             launch drones from the drone bay (one Drop call, like dragging a stack)
 *   drones <command> [targetID]       entity command on every drone we have in space:
 *                                     engage, returnhome, returnbay, abandon, reconnect
 *   wait <seconds>                    keep reading from the server
 *   expect <what> ...                 checks, printed as PASS/FAIL (see Expect())
 *   quit
 *
 * Everything the server sends is printed in compact form, so drone state
 * changes and destiny commands (CmdOrbit, CmdFollowBall, CmdWarpTo...) show up
 * in the output with timestamps.
 */

#include "eve-bot.h"

#include <functional>

#include <arpa/inet.h>
#include <cstdarg>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <thread>
#include <chrono>

/* eve-core's database code reports timings to the server's profiler; the bot has none */
class Profiler : public Singleton<Profiler> {
public:
    void AddTime(uint8 key, double value);
};
void Profiler::AddTime(uint8, double) { }

namespace {

double g_start = 0;
double Now()            { return GetTimeMSeconds() / 1000.0; }
double Elapsed()        { return Now() - g_start; }

void Out(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::printf("[%7.2f] ", Elapsed());
    std::vprintf(fmt, ap);
    std::printf("\n");
    std::fflush(stdout);
    va_end(ap);
}

/* compact text form of a PyRep, for logging */
void Repr(const PyRep* rep, std::string& out, int depth = 0) {
    if (rep == nullptr) { out += "null"; return; }
    if (depth > 8) { out += "..."; return; }
    char buf[64];
    switch (rep->GetType()) {
        case PyRep::PyTypeInt:      std::snprintf(buf, sizeof(buf), "%d", rep->AsInt()->value()); out += buf; break;
        case PyRep::PyTypeLong:     std::snprintf(buf, sizeof(buf), "%lld", (long long)rep->AsLong()->value()); out += buf; break;
        case PyRep::PyTypeFloat:    std::snprintf(buf, sizeof(buf), "%.2f", rep->AsFloat()->value()); out += buf; break;
        case PyRep::PyTypeBool:     out += rep->AsBool()->value() ? "True" : "False"; break;
        case PyRep::PyTypeNone:     out += "None"; break;
        case PyRep::PyTypeString:   out += "'" + rep->AsString()->content() + "'"; break;
        case PyRep::PyTypeWString:  out += "u'" + rep->AsWString()->content() + "'"; break;
        case PyRep::PyTypeToken:    out += "<" + rep->AsToken()->content() + ">"; break;
        case PyRep::PyTypeBuffer:   std::snprintf(buf, sizeof(buf), "<buffer %zu>", rep->AsBuffer()->size()); out += buf; break;
        case PyRep::PyTypeTuple: {
            out += "(";
            const PyTuple* t = rep->AsTuple();
            for (size_t i = 0; i < t->size(); ++i) {
                if (i) out += ", ";
                if (i >= 12) { out += "..."; break; }
                Repr(t->GetItem(i), out, depth + 1);
            }
            out += ")";
        } break;
        case PyRep::PyTypeList: {
            out += "[";
            const PyList* l = rep->AsList();
            for (size_t i = 0; i < l->size(); ++i) {
                if (i) out += ", ";
                if (i >= 12) { out += "..."; break; }
                Repr(l->GetItem(i), out, depth + 1);
            }
            out += "]";
        } break;
        case PyRep::PyTypeDict: {
            out += "{";
            const PyDict* d = rep->AsDict();
            int n = 0;
            for (PyDict::const_iterator itr = d->begin(); itr != d->end(); ++itr, ++n) {
                if (n) out += ", ";
                if (n >= 16) { out += "..."; break; }
                Repr(itr->first, out, depth + 1);
                out += ": ";
                Repr(itr->second, out, depth + 1);
            }
            out += "}";
        } break;
        case PyRep::PyTypeSubStream: {
            const PySubStream* s = rep->AsSubStream();
            s->DecodeData();
            out += "ss:";
            Repr(s->decoded(), out, depth + 1);
        } break;
        case PyRep::PyTypeSubStruct: out += "struct:"; Repr(rep->AsSubStruct()->sub(), out, depth + 1); break;
        case PyRep::PyTypeObject: {
            const PyObject* o = rep->AsObject();
            out += "obj<" + o->type()->content() + ">";
            Repr(o->arguments(), out, depth + 1);
        } break;
        case PyRep::PyTypeObjectEx: {
            const PyObjectEx* o = rep->AsObjectEx();
            out += "objex";
            Repr(o->header(), out, depth + 1);
        } break;
        case PyRep::PyTypePackedRow: out += "<row>"; break;
        default: out += "?"; break;
    }
}

std::string Repr(const PyRep* rep) { std::string s; Repr(rep, s); return s; }

/* first "N=node:id" string found anywhere inside a reply: a bound object's ID */
std::string FindOID(const PyRep* rep, int depth = 0) {
    if ((rep == nullptr) or (depth > 10))
        return "";
    switch (rep->GetType()) {
        case PyRep::PyTypeString: {
            const std::string& s = rep->AsString()->content();
            if ((s.size() > 2) and (s[0] == 'N') and (s[1] == '='))
                return s;
        } break;
        case PyRep::PyTypeTuple: {
            for (size_t i = 0; i < rep->AsTuple()->size(); ++i) {
                std::string s = FindOID(rep->AsTuple()->GetItem(i), depth + 1);
                if (!s.empty()) return s;
            }
        } break;
        case PyRep::PyTypeList: {
            for (size_t i = 0; i < rep->AsList()->size(); ++i) {
                std::string s = FindOID(rep->AsList()->GetItem(i), depth + 1);
                if (!s.empty()) return s;
            }
        } break;
        case PyRep::PyTypeSubStream: rep->AsSubStream()->DecodeData(); return FindOID(rep->AsSubStream()->decoded(), depth + 1);
        case PyRep::PyTypeSubStruct: return FindOID(rep->AsSubStruct()->sub(), depth + 1);
        case PyRep::PyTypeObject: return FindOID(rep->AsObject()->arguments(), depth + 1);
        case PyRep::PyTypeDict: {
            for (PyDict::const_iterator itr = rep->AsDict()->begin(); itr != rep->AsDict()->end(); ++itr) {
                std::string s = FindOID(itr->second, depth + 1);
                if (!s.empty()) return s;
            }
        } break;
        default: break;
    }
    return "";
}

struct Item {
    int64 itemID = 0;
    int64 typeID = 0;
    int64 locationID = 0;
    int64 flag = 0;
    int64 quantity = 0;
};

struct DroneState {
    int64 droneID = 0;
    int64 controllerID = 0;
    int64 activity = -1;
    int64 targetID = 0;
    bool known = false;     // ownerID was set: still ours
};

class Bot {
public:
    Bot() : m_con(nullptr), m_callID(0), m_clientID(0), m_userID(0), m_nodeID(888444) { }

    bool Connect(const std::string& host, uint16 port) {
        m_con = new EVETCPConnection();
        char err[TCPCONN_ERRBUF_SIZE];
        if (!m_con->Connect(inet_addr(host.c_str()), port, err)) {
            Out("connect failed: %s", err);
            return false;
        }
        Out("connected to %s:%u", host.c_str(), port);
        return true;
    }

    PyRep* WaitRep(double seconds) {
        double until = Now() + seconds;
        while (Now() < until) {
            PyRep* rep = m_con->PopRep();
            if (rep != nullptr)
                return rep;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return nullptr;
    }

    bool Login(const std::string& user, const std::string& pass) {
        PyRep* rep = WaitRep(10);
        if (rep == nullptr) { Out("no version exchange from server"); return false; }
        Out("server version: %s", Repr(rep).c_str());

        VersionExchangeClient ve;
            ve.birthday = EVEBirthday;
            ve.macho_version = MachoNetVersion;
            ve.user_count = 0;
            ve.version_number = EVEVersionNumber;
            ve.build_version = EVEBuildVersion;
            ve.project_version = EVEProjectVersion;
        m_con->QueueRep(ve.Encode());

        NetCommand_VK vk;
            vk.vipKey = "";
        m_con->QueueRep(vk.Encode());

        CryptoRequestPacket cr;
            cr.keyVersion = "placebo";
            cr.keyParams = new PyDict();
        m_con->QueueRep(cr.Encode());
        rep = WaitRep(10);
        Out("crypto: %s", Repr(rep).c_str());

        CryptoChallengePacket cc;
            cc.clientChallenge = "";
            cc.macho_version = MachoNetVersion;
            cc.boot_version = EVEVersionNumber;
            cc.boot_build = EVEBuildVersion;
            cc.boot_codename = EVEProjectCodename;
            cc.boot_region = EVEProjectRegion;
            cc.user_name = user;
            cc.user_password = pass;
            cc.user_password_hash = "";
            cc.user_languageid = "EN";
            cc.user_affiliateid = 0;
        m_con->QueueRep(cc.Encode());

        // password version, then the server handshake (or a login failure)
        for (int i = 0; i < 2; ++i) {
            rep = WaitRep(10);
            if (rep == nullptr) { Out("login: no reply"); return false; }
            Out("login reply: %s", Repr(rep).c_str());
        }

        CryptoHandshakeResult hr;
            hr.challenge_responsehash = "55087";
            hr.func_output = "";
            hr.func_result = PyStatic.NewNone();
        m_con->QueueRep(hr.Encode());

        rep = WaitRep(10);
        if ((rep == nullptr) or !rep->IsDict()) { Out("handshake failed: %s", Repr(rep).c_str()); return false; }
        PyDict* ack = rep->AsDict();
        PyRep* sinit = ack->GetItemString("session_init");
        if ((sinit != nullptr) and sinit->IsDict()) {
            PyRep* uid = sinit->AsDict()->GetItemString("userid");
            if (uid != nullptr) m_userID = PyRep::IntegerValue(uid);
        }
        PyRep* cid = ack->GetItemString("client_hash");
        (void)cid;
        // the client ID is in the handshake ack as user_clientid
        PyRep* ucid = ack->GetItemString("user_clientid");
        if (ucid != nullptr) m_clientID = PyRep::IntegerValue(ucid);
        Out("logged in: userid %lld clientid %lld", (long long)m_userID, (long long)m_clientID);
        return true;
    }

    /* send a call and wait for its reply.  'oid' empty = service call, else bound object call */
    PyRep* Call(const std::string& service, const std::string& oid, const std::string& method,
                PyTuple* args, PyDict* kw = nullptr, double timeout = 30, bool* failed = nullptr) {
        int64 callID = ++m_callID;
        PyPacket* p = new PyPacket();
        p->type_string = "macho.CallReq";
        p->type = CALL_REQ;
        p->source.type = PyAddress::Client;
        p->source.objectID = m_clientID;
        p->source.callID = callID;
        p->source.service = "";
        if (oid.empty()) {
            p->dest.type = PyAddress::Any;
            p->dest.service = service;
            p->dest.callID = 0;
        } else {
            p->dest.type = PyAddress::Node;
            p->dest.objectID = m_nodeID;
            p->dest.service = "";
            p->dest.callID = 0;
        }
        p->userid = (uint32)m_userID;

        PyTuple* call = new PyTuple(4);
        if (oid.empty())
            call->SetItem(0, new PyInt(1));
        else
            call->SetItem(0, new PyString(oid));
        call->SetItem(1, new PyString(method));
        call->SetItem(2, args != nullptr ? args : new PyTuple(0));
        // the game client always sends a keyword dict (the server expects one)
        if (kw == nullptr)
            kw = new PyDict();
        kw->SetItemString("machoVersion", new PyInt(1));
        call->SetItem(3, kw);
        PyTuple* inner = new PyTuple(2);
        inner->SetItem(0, new PyInt(oid.empty() ? 0 : 1));
        inner->SetItem(1, new PySubStream(call));
        PyTuple* payload = new PyTuple(1);
        payload->SetItem(0, inner);
        p->payload = payload;
        p->named_payload = new PyDict();
        m_con->QueueRep(p->Encode());
        delete p;

        double until = Now() + timeout;
        while (Now() < until) {
            Pump(0.02);
            std::map<int64, PyRep*>::iterator itr = m_replies.find(callID);
            if (itr != m_replies.end()) {
                PyRep* r = itr->second;
                m_replies.erase(itr);
                bool err = (m_errors.erase(callID) > 0);
                if (failed != nullptr) *failed = err;
                std::string what = service.empty() ? oid : service;
                Out("call %s.%s -> %s%s", what.c_str(), method.c_str(), err ? "ERROR " : "", Repr(r).substr(0, 600).c_str());
                return r;
            }
        }
        Out("call %s.%s timed out", service.c_str(), method.c_str());
        if (failed != nullptr) *failed = true;
        return nullptr;
    }

    /* MachoBindObject; returns the bound object's ID string */
    std::string Bind(const std::string& service, PyRep* bindParams) {
        PyTuple* args = new PyTuple(2);
        args->SetItem(0, bindParams);
        args->SetItem(1, PyStatic.NewNone());
        PyRep* r = Call(service, "", "MachoBindObject", args);
        std::string oid = FindOID(r);
        if (oid.empty())
            Out("bind %s failed", service.c_str());
        return oid;
    }

    void Pump(double seconds) {
        double until = Now() + seconds;
        do {
            PyRep* rep = m_con->PopRep();
            if (rep == nullptr) {
                if (m_con->GetState() != TCPConnection::STATE_CONNECTED) {
                    Out("connection closed by server");
                    std::exit(2);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }
            Handle(rep);
        } while (Now() < until);
    }

    void Handle(PyRep* rep) {
        PyPacket* p = new PyPacket();
        if (!p->Decode(&rep)) {
            Out("undecodable packet");
            delete p;
            return;
        }
        switch (p->type) {
            case CALL_RSP: {
                PyRep* result = PyStatic.NewNone();
                if ((p->payload != nullptr) and (p->payload->size() > 0)) {
                    PyRep* r = p->payload->GetItem(0);
                    if (r->IsSubStream()) {
                        r->AsSubStream()->DecodeData();
                        result = r->AsSubStream()->decoded();
                    } else {
                        result = r;
                    }
                    PyIncRef(result);
                }
                m_replies[p->dest.callID] = result;
            } break;
            case ERRORRESPONSE: {
                m_errors.insert(p->dest.callID);
                PyIncRef(p->payload);
                m_replies[p->dest.callID] = p->payload;
            } break;
            case NOTIFICATION: {
                HandleNotification(p);
            } break;
            case SESSIONCHANGENOTIFICATION: {
                ReadSession(p->payload);
                {
                    std::string keys;
                    for (auto& cur : m_session) keys += " " + cur.first + "=" + std::to_string(cur.second);
                    Out("session:%s", keys.c_str());
                }
            } break;
            case PING_REQ: break;
            default: Out("packet type %d: %s", p->type, Repr(p->payload).substr(0, 300).c_str()); break;
        }
        delete p;
    }

    /* session changes are {name: (old, new)} pairs somewhere inside the payload */
    void ReadSession(const PyRep* rep, int depth = 0) {
        if ((rep == nullptr) or (depth > 6)) return;
        if (rep->IsTuple()) {
            for (size_t i = 0; i < rep->AsTuple()->size(); ++i) ReadSession(rep->AsTuple()->GetItem(i), depth + 1);
        } else if (rep->IsList()) {
            for (size_t i = 0; i < rep->AsList()->size(); ++i) ReadSession(rep->AsList()->GetItem(i), depth + 1);
        } else if (rep->IsSubStream()) {
            rep->AsSubStream()->DecodeData();
            ReadSession(rep->AsSubStream()->decoded(), depth + 1);
        } else if (rep->IsDict()) {
            for (PyDict::const_iterator itr = rep->AsDict()->begin(); itr != rep->AsDict()->end(); ++itr) {
                if (itr->first->IsString() and itr->second->IsTuple() and (itr->second->AsTuple()->size() == 2)) {
                    PyRep* nv = itr->second->AsTuple()->GetItem(1);
                    if (nv->IsInt() or nv->IsLong())
                        m_session[itr->first->AsString()->content()] = PyRep::IntegerValue(nv);
                    else if (nv->IsNone())
                        m_session[itr->first->AsString()->content()] = 0;
                } else {
                    ReadSession(itr->second, depth + 1);
                }
            }
        }
    }

    /* pick out item rows (OnItemChange) so we know item IDs and where things are */
    void ReadItemRows(const PyRep* rep, int depth = 0) {
        if ((rep == nullptr) or (depth > 6)) return;
        switch (rep->GetType()) {
            case PyRep::PyTypePackedRow: {
                const PyPackedRow* row = rep->AsPackedRow();
                DBRowDescriptor* hdr = row->header();
                Item it;
                for (size_t i = 0; i < hdr->ColumnCount(); ++i) {
                    std::string name = hdr->GetColumnName(i)->content();
                    PyRep* v = row->GetField(i);
                    if ((v == nullptr) or !(v->IsInt() or v->IsLong())) continue;
                    int64 n = PyRep::IntegerValue(v);
                    if (name == "itemID") it.itemID = n;
                    else if (name == "typeID") it.typeID = n;
                    else if (name == "locationID") it.locationID = n;
                    else if (name == "flagID" or name == "flag") it.flag = n;
                    else if (name == "quantity" or name == "stacksize") it.quantity = n;
                }
                if (it.itemID != 0)
                    m_items[it.itemID] = it;
            } break;
            case PyRep::PyTypeTuple:
                for (size_t i = 0; i < rep->AsTuple()->size(); ++i) ReadItemRows(rep->AsTuple()->GetItem(i), depth + 1);
                break;
            case PyRep::PyTypeList:
                for (size_t i = 0; i < rep->AsList()->size(); ++i) ReadItemRows(rep->AsList()->GetItem(i), depth + 1);
                break;
            case PyRep::PyTypeSubStream:
                rep->AsSubStream()->DecodeData();
                ReadItemRows(rep->AsSubStream()->decoded(), depth + 1);
                break;
            default: break;
        }
    }

    void HandleNotification(PyPacket* p) {
        const std::string& name = p->dest.service;
        // payload = ((0, substream((0, (1, args)))),)  -- see EVENotificationStream::Encode()
        PyTuple* args = nullptr;
        PyTuple* payload = p->payload;
        if ((payload != nullptr) and (payload->size() == 1) and payload->GetItem(0)->IsTuple()
        and (payload->GetItem(0)->AsTuple()->size() == 2) and payload->GetItem(0)->AsTuple()->GetItem(1)->IsSubStream()) {
            PySubStream* ss = payload->GetItem(0)->AsTuple()->GetItem(1)->AsSubStream();
            ss->DecodeData();
            PyRep* t3 = ss->decoded();
            if ((t3 != nullptr) and t3->IsTuple() and (t3->AsTuple()->size() == 2) and t3->AsTuple()->GetItem(1)->IsTuple()) {
                PyTuple* t4 = t3->AsTuple()->GetItem(1)->AsTuple();
                if ((t4->size() == 2) and t4->GetItem(1)->IsTuple())
                    args = t4->GetItem(1)->AsTuple();
            }
        }
        if (args == nullptr) {
            Out("notify %s (undecodable) %s", name.c_str(), Repr(payload).substr(0, 300).c_str());
            return;
        }
        if (name == "DoDestinyUpdate") {
            // args = (updates list, waitForBubble, ...); each update = (stamp, (name, args))
            if ((args == nullptr) or (args->size() < 1) or !args->GetItem(0)->IsList()) {
                Out("destiny: %s", Repr(args).substr(0, 400).c_str());
                return;
            }
            PyList* ups = args->GetItem(0)->AsList();
            for (size_t i = 0; i < ups->size(); ++i) {
                PyRep* u = ups->GetItem(i);
                if (!u->IsTuple() or (u->AsTuple()->size() < 2) or !u->AsTuple()->GetItem(1)->IsTuple()) {
                    Out("destiny ?: %s", Repr(u).substr(0, 300).c_str());
                    continue;
                }
                PyTuple* ev = u->AsTuple()->GetItem(1)->AsTuple();
                std::string evName = ev->GetItem(0)->IsString() ? ev->GetItem(0)->AsString()->content() : "?";
                PyRep* evArgs = (ev->size() > 1) ? ev->GetItem(1) : nullptr;
                if (evName == "OnDroneStateChange")
                    ReadDroneState(evArgs);
                if ((evName == "AddBalls") or (evName == "AddBalls2") or (evName == "SetState")) {
                    Out("destiny %s (%zu bytes)", evName.c_str(), Repr(evArgs).size());
                    ReadSlims(evArgs);
                    continue;
                }
                if (m_quietDestiny.count(evName))
                    continue;
                Out("destiny %s %s", evName.c_str(), Repr(evArgs).substr(0, 300).c_str());
            }
            return;
        }
        if (name == "OnItemChange") {
            ReadItemRows(args);
            if (m_verboseItems)
                Out("notify %s %s", name.c_str(), Repr(args).substr(0, 200).c_str());
            return;
        }
        if (name == "OnDroneStateChange") {
            ReadDroneState(args);
            return;
        }
        if (name == "OnRemoteMessage" or name == "OnNotifyMsg" or name == "OnMessage" or name == "OnSystemMessage" or name == "OnServerMessage") {
            Out("MESSAGE %s %s", name.c_str(), Repr(args).substr(0, 400).c_str());
            return;
        }
        if (m_quietNotify.count(name))
            return;
        Out("notify %s %s", name.c_str(), Repr(args).substr(0, 300).c_str());
    }

    /* slim items in AddBalls/SetState: remember itemID -> typeID for everything in space */
    void ReadSlims(const PyRep* rep, int depth = 0) {
        if ((rep == nullptr) or (depth > 8)) return;
        switch (rep->GetType()) {
            case PyRep::PyTypeDict: {
                const PyDict* d = rep->AsDict();
                PyRep* id = d->GetItemString("itemID");
                PyRep* type = d->GetItemString("typeID");
                if ((id != nullptr) and (type != nullptr)) {
                    m_balls[PyRep::IntegerValue(id)] = PyRep::IntegerValue(type);
                } else {
                    for (PyDict::const_iterator itr = d->begin(); itr != d->end(); ++itr) ReadSlims(itr->second, depth + 1);
                }
            } break;
            case PyRep::PyTypeObject: ReadSlims(rep->AsObject()->arguments(), depth + 1); break;
            case PyRep::PyTypeTuple:
                for (size_t i = 0; i < rep->AsTuple()->size(); ++i) ReadSlims(rep->AsTuple()->GetItem(i), depth + 1);
                break;
            case PyRep::PyTypeList:
                for (size_t i = 0; i < rep->AsList()->size(); ++i) ReadSlims(rep->AsList()->GetItem(i), depth + 1);
                break;
            case PyRep::PyTypeSubStream:
                rep->AsSubStream()->DecodeData();
                ReadSlims(rep->AsSubStream()->decoded(), depth + 1);
                break;
            default: break;
        }
    }

    void ReadDroneState(const PyRep* args) {
        // (droneID, ownerID, controllerID, activityState, typeID, controllerOwnerID, targetID)
        const PyRep* list = args;
        if ((list != nullptr) and list->IsTuple() and (list->AsTuple()->size() == 1))
            list = list->AsTuple()->GetItem(0);
        std::vector<PyRep*> v;
        if ((list != nullptr) and list->IsList())
            for (size_t i = 0; i < list->AsList()->size(); ++i) v.push_back(list->AsList()->GetItem(i));
        else if ((list != nullptr) and list->IsTuple())
            for (size_t i = 0; i < list->AsTuple()->size(); ++i) v.push_back(list->AsTuple()->GetItem(i));
        if (v.size() < 7) { Out("drone state ?: %s", Repr(args).c_str()); return; }
        auto num = [](PyRep* r) -> int64 { return (r->IsInt() or r->IsLong()) ? PyRep::IntegerValue(r) : -1; };
        DroneState s;
        s.droneID = num(v[0]);
        s.known = (num(v[1]) > 0);
        s.controllerID = num(v[2]);
        s.activity = num(v[3]);
        s.targetID = num(v[6]);
        m_drones[s.droneID] = s;
        Out("drone %lld: state %s controller %lld target %lld%s", (long long)s.droneID, StateName(s.activity),
            (long long)s.controllerID, (long long)s.targetID, s.known ? "" : " (gone)");
    }

    static const char* StateName(int64 s) {
        switch (s) {
            case -1: return "None";
            case 0:  return "Idle";
            case 1:  return "Fighting";
            case 2:  return "Mining";
            case 3:  return "Approaching";
            case 4:  return "Returning";
            case 10: return "Engaged";
            default: return "Other";
        }
    }

    int64 Session(const std::string& key) {
        std::map<std::string, int64>::iterator itr = m_session.find(key);
        if ((itr == m_session.end()) and (key == "solarsystemid"))
            itr = m_session.find("solarsystemid2");
        return itr == m_session.end() ? 0 : itr->second;
    }

    std::vector<int64> DronesInSpace() {
        std::vector<int64> ids;
        for (auto cur : m_drones)
            if (cur.second.known and (cur.second.activity >= 0))
                ids.push_back(cur.first);
        return ids;
    }

    EVETCPConnection* m_con;
    int64 m_callID;
    int64 m_clientID;
    int64 m_userID;
    int64 m_nodeID;
    std::map<int64, PyRep*> m_replies;
    std::set<int64> m_errors;
    std::map<std::string, int64> m_session;
    std::map<int64, Item> m_items;
    std::map<int64, int64> m_balls;
    std::map<int64, DroneState> m_drones;
    std::set<std::string> m_quietNotify;
    std::set<std::string> m_quietDestiny;
    bool m_verboseItems = false;
};

PyObject* KeyVal(PyDict* d) { return new PyObject("util.KeyVal", d); }

PyTuple* Float3(double x, double y, double z) {
    PyTuple* t = new PyTuple(3);
    t->SetItem(0, new PyFloat(x));
    t->SetItem(1, new PyFloat(y));
    t->SetItem(2, new PyFloat(z));
    return t;
}

/* the smallest appearance and portrait the server will accept */
int64 CreateCharacter(Bot& bot, const std::string& name) {
    PyDict* info = new PyDict();
    info->SetItemString("colors", new PyList());
    info->SetItemString("modifiers", new PyList());
    info->SetItemString("sculpts", new PyList());
    PyTuple* app = new PyTuple(2);
    app->SetItem(0, new PyToken("eve.common.script.paperDoll.paperDollDefinitions.Appearance"));
    app->SetItem(1, new PyFloat(0.5));
    info->SetItemString("appearance", new PyObjectEx_Type2(app, new PyDict()));

    PyDict* pose = new PyDict();
    pose->SetItemString("HeadLookTarget", Float3(0, 1.6, 0.5));
    const char* poseFloats[] = { "BrowLeftCurl", "BrowLeftUpDown", "BrowLeftTighten", "BrowRightCurl", "BrowRightTighten",
        "BrowRightUpDown", "EyeClose", "EyesLookHorizontal", "EyesLookVertical", "FrownLeft", "FrownRight", "HeadTilt",
        "JawSideways", "JawUp", "OrientChar", "PortraitPoseNumber", "PuckerLips", "SmileLeft", "SmileRight",
        "SquintLeft", "SquintRight" };
    for (auto f : poseFloats)
        pose->SetItemString(f, new PyFloat(0));
    PyDict* portrait = new PyDict();
    portrait->SetItemString("backgroundID", new PyInt(1003));
    portrait->SetItemString("lightColorID", new PyInt(1));
    portrait->SetItemString("lightID", new PyInt(1));
    portrait->SetItemString("cameraFieldOfView", new PyFloat(0.3));
    portrait->SetItemString("lightIntensity", new PyFloat(1.0));
    portrait->SetItemString("cameraPoi", Float3(0, 1.6, 0));
    portrait->SetItemString("cameraPosition", Float3(0, 1.6, 1.5));
    portrait->SetItemString("poseData", pose);

    PyTuple* args = new PyTuple(7);
    args->SetItem(0, new PyWString(name));
    args->SetItem(1, new PyInt(1));     // bloodline: Deteis
    args->SetItem(2, new PyInt(1));     // male
    args->SetItem(3, new PyInt(10));    // ancestry
    args->SetItem(4, KeyVal(info));
    args->SetItem(5, KeyVal(portrait));
    args->SetItem(6, new PyInt(17));    // school (Caldari)
    PyRep* r = bot.Call("charUnboundMgr", "", "CreateCharacterWithDoll", args);
    return (r != nullptr) and (r->IsInt() or r->IsLong()) ? PyRep::IntegerValue(r) : 0;
}

std::vector<std::string> Split(const std::string& line) {
    std::vector<std::string> out;
    std::istringstream ss(line);
    std::string w;
    while (ss >> w) out.push_back(w);
    return out;
}

int g_fail = 0;
void Check(bool ok, const std::string& what) {
    Out("%s  %s", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++g_fail;
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::printf("usage: eve-bot <host> <port> <script>\n");
        return 1;
    }
    g_start = Now();
    Bot bot;
    bot.m_quietNotify = { "OnAccountChange", "OnSkillTrained", "OnGodmaPrimeItem", "OnModuleAttributeChange",
        "OnModuleAttributeChanges", "OnLSC", "OnStandingSet", "OnCharacterSessionChanged", "OnJumpQueueUpdate",
        "OnSessionChanged", "OnGodmaShipEffect", "OnEffectHit", "OnMultiEvent", "OnSkillQueueRefreshed" };
    if (!bot.Connect(argv[1], (uint16)std::atoi(argv[2])))
        return 1;

    std::ifstream script(argv[3]);
    if (!script) { std::printf("cannot read %s\n", argv[3]); return 1; }

    std::map<std::string, std::string> vars;
    // extra arguments name=value become script variables ($name)
    for (int i = 4; i < argc; ++i) {
        std::string a(argv[i]);
        size_t eq = a.find('=');
        if (eq != std::string::npos)
            vars[a.substr(0, eq)] = a.substr(eq + 1);
    }
    std::string ship, oidShip, oidEntity, oidBeyonce, last;
    std::string line;
    while (std::getline(script, line)) {
        size_t hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);
        // substitute $vars
        for (auto& v : vars) {
            size_t pos;
            while ((pos = line.find("$" + v.first)) != std::string::npos)
                line.replace(pos, v.first.size() + 1, v.second);
        }
        std::vector<std::string> w = Split(line);
        if (w.empty()) continue;
        Out(">> %s", line.c_str());
        const std::string& cmd = w[0];

        if (cmd == "login") {
            if (!bot.Login(w[1], w[2])) return 1;
            bot.Pump(1);
        } else if (cmd == "character") {
            // the name is the rest of the line
            std::string fullName = line.substr(line.find("character") + 10);
            while (!fullName.empty() and (fullName.back() == ' ' or fullName.back() == '\r')) fullName.pop_back();
            while (!fullName.empty() and fullName[0] == ' ') fullName.erase(0, 1);
            w.resize(2);
            w[1] = fullName;
            PyRep* chars = bot.Call("charUnboundMgr", "", "GetCharactersToSelect", new PyTuple(0));
            int64 charID = 0;
            // look for our name in the reply
            std::string text = Repr(chars);
            (void)text;
            std::function<void(const PyRep*)> find = [&](const PyRep* r) {
                if (r == nullptr or charID) return;
                if (r->IsPackedRow()) {
                    const PyPackedRow* row = r->AsPackedRow();
                    int64 id = 0; std::string n;
                    for (size_t i = 0; i < row->header()->ColumnCount(); ++i) {
                        std::string c = row->header()->GetColumnName(i)->content();
                        PyRep* f = row->GetField(i);
                        if (c == "characterID") id = PyRep::IntegerValue(f);
                        if (c == "characterName") n = PyRep::StringContent(f);
                    }
                    if (n == w[1]) charID = id;
                } else if (r->IsList()) {
                    for (size_t i = 0; i < r->AsList()->size(); ++i) find(r->AsList()->GetItem(i));
                } else if (r->IsTuple()) {
                    for (size_t i = 0; i < r->AsTuple()->size(); ++i) find(r->AsTuple()->GetItem(i));
                } else if (r->IsObjectEx()) {
                    for (size_t i = 0; i < r->AsObjectEx()->list().size(); ++i) find(r->AsObjectEx()->list().GetItem(i));
                } else if (r->IsSubStream()) {
                    r->AsSubStream()->DecodeData(); find(r->AsSubStream()->decoded());
                } else if (r->IsDict()) {
                    PyRep* id = r->AsDict()->GetItemString("characterID");
                    PyRep* n = r->AsDict()->GetItemString("characterName");
                    if (id and n and PyRep::StringContent(n) == w[1]) charID = PyRep::IntegerValue(id);
                    for (PyDict::const_iterator itr = r->AsDict()->begin(); itr != r->AsDict()->end(); ++itr) find(itr->second);
                } else if (r->IsObject()) {
                    find(r->AsObject()->arguments());
                }
            };
            find(chars);
            if (charID == 0) {
                Out("creating character %s", w[1].c_str());
                charID = CreateCharacter(bot, w[1]);
                if (charID == 0) { Out("character creation failed"); return 1; }
            }
            PyTuple* args = new PyTuple(1);
            args->SetItem(0, new PyInt((int32)charID));
            bot.Call("charUnboundMgr", "", "SelectCharacterID", args);
            bot.Pump(2);
            vars["char"] = std::to_string(charID);
            Out("character %lld in station %lld, ship %lld", (long long)charID, (long long)bot.Session("stationid"), (long long)bot.Session("shipid"));
        } else if (cmd == "slash") {
            std::string text = line.substr(line.find("slash") + 6);
            while (!text.empty() and text[0] == ' ') text.erase(0, 1);
            PyTuple* args = new PyTuple(1);
            args->SetItem(0, new PyWString(text));
            PyRep* r = bot.Call("slash", "", "SlashCmd", args);
            last = ((r != nullptr) and (r->IsInt() or r->IsLong())) ? std::to_string(PyRep::IntegerValue(r)) : "";
            bot.Pump(0.5);
        } else if (cmd == "set") {
            std::string val = w[2];
            if (val == "$last") val = last;
            vars[w[1]] = val;
            Out("%s = %s", w[1].c_str(), val.c_str());
        } else if (cmd == "spaceobject") {
            // set <var> to the newest ball in space of this typeID
            int64 type = std::atoll(w[2].c_str()), best = 0;
            for (auto cur : bot.m_balls)
                if ((cur.second == type) and (cur.first > best)) best = cur.first;
            vars[w[1]] = std::to_string(best);
            Out("%s = %lld", w[1].c_str(), (long long)best);
        } else if (cmd == "assemble" or cmd == "board") {
            if (oidShip.empty()) {
                PyTuple* bp = new PyTuple(2);
                bp->SetItem(0, new PyInt((int32)bot.Session("stationid")));
                bp->SetItem(1, new PyInt(15));
                oidShip = bot.Bind("ship", bp);
            }
            PyTuple* args;
            if (cmd == "assemble") {
                // the game client sends a list of item IDs
                PyList* ids = new PyList();
                ids->AddItem(new PyInt(std::atoi(w[1].c_str())));
                args = new PyTuple(1);
                args->SetItem(0, ids);
                bot.Call("", oidShip, "AssembleShip", args);
            } else {
                args = new PyTuple(2);
                args->SetItem(0, new PyInt(std::atoi(w[1].c_str())));
                args->SetItem(1, new PyInt((int32)bot.Session("shipid")));
                bot.Call("", oidShip, "ActivateShip", args);
                ship = w[1];
            }
            bot.Pump(1);
        } else if (cmd == "movetobay") {
            // movetobay <shipID> <itemID> <qty>
            int64 shipID = std::atoll(w[1].c_str());
            w.erase(w.begin() + 1);
            PyTuple* bp = new PyTuple(2);
            bp->SetItem(0, new PyInt((int32)bot.Session("stationid")));
            bp->SetItem(1, new PyInt(15));
            std::string oidInv = bot.Bind("invbroker", bp);
            PyTuple* args = new PyTuple(2);
            args->SetItem(0, new PyInt((int32)shipID));
            args->SetItem(1, new PyInt(0));
            PyRep* r = bot.Call("", oidInv, "GetInventoryFromId", args);
            std::string oidShipInv = FindOID(r);
            args = new PyTuple(2);
            args->SetItem(0, new PyInt(std::atoi(w[1].c_str())));
            args->SetItem(1, new PyInt((int32)bot.Session("stationid")));
            PyDict* kw = new PyDict();
            kw->SetItemString("flag", new PyInt(87));
            kw->SetItemString("qty", new PyInt(std::atoi(w[2].c_str())));
            bot.Call("", oidShipInv, "Add", args, kw);
            bot.Pump(0.5);
        } else if (cmd == "undock") {
            if (oidShip.empty()) {
                PyTuple* bp = new PyTuple(2);
                bp->SetItem(0, new PyInt((int32)bot.Session("stationid")));
                bp->SetItem(1, new PyInt(15));
                oidShip = bot.Bind("ship", bp);
            }
            PyTuple* args = new PyTuple(2);
            args->SetItem(0, new PyInt((int32)bot.Session("shipid")));
            args->SetItem(1, new PyBool(false));
            bot.Call("", oidShip, "Undock", args);
            bot.Pump(1);
            oidBeyonce = bot.Bind("beyonce", new PyInt((int32)bot.Session("solarsystemid")));
            bot.Call("beyonce", "", "GetFormations", new PyTuple(0));
            bot.Pump(3);
            Out("in space: system %lld ship %lld", (long long)bot.Session("solarsystemid"), (long long)bot.Session("shipid"));
        } else if (cmd == "beyonce") {
            // beyonce <method> [args...]: a movement call on the ballpark; whole numbers are sent as ints
            if (oidBeyonce.empty())
                oidBeyonce = bot.Bind("beyonce", new PyInt((int32)bot.Session("solarsystemid")));
            PyTuple* args = new PyTuple(w.size() - 2);
            for (size_t i = 2; i < w.size(); ++i) {
                const std::string& a = w[i];
                if (a.find_first_not_of("-0123456789") == std::string::npos)
                    args->SetItem(i - 2, new PyInt(std::atoi(a.c_str())));
                else if (a.find_first_not_of("-0123456789.") == std::string::npos)
                    args->SetItem(i - 2, new PyFloat(std::atof(a.c_str())));
                else
                    args->SetItem(i - 2, new PyString(a));
            }
            bot.Call("", oidBeyonce, w[1], args);
        } else if (cmd == "rebind") {
            // after a jump the ballpark is a new one
            oidBeyonce = bot.Bind("beyonce", new PyInt((int32)bot.Session("solarsystemid")));
            bot.Call("beyonce", "", "GetFormations", new PyTuple(0));
        } else if (cmd == "stop") {
            bot.Call("", oidBeyonce, "CmdStop", new PyTuple(0));
        } else if (cmd == "launch") {
            PyTuple* bp = new PyTuple(2);
            bp->SetItem(0, new PyInt((int32)bot.Session("solarsystemid")));
            bp->SetItem(1, new PyInt(5));
            std::string oid = bot.Bind("ship", bp);
            PyList* drop = new PyList();
            PyTuple* entry = new PyTuple(2);
            entry->SetItem(0, new PyInt(std::atoi(w[1].c_str())));
            entry->SetItem(1, new PyInt(std::atoi(w[2].c_str())));
            drop->AddItem(entry);
            PyTuple* args = new PyTuple(3);
            args->SetItem(0, drop);
            args->SetItem(1, PyStatic.NewNone());
            args->SetItem(2, new PyBool(false));
            bot.Call("", oid, "Drop", args);
            bot.Pump(1);
        } else if (cmd == "drones") {
            if (oidEntity.empty())
                oidEntity = bot.Bind("entity", new PyInt((int32)bot.Session("solarsystemid")));
            std::vector<int64> ids = bot.DronesInSpace();
            PyList* list = new PyList();
            for (auto id : ids) list->AddItem(new PyInt((int32)id));
            std::string what = w[1];
            PyTuple* args;
            std::string method;
            if (what == "engage") {
                method = "CmdEngage";
                args = new PyTuple(2);
                args->SetItem(0, list);
                args->SetItem(1, new PyInt(std::atoi(w[2].c_str())));
            } else {
                if (what == "returnhome") method = "CmdReturnHome";
                else if (what == "returnbay") method = "CmdReturnBay";
                else if (what == "abandon") method = "CmdAbandonDrone";
                else if (what == "reconnect") method = "CmdReconnectToDrones";
                else { Out("unknown drone command %s", what.c_str()); continue; }
                args = new PyTuple(1);
                args->SetItem(0, list);
            }
            Out("%s on %zu drones", method.c_str(), ids.size());
            bot.Call("", oidEntity, method, args);
        } else if (cmd == "wait") {
            bot.Pump(std::atof(w[1].c_str()));
        } else if (cmd == "expect") {
            const std::string& what = w[1];
            if (what == "drones_in_space") {
                size_t n = bot.DronesInSpace().size();
                Check(n == (size_t)std::atoi(w[2].c_str()), "drones in space = " + w[2] + " (got " + std::to_string(n) + ")");
            } else if (what == "drones_state") {
                int64 want = std::atoll(w[2].c_str());
                size_t bad = 0, n = 0;
                for (auto cur : bot.m_drones) {
                    if (!cur.second.known or (cur.second.activity < 0)) continue;
                    ++n;
                    if (cur.second.activity != want) ++bad;
                }
                Check((n > 0) and (bad == 0), "all " + std::to_string(n) + " drones in state " + Bot::StateName(want) + " (" + std::to_string(bad) + " not)");
            } else if (what == "in_bay") {
                // items of a type in the active ship's drone bay
                int64 type = std::atoll(w[2].c_str());
                int64 qty = 0;
                for (auto cur : bot.m_items)
                    if ((cur.second.typeID == type) and (cur.second.locationID == bot.Session("shipid")) and (cur.second.flag == 87))
                        qty += (cur.second.quantity > 0 ? cur.second.quantity : 1);
                Check(qty == std::atoll(w[3].c_str()), "drone bay holds " + w[3] + " of type " + w[2] + " (got " + std::to_string(qty) + ")");
            } else {
                Out("unknown expect %s", what.c_str());
            }
        } else if (cmd == "quit") {
            break;
        } else {
            Out("unknown command %s", cmd.c_str());
        }
    }
    Out("done, %d check(s) failed", g_fail);
    std::exit(g_fail == 0 ? 0 : 3);
}
