/*
    ------------------------------------------------------------------------------------
    LICENSE:
    ------------------------------------------------------------------------------------
    This file is part of EVEmu: EVE Online Server Emulator
    Copyright 2006 - 2021 The EVEmu Team
    For the latest information visit https://evemu.dev
    ------------------------------------------------------------------------------------
    This program is free software; you can redistribute it and/or modify it under
    the terms of the GNU Lesser General Public License as published by the Free Software
    Foundation; either version 2 of the License, or (at your option) any later
    version.

    This program is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License along with
    this program; if not, write to the Free Software Foundation, Inc., 59 Temple
    Place - Suite 330, Boston, MA 02111-1307, USA, or go to
    http://www.gnu.org/copyleft/lesser.txt.
    ------------------------------------------------------------------------------------
*/

#include "eve-server.h"

#include "Client.h"
#include "EntityList.h"
#include "EVEServerConfig.h"
#include "StaticDataMgr.h"
#include "imageserver/ImageServer.h"
#include "killboard/Killboard.h"
#include "mail/MailMgr.h"

/* the pages are plain html 4 with a little css and no layout the in-game browser (an old chromium) can't do:  tables
 *  and floats, no flexbox.  images come from the image server itself (the "?kb" asks it for a 404 instead of the dead
 *  fallback redirect when it hasn't got one), and CCPEVE.showInfo() opens show info windows in game.
 */

static const size_t KbPageSize = 50;
static const int64 KbWeek = 7 * 24 * 3600;

// show info type ids for owners and locations
static const uint32 KbInfoCharacter = 1377;
static const uint32 KbInfoCorporation = 2;
static const uint32 KbInfoAlliance = 16159;
static const uint32 KbInfoFaction = 30;
static const uint32 KbInfoRegion = 3;
static const uint32 KbInfoConstellation = 4;
static const uint32 KbInfoSystem = 5;

static const char* KbStyle =
    "body{margin:0;background:#0c0e11;color:#c5cad0;font:12px Tahoma,Verdana,Arial,sans-serif}"
    "a{color:#86b8e8;text-decoration:none}a:hover{text-decoration:underline}"
    "#top{background:#15181d;border-bottom:1px solid #2a3038;padding:7px 12px;overflow:hidden}"
    "#top a{margin-right:14px}#top .brand{font-size:15px;font-weight:bold;color:#e0bb5e}"
    "#top form{float:right;margin:0}"
    "input,button{background:#0c0e11;color:#c5cad0;border:1px solid #3a424c;padding:3px 6px;font-size:12px}"
    "#main{padding:10px 12px}"
    "#foot{color:#5f6770;font-size:11px;padding:8px 12px 14px;border-top:1px solid #1d2127}"
    "h1{font-size:17px;margin:2px 0 6px;color:#fff;font-weight:normal}"
    "h2{font-size:13px;margin:12px 0 5px;color:#e0bb5e;border-bottom:1px solid #2a3038;padding-bottom:3px;font-weight:normal}"
    "h2 .dim{float:right}"
    "table{border-collapse:collapse}"
    "table.list{width:100%}"
    "table.list td,table.list th{padding:3px 5px;border-bottom:1px solid #1b1f25;vertical-align:middle;text-align:left}"
    "table.list th{color:#7c858f;font-weight:normal;font-size:11px}"
    "table.list td.r,table.list th.r{text-align:right}"
    "tr.k td{background:#11221a}tr.l td{background:#2a1517}tr.n td{color:#8a929b}"
    "tr.drop td{background:#13291a}tr.dest td{background:#1a1416}"
    "tr.sum td{border-top:1px solid #3a424c;color:#fff}"
    ".isk{color:#e0bb5e;white-space:nowrap}.red{color:#e06a5e}.green{color:#5ec27a}"
    ".dim{color:#7c858f;font-size:11px}.big{font-size:15px;color:#fff}"
    ".box{background:#13161a;border:1px solid #242930;padding:8px;margin-bottom:10px}"
    ".left{float:left;width:63%}.right{float:right;width:35%}.clear{clear:both}"
    ".half{float:left;width:49%}.half2{float:right;width:49%}"
    "img{border:0;vertical-align:middle}"
    "img.p{background:#1b1f25}"
    ".stat{float:left;margin:0 22px 6px 0}.stat b{display:block;font-size:15px;color:#fff;font-weight:normal}"
    ".bar{height:7px;background:#7a2d27;margin-top:4px}.bar span{display:block;height:7px;background:#3f8f57}"
    ".tabs a{display:inline-block;padding:3px 10px;border:1px solid #2a3038;margin-right:4px}"
    ".tabs a.on{background:#232931;color:#fff}"
    ".pager{margin:6px 0}.pager a,.pager b{margin-right:8px}"
    ".si{display:none;margin-left:5px;padding:0 4px;border:1px solid #3a424c;font-size:10px;color:#aab}"
    ".igb .si{display:inline}"
    ".sa{color:#6fb0ff}.sb{color:#ff7d6e}.sc{color:#c0a0ff}"
    "td.sa,th.sa{border-left:3px solid #3d74b5}td.sb,th.sb{border-left:3px solid #b5503d}"
    ".lost{border:1px solid #b5503d}.used{border:1px solid #2d3a48}span.lost,span.used{padding:0 3px;margin-right:2px}"
    "pre,textarea{background:#0c0e11;color:#aab2bb;border:1px solid #2a3038;font:11px monospace}";

static const char* KbScript =
    "function si(t,i){if(window.CCPEVE){CCPEVE.showInfo(t,i);}return false;}"
    "if(window.CCPEVE){document.documentElement.className='igb';}";

static std::string Fmt(const char* fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0)
        return std::string();
    if ((size_t)n < sizeof(buf))
        return std::string(buf, n);
    std::string out(n + 1, '\0');
    va_start(ap, fmt);
    vsnprintf(&out[0], n + 1, fmt, ap);
    va_end(ap);
    out.resize(n);
    return out;
}

static std::string Esc(const std::string& in)
{
    std::string out;
    out.reserve(in.size() + 8);
    for (char c : in) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out += c; break;
        }
    }
    return out;
}

static std::string UrlEnc(const std::string& in)
{
    std::string out;
    for (unsigned char c : in) {
        if (isalnum(c) or (c == '-') or (c == '_') or (c == '.') or (c == '~'))
            out += c;
        else
            out += Fmt("%%%02X", c);
    }
    return out;
}

static std::string Num(uint64_t v)
{
    std::string s(std::to_string(v));
    for (int i = (int)s.size() - 3; i > 0; i -= 3)
        s.insert(i, ",");
    return s;
}

// 1.23b, 456.78m, 12.3k
static std::string IskShort(double v)
{
    if (v >= 1e12) return Fmt("%.2ft", v / 1e12);
    if (v >= 1e9)  return Fmt("%.2fb", v / 1e9);
    if (v >= 1e6)  return Fmt("%.2fm", v / 1e6);
    if (v >= 1e3)  return Fmt("%.1fk", v / 1e3);
    return Fmt("%.0f", v);
}

static std::string IskFull(double v)
{
    uint64_t whole((uint64_t)v);
    uint32 cents((uint32)((v - (double)whole) * 100.0 + 0.5));
    if (cents >= 100) {
        ++whole;
        cents -= 100;
    }
    return Num(whole) + Fmt(".%02u ISK", cents);
}

static std::string FmtTime(int64 t, const char* fmt = "%Y.%m.%d %H:%M")
{
    time_t tt((time_t)t);
    struct tm tmv;
    gmtime_r(&tt, &tmv);
    char buf[64];
    strftime(buf, sizeof(buf), fmt, &tmv);
    return buf;
}

static std::string FmtDuration(int64 secs)
{
    if (secs < 60)
        return Fmt("%lld sec", (long long)secs);
    if (secs < 3600)
        return Fmt("%lld min", (long long)(secs / 60));
    return Fmt("%lldh %02lldm", (long long)(secs / 3600), (long long)((secs % 3600) / 60));
}

// security as the client shows it:  one decimal, and 0.0 only for null sec
static double DisplaySec(double sec)
{
    if ((sec > 0.0) and (sec < 0.05))
        return 0.1;
    return std::round(sec * 10.0) / 10.0;
}

static const char* SecColor(double sec)
{
    static const char* colors[] = { "#f00000", "#d73000", "#f04800", "#f06000", "#d77700", "#efef00",
                                    "#8fef2f", "#00f000", "#00ef47", "#48f0c0", "#2fefef" };
    int idx((int)std::round(DisplaySec(sec) * 10.0));
    if (idx < 0)
        idx = 0;
    if (idx > 10)
        idx = 10;
    return colors[idx];
}

static std::string SecSpan(double sec)
{
    return Fmt("<span style=\"color:%s\">%.1f</span>", SecColor(sec), DisplaySec(sec));
}

// a missing image is hidden, or replaced by its name where the picture is all there is (the battle report's ships)
static std::string Img(const std::string& path, int size, const char* cls = "p", const std::string& name = std::string())
{
    if (name.empty())
        return Fmt("<img class=\"%s\" src=\"/%s?kb\" width=\"%d\" height=\"%d\" alt=\"\" onerror=\"this.style.visibility='hidden'\">",
                   cls, path.c_str(), size, size);
    return Fmt("<img class=\"%s\" src=\"/%s?kb\" width=\"%d\" height=\"%d\" alt=\"\" title=\"%s\" onerror=\"var s=document.createElement('span');"
               "s.className='dim %s';s.appendChild(document.createTextNode(this.title));this.parentNode.replaceChild(s,this);\">",
               cls, path.c_str(), size, size, Esc(name).c_str(), cls);
}

