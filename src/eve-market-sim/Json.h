
 /**
  * @name Json.h
  *   a small json reader for the harness' static data snapshot (utils/market-calibration --export-json).  enough json
  *   for that file:  objects, arrays, numbers, strings, true/false/null
  *
  * @date:          13Oct26
  *
  */

#ifndef _EVE_MARKET_SIM_JSON_H__
#define _EVE_MARKET_SIM_JSON_H__

#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace MktHarness {

    struct Json {
        enum Kind { Null, Bool, Number, String, Array, Object };
        Kind kind = Null;
        bool b = false;
        double n = 0.0;
        std::string s;
        std::vector<Json> a;
        std::map<std::string, Json> o;

        bool IsNull() const                             { return kind == Null; }
        double Num(double dflt = 0.0) const             { return (kind == Number) ? n : ((kind == Bool) ? (b ? 1.0 : 0.0) : dflt); }
        const Json& operator[](const std::string& key) const {
            static const Json none;
            std::map<std::string, Json>::const_iterator itr = o.find(key);
            return (itr == o.end()) ? none : itr->second;
        }
        const Json& operator[](size_t i) const {
            static const Json none;
            return (i < a.size()) ? a[i] : none;
        }

        // false (and a message in err) when text isn't json
        static bool Parse(const std::string& text, Json& out, std::string& err);
    };

    namespace JsonDetail {
        struct Reader {
            const std::string& t;
            size_t i;
            std::string err;
            explicit Reader(const std::string& text) : t(text), i(0) { }
            void Ws() { while ((i < t.size()) and ((t[i] == ' ') or (t[i] == '\n') or (t[i] == '\r') or (t[i] == '\t'))) ++i; }
            bool Fail(const char* what) { if (err.empty()) err = std::string(what) + " at " + std::to_string(i); return false; }
            bool Str(std::string& out) {
                if ((i >= t.size()) or (t[i] != '"'))
                    return Fail("expected a string");
                ++i;
                while (i < t.size()) {
                    char c(t[i++]);
                    if (c == '"')
                        return true;
                    if (c != '\\') {
                        out += c;
                        continue;
                    }
                    if (i >= t.size())
                        break;
                    char e(t[i++]);
                    switch (e) {
                        case 'n': out += '\n'; break;
                        case 't': out += '\t'; break;
                        case 'r': out += '\r'; break;
                        case 'b': out += '\b'; break;
                        case 'f': out += '\f'; break;
                        case 'u': {
                            if (i + 4 > t.size())
                                return Fail("bad escape");
                            unsigned long cp(std::strtoul(t.substr(i, 4).c_str(), nullptr, 16));
                            i += 4;
                            // names only:  keep ascii, mark the rest
                            out += (cp < 128) ? (char)cp : '?';
                        } break;
                        default: out += e; break;
                    }
                }
                return Fail("unterminated string");
            }
            bool Value(Json& v) {
                Ws();
                if (i >= t.size())
                    return Fail("unexpected end");
                char c(t[i]);
                if (c == '{') {
                    v.kind = Json::Object;
                    ++i;
                    Ws();
                    if ((i < t.size()) and (t[i] == '}')) { ++i; return true; }
                    while (true) {
                        Ws();
                        std::string key;
                        if (!Str(key))
                            return false;
                        Ws();
                        if ((i >= t.size()) or (t[i] != ':'))
                            return Fail("expected ':'");
                        ++i;
                        if (!Value(v.o[key]))
                            return false;
                        Ws();
                        if ((i < t.size()) and (t[i] == ',')) { ++i; continue; }
                        if ((i < t.size()) and (t[i] == '}')) { ++i; return true; }
                        return Fail("expected ',' or '}'");
                    }
                }
                if (c == '[') {
                    v.kind = Json::Array;
                    ++i;
                    Ws();
                    if ((i < t.size()) and (t[i] == ']')) { ++i; return true; }
                    while (true) {
                        v.a.push_back(Json());
                        if (!Value(v.a.back()))
                            return false;
                        Ws();
                        if ((i < t.size()) and (t[i] == ',')) { ++i; continue; }
                        if ((i < t.size()) and (t[i] == ']')) { ++i; return true; }
                        return Fail("expected ',' or ']'");
                    }
                }
                if (c == '"') {
                    v.kind = Json::String;
                    return Str(v.s);
                }
                if (t.compare(i, 4, "true") == 0) { v.kind = Json::Bool; v.b = true; i += 4; return true; }
                if (t.compare(i, 5, "false") == 0) { v.kind = Json::Bool; v.b = false; i += 5; return true; }
                if (t.compare(i, 4, "null") == 0) { v.kind = Json::Null; i += 4; return true; }
                char* end(nullptr);
                v.n = std::strtod(t.c_str() + i, &end);
                if (end == t.c_str() + i)
                    return Fail("unexpected character");
                v.kind = Json::Number;
                i = (size_t)(end - t.c_str());
                return true;
            }
        };
    }

    inline bool Json::Parse(const std::string& text, Json& out, std::string& err)
    {
        JsonDetail::Reader r(text);
        if (!r.Value(out)) {
            err = r.err;
            return false;
        }
        return true;
    }

}

#endif  // _EVE_MARKET_SIM_JSON_H__
