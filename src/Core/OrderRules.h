/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 * Pure. The shape check for every order the app can send (contract §4). World-state checks live in the executors.
 */

#ifndef MOD_GUILD_BRIDGE_CORE_ORDERRULES_H
#define MOD_GUILD_BRIDGE_CORE_ORDERRULES_H

#include "NewGameRules.h"
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

// Every field as text, exactly as MySQL's ->> returned it ("" when absent or null).
struct OrderRow
{
    uint64_t id = 0;
    std::string type, bot, focus, guild, rank, first, second, dungeon;
    std::array<std::string, 5> party;
    std::string partyLength, heroic, approach, snapshotId, takenBefore, dryRun, testFail;
    std::string faction, guildName, leaderName, role, leaderRace, guildId, foundersLength;  // New Game orders
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
    // Test seams only: "load", "crash", "purge_timeout", "delete_fail" (restore), "entrance", "dcfail" (run_dungeon),
    // "cut" (create_founders).
    std::string testFail;
    // create_guild / create_founders (spec §2b)
    std::string faction, guildName, leaderName, role;
    uint8_t leaderRace = 0;  // 0 = the faction's default (Human / Orc)
    uint32_t guildId = 0;
    uint32_t founderCount = 0;
};

inline bool ParseUInt(std::string const& text, uint64_t max, uint64_t& out)
{
    if (text.empty() || text.size() > 20 || !std::all_of(text.begin(), text.end(), ::isdigit))
        return false;
    uint64_t value = 0;
    for (char c : text)
    {
        uint64_t const digit = static_cast<uint64_t>(c - '0');
        // digit > max first: (max - digit) would wrap and let a one-digit value over max through ("6" for 5).
        if (digit > max || value > (max - digit) / 10)
            return false;
        value = value * 10 + digit;
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
    if (name == "create_guild") return OrderType::CreateGuild;
    if (name == "create_founders") return OrderType::CreateFounders;
    return OrderType::Unknown;
}

// Final review I2: restores, snapshots and the order runner read back what they just wrote and trust the answer,
// which holds only with one async writer per pool. "" when CharacterDatabase.WorkerThreads and
// GuildmasterDatabase.WorkerThreads are both 1; else what is wrong (the bridge then stays off).
inline std::string WriterThreadsProblem(uint32_t characters, uint32_t guildmaster)
{
    std::string out;
    if (characters != 1)
        out = "CharacterDatabase.WorkerThreads is " + std::to_string(characters) + " (must be 1)";
    if (guildmaster != 1)
        out += (out.empty() ? "" : ", ") + std::string("GuildmasterDatabase.WorkerThreads is ") +
               std::to_string(guildmaster) + " (must be 1)";
    return out;
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
            // Test seams (test-guild bots only, checked by the run): "entrance" stops the run once the party has
            // gathered at the entrance; "dcfail" refuses the handover to mod-dungeon-clear on purpose. Anything else
            // fails the order rather than start a real run.
            if (!row.testFail.empty() && row.testFail != "entrance" && row.testFail != "dcfail")
                return "bad value for test_fail";
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
        {
            out.role = row.role.empty() ? "user" : row.role;
            if (out.role != "user" && out.role != "test")
                return "bad value for role";
            if (row.faction.empty())
                return "missing field faction";
            Faction faction = Faction::Alliance;
            if (!FactionFromName(row.faction, faction))
                return "bad value for faction";
            out.faction = row.faction;
            if (row.guildName.empty())
                return "missing field guild_name";
            if (row.guildName.size() < 2 || row.guildName.size() > 24 || row.guildName.front() == ' ' ||
                row.guildName.back() == ' ' ||
                !std::all_of(row.guildName.begin(), row.guildName.end(),
                             [](unsigned char c) { return (std::isalpha(c) && c < 0x80) || c == ' '; }))
                return "bad value for guild_name";
            out.guildName = row.guildName;
            if (row.leaderName.empty())
                return "missing field leader_name";
            out.leaderName = NormalizeName(row.leaderName);
            if (out.leaderName.empty())
                return "bad name: " + row.leaderName;
            if (!row.leaderRace.empty())
            {
                Faction raceFaction = Faction::Alliance;
                if (!ParseUInt(row.leaderRace, 255, value) || !RaceFaction(static_cast<uint8_t>(value), raceFaction) ||
                    raceFaction != faction)
                    return "bad value for leader_race";
                out.leaderRace = static_cast<uint8_t>(value);
            }
            return "";
        }
        case OrderType::CreateFounders:
            if (row.guildId.empty())
                return "missing field guild_id";
            if (!ParseUInt(row.guildId, UINT32_MAX, value) || !value)
                return "bad value for guild_id";
            out.guildId = static_cast<uint32_t>(value);
            if (row.foundersLength.empty())
                return "missing field founders";
            if (!ParseUInt(row.foundersLength, 5, value) || !value)
                return "bad value for founders (1 to 5)";
            out.founderCount = static_cast<uint32_t>(value);
            // Test seam (test guild only, checked by the order): "cut" stops between making the founders and
            // finishing them, as a restart would (final review I1). Anything else fails the order.
            if (!row.testFail.empty() && row.testFail != "cut")
                return "bad value for test_fail";
            return "";  // each founder is read and checked by NewGameOrders (JSON_TABLE + ParseFounders)
        case OrderType::Restore:
            // Test seams (test-guild bots only, checked by the restore): "load" (the chosen dump fails to load),
            // "crash" (stops right after the delete), "purge_timeout" (the first answer about the emptied mailbox is
            // lost), "delete_fail" (the delete never reaches the database). Anything else fails the order rather
            // than run a real restore.
            if (!row.testFail.empty() && row.testFail != "load" && row.testFail != "crash" &&
                row.testFail != "purge_timeout" && row.testFail != "delete_fail")
                return "bad value for test_fail";
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