static std::string CharImg(uint32 charID, int size)
{
    return Img(Fmt("Character/%u_%d.jpg", charID, (size <= 32 ? 32 : (size <= 64 ? 64 : (size <= 128 ? 128 : 256)))), size);
}

static std::string CorpImg(uint32 corpID, int size)
{
    return Img(Fmt("Corporation/%u_%d.png", corpID, (size <= 32 ? 32 : (size <= 64 ? 64 : 128))), size);
}

static std::string AllyImg(uint32 allyID, int size)
{
    return Img(Fmt("Alliance/%u_%d.png", allyID, (size <= 32 ? 32 : (size <= 64 ? 64 : 128))), size);
}

static std::string TypeImg(uint32 typeID, int size)
{
    return Img(Fmt("InventoryType/%u_%d.png", typeID, (size <= 32 ? 32 : 64)), size);
}

static std::string RenderImg(uint32 typeID, int size)
{
    return Img(Fmt("Render/%u_%d.png", typeID, (size <= 128 ? 128 : 256)), size);
}

static std::string OwnerImg(uint32 ownerID, int size)
{
    if (IsCharacterID(ownerID))
        return CharImg(ownerID, size);
    if (IsAlliance(ownerID))
        return AllyImg(ownerID, size);
    return CorpImg(ownerID, size);
}

static std::string ShowInfo(uint32 typeID, uint32 itemID = 0)
{
    if (itemID == 0)
        return Fmt("<a href=\"#\" class=\"si\" onclick=\"return si(%u)\">i</a>", typeID);
    return Fmt("<a href=\"#\" class=\"si\" onclick=\"return si(%u,%u)\">i</a>", typeID, itemID);
}

static uint32 OwnerInfoType(uint32 ownerID)
{
    if (IsCharacterID(ownerID))
        return KbInfoCharacter;
    if (IsAlliance(ownerID))
        return KbInfoAlliance;
    if (IsFaction(ownerID))
        return KbInfoFaction;
    return KbInfoCorporation;
}

static const char* OwnerPath(uint32 ownerID)
{
    if (IsCharacterID(ownerID))
        return "pilot";
    if (IsAlliance(ownerID))
        return "alliance";
    if (IsFaction(ownerID))
        return "faction";
    return "corp";
}

static size_t PageParam(const KbRequest& req)
{
    auto itr = req.query.find("page");
    if (itr == req.query.end())
        return 1;
    long page(strtol(itr->second.c_str(), nullptr, 10));
    return (page < 1 ? 1 : (size_t)page);
}

static std::string QueryParam(const KbRequest& req, const char* name)
{
    auto itr = req.query.find(name);
    return (itr == req.query.end() ? std::string() : itr->second);
}

static std::string ReqHeader(const KbRequest& req, const char* name)
{
    auto itr = req.headers.find(name);
    return (itr == req.headers.end() ? std::string() : itr->second);
}

// a top-ten list:  key -> count, highest first
template <typename K>
static std::vector<std::pair<K, uint32> > TopOf(const std::map<K, uint32>& counts, size_t n = 10)
{
    std::vector<std::pair<K, uint32> > top(counts.begin(), counts.end());
    std::sort(top.begin(), top.end(), [](const std::pair<K, uint32>& a, const std::pair<K, uint32>& b) {
        return (a.second != b.second ? a.second > b.second : a.first < b.first);
    });
    if (top.size() > n)
        top.resize(n);
    return top;
}

/* where an item was on the victim's ship, in the order the kill report lists them */
struct KbSlotGroup {
    const char* name;
    uint16 lo;
    uint16 hi;
};

static const KbSlotGroup KbSlotGroups[] = {
    { "High Slots", flagHiSlot0, flagHiSlot7 },
    { "Medium Slots", flagMidSlot0, flagMidSlot7 },
    { "Low Slots", flagLowSlot0, flagLowSlot7 },
    { "Rigs", flagRigSlot0, flagRigSlot7 },
    { "Subsystems", flagSubSystem0, flagSubSystem7 },
    { "Drone Bay", flagDroneBay, flagDroneBay },
    { "Implants", flagImplant, flagImplant },
    { "Cargo", flagCargoHold, flagCargoHold },
};

static int SlotGroupOf(uint16 flag)
{
    for (size_t i = 0; i < sizeof(KbSlotGroups) / sizeof(KbSlotGroups[0]); ++i)
        if ((flag >= KbSlotGroups[i].lo) and (flag <= KbSlotGroups[i].hi))
            return (int)i;
    return -1;      // other holds and bays
}

// the classic kill mail's location suffix
static const char* FlagSuffix(uint16 flag)
{
    if (flag == flagCargoHold)
        return " (Cargo)";
    if (flag == flagDroneBay)
        return " (Drone Bay)";
    if (flag == flagImplant)
        return " (Implant)";
    if ((flag >= flagLowSlot0) and (flag <= flagHiSlot7))
        return "";
    if ((flag >= flagRigSlot0) and (flag <= flagRigSlot7))
        return "";
    if ((flag >= flagSubSystem0) and (flag <= flagSubSystem7))
        return "";
    return " (Other)";
}

Killboard::Killboard()
: m_notFound(false)
{
}

void Killboard::Handle(const KbRequest& req, KbResponse& rsp)
{
    std::lock_guard<std::mutex> lock(m_lock);
    if (!sConfig.killboard.Enabled) {
        rsp.status = 404;
        rsp.contentType = "text/plain";
        rsp.body = "The killboard is turned off.";
        return;
    }
    double start(GetTimeMSeconds());
    m_data.Refresh();
    if (!m_data.IsReady()) {
        rsp.status = 503;
        rsp.contentType = "text/plain";
        rsp.body = "The killboard can't reach the database.";
        return;
    }

    // /kill/123, /pilot/123, ...
    std::vector<std::string> parts;
    size_t pos(0);
    while (pos < req.path.size()) {
        size_t next(req.path.find('/', pos));
        if (next == std::string::npos)
            next = req.path.size();
        if (next > pos)
            parts.push_back(req.path.substr(pos, next - pos));
        pos = next + 1;
    }
    std::string& out = rsp.body;
    out.reserve(64 * 1024);
    m_notFound = false;
    std::string page(parts.empty() ? std::string() : parts[0]);
    uint32 id((parts.size() > 1) ? (uint32)strtoul(parts[1].c_str(), nullptr, 10) : 0);
    if (page.empty() or (page == "kills")) {
        PageHome(req, out);
    } else if ((page == "kill") and (id != 0)) {
        PageKill(id, req, out);
    } else if ((page == "pilot") and (id != 0)) {
        PageEntity(KbFilter::Pilot, id, req, out);
    } else if ((page == "corp") and (id != 0)) {
        PageEntity(KbFilter::Corp, id, req, out);
    } else if ((page == "alliance") and (id != 0)) {
        PageEntity(KbFilter::Alliance, id, req, out);
    } else if ((page == "faction") and (id != 0)) {
        PageEntity(KbFilter::Faction, id, req, out);
    } else if ((page == "ship") and (id != 0)) {
        PageEntity(KbFilter::Ship, id, req, out);
    } else if ((page == "group") and (id != 0)) {
        PageEntity(KbFilter::Group, id, req, out);
    } else if ((page == "system") and (id != 0)) {
        PageEntity(KbFilter::System, id, req, out);
    } else if ((page == "region") and (id != 0)) {
        PageEntity(KbFilter::Region, id, req, out);
    } else if (page == "battles") {
        PageBattles(req, out);
    } else if ((page == "battle") and (id != 0)) {
        PageBattle(id, req, out);
    } else if (page == "search") {
        PageSearch(req, out);
    } else {
        PageNotFound("There is no such page.", req, out);
    }
    if (m_notFound)
        rsp.status = 404;
    _log(KILLBOARD__TRACE, "Killboard: /kb%s in %.1fms", req.path.c_str(), GetTimeMSeconds() - start);
}

void Killboard::Header(const std::string& title, const KbRequest& req, std::string& out)
{
    out += "<!DOCTYPE html>\n<html><head><meta charset=\"utf-8\"><title>";
    out += Esc(title) + " - " + Esc(sConfig.killboard.Title);
    out += "</title><style>";
    out += KbStyle;
    out += "</style><script>";
    out += KbScript;
    out += "</script></head><body><div id=\"top\">";
    out += "<a class=\"brand\" href=\"/kb/\">" + Esc(sConfig.killboard.Title) + "</a>";
    out += "<a href=\"/kb/\">Kills</a><a href=\"/kb/battles\">Battles</a>";
    // the in-game browser names the character on a trusted site
    std::string charID(ReqHeader(req, "eve_charid"));
    std::string trusted(ReqHeader(req, "eve_trusted"));
    if (!charID.empty() and (strcasecmp(trusted.c_str(), "yes") == 0)) {
        out += "<a href=\"/kb/pilot/" + Esc(charID) + "\">My kills</a>";
        std::string corpID(ReqHeader(req, "eve_corpid"));
        if (!corpID.empty())
            out += "<a href=\"/kb/corp/" + Esc(corpID) + "\">My corporation</a>";
    } else if (ReqHeader(req, "user_agent").find("EVE-IGB") != std::string::npos) {
        // the host goes into a script, so only what a host name and port can hold
        std::string host;
        for (char c : ReqHeader(req, "host"))
            if (isalnum((unsigned char)c) or (c == '.') or (c == ':') or (c == '-') or (c == '[') or (c == ']'))
                host += c;
        if (!host.empty())
            out += "<a href=\"#\" onclick=\"CCPEVE.requestTrust('http://" + Esc(host) + "/');return false;\">Trust this site</a>";
    }
    out += "<form action=\"/kb/search\" method=\"get\"><input type=\"text\" name=\"q\" size=\"22\" value=\"";
    out += Esc(QueryParam(req, "q"));
    out += "\"> <button type=\"submit\">Search</button></form></div><div id=\"main\">";
}

