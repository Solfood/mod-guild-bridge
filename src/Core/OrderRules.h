/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 * Pure. The shape check for every order the app can send (contract §4). World-state checks live in the executors.
 */

#ifndef MOD_GUILD_BRIDGE_CORE_ORDERRULES_H
#define MOD_GUILD_BRIDGE_CORE_ORDERRULES_H

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <string>

namespace GuildBridge
{
enum class OrderType
{
    Focus, Invite, Remove, Rank, PresetProfessions, RunDungeon, Snapshot, SnapshotAll, Restore, CreateGuild,
    CreateFounders, Unknown
};
// Same numbers as the fork's NewRpgInfo::focus.
enum class Focus : uint8_t { None = 0, Questing = 1, Grinding = 2, Pvp = 3, Gathering = 4, Resting = 5 };
enum class Approach { Auto, Travel, Teleport };

inline char const* FocusName(Focus focus)
{
    static char const* const names[] = {"none", "questing", "grinding", "pvp", "gathering", "resting"};
    return names[static_cast<int>(focus)];
}

inline bool FocusFromName(std::string const& name, Focus& out)
{
    for (int i = 0; i <= 5; ++i)
        if (name == FocusName(static_cast<Focus>(i)))
        {
            out = static_cast<Focus>(i);
            return true;
        }
    return false;
}

inline bool IsPrimaryProfession(uint32_t skill)
{
    static constexpr std::array<uint32_t, 11> primary = {171, 164, 333, 202, 182, 773, 755, 165, 186, 393, 197};
    return std::find(primary.begin(), primary.end(), skill) != primary.end();
}

// Every field as text, exactly as MySQL's ->> returned it ("" when absent or null).
struct OrderRow
{
    uint64_t id = 0;
    std::string type, bot, focus, guild, rank, first, second, dungeon;
    std::array<std::string, 5> party;
    std::string partyLength, heroic, approach, snapshotId, takenBefore, dryRun, testFail;
};

struct ParsedOrder
{
    uint64_t id = 0;
    OrderType type = OrderType::Unknown;
    uint32_t bot = 0;
    Focus focus = Focus::None;
    std::string guild;  // "user" or "test" ("" when the order has no guild)
    int rank = -1;
    uint16_t first = 0, second = 0;
    std::string dungeon;
    std::array<uint32_t, 5> party{};
    bool heroic = false;
    Approach approach = Approach::Auto;
    uint64_t snapshotId = 0;
    uint32_t takenBefore = 0;
    bool dryRun = false;
    std::string testFail;  // test seams only: "load" (restore), "dc" (run_dungeon)
};

inline bool ParseUInt(std::string const& text, uint64_t max, uint64_t& out)
{
    if (text.empty() || text.size() > 20 || !std::all_of(text.begin(), text.end(), ::isdigit))
        return false;
    uint64_t value = 0;
    for (char c : text)
    {
        if (value > (max - static_cast<uint64_t>(c - '0')) / 10)
            return false;
        value = value * 10 + static_cast<uint64_t>(c - '0');
    }
    out = value;
    return true;
}

// A JSON boolean as MySQL's ->> returns it: "true"/"1" or "false"/"0"; absent ("") is false. Anything else
// ("yes", "True", ...) is refused, so a mistyped dry_run fails the order instead of running it for real.
inline bool ParseBool(std::string const& text, bool& out)
{
    if (text.empty() || text == "false" || text == "0")
        out = false;
    else if (text == "true" || text == "1")
        out = true;
    else
        return false;
    return true;
}

inline OrderType OrderTypeFromName(std::string const& name)
{
    if (name == "focus") return OrderType::Focus;
    if (name == "invite") return OrderType::Invite;
    if (name == "remove") return OrderType::Remove;
    if (name == "rank") return OrderType::Rank;
    if (name == "preset_professions") return OrderType::PresetProfessions;
    if (name == "run_dungeon") return OrderType::RunDungeon;
    if (name == "snapshot") return OrderType::Snapshot;
    if (name == "snapshot_all") return OrderType::SnapshotAll;
    if (name == "restore") return OrderType::Restore;
    return OrderType::Unknown;  // Task 15 adds create_guild and create_founders here
}

// "" when the order is well-formed; else the failure reason (contract §4 prefixes).
inline std::string ParseOrder(OrderRow const& row, ParsedOrder& out)
{
    out = ParsedOrder();
    out.id = row.id;
    out.type = OrderTypeFromName(row.type);
    out.testFail = row.testFail;
    if (out.type == OrderType::Unknown)
        return "unknown order type '" + row.type + "'";
    if (!ParseBool(row.dryRun, out.dryRun))
        return "bad value for dry_run";

    uint64_t value = 0;
    auto needBot = [&]() -> std::string {
        if (row.bot.empty())
            return "missing field bot";
        if (!ParseUInt(row.bot, UINT32_MAX, value) || !value)
            return "bad value for bot";
        out.bot = static_cast<uint32_t>(value);
        return "";
    };
    auto guildOf = [&](bool required) -> std::string {
        if (row.guild.empty())
        {
            out.guild = required ? "" : "user";
            return required ? "missing field guild" : "";
        }
        if (row.guild != "user" && row.guild != "test")
            return "bad value for guild";
        out.guild = row.guild;
        return "";
    };
    auto rankOf = [&](bool required) -> std::string {
        if (row.rank.empty())
            return required ? "missing field rank" : "";
        if (!ParseUInt(row.rank, 9, value) || value < 1)
            return "bad value for rank";
        out.rank = static_cast<int>(value);
        return "";
    };

    std::string error;
    switch (out.type)
    {
        case OrderType::Focus:
            if (!(error = needBot()).empty())
                return error;
            if (row.focus.empty())
                return "missing field focus";
            return FocusFromName(row.focus, out.focus) ? "" : "bad value for focus";
        case OrderType::Invite:
            if (!(error = needBot()).empty() || !(error = guildOf(false)).empty())
                return error;
            return rankOf(false);
        case OrderType::Remove:
            return needBot();
        case OrderType::Rank:
            if (!(error = needBot()).empty())
                return error;
            return rankOf(true);
        case OrderType::PresetProfessions:
            if (!(error = needBot()).empty())
                return error;
            if (row.first.empty())
                return "missing field first";
            if (!ParseUInt(row.first, 65535, value) || !IsPrimaryProfession(static_cast<uint32_t>(value)))
                return "bad value for first";
            out.first = static_cast<uint16_t>(value);
            if (!row.second.empty() && row.second != "0")
            {
                if (!ParseUInt(row.second, 65535, value) || !IsPrimaryProfession(static_cast<uint32_t>(value)) ||
                    value == out.first)
                    return "bad value for second";
                out.second = static_cast<uint16_t>(value);
            }
            return "";
        case OrderType::RunDungeon:
        {
            if (row.dungeon.empty())
                return "missing field dungeon";
            if (row.dungeon.size() > 24 ||
                !std::all_of(row.dungeon.begin(), row.dungeon.end(), [](char c) { return ::isalnum(c) || c == '-'; }))
                return "bad value for dungeon";
            out.dungeon = row.dungeon;
            if (row.partyLength != "5")
                return "party must be 5 different bots";
            for (std::size_t i = 0; i < 5; ++i)
            {
                if (!ParseUInt(row.party[i], UINT32_MAX, value) || !value)
                    return "bad value for party";
                out.party[i] = static_cast<uint32_t>(value);
                for (std::size_t j = 0; j < i; ++j)
                    if (out.party[j] == out.party[i])
                        return "party must be 5 different bots";
            }
            if (!ParseBool(row.heroic, out.heroic))
                return "bad value for heroic";
            if (row.approach.empty() || row.approach == "auto")
                out.approach = Approach::Auto;
            else if (row.approach == "travel")
                out.approach = Approach::Travel;
            else if (row.approach == "teleport")
                out.approach = Approach::Teleport;
            else
                return "bad value for approach";
            return "";
        }
        case OrderType::Snapshot:
            if (!row.bot.empty() && !row.guild.empty())
                return "give bot or guild, not both";
            if (!row.guild.empty())
                return guildOf(true);
            return needBot();
        case OrderType::SnapshotAll:
            return "";  // no fields
        case OrderType::CreateGuild:
        case OrderType::CreateFounders:
            return "";  // Task 15 replaces these two lines with the real checks
        case OrderType::Restore:
            if (!row.bot.empty() && !row.guild.empty())
                return "give bot or guild, not both";
            if (!row.guild.empty())
            {
                if (!(error = guildOf(true)).empty())
                    return error;
                if (row.takenBefore.empty())
                    return "missing field taken_before";
                if (!ParseUInt(row.takenBefore, UINT32_MAX, value))
                    return "bad value for taken_before";
                out.takenBefore = static_cast<uint32_t>(value);
                return "";
            }
            if (!(error = needBot()).empty())
                return error;
            if (row.snapshotId.empty())
                return "missing field snapshot_id";
            if (!ParseUInt(row.snapshotId, UINT64_MAX, value) || !value)
                return "bad value for snapshot_id";
            out.snapshotId = value;
            return "";
        case OrderType::Unknown:
            break;
    }
    return "unknown order type '" + row.type + "'";
}
}  // namespace GuildBridge

#endif
