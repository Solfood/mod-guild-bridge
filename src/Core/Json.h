// src/Core/Json.h
/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 * Pure (standard library only): unit-tested on the Mac by tests/unit.
 */

#ifndef MOD_GUILD_BRIDGE_CORE_JSON_H
#define MOD_GUILD_BRIDGE_CORE_JSON_H

#include <cstdint>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace GuildBridge
{
// Builds one JSON object as text, keys in the order they are added.
class JsonObject
{
public:
    JsonObject& Str(std::string_view key, std::string_view value)
    {
        Key(key);
        AppendEscaped(_out, value);
        return *this;
    }
    JsonObject& Int(std::string_view key, int64_t value)
    {
        Key(key);
        _out += std::to_string(value);
        return *this;
    }
    JsonObject& UInt(std::string_view key, uint64_t value)
    {
        Key(key);
        _out += std::to_string(value);
        return *this;
    }
    // One decimal: positions and distances need no more, and fixed text keeps tests exact.
    JsonObject& Num(std::string_view key, double value)
    {
        Key(key);
        // JSON has no nan/inf: a bad value becomes null so the event is still stored.
        if (!std::isfinite(value))
        {
            _out += "null";
            return *this;
        }
        // to_chars ignores the process locale (snprintf would print "1,3" under a comma locale).
        char buf[400];
        auto res = std::to_chars(buf, buf + sizeof(buf), value, std::chars_format::fixed, 1);
        _out.append(buf, res.ptr);
        return *this;
    }
    JsonObject& Bool(std::string_view key, bool value)
    {
        Key(key);
        _out += value ? "true" : "false";
        return *this;
    }
    JsonObject& UIntArray(std::string_view key, std::vector<uint64_t> const& values)
    {
        Key(key);
        _out += '[';
        for (std::size_t i = 0; i < values.size(); ++i)
        {
            if (i)
                _out += ',';
            _out += std::to_string(values[i]);
        }
        _out += ']';
        return *this;
    }
    // `json` must already be valid JSON (an object or array built elsewhere).
    JsonObject& Raw(std::string_view key, std::string_view json)
    {
        Key(key);
        _out += json;
        return *this;
    }
    std::string Build() const { return "{" + _out + "}"; }

    // Valid UTF-8 passes through unchanged. Each invalid byte becomes U+FFFD, because MySQL's JSON column rejects
    // invalid UTF-8 and the whole event would be lost.
    static void AppendEscaped(std::string& out, std::string_view value)
    {
        out += '"';
        for (std::size_t i = 0; i < value.size();)
        {
            unsigned char c = value[i];
            if (c >= 0x80)
            {
                std::size_t len = Utf8Length(value.substr(i));
                if (len == 0)
                {
                    out += "\xEF\xBF\xBD";
                    ++i;
                }
                else
                {
                    out.append(value.substr(i, len));
                    i += len;
                }
                continue;
            }
            ++i;
            switch (c)
            {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (c < 0x20)
                    {
                        char buf[8];
                        std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                        out += buf;
                    }
                    else
                        out += static_cast<char>(c);
            }
        }
        out += '"';
    }

private:
    // Length of the valid multi-byte UTF-8 sequence at the start of `s` (first byte >= 0x80), or 0 if invalid.
    // Rejects overlong forms, surrogates and code points above U+10FFFF.
    static std::size_t Utf8Length(std::string_view s)
    {
        unsigned char c = s[0];
        std::size_t len;
        uint32_t cp;
        if (c >= 0xC2 && c <= 0xDF) { len = 2; cp = c & 0x1F; }
        else if (c >= 0xE0 && c <= 0xEF) { len = 3; cp = c & 0x0F; }
        else if (c >= 0xF0 && c <= 0xF4) { len = 4; cp = c & 0x07; }
        else return 0;
        if (s.size() < len)
            return 0;
        for (std::size_t k = 1; k < len; ++k)
        {
            unsigned char cc = s[k];
            if ((cc & 0xC0) != 0x80)
                return 0;
            cp = (cp << 6) | (cc & 0x3F);
        }
        if ((len == 3 && cp < 0x800) || (len == 4 && cp < 0x10000) || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
            return 0;
        return len;
    }

    void Key(std::string_view key)
    {
        if (!_out.empty())
            _out += ',';
        AppendEscaped(_out, key);
        _out += ':';
    }

    std::string _out;
};
}  // namespace GuildBridge

#endif