void Killboard::Footer(std::string& out)
{
    out += "<div class=\"clear\"></div></div><div id=\"foot\">EVE time ";
    out += FmtTime(time(nullptr));
    out += Fmt(" &middot; %s kills on the board", Num(m_data.Kills().size()).c_str());
    out += "</div></body></html>\n";
}

std::string Killboard::OwnerLink(uint32 ownerID)
{
    if (ownerID == 0)
        return std::string();
    return Fmt("<a href=\"/kb/%s/%u\">", OwnerPath(ownerID), ownerID) + Esc(m_data.OwnerName(ownerID)) + "</a>";
}

void Killboard::Pager(const std::string& base, size_t page, size_t total, size_t perPage, std::string& out)
{
    size_t pages((total + perPage - 1) / perPage);
    if (pages < 2)
        return;
    std::string sep(base.find('?') == std::string::npos ? "?" : "&");
    out += "<div class=\"pager\">";
    if (page > 1)
        out += "<a href=\"" + base + sep + Fmt("page=%zu", page - 1) + "\">&laquo; newer</a>";
    out += Fmt("<b>page %zu of %zu</b>", page, pages);
    if (page < pages)
        out += "<a href=\"" + base + sep + Fmt("page=%zu", page + 1) + "\">older &raquo;</a>";
    out += "</div>";
}

void Killboard::KillTable(const std::vector<const KbKill*>& kills, KbFilter::Type type, uint32 id, std::string& out)
{
    if (kills.empty()) {
        out += "<p class=\"dim\">No kills.</p>";
        return;
    }
    std::vector<uint32> owners;
    for (auto kill : kills) {
        owners.push_back(kill->victimChar);
        owners.push_back(kill->victimCorp);
        owners.push_back(kill->victimAlly);
        const KbAttacker& fb = kill->attackers.front();
        owners.push_back(fb.charID);
        owners.push_back(fb.corpID);
        owners.push_back(fb.allyID);
    }
    m_data.PrefetchOwners(owners);

    out += "<table class=\"list\"><tr><th>Time</th><th colspan=\"2\">Ship</th><th>Place</th><th colspan=\"2\">Victim</th>"
           "<th>Final blow</th></tr>";
    for (auto kill : kills) {
        const char* cls("");
        if (type != KbFilter::All) {
            if (KillboardData::IsLoss(*kill, type, id, m_data))
                cls = " class=\"l\"";
            else if ((type != KbFilter::System) and (type != KbFilter::Region))
                cls = " class=\"k\"";
        } else if (kill->npcOnly) {
            cls = " class=\"n\"";
        }
        const KbTypeInfo* ship(m_data.GetType(kill->victimShip));
        const KbSystemInfo* sys(m_data.GetSystem(kill->systemID));
        std::string link(Fmt("/kb/kill/%u", kill->killID));
        out += Fmt("<tr%s><td>%s<br><b>%s</b></td>", cls, FmtTime(kill->time, "%Y.%m.%d").c_str(), FmtTime(kill->time, "%H:%M").c_str());
        out += "<td width=\"34\"><a href=\"" + link + "\">" + TypeImg(kill->victimShip, 32) + "</a></td>";
        out += "<td><a href=\"" + link + "\">" + Esc(m_data.TypeName(kill->victimShip)) + "</a>";
        if (ship != nullptr)
            out += " <span class=\"dim\">" + Esc(m_data.GroupName(ship->groupID)) + "</span>";
        out += "<br><span class=\"isk\">" + IskShort(kill->TotalValue()) + "</span></td>";
        out += Fmt("<td><a href=\"/kb/system/%u\">", kill->systemID) + Esc(m_data.SystemName(kill->systemID)) + "</a> ";
        if (sys != nullptr) {
            out += SecSpan(sys->security);
            out += Fmt("<br><a class=\"dim\" href=\"/kb/region/%u\">", sys->regionID) + Esc(m_data.RegionName(sys->regionID)) + "</a>";
        }
        out += "</td>";
        uint32 victim(kill->victimChar != 0 ? kill->victimChar : kill->victimCorp);
        out += "<td width=\"34\">" + OwnerImg(victim, 32) + "</td><td>";
        if (kill->victimChar != 0)
            out += OwnerLink(kill->victimChar) + "<br>";
        out += "<span class=\"dim\">" + OwnerLink(kill->victimCorp);
        if (kill->victimAlly != 0)
            out += " / " + OwnerLink(kill->victimAlly);
        out += "</span></td><td>";
        const KbAttacker& fb = kill->attackers.front();
        if (fb.charID != 0)
            out += OwnerLink(fb.charID);
        else
            out += Esc(m_data.TypeName(fb.shipTypeID));
        out += "<br><span class=\"dim\">" + OwnerLink(fb.corpID);
        if (fb.allyID != 0)
            out += " / " + OwnerLink(fb.allyID);
        out += Fmt(" (%zu)</span></td></tr>", kill->attackers.size());
    }
    out += "</table>";
}

void Killboard::PageHome(const KbRequest& req, std::string& out)
{
    bool pvp(QueryParam(req, "pvp") == "1");
    size_t page(PageParam(req));
    std::vector<const KbKill*> list;
    size_t total(0);
    const std::vector<KbKill*>& kills = m_data.Kills();
    for (auto itr = kills.rbegin(); itr != kills.rend(); ++itr) {
        if (pvp and (*itr)->npcOnly)
            continue;
        if ((total >= (page - 1) * KbPageSize) and (list.size() < KbPageSize))
            list.push_back(*itr);
        ++total;
    }

    Header("Kills", req, out);
    out += "<div class=\"left\"><div class=\"tabs\">";
    out += Fmt("<a href=\"/kb/\"%s>All kills</a>", pvp ? "" : " class=\"on\"");
    out += Fmt("<a href=\"/kb/?pvp=1\"%s>Player kills</a>", pvp ? " class=\"on\"" : "");
    out += "</div>";
    Pager(pvp ? "/kb/?pvp=1" : "/kb/", page, total, KbPageSize, out);
    KillTable(list, KbFilter::All, 0, out);
    Pager(pvp ? "/kb/?pvp=1" : "/kb/", page, total, KbPageSize, out);
    out += "</div><div class=\"right\">";

    // this week's top lists (kills by players only)
    int64 since(time(nullptr) - KbWeek);
    std::map<uint32, uint32> pilots, corps, allys, ships, systems;
    std::vector<const KbKill*> valuable;
    for (auto itr = kills.rbegin(); itr != kills.rend(); ++itr) {
        const KbKill* kill(*itr);
        if (kill->time < since)
            break;
        if (kill->npcOnly)
            continue;
        valuable.push_back(kill);
        ++systems[kill->systemID];
        std::set<uint32> kc, ka;
        for (auto& a : kill->attackers) {
            if (a.charID == 0)
                continue;
            ++pilots[a.charID];
            if (a.shipTypeID != 0)
                ++ships[a.shipTypeID];
            if (a.corpID != 0)
                kc.insert(a.corpID);
            if (a.allyID != 0)
                ka.insert(a.allyID);
        }
        for (auto c : kc)
            ++corps[c];
        for (auto a : ka)
            ++allys[a];
    }
    std::sort(valuable.begin(), valuable.end(), [](const KbKill* a, const KbKill* b) { return a->TotalValue() > b->TotalValue(); });
    if (valuable.size() > 6)
        valuable.resize(6);

    if (!valuable.empty()) {
        out += "<h2>Most valuable this week</h2><table class=\"list\">";
        for (auto kill : valuable) {
            out += Fmt("<tr><td width=\"34\"><a href=\"/kb/kill/%u\">", kill->killID) + TypeImg(kill->victimShip, 32) + "</a></td>";
            out += Fmt("<td><a href=\"/kb/kill/%u\">", kill->killID) + Esc(m_data.TypeName(kill->victimShip)) + "</a><br>";
            out += "<span class=\"dim\">" + Esc(m_data.OwnerName(kill->victimChar != 0 ? kill->victimChar : kill->victimCorp)) + "</span></td>";
            out += "<td class=\"r\"><span class=\"isk\">" + IskShort(kill->TotalValue()) + "</span></td></tr>";
        }
        out += "</table>";
    }

    const std::vector<KbBattle>& battles = m_data.Battles();
    if (!battles.empty()) {
        out += "<h2>Recent battles <a class=\"dim\" href=\"/kb/battles\">all</a></h2><table class=\"list\">";
        for (size_t i = 0; (i < battles.size()) and (i < 5); ++i) {
            const KbBattle& b = battles[i];
            const KbSystemInfo* sys(m_data.GetSystem(b.systemID));
            out += Fmt("<tr><td><a href=\"/kb/battle/%u\">", b.firstKillID()) + Esc(m_data.SystemName(b.systemID)) + "</a> ";
            if (sys != nullptr)
                out += SecSpan(sys->security);
            out += "<br><span class=\"dim\">" + FmtTime(b.start) + "</span></td>";
            out += Fmt("<td class=\"r\">%zu kills<br><span class=\"isk\">", b.kills.size()) + IskShort(b.value) + "</span></td></tr>";
        }
        out += "</table>";
    }

    auto topList = [&](const char* title, const std::map<uint32, uint32>& counts, int kind) {
        if (counts.empty())
            return;
        std::vector<std::pair<uint32, uint32> > top(TopOf(counts));
        std::vector<uint32> ids;
        for (auto& t : top)
            ids.push_back(t.first);
        if (kind == 0)
            m_data.PrefetchOwners(ids);
        out += "<h2>" + std::string(title) + " <span class=\"dim\">kills, 7 days</span></h2><table class=\"list\">";
        for (auto& t : top) {
            out += "<tr><td width=\"34\">";
            if (kind == 0) {
                out += OwnerImg(t.first, 32) + "</td><td>" + OwnerLink(t.first);
            } else if (kind == 1) {
                out += TypeImg(t.first, 32) + Fmt("</td><td><a href=\"/kb/ship/%u\">", t.first) + Esc(m_data.TypeName(t.first)) + "</a>";
            } else {
                const KbSystemInfo* sys(m_data.GetSystem(t.first));
                out += Fmt("</td><td><a href=\"/kb/system/%u\">", t.first) + Esc(m_data.SystemName(t.first)) + "</a> ";
                if (sys != nullptr)
                    out += SecSpan(sys->security) + " <span class=\"dim\">" + Esc(m_data.RegionName(sys->regionID)) + "</span>";
            }
            out += Fmt("</td><td class=\"r\">%u</td></tr>", t.second);
        }
        out += "</table>";
    };
    topList("Top pilots", pilots, 0);
    topList("Top corporations", corps, 0);
    topList("Top alliances", allys, 0);
    topList("Top ships", ships, 1);
    topList("Top systems", systems, 2);
    if (pilots.empty() and valuable.empty())
        out += "<p class=\"dim\">No player kills this week.</p>";
    out += "</div>";
    Footer(out);
}

