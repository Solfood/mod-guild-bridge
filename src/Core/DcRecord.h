/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 * Pure. Finds one run's line in mod-dungeon-clear's dc_testruns.jsonl and reads the few top-level fields we keep.
 * Top-level fields come before the nested arrays in each line, so the FIRST "key": in the line is the top-level one.
 * dungeon-clear ends every line with '\n' (one flush per line), so a line without one is still being written.
 */

#ifndef MOD_GUILD_BRIDGE_CORE_DCRECORD_H
#define MOD_GUILD_BRIDGE_CORE_DCRECORD_H

#include <cstdint>
#include <string>
#include <string_view>

namespace GuildBridge
{
struct DcOutcome
{
    bool found = false;    // the run's complete line is there
    bool partial = false;  // the run's line is there but not finished yet (no '\n'): read again, never final
    std::string result;
    std::string failReason;
    uint32_t durationS = 0;
    uint32_t bossesKilled = 0;
    uint32_t bossesTotal = 0;
};

inline std::string JsonStringField(std::string_view line, std::string_view key)
{
    std::string const needle = "\"" + std::string(key) + "\":\"";
    std::size_t pos = line.find(needle);
    if (pos == std::string_view::npos)
        return "";
    std::string out;
    for (pos += needle.size(); pos < line.size() && line[pos] != '"'; ++pos)
    {
        if (line[pos] == '\\' && pos + 1 < line.size())
        {
            char const next = line[++pos];
            out += next == 'n' ? '\n' : next == 't' ? '\t' : next;  // \" \\ \/ and the two common ones
            continue;
        }
        out += line[pos];
    }
    return out;
}

inline uint32_t JsonUIntField(std::string_view line, std::string_view key)
{
    std::string const needle = "\"" + std::string(key) + "\":";
    std::size_t pos = line.find(needle);
    if (pos == std::string_view::npos)
        return 0;
    uint32_t value = 0;
    for (pos += needle.size(); pos < line.size() && line[pos] >= '0' && line[pos] <= '9'; ++pos)
        value = value * 10 + static_cast<uint32_t>(line[pos] - '0');
    return value;
}

inline DcOutcome FindDcRun(std::string_view text, std::string_view runId)
{
    DcOutcome outcome;
    if (runId.empty())
        return outcome;
    std::string const needle = "\"runId\":\"" + std::string(runId) + "\"";  // closing quote: r-1 never matches r-10
    std::size_t const hit = text.rfind(needle);
    if (hit == std::string_view::npos)
        return outcome;
    std::size_t const begin = text.rfind('\n', hit);
    std::size_t const start = begin == std::string_view::npos ? 0 : begin + 1;
    std::size_t const end = text.find('\n', hit);
    if (end == std::string_view::npos)
    {
        outcome.partial = true;
        return outcome;
    }
    std::string_view const line = text.substr(start, end - start);
    outcome.found = true;
    outcome.result = JsonStringField(line, "result");
    outcome.failReason = JsonStringField(line, "failReason");
    outcome.durationS = JsonUIntField(line, "durationS");
    outcome.bossesKilled = JsonUIntField(line, "bossesKilled");
    outcome.bossesTotal = JsonUIntField(line, "bossesTotal");
    return outcome;
}

inline char const* RunResultFromDc(std::string const& dcResult)
{
    if (dcResult == "success")
        return "cleared";
    if (dcResult == "wipe")
        return "wiped";
    return "abandoned";
}
}  // namespace GuildBridge

#endif
