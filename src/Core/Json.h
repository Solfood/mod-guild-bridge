// src/Core/Json.h
/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 * Pure (standard library only): unit-tested on the Mac by tests/unit.
 */

#ifndef MOD_GUILD_BRIDGE_CORE_JSON_H
#define MOD_GUILD_BRIDGE_CORE_JSON_H

#include <cstdint>
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
        char buf[48];
        std::snprintf(buf, sizeof(buf), "%.1f", value);
        _out += buf;
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

    static void AppendEscaped(std::string& out, std::string_view value)
    {
        out += '"';
        for (unsigned char c : value)
        {
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