void Killboard::PageKill(uint32 killID, const KbRequest& req, std::string& out)
{
    KbKill kill;
    if (!m_data.GetKill(killID, kill)) {
        PageNotFound(Fmt("There is no kill %u.", killID), req, out);
        return;
    }
    std::vector<uint32> owners = { kill.victimChar, kill.victimCorp, kill.victimAlly, kill.victimFaction };
    for (auto& a : kill.attackers) {
        owners.push_back(a.charID);
        owners.push_back(a.corpID);
        owners.push_back(a.allyID);
        owners.push_back(a.factionID);
    }
    m_data.PrefetchOwners(owners);

    const KbTypeInfo* ship(m_data.GetType(kill.victimShip));
    const KbSystemInfo* sys(m_data.GetSystem(kill.systemID));
    std::string victimName(m_data.OwnerName(kill.victimChar != 0 ? kill.victimChar : kill.victimCorp));
    Header(victimName + "'s " + m_data.TypeName(kill.victimShip), req, out);

    // the victim, the ship, where and when, and what it cost
    out += "<div class=\"box\"><table width=\"100%\"><tr><td width=\"136\" valign=\"top\">";
    out += (kill.victimChar != 0 ? CharImg(kill.victimChar, 128) : CorpImg(kill.victimCorp, 128));
    out += "</td><td width=\"136\" valign=\"top\">" + RenderImg(kill.victimShip, 128) + "</td><td valign=\"top\">";
    if (kill.victimChar != 0) {
        out += "<h1>" + OwnerLink(kill.victimChar) + ShowInfo(KbInfoCharacter, kill.victimChar) + "</h1>";
    } else {
        out += "<h1>" + Esc(victimName) + "</h1>";
    }
    out += "<table><tr><td width=\"36\">" + CorpImg(kill.victimCorp, 32) + "</td><td>" + OwnerLink(kill.victimCorp)
         + ShowInfo(OwnerInfoType(kill.victimCorp), kill.victimCorp) + "</td></tr>";
    if (kill.victimAlly != 0)
        out += "<tr><td>" + AllyImg(kill.victimAlly, 32) + "</td><td>" + OwnerLink(kill.victimAlly)
             + ShowInfo(KbInfoAlliance, kill.victimAlly) + "</td></tr>";
    if (kill.victimFaction != 0)
        out += "<tr><td></td><td>" + OwnerLink(kill.victimFaction) + " <span class=\"dim\">(militia)</span></td></tr>";
    out += "</table><p><span class=\"dim\">Ship</span><br><span class=\"big\">";
    out += Fmt("<a href=\"/kb/ship/%u\">", kill.victimShip) + Esc(m_data.TypeName(kill.victimShip)) + "</a></span>"
         + ShowInfo(kill.victimShip);
    if (ship != nullptr)
        out += Fmt(" <a class=\"dim\" href=\"/kb/group/%u\">", ship->groupID) + Esc(m_data.GroupName(ship->groupID)) + "</a>";
    out += "</p></td><td valign=\"top\" width=\"36%\"><table class=\"list\">";
    out += "<tr><td class=\"dim\">System</td><td>";
    out += Fmt("<a href=\"/kb/system/%u\">", kill.systemID) + Esc(m_data.SystemName(kill.systemID)) + "</a> ";
    if (sys != nullptr) {
        out += SecSpan(sys->security) + ShowInfo(KbInfoSystem, kill.systemID) + "<br><span class=\"dim\">"
             + Esc(m_data.ConstellationName(sys->constellationID)) + " &lt; "
             + Fmt("<a href=\"/kb/region/%u\">", sys->regionID) + Esc(m_data.RegionName(sys->regionID)) + "</a></span>";
    }
    out += "</td></tr>";
    if (kill.moonID != 0)
        out += "<tr><td class=\"dim\">Moon</td><td>" + Esc(m_data.CelestialName(kill.moonID)) + "</td></tr>";
    out += "<tr><td class=\"dim\">Time</td><td>" + FmtTime(kill.time) + "</td></tr>";
    out += "<tr><td class=\"dim\">Damage taken</td><td>" + Num(kill.damageTaken) + "</td></tr>";
    out += "<tr><td class=\"dim\">Ship</td><td class=\"isk\">" + IskFull(kill.shipValue) + "</td></tr>";
    out += "<tr><td class=\"dim\">Destroyed</td><td class=\"red\">" + IskFull(kill.destroyedValue) + "</td></tr>";
    out += "<tr><td class=\"dim\">Dropped</td><td class=\"green\">" + IskFull(kill.droppedValue) + "</td></tr>";
    out += "<tr><td class=\"dim\">Total</td><td class=\"isk\"><b>" + IskFull(kill.TotalValue()) + "</b></td></tr>";
    out += "</table></td></tr></table></div>";

    // fitting and cargo, by where they were
    out += "<div class=\"left\">";
    std::vector<std::vector<const KbItem*> > groups(sizeof(KbSlotGroups) / sizeof(KbSlotGroups[0]) + 1);
    for (auto& item : kill.items) {
        if ((item.flag == 0) and (item.typeID == kill.victimShip))
            continue;
        int g(SlotGroupOf(item.flag));
        groups[g < 0 ? groups.size() - 1 : g].push_back(&item);
    }
    bool any(false);
    out += "<table class=\"list\">";
    for (size_t g = 0; g < groups.size(); ++g) {
        if (groups[g].empty())
            continue;
        any = true;
        // modules before the charges loaded in them
        std::stable_sort(groups[g].begin(), groups[g].end(), [this](const KbItem* a, const KbItem* b) {
            if (a->flag != b->flag)
                return a->flag < b->flag;
            const KbTypeInfo* ta(m_data.GetType(a->typeID));
            const KbTypeInfo* tb(m_data.GetType(b->typeID));
            bool ca((ta != nullptr) and (ta->categoryID == EVEDB::invCategories::Charge));
            bool cb((tb != nullptr) and (tb->categoryID == EVEDB::invCategories::Charge));
            return (!ca and cb);
        });
        out += Fmt("<tr><th colspan=\"3\">%s</th><th class=\"r\">Qty</th><th class=\"r\">Value</th></tr>",
                   (g < groups.size() - 1 ? KbSlotGroups[g].name : "Other"));
        for (auto item : groups[g]) {
            double price(item->singleton == 2 ? 0.0 : m_data.TypePrice(item->typeID));
            const KbTypeInfo* info(m_data.GetType(item->typeID));
            bool charge((info != nullptr) and (info->categoryID == EVEDB::invCategories::Charge) and (g < 5));
            for (int pass = 0; pass < 2; ++pass) {
                uint32 qty(pass == 0 ? item->destroyed : item->dropped);
                if (qty == 0)
                    continue;
                out += Fmt("<tr class=\"%s\"><td width=\"34\">", (pass == 0 ? "dest" : "drop")) + TypeImg(item->typeID, 32) + "</td>";
                out += "<td>" + std::string(charge ? "&nbsp;&nbsp;&nbsp;" : "") + Esc(m_data.TypeName(item->typeID)) + ShowInfo(item->typeID);
                if (item->singleton == 2)
                    out += " <span class=\"dim\">(copy)</span>";
                out += Fmt("</td><td class=\"dim\">%s</td>", (pass == 0 ? "destroyed" : "dropped"));
                out += "<td class=\"r\">" + Num(qty) + "</td><td class=\"r isk\">" + IskShort(price * qty) + "</td></tr>";
            }
        }
    }
    out += "<tr class=\"sum\"><td colspan=\"3\">Ship</td><td></td><td class=\"r isk\">" + IskShort(kill.shipValue) + "</td></tr>";
    out += "<tr><td colspan=\"3\" class=\"red\">Destroyed</td><td></td><td class=\"r isk\">" + IskShort(kill.destroyedValue) + "</td></tr>";
    out += "<tr><td colspan=\"3\" class=\"green\">Dropped</td><td></td><td class=\"r isk\">" + IskShort(kill.droppedValue) + "</td></tr>";
    out += "<tr><td colspan=\"3\"><b>Total</b></td><td></td><td class=\"r isk\"><b>" + IskShort(kill.TotalValue()) + "</b></td></tr>";
    out += "</table>";
    if (!any)
        out += "<p class=\"dim\">Nothing was fitted or carried.</p>";

    KbBattle battle;
    if (m_data.FindBattle(kill.killID, battle))
        out += Fmt("<h2>Battle</h2><p>This kill was part of a battle in %s: <a href=\"/kb/battle/%u\">%zu kills, %s ISK</a>.</p>",
                   Esc(m_data.SystemName(battle.systemID)).c_str(), battle.firstKillID(), battle.kills.size(), IskShort(battle.value).c_str());

    out += "<h2>Kill mail</h2><textarea rows=\"14\" cols=\"70\" readonly onclick=\"this.select()\">";
    out += Esc(KillmailText(kill));
    out += "</textarea></div>";

    // everyone involved:  final blow first, then by damage
    double totalDamage(0.0);
    for (auto& a : kill.attackers)
        totalDamage += a.damage;
    out += "<div class=\"right\"><h2>" + Fmt("Involved parties: %zu", kill.attackers.size())
         + "<span class=\"dim\">" + Num((uint64_t)totalDamage) + " damage</span></h2><table class=\"list\">";
    for (auto& a : kill.attackers) {
        out += "<tr><td width=\"34\" valign=\"top\">";
        out += (a.charID != 0 ? CharImg(a.charID, 32) : TypeImg(a.shipTypeID, 32));
        out += "</td><td width=\"34\" valign=\"top\">";
        if (a.charID != 0)
            out += Fmt("<a href=\"/kb/ship/%u\">", a.shipTypeID) + TypeImg(a.shipTypeID, 32) + "</a>";
        out += "</td><td width=\"34\" valign=\"top\">";
        if ((a.weaponTypeID != 0) and (a.weaponTypeID != a.shipTypeID))
            out += TypeImg(a.weaponTypeID, 32);
        out += "</td><td>";
        if (a.charID != 0) {
            out += OwnerLink(a.charID) + ShowInfo(KbInfoCharacter, a.charID);
            out += Fmt(" <span class=\"dim\">(%.1f)</span>", a.security);
        } else {
            out += Esc(m_data.TypeName(a.shipTypeID)) + ShowInfo(a.shipTypeID);
        }
        if (a.finalBlow)
            out += " <span class=\"isk\">final blow</span>";
        out += "<br><span class=\"dim\">" + OwnerLink(a.corpID);
        if (a.allyID != 0)
            out += " / " + OwnerLink(a.allyID);
        out += "</span>";
        if ((a.charID != 0) and (a.weaponTypeID != 0))
            out += "<br><span class=\"dim\">" + Esc(m_data.TypeName(a.shipTypeID))
                 + ((a.weaponTypeID != a.shipTypeID) ? " &middot; " + Esc(m_data.TypeName(a.weaponTypeID)) : std::string()) + "</span>";
        out += "</td><td class=\"r\" valign=\"top\">" + Num(a.damage);
        if (totalDamage > 0.0)
            out += Fmt("<br><span class=\"dim\">%.1f%%</span>", (a.damage * 100.0) / totalDamage);
        out += "</td></tr>";
    }
    out += "</table></div>";
    Footer(out);
}

