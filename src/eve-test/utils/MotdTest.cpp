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

#include "eve-test.h"

#include <cstdio>
#include <string>

#include "EVE_Motd.h"

namespace {
    int failures = 0;

    void Check(bool ok, const char* what)
    {
        ::printf("%s %s\n", (ok ? "  ok  " : "  FAIL"), what);
        if (!ok)
            ++failures;
    }
}

int utils_MotdTest( int /*argc*/, char* /*argv*/[] )
{
    using Motd::Parse;

    ::printf("motd.txt:\n");
    Check(Parse("") == "", "an empty file is no MOTD");
    Check(Parse("# only\n  # comments\n\n") == "", "nor is one with only comments and blank lines");
    Check(Parse("Hello") == "Hello", "one line is sent as it is");
    Check(Parse("one\ntwo\n") == "one<br>two", "line breaks become <br>, the last one is dropped");
    Check(Parse("one\r\ntwo\r\n") == "one<br>two", "windows line ends too");
    Check(Parse("\n\n  \none\n\ntwo\n\n\n") == "one<br><br>two", "blank lines at the ends go, those inside stay");
    Check(Parse("# header\none\n   # indented comment\ntwo") == "one<br>two", "comment lines are left out");
    Check(Parse("a # not a comment") == "a # not a comment", "a '#' later in a line is text");
    Check(Parse("\xEF\xBB\xBFHi") == "Hi", "a utf-8 byte order mark is dropped");
    Check(Parse("<br><color=0xff00ff00><b>Hi</b></color>") == "<br><color=0xff00ff00><b>Hi</b></color>", "markup is passed through");
    Check(Parse("caf\xC3\xA9") == "caf\xC3\xA9", "utf-8 text is kept");

    std::string big(Motd::MaxBytes + 100, 'x');
    Check(Parse(big).size() == Motd::MaxBytes, "a long file is cut at MaxBytes");
    std::string split(Motd::MaxBytes - 1, 'x');
    split += "\xC3\xA9";    // the 2-byte character straddles the limit
    Check(Parse(split) == std::string(Motd::MaxBytes - 1, 'x'), "... without leaving half a utf-8 character");
    std::string whole(Motd::MaxBytes - 2, 'x');
    whole += "\xC3\xA9";    // ends exactly at the limit
    Check(Parse(whole) == whole, "... and keeps one that ends at the limit");

    ::printf("ascii:\n");
    Check(Motd::IsAscii("<b>Hello</b>"), "plain text is ascii");
    Check(!Motd::IsAscii("caf\xC3\xA9"), "utf-8 is not (sent as unicode)");

    ::printf("server info:\n");
    Motd::ServerInfo info;
        info.revision = "abc123";
        info.buildDate = "2026-10-14";
        info.uptime = "1h2m3s";
        info.population = 7;
        info.autoStop = true;
    std::string text(Motd::BuildServerInfo(info));
    Check(text.find("Server Version: abc123") != std::string::npos, "has the server version");
    Check(text.find("Revision Date: 2026-10-14") != std::string::npos, "has the revision date");
    Check(text.find("Uptime: 1h2m3s") != std::string::npos, "has the uptime");
    Check(text.find("Current Population: 7") != std::string::npos, "has the population");
    Check(text.find("Module AutoStop: On") != std::string::npos, "has the character's options");
    Check(text.find("ShowAll: Off") != std::string::npos, "... each as it is");

    ::printf("gm roles:\n");
    Check(!Motd::IsGMRole(Acct::Role::PLAYER), "a player is not a GM");
    Check(!Motd::IsGMRole(Acct::Role::PLAYER | Acct::Role::NEWBIE | Acct::Role::VIP), "nor a VIP newbie");
    Check(Motd::IsGMRole(Acct::Role::PLAYER | Acct::Role::GML), "GML is");
    Check(Motd::IsGMRole(Acct::Role::GMH), "GMH is");
    Check(Motd::IsGMRole(Acct::Role::ADMIN), "ADMIN is");

    ::printf("%s\n", (failures ? "MotdTest FAILED" : "MotdTest passed"));
    return failures;
}
