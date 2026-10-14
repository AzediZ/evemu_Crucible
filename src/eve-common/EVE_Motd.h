 /**
  * @name EVE_Motd.h
  *     the login message of the day (etc/motd.txt) and the GM-only server info sent after it
  *
  * these are pure functions, so the unit tests (eve-test) can check them
  *
  *  motd.txt is plain text with the chat markup the Crucible client renders in a chat window (the same markup a channel
  *  MOTD takes):  <color=0xAARRGGBB>, <b>, <i>, <u>, <br>, <url=...>, <fontsize=N>.  each line break in the file becomes
  *  a <br>.  a line whose first non-blank character is '#' is a comment.  blank lines at the start and the end are dropped.
  *  an empty file (or one with only comments) means no MOTD.
  */


#ifndef EVE_MOTD_H
#define EVE_MOTD_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "EVE_Roles.h"


namespace Motd {

    // bytes.  a longer file is cut here (the chat window takes long system messages, but this is one message per login)
    static constexpr size_t MaxBytes = 16384;

    struct ServerInfo {
        std::string revision;
        std::string buildDate;
        std::string uptime;
        uint32_t population = 0;
        bool autoStop = false;
        bool ramEvent = true;
        bool showAll = false;
    };

    // the chat text for the contents of motd.txt.  "" = no MOTD
    inline std::string Parse(const std::string& text)
    {
        std::string body(text);
        // utf-8 byte order mark
        if ((body.size() >= 3) and (body.compare(0, 3, "\xEF\xBB\xBF") == 0))
            body.erase(0, 3);

        std::vector<std::string> lines;
        size_t start(0);
        while (start <= body.size()) {
            size_t end(body.find('\n', start));
            if (end == std::string::npos)
                end = body.size();
            std::string line(body.substr(start, end - start));
            if (!line.empty() and (line.back() == '\r'))
                line.pop_back();
            size_t first(line.find_first_not_of(" \t"));
            if ((first == std::string::npos) or (line[first] != '#'))
                lines.push_back(line);
            start = end + 1;
        }

        // blank lines at either end
        auto blank = [](const std::string& s) { return (s.find_first_not_of(" \t") == std::string::npos); };
        while (!lines.empty() and blank(lines.front()))
            lines.erase(lines.begin());
        while (!lines.empty() and blank(lines.back()))
            lines.pop_back();

        std::string motd;
        for (size_t i = 0; i < lines.size(); ++i) {
            if (i > 0)
                motd += "<br>";
            motd += lines[i];
        }

        if (motd.size() > MaxBytes) {
            motd.resize(MaxBytes);
            // don't leave half a utf-8 character
            size_t lead(motd.size());
            while ((lead > 0) and ((static_cast<unsigned char>(motd[lead - 1]) & 0xC0) == 0x80))
                --lead;
            if (lead > 0) {
                unsigned char c(static_cast<unsigned char>(motd[lead - 1]));
                size_t need((c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : (c >= 0xC0) ? 2 : 1);
                if ((motd.size() - (lead - 1)) < need)
                    motd.resize(lead - 1);
            }
        }
        return motd;
    }

    // the server info only GMs get after the MOTD (world.loginInfo)
    inline std::string BuildServerInfo(const ServerInfo& info)
    {
        std::string msg("<b>Server Info</b> (GM only)");
        msg += "<br>Server Version: ";
        msg += info.revision;
        msg += "<br>Revision Date: ";
        msg += info.buildDate;
        msg += "<br>Uptime: ";
        msg += info.uptime;
        msg += "<br>Current Population: ";
        msg += std::to_string(info.population);
        msg += "<br><br>Character Options:";
        msg += "<br>Module AutoStop: ";
        msg += (info.autoStop ? "On" : "Off");
        msg += "<br>RAM Event: ";
        msg += (info.ramEvent ? "On" : "Off");
        msg += "<br>ShowAll: ";
        msg += (info.showAll ? "On" : "Off");
        return msg;
    }

    // the GMs that see the server info:  the roles the server treats as GMs elsewhere (petitions, login queue)
    inline bool IsGMRole(int64_t role)
    {
        return ((role & (Acct::Role::GML | Acct::Role::GMH | Acct::Role::ADMIN)) != 0);
    }

    // true when the text is plain ascii (else it goes to the client as a unicode string)
    inline bool IsAscii(const std::string& text)
    {
        for (unsigned char c : text)
            if (c >= 0x80)
                return false;
        return true;
    }
}

#endif  // EVE_MOTD_H