std::string Killboard::KillmailText(const KbKill& kill)
{
    const KbSystemInfo* sys(m_data.GetSystem(kill.systemID));
    auto ownerOrNone = [this](uint32 id) { return (id == 0 ? std::string("None") : m_data.OwnerName(id)); };
    std::string t;
    t += FmtTime(kill.time) + "\n\n";
    if (kill.victimChar != 0) {
        t += "Victim: " + m_data.OwnerName(kill.victimChar) + "\n";
        t += "Corp: " + ownerOrNone(kill.victimCorp) + "\n";
    } else {
        t += "Corp: " + ownerOrNone(kill.victimCorp) + "\n";
    }
    t += "Alliance: " + ownerOrNone(kill.victimAlly) + "\n";
    t += "Faction: " + ownerOrNone(kill.victimFaction) + "\n";
    t += "Destroyed: " + m_data.TypeName(kill.victimShip) + "\n";
    t += "System: " + m_data.SystemName(kill.systemID) + "\n";
    if (sys != nullptr)
        t += Fmt("Security: %.1f\n", DisplaySec(sys->security));
    if (kill.moonID != 0)
        t += "Moon: " + m_data.CelestialName(kill.moonID) + "\n";
    t += Fmt("Damage Taken: %u\n\nInvolved parties:\n\n", kill.damageTaken);
    for (auto& a : kill.attackers) {
        if (a.charID != 0) {
            t += "Name: " + m_data.OwnerName(a.charID) + (a.finalBlow ? " (laid the final blow)" : "") + "\n";
            t += Fmt("Security: %.1f\n", a.security);
            t += "Corp: " + ownerOrNone(a.corpID) + "\n";
            t += "Alliance: " + ownerOrNone(a.allyID) + "\n";
            t += "Faction: " + ownerOrNone(a.factionID) + "\n";
            t += "Ship: " + m_data.TypeName(a.shipTypeID) + "\n";
            t += "Weapon: " + m_data.TypeName(a.weaponTypeID != 0 ? a.weaponTypeID : a.shipTypeID) + "\n";
        } else {
            t += "Name: " + m_data.TypeName(a.shipTypeID) + " / " + ownerOrNone(a.corpID) + (a.finalBlow ? " (laid the final blow)" : "") + "\n";
        }
        t += Fmt("Damage Done: %u\n\n", a.damage);
    }
    for (int pass = 0; pass < 2; ++pass) {
        std::string lines;
        for (auto& item : kill.items) {
            if ((item.flag == 0) and (item.typeID == kill.victimShip))
                continue;
            uint32 qty(pass == 0 ? item.destroyed : item.dropped);
            if (qty == 0)
                continue;
            lines += m_data.TypeName(item.typeID);
            if (item.singleton == 2)
                lines += " (Copy)";
            if (qty > 1)
                lines += Fmt(", Qty: %u", qty);
            lines += FlagSuffix(item.flag);
            lines += "\n";
        }
        if (!lines.empty())
            t += (pass == 0 ? "Destroyed items:\n\n" : "Dropped items:\n\n") + lines + "\n";
    }
    return t;
}

void Killboard::PageEntity(KbFilter::Type type, uint32 id, const KbRequest& req, std::string& out)
{
    int side(0);
    std::string sideParam(QueryParam(req, "side"));
    if (sideParam == "kills")
        side = 1;
    else if (sideParam == "losses")
        side = 2;
    bool place((type == KbFilter::System) or (type == KbFilter::Region));
    if (place)
        side = 0;
    size_t page(PageParam(req));

    static const char* paths[] = { "", "pilot", "corp", "alliance", "faction", "ship", "group", "system", "region" };
    std::string base(Fmt("/kb/%s/%u", paths[type], id));

    // the name and picture
    std::string name, sub, image;
    uint32 newestCorp(0), newestAlly(0);
    if ((type == KbFilter::Pilot) or (type == KbFilter::Corp)) {
        // the corporation and alliance of their newest kill or loss
        const std::vector<KbKill*>& kills = m_data.Kills();
        for (auto itr = kills.rbegin(); (itr != kills.rend()) and (newestCorp == 0); ++itr) {
            const KbKill& k = **itr;
            if (((type == KbFilter::Pilot) and (k.victimChar == id)) or ((type == KbFilter::Corp) and (k.victimCorp == id))) {
                newestCorp = k.victimCorp;
                newestAlly = k.victimAlly;
                break;
            }
            for (auto& a : k.attackers)
                if (((type == KbFilter::Pilot) and (a.charID == id)) or ((type == KbFilter::Corp) and (a.corpID == id))) {
                    newestCorp = a.corpID;
                    newestAlly = a.allyID;
                    break;
                }
        }
    }
    switch (type) {
        case KbFilter::Pilot:
            name = m_data.OwnerName(id);
            image = CharImg(id, 128);
            if (newestCorp != 0) {
                sub = CorpImg(newestCorp, 32) + " " + OwnerLink(newestCorp);
                if (newestAlly != 0)
                    sub += "<br>" + AllyImg(newestAlly, 32) + " " + OwnerLink(newestAlly);
            }
            sub = ShowInfo(KbInfoCharacter, id) + "<br>" + sub;
            break;
        case KbFilter::Corp:
            name = m_data.OwnerName(id);
            image = CorpImg(id, 128);
            if (newestAlly != 0)
                sub = AllyImg(newestAlly, 32) + " " + OwnerLink(newestAlly);
            sub = ShowInfo(KbInfoCorporation, id) + "<br>" + sub;
            break;
        case KbFilter::Alliance:
            name = m_data.OwnerName(id);
            image = AllyImg(id, 128);
            sub = ShowInfo(KbInfoAlliance, id);
            break;
        case KbFilter::Faction:
            name = m_data.OwnerName(id);
            image = CorpImg(id, 128);
            sub = ShowInfo(KbInfoFaction, id);
            break;
        case KbFilter::Ship: {
            name = m_data.TypeName(id);
            image = RenderImg(id, 128);
            const KbTypeInfo* info(m_data.GetType(id));
            if (info != nullptr)
                sub = Fmt("<a href=\"/kb/group/%u\">", info->groupID) + Esc(m_data.GroupName(info->groupID)) + "</a>";
            sub += ShowInfo(id);
        } break;
        case KbFilter::Group:
            name = m_data.GroupName(id);
            sub = "Ship class";
            break;
        case KbFilter::System: {
            name = m_data.SystemName(id);
            const KbSystemInfo* sys(m_data.GetSystem(id));
            if (sys != nullptr)
                sub = SecSpan(sys->security) + ShowInfo(KbInfoSystem, id) + "<br>" + Esc(m_data.ConstellationName(sys->constellationID))
                    + " &lt; " + Fmt("<a href=\"/kb/region/%u\">", sys->regionID) + Esc(m_data.RegionName(sys->regionID)) + "</a>";
        } break;
        case KbFilter::Region:
            name = m_data.RegionName(id);
            sub = ShowInfo(KbInfoRegion, id);
            break;
        default:
            break;
    }
    if (name.empty())
        name = Fmt("#%u", id);

    KbStats stats;
    m_data.GetStats(type, id, 0, stats);
    std::vector<const KbKill*> list;
    size_t total(0);
    m_data.GetKillList(type, id, side, 0, (page - 1) * KbPageSize, KbPageSize, list, total);

    Header(name, req, out);
    out += "<div class=\"box\"><table width=\"100%\"><tr>";
    if (!image.empty())
        out += "<td width=\"136\" valign=\"top\">" + image + "</td>";
    out += "<td valign=\"top\"><h1>" + Esc(name) + "</h1>" + sub + "</td><td valign=\"top\" width=\"50%\">";
    if (place) {
        out += Fmt("<div class=\"stat\"><b>%s</b>kills</div>", Num(stats.kills).c_str());
        out += "<div class=\"stat\"><b class=\"isk\">" + IskShort(stats.iskKilled) + "</b>ISK destroyed</div>";
    } else {
        double iskTotal(stats.iskKilled + stats.iskLost);
        double eff(iskTotal > 0.0 ? (stats.iskKilled * 100.0) / iskTotal : 0.0);
        out += Fmt("<div class=\"stat\"><b class=\"green\">%s</b>kills</div>", Num(stats.kills).c_str());
        out += Fmt("<div class=\"stat\"><b class=\"red\">%s</b>losses</div>", Num(stats.losses).c_str());
        out += "<div class=\"stat\"><b class=\"green\">" + IskShort(stats.iskKilled) + "</b>ISK destroyed</div>";
        out += "<div class=\"stat\"><b class=\"red\">" + IskShort(stats.iskLost) + "</b>ISK lost</div>";
        out += Fmt("<div class=\"stat\"><b>%.1f%%</b>efficiency</div>", eff);
        if (type == KbFilter::Pilot)
            out += Fmt("<div class=\"stat\"><b>%u</b>solo kills</div>", stats.soloKills);
        out += Fmt("<div class=\"clear\"></div><div class=\"bar\"><span style=\"width:%.0f%%\"></span></div>", eff);
    }
    out += "</td></tr></table></div>";

    out += "<div class=\"left\">";
    if (!place) {
        out += "<div class=\"tabs\">";
        out += "<a href=\"" + base + "\"" + (side == 0 ? " class=\"on\"" : "") + ">All</a>";
        out += "<a href=\"" + base + "?side=kills\"" + (side == 1 ? " class=\"on\"" : "") + ">Kills</a>";
        out += "<a href=\"" + base + "?side=losses\"" + (side == 2 ? " class=\"on\"" : "") + ">Losses</a>";
        out += "</div>";
    }
    std::string pageBase(base + (side == 1 ? "?side=kills" : (side == 2 ? "?side=losses" : "")));
    Pager(pageBase, page, total, KbPageSize, out);
    KillTable(list, type, id, out);
    Pager(pageBase, page, total, KbPageSize, out);
    out += "</div><div class=\"right\">";

    // what they flew, who flew it, where it happens
    std::map<uint32, uint32> ships, pilots, systems, corps;
    for (auto cur : m_data.Kills()) {
        const KbKill& k = *cur;
        bool kill(KillboardData::IsKill(k, type, id, m_data));
        if (!kill)
            continue;
        if (!place)
            ++systems[k.systemID];
        for (auto& a : k.attackers) {
            if (a.charID == 0)
                continue;
            bool mine(false);
            switch (type) {
                case KbFilter::Pilot:       mine = (a.charID == id); break;
                case KbFilter::Corp:        mine = (a.corpID == id); break;
                case KbFilter::Alliance:    mine = (a.allyID == id); break;
                case KbFilter::Faction:     mine = (a.factionID == id); break;
                case KbFilter::Ship:        mine = (a.shipTypeID == id); break;
                case KbFilter::Group: {
                    const KbTypeInfo* info(m_data.GetType(a.shipTypeID));
                    mine = ((info != nullptr) and (info->groupID == id));
                } break;
                default:                    mine = true; break;
            }
            if (!mine)
                continue;
            if ((type != KbFilter::Ship) and (a.shipTypeID != 0))
                ++ships[a.shipTypeID];
            if (type != KbFilter::Pilot)
                ++pilots[a.charID];
            if (place and (a.corpID != 0))
                ++corps[a.corpID];
        }
    }
    auto ownerList = [&](const char* title, const std::map<uint32, uint32>& counts) {
        if (counts.empty())
            return;
        std::vector<std::pair<uint32, uint32> > top(TopOf(counts));
        std::vector<uint32> ids;
        for (auto& t : top)
            ids.push_back(t.first);
        m_data.PrefetchOwners(ids);
        out += "<h2>" + std::string(title) + "</h2><table class=\"list\">";
        for (auto& t : top)
            out += "<tr><td width=\"34\">" + OwnerImg(t.first, 32) + "</td><td>" + OwnerLink(t.first) + Fmt("</td><td class=\"r\">%u</td></tr>", t.second);
        out += "</table>";
    };
    if (!ships.empty()) {
        out += std::string("<h2>") + (place ? "Top ships" : "Ships flown") + "</h2><table class=\"list\">";
        for (auto& t : TopOf(ships))
            out += "<tr><td width=\"34\">" + TypeImg(t.first, 32) + Fmt("</td><td><a href=\"/kb/ship/%u\">", t.first)
                 + Esc(m_data.TypeName(t.first)) + Fmt("</a></td><td class=\"r\">%u</td></tr>", t.second);
        out += "</table>";
    }
    ownerList("Top pilots", pilots);
    ownerList("Top corporations", corps);
    if (!systems.empty()) {
        out += "<h2>Top systems</h2><table class=\"list\">";
        for (auto& t : TopOf(systems)) {
            const KbSystemInfo* sys(m_data.GetSystem(t.first));
            out += Fmt("<tr><td><a href=\"/kb/system/%u\">", t.first) + Esc(m_data.SystemName(t.first)) + "</a> ";
            if (sys != nullptr)
                out += SecSpan(sys->security) + " <span class=\"dim\">" + Esc(m_data.RegionName(sys->regionID)) + "</span>";
            out += Fmt("</td><td class=\"r\">%u</td></tr>", t.second);
        }
        out += "</table>";
    }
    if (place) {
        std::vector<const KbBattle*> here;
        for (auto& b : m_data.Battles()) {
            const KbSystemInfo* sys(m_data.GetSystem(b.systemID));
            if ((b.systemID == id) or ((type == KbFilter::Region) and (sys != nullptr) and (sys->regionID == id)))
                here.push_back(&b);
            if (here.size() >= 10)
                break;
        }
        if (!here.empty()) {
            out += "<h2>Battles</h2><table class=\"list\">";
            for (auto b : here)
                out += Fmt("<tr><td><a href=\"/kb/battle/%u\">", b->firstKillID()) + FmtTime(b->start) + "</a> <span class=\"dim\">"
                     + Esc(m_data.SystemName(b->systemID)) + Fmt("</span></td><td class=\"r\">%zu kills</td></tr>", b->kills.size());
            out += "</table>";
        }
    }
    out += "</div>";
    Footer(out);
}

void Killboard::PageBattles(const KbRequest& req, std::string& out)
{
    size_t page(PageParam(req));
    const std::vector<KbBattle>& battles = m_data.Battles();
    Header("Battles", req, out);
    out += "<h1>Battles</h1><p class=\"dim\">";
    out += Fmt("%u or more kills by players in one system, each within %u minutes of the one before.",
               std::max<uint32>(2, sConfig.killboard.BattleMinKills), sConfig.killboard.BattleGap);
    out += "</p>";
    if (battles.empty()) {
        out += "<p>No battles yet.</p>";
        Footer(out);
        return;
    }
    Pager("/kb/battles", page, battles.size(), KbPageSize, out);
    out += "<table class=\"list\"><tr><th>Time</th><th>System</th><th>Region</th><th class=\"r\">Kills</th>"
           "<th class=\"r\">Pilots</th><th class=\"r\">ISK destroyed</th><th>Duration</th></tr>";
    for (size_t i = (page - 1) * KbPageSize; (i < battles.size()) and (i < page * KbPageSize); ++i) {
        const KbBattle& b = battles[i];
        const KbSystemInfo* sys(m_data.GetSystem(b.systemID));
        out += Fmt("<tr><td><a href=\"/kb/battle/%u\">", b.firstKillID()) + FmtTime(b.start) + "</a></td>";
        out += Fmt("<td><a href=\"/kb/system/%u\">", b.systemID) + Esc(m_data.SystemName(b.systemID)) + "</a> ";
        if (sys != nullptr)
            out += SecSpan(sys->security);
        out += "</td><td>";
        if (sys != nullptr)
            out += Esc(m_data.RegionName(sys->regionID));
        out += Fmt("</td><td class=\"r\">%zu</td><td class=\"r\">%u</td>", b.kills.size(), b.pilots);
        out += "<td class=\"r isk\">" + IskShort(b.value) + "</td><td>" + FmtDuration(b.end - b.start) + "</td></tr>";
    }
    out += "</table>";
    Pager("/kb/battles", page, battles.size(), KbPageSize, out);
    Footer(out);
}

/* the battle report.  the sides come from who shot whom:  teams (alliances, or corporations without one) on the same
 *  kill's attacker list are friends, the victim's team is their enemy.  the biggest team starts side A and every other
 *  team goes where its friends are and its enemies aren't, repeated until nobody moves.  ?a= and ?b= (team ids) override
 */
void Killboard::PageBattle(uint32 killID, const KbRequest& req, std::string& out)
{
    KbBattle battle;
    if (!m_data.FindBattle(killID, battle)) {
        PageNotFound("That kill isn't part of a battle.", req, out);
        return;
    }

    auto teamOf = [](uint32 corp, uint32 ally) { return (ally != 0 ? ally : corp); };
    std::map<uint32, double> involvement;
    std::map<std::pair<uint32, uint32>, double> friends, enemies;
    auto key = [](uint32 a, uint32 b) { return (a < b ? std::make_pair(a, b) : std::make_pair(b, a)); };
    for (auto kill : battle.kills) {
        uint32 vt(kill->VictimTeam());
        involvement[vt] += 1.0;
        std::set<uint32> ats;
        for (auto& a : kill->attackers)
            ats.insert(teamOf(a.corpID, a.allyID));
        for (auto t : ats) {
            involvement[t] += 1.0;
            if (t != vt)
                enemies[key(t, vt)] += 1.0;
            for (auto u : ats)
                if (u < t)
                    friends[key(t, u)] += 1.0;
        }
    }
    std::vector<uint32> teams;
    for (auto& cur : involvement)
        teams.push_back(cur.first);
    std::stable_sort(teams.begin(), teams.end(), [&involvement](uint32 a, uint32 b) { return involvement[a] > involvement[b]; });

    std::set<uint32> forceA, forceB;
    for (int s = 0; s < 2; ++s) {
        std::string list(QueryParam(req, s == 0 ? "a" : "b"));
        size_t p(0);
        while (p < list.size()) {
            size_t c(list.find(',', p));
            if (c == std::string::npos)
                c = list.size();
            uint32 t((uint32)strtoul(list.substr(p, c - p).c_str(), nullptr, 10));
            if (t != 0)
                (s == 0 ? forceA : forceB).insert(t);
            p = c + 1;
        }
    }
    std::map<uint32, int> side;     // 0 A, 1 B
    if (!teams.empty())
        side[teams.front()] = 0;
    for (auto t : forceA)
        side[t] = 0;
    for (auto t : forceB)
        side[t] = 1;
    for (int pass = 0; pass < 10; ++pass) {
        bool moved(false);
        for (auto t : teams) {
            if ((forceA.count(t) > 0) or (forceB.count(t) > 0) or ((t == teams.front()) and forceB.empty() and forceA.empty()))
                continue;
            double score[2] = { 0.0, 0.0 };
            for (auto& other : side) {
                if (other.first == t)
                    continue;
                auto f(friends.find(key(t, other.first)));
                auto e(enemies.find(key(t, other.first)));
                score[other.second] += ((f != friends.end()) ? f->second : 0.0) - ((e != enemies.end()) ? e->second : 0.0);
            }
            if ((score[0] == 0.0) and (score[1] == 0.0))
                continue;   // nothing to go on yet
            int best(score[0] >= score[1] ? 0 : 1);
            auto itr(side.find(t));
            if ((itr == side.end()) or (itr->second != best)) {
                side[t] = best;
                moved = true;
            }
        }
        if (!moved)
            break;
    }
    for (auto t : teams)
        if (side.find(t) == side.end())
            side[t] = 1;

    // pilots (and npcs, by type and owner) on each side
    struct Pilot {
        uint32 charID = 0;
        uint32 corpID = 0;
        uint32 allyID = 0;
        uint32 npcType = 0;
        double damage = 0.0;
        std::set<uint32> ships;
        std::vector<const KbKill*> lost;
    };
    std::map<std::string, Pilot> pilots[2];
    double iskLost[2] = { 0.0, 0.0 };
    uint32 shipsLost[2] = { 0, 0 };
    std::vector<uint32> owners;
    for (auto kill : battle.kills) {
        int vs(side[kill->VictimTeam()]);
        iskLost[vs] += kill->TotalValue();
        ++shipsLost[vs];
        std::string vk(kill->victimChar != 0 ? Fmt("c%u", kill->victimChar) : Fmt("s%u:%u", kill->victimCorp, kill->victimShip));
        Pilot& v = pilots[vs][vk];
        v.charID = kill->victimChar;
        v.corpID = kill->victimCorp;
        v.allyID = kill->victimAlly;
        if (v.charID == 0)
            v.npcType = kill->victimShip;
        v.lost.push_back(kill);
        owners.push_back(kill->victimChar);
        owners.push_back(kill->victimCorp);
        owners.push_back(kill->victimAlly);
        for (auto& a : kill->attackers) {
            int as(side[teamOf(a.corpID, a.allyID)]);
            std::string ak(a.charID != 0 ? Fmt("c%u", a.charID) : Fmt("n%u:%u", a.corpID, a.shipTypeID));
            Pilot& p = pilots[as][ak];
            p.charID = a.charID;
            p.corpID = a.corpID;
            p.allyID = a.allyID;
            if (a.charID == 0)
                p.npcType = a.shipTypeID;
            p.damage += a.damage;
            if ((a.charID != 0) and (a.shipTypeID != 0))
                p.ships.insert(a.shipTypeID);
            owners.push_back(a.charID);
            owners.push_back(a.corpID);
            owners.push_back(a.allyID);
        }
    }
    m_data.PrefetchOwners(owners);

    const KbSystemInfo* sys(m_data.GetSystem(battle.systemID));
    Header("Battle in " + m_data.SystemName(battle.systemID), req, out);
    out += "<div class=\"box\"><h1>Battle in " + Fmt("<a href=\"/kb/system/%u\">", battle.systemID) + Esc(m_data.SystemName(battle.systemID)) + "</a> ";
    if (sys != nullptr)
        out += SecSpan(sys->security) + " <span class=\"dim\">" + Esc(m_data.RegionName(sys->regionID)) + "</span>";
    out += "</h1>" + FmtTime(battle.start) + " to " + FmtTime(battle.end, "%H:%M") + " (" + FmtDuration(battle.end - battle.start) + ")";
    out += Fmt(" &middot; %zu kills &middot; %u pilots &middot; ", battle.kills.size(), battle.pilots);
    out += "<span class=\"isk\">" + IskShort(battle.value) + " ISK</span> destroyed</div>";

    for (int s = 0; s < 2; ++s) {
        double killed(iskLost[1 - s]);
        double eff((killed + iskLost[s]) > 0.0 ? (killed * 100.0) / (killed + iskLost[s]) : 0.0);
        out += Fmt("<div class=\"%s\"><h2 class=\"%s\">Side %c</h2>", (s == 0 ? "half" : "half2"), (s == 0 ? "sa" : "sb"), (s == 0 ? 'A' : 'B'));
        out += Fmt("<div class=\"stat\"><b class=\"red\">%s</b>ISK lost</div>", IskShort(iskLost[s]).c_str());
        out += Fmt("<div class=\"stat\"><b>%u</b>ships lost</div>", shipsLost[s]);
        size_t chars(0);
        for (auto& p : pilots[s])
            if (p.second.charID != 0)
                ++chars;
        out += Fmt("<div class=\"stat\"><b>%zu</b>pilots</div>", chars);
        out += Fmt("<div class=\"stat\"><b>%.1f%%</b>efficiency</div>", eff);
        out += Fmt("<div class=\"clear\"></div><div class=\"bar\"><span style=\"width:%.0f%%\"></span></div>", eff);

        // the teams, each with a link that moves it to the other side
        out += "<table class=\"list\">";
        for (auto t : teams) {
            if (side[t] != s)
                continue;
            std::string a, b;
            for (auto& cur : side) {
                int ns(cur.first == t ? 1 - cur.second : cur.second);
                std::string& l(ns == 0 ? a : b);
                if (!l.empty())
                    l += ",";
                l += std::to_string(cur.first);
            }
            out += "<tr><td width=\"34\">" + OwnerImg(t, 32) + "</td><td>" + OwnerLink(t) + "</td>";
            out += Fmt("<td class=\"r\"><a class=\"dim\" href=\"/kb/battle/%u?a=%s&amp;b=%s\">move to %c</a></td></tr>",
                       battle.firstKillID(), a.c_str(), b.c_str(), (s == 0 ? 'B' : 'A'));
        }
        out += "</table>";

        // the pilots:  the ones who lost ships first, then by damage done
        std::vector<const Pilot*> list;
        for (auto& p : pilots[s])
            list.push_back(&p.second);
        std::stable_sort(list.begin(), list.end(), [](const Pilot* a, const Pilot* b) {
            if (a->lost.empty() != b->lost.empty())
                return !a->lost.empty();
            return a->damage > b->damage;
        });
        out += Fmt("<table class=\"list\" style=\"margin-top:8px\"><tr><th class=\"%s\" colspan=\"2\">Pilot</th><th>Ships</th><th class=\"r\">Damage</th></tr>",
                   (s == 0 ? "sa" : "sb"));
        for (auto p : list) {
            out += Fmt("<tr><td class=\"%s\" width=\"34\">", (s == 0 ? "sa" : "sb"));
            out += (p->charID != 0 ? CharImg(p->charID, 32) : TypeImg(p->npcType, 32));
            out += "</td><td>";
            out += (p->charID != 0 ? OwnerLink(p->charID) : Esc(m_data.TypeName(p->npcType)));
            out += "<br><span class=\"dim\">" + OwnerLink(p->corpID);
            if (p->allyID != 0)
                out += " / " + OwnerLink(p->allyID);
            out += "</span></td><td>";
            for (auto k : p->lost)
                out += Fmt("<a href=\"/kb/kill/%u\" title=\"lost\">", k->killID) + Img(Fmt("InventoryType/%u_32.png", k->victimShip), 32, "lost", m_data.TypeName(k->victimShip)) + "</a> ";
            for (auto ship : p->ships) {
                bool alsoLost(false);
                for (auto k : p->lost)
                    if (k->victimShip == ship)
                        alsoLost = true;
                if (!alsoLost)
                    out += Img(Fmt("InventoryType/%u_32.png", ship), 32, "used", m_data.TypeName(ship)) + " ";
            }
            out += "</td><td class=\"r\">" + Num((uint64_t)p->damage) + "</td></tr>";
        }
        out += "</table></div>";
    }

    // what happened, in order
    out += "<div class=\"clear\"></div><h2>Timeline</h2><table class=\"list\"><tr><th>Time</th><th></th><th colspan=\"2\">Lost</th>"
           "<th>Victim</th><th>Final blow</th><th class=\"r\">Value</th></tr>";
    for (auto kill : battle.kills) {
        int vs(side[kill->VictimTeam()]);
        const KbAttacker& fb = kill->attackers.front();
        out += "<tr><td>" + FmtTime(kill->time, "%H:%M") + Fmt("</td><td class=\"%s\">%c</td>", (vs == 0 ? "sa" : "sb"), (vs == 0 ? 'A' : 'B'));
        out += Fmt("<td width=\"34\"><a href=\"/kb/kill/%u\">", kill->killID) + TypeImg(kill->victimShip, 32) + "</a></td>";
        out += Fmt("<td><a href=\"/kb/kill/%u\">", kill->killID) + Esc(m_data.TypeName(kill->victimShip)) + "</a></td>";
        out += "<td>" + (kill->victimChar != 0 ? OwnerLink(kill->victimChar) : OwnerLink(kill->victimCorp)) + "</td><td>";
        out += (fb.charID != 0 ? OwnerLink(fb.charID) : Esc(m_data.TypeName(fb.shipTypeID)));
        out += Fmt(" <span class=\"dim\">(%zu)</span></td>", kill->attackers.size());
        out += "<td class=\"r isk\">" + IskShort(kill->TotalValue()) + "</td></tr>";
    }
    out += "</table>";
    Footer(out);
}

void Killboard::PageSearch(const KbRequest& req, std::string& out)
{
    std::string q(QueryParam(req, "q"));
    // trim
    q.erase(0, q.find_first_not_of(" \t"));
    q.erase(q.find_last_not_of(" \t") + 1);
    std::vector<KbSearchHit> hits;
    if (q.size() >= 2)
        m_data.Search(q, hits);
    // one exact match goes straight there
    std::vector<const KbSearchHit*> exact;
    for (auto& h : hits)
        if (strcasecmp(h.name.c_str(), q.c_str()) == 0)
            exact.push_back(&h);

    Header("Search", req, out);
    out += "<h1>Search: " + Esc(q) + "</h1>";
    if (q.size() < 2) {
        out += "<p class=\"dim\">Type at least two letters.</p>";
    } else if (hits.empty()) {
        out += "<p>Nothing found.</p>";
    } else {
        if (exact.size() == 1)
            out += "<script>location.replace('/kb/" + exact.front()->kind + Fmt("/%u');</script>", exact.front()->id);
        static const std::pair<const char*, const char*> kinds[] = {
            {"pilot", "Pilots"}, {"corp", "Corporations"}, {"alliance", "Alliances"},
            {"system", "Systems"}, {"region", "Regions"}, {"ship", "Ships"} };
        for (auto& k : kinds) {
            std::string rows;
            for (auto& h : hits) {
                if (h.kind != k.first)
                    continue;
                rows += "<tr><td width=\"34\">";
                if (h.kind == "pilot")
                    rows += CharImg(h.id, 32);
                else if (h.kind == "corp")
                    rows += CorpImg(h.id, 32);
                else if (h.kind == "alliance")
                    rows += AllyImg(h.id, 32);
                else if (h.kind == "ship")
                    rows += TypeImg(h.id, 32);
                rows += "</td><td><a href=\"/kb/" + h.kind + Fmt("/%u\">", h.id) + Esc(h.name) + "</a></td></tr>";
            }
            if (!rows.empty())
                out += "<h2>" + std::string(k.second) + "</h2><table class=\"list\">" + rows + "</table>";
        }
    }
    Footer(out);
}

void Killboard::PageNotFound(const std::string& what, const KbRequest& req, std::string& out)
{
    m_notFound = true;
    Header("Not found", req, out);
    out += "<h1>Not found</h1><p>" + Esc(what) + "</p><p><a href=\"/kb/\">Back to the killboard</a></p>";
    Footer(out);
}

/* called on the game's thread when a kill is saved:  everyone involved gets a mail with a link to the report.  the link
 *  names the image server as the recipient's client reaches it, when they're online
 */
void Killboard::MailKillReport(uint32 killID, const KillData& data)
{
    if (!sConfig.killboard.Enabled or !sConfig.killboard.MailKillReports or (killID == 0))
        return;
    std::vector<KbAttacker> attackers;
    KillboardData::ParseAttackers(data.killBlob, attackers);
    std::set<uint32> chars;
    if (IsCharacterID(data.victimCharacterID))
        chars.insert(data.victimCharacterID);
    if (IsCharacterID(data.finalCharacterID))
        chars.insert(data.finalCharacterID);
    for (auto& a : attackers)
        if (IsCharacterID(a.charID))
            chars.insert(a.charID);
    if (chars.empty())
        return;

    std::string ship(sDataMgr.GetTypeName(data.victimShipTypeID));
    SystemData sys;
    std::string system(sDataMgr.GetSystemData(data.solarSystemID, sys) ? sys.name : std::string("space"));
    std::string victim(sDataMgr.GetOwnerName(data.victimCharacterID != 0 ? data.victimCharacterID : data.victimCorporationID));
    for (auto charID : chars) {
        Client* pClient(sEntityList.FindClientByCharID(charID));
        std::string base((pClient != nullptr) ? pClient->GetImageServerURL() : sImageServer.url());
        std::string url(base + Fmt("kb/kill/%u", killID));
        std::string title, line;
        if (charID == data.victimCharacterID) {
            title = "Kill report: " + ship + " lost in " + system;
            line = "Your " + ship + " was destroyed in " + system + ".";
        } else {
            title = "Kill report: " + victim + "'s " + ship + " in " + system;
            line = "You took part in the kill of " + victim + "'s " + ship + " in " + system + ".";
        }
        std::string body("<font size=\"12\">" + Esc(line) + "<br><br><a href=\"" + url + "\">Open the kill report</a>"
                         "<br><br>All kills, pilots and battle reports:  <a href=\"" + base + "kb/\">" + Esc(sConfig.killboard.Title) + "</a></font>");
        // from CONCORD, who keep the records
        MailMgr::SendMail(1000125, charID, title, body);
    }
}
