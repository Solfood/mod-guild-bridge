/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 * Pure. Checks for the New Game orders (spec §2b): factions, race/class pairs, names, founders.
 * World checks (name free, reserved names, the guild, free account slots) live in NewGameOrders.cpp.
 */

#ifndef MOD_GUILD_BRIDGE_CORE_NEWGAMERULES_H
#define MOD_GUILD_BRIDGE_CORE_NEWGAMERULES_H

#include "Json.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace GuildBridge
{
enum class Faction : uint8_t { Alliance, Horde };

inline char const* FactionName(Faction f) { return f == Faction::Alliance ? "alliance" : "horde"; }

inline bool FactionFromName(std::string const& name, Faction& out)
{
    if (name == "alliance")
        out = Faction::Alliance;
    else if (name == "horde")
        out = Faction::Horde;
    else
        return false;
    return true;
}

// WotLK playable races: Alliance 1 Human, 3 Dwarf, 4 Night Elf, 7 Gnome, 11 Draenei; Horde 2 Orc, 5 Undead,
// 6 Tauren, 8 Troll, 10 Blood Elf.
inline bool RaceFaction(uint8_t race, Faction& out)
{
    switch (race)
    {
        case 1: case 3: case 4: case 7: case 11: out = Faction::Alliance; return true;
        case 2: case 5: case 6: case 8: case 10: out = Faction::Horde; return true;
        default: return false;
    }
}

// WotLK race/class pairs (class 6 Death Knight is playable for every race, but founders never get it).
inline bool IsPlayableCombo(uint8_t race, uint8_t cls)
{
    auto has = [cls](std::initializer_list<uint8_t> classes) {
        return std::find(classes.begin(), classes.end(), cls) != classes.end();
    };
    switch (race)
    {
        case 1: return has({1, 2, 4, 5, 6, 8, 9});
        case 2: return has({1, 3, 4, 6, 7, 9});
        case 3: return has({1, 2, 3, 4, 5, 6});
        case 4: return has({1, 3, 4, 5, 6, 11});
        case 5: return has({1, 4, 5, 6, 8, 9});
        case 6: return has({1, 3, 6, 7, 11});
        case 7: return has({1, 4, 6, 8, 9});
        case 8: return has({1, 3, 4, 5, 6, 7, 8});
        case 10: return has({2, 3, 4, 5, 6, 8, 9});
        case 11: return has({1, 2, 3, 5, 6, 7, 8});
        default: return false;
    }
}

inline uint8_t DefaultLeaderRace(Faction f) { return f == Faction::Alliance ? 1 : 2; }  // Human / Orc

// The game's name rule for new characters: 2-12 ASCII letters, first upper, rest lower. "" = not a valid shape.
inline std::string NormalizeName(std::string const& name)
{
    if (name.size() < 2 || name.size() > 12 ||
        !std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::isalpha(c) && c < 0x80; }))
        return "";
    std::string out = name;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
    return out;
}

// A JSON array of strings, as MySQL prints it (["a", "b"]). Only \" \\ \/ \n \t escapes. False on anything else.
inline bool ParseStringArray(std::string_view json, std::vector<std::string>& out)
{
    out.clear();
    std::size_t i = 0;
    auto skip = [&]() { while (i < json.size() && std::isspace(static_cast<unsigned char>(json[i]))) ++i; };
    skip();
    if (i >= json.size() || json[i++] != '[')
        return false;
    skip();
    if (i < json.size() && json[i] == ']')
    {
        ++i;
        skip();
        return i == json.size();
    }
    while (true)
    {
        skip();
        if (i >= json.size() || json[i++] != '"')
            return false;
        std::string value;
        while (i < json.size() && json[i] != '"')
        {
            if (json[i] == '\\')
            {
                if (++i >= json.size())
                    return false;
                char const e = json[i];
                if (e != '"' && e != '\\' && e != '/' && e != 'n' && e != 't')
                    return false;
                value += e == 'n' ? '\n' : e == 't' ? '\t' : e;
            }
            else
                value += json[i];
            ++i;
        }
        if (i++ >= json.size())
            return false;
        out.push_back(std::move(value));
        skip();
        if (i < json.size() && json[i] == ',')
        {
            ++i;
            continue;
        }
        if (i < json.size() && json[i] == ']')
        {
            ++i;
            skip();
            return i == json.size();
        }
        return false;
    }
}

// Every field as text from MySQL's JSON_TABLE over params.founders ("" when absent).
struct FounderRow
{
    std::string name, race, cls, gender, profCount, prof1, prof2, traitsType, traits, backstory;
};

struct Founder
{
    std::string name;
    uint8_t race = 0, cls = 0;
    uint8_t gender = 2;  // 0 male, 1 female, 2 random
    uint16_t prof1 = 0, prof2 = 0;
    std::string traitsJson;  // re-serialised by us: ["a","b"]
    std::string backstory;
};

// The 11 primary professions' skill ids (the one list; OrderRules' preset_professions uses it too, preflight D12).
inline bool IsPrimaryProfession(uint32_t skill)
{
    static constexpr uint32_t primary[] = {171, 164, 333, 202, 182, 773, 755, 165, 186, 393, 197};
    return std::find(std::begin(primary), std::end(primary), skill) != std::end(primary);
}

inline bool SmallUInt(std::string const& text, uint32_t max, uint32_t& out)
{
    if (text.empty() || text.size() > 5 ||
        !std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isdigit(c); }))
        return false;
    out = static_cast<uint32_t>(std::stoul(text));
    return out <= max;
}

// "" when every founder is fine (then `out` holds them all); else the first problem, naming the founder,
// and `out` is empty: an order is accepted whole or not at all.
inline std::string ParseFounders(std::vector<FounderRow> const& rows, Faction guildFaction, std::vector<Founder>& out)
{
    out.clear();
    if (rows.empty() || rows.size() > 5)
        return "bad value for founders (1 to 5)";
    std::vector<Founder> founders;
    for (FounderRow const& row : rows)
    {
        Founder f;
        f.name = NormalizeName(row.name);
        if (f.name.empty())
            return "bad name: " + row.name;
        for (Founder const& earlier : founders)
            if (earlier.name == f.name)
                return "name taken: " + f.name + " (twice in this order)";
        uint32_t value = 0;
        Faction raceFaction = Faction::Alliance;
        if (!SmallUInt(row.race, 255, value) || !RaceFaction(static_cast<uint8_t>(value), raceFaction))
            return "bad value for race: " + f.name;
        f.race = static_cast<uint8_t>(value);
        if (raceFaction != guildFaction)
            return "wrong faction: " + f.name;
        if (!SmallUInt(row.cls, 255, value))
            return "bad value for class: " + f.name;
        if (value == 6)
            return "bad value for class: " + f.name + " (death knights come only from the raisings)";
        f.cls = static_cast<uint8_t>(value);
        if (!IsPlayableCombo(f.race, f.cls))
            return "race and class don't match: " + f.name;
        if (row.gender.empty())
            f.gender = 2;
        else if (row.gender == "male" || row.gender == "female")
            f.gender = row.gender == "male" ? 0 : 1;
        else
            return "bad value for gender: " + f.name;
        uint32_t count = 0;
        if (!SmallUInt(row.profCount.empty() ? "0" : row.profCount, 2, count))
            return "bad value for professions: " + f.name;
        uint32_t p1 = 0, p2 = 0;
        if (count >= 1 && (!SmallUInt(row.prof1, 65535, p1) || !IsPrimaryProfession(p1)))
            return "bad value for professions: " + f.name;
        if (count == 2 && (!SmallUInt(row.prof2, 65535, p2) || !IsPrimaryProfession(p2) || p1 == p2))
            return "bad value for professions: " + f.name;
        f.prof1 = static_cast<uint16_t>(p1);
        f.prof2 = static_cast<uint16_t>(p2);
        std::vector<std::string> traits;
        if (row.traitsType != "ARRAY" || !ParseStringArray(row.traits, traits) || traits.empty() || traits.size() > 5 ||
            std::any_of(traits.begin(), traits.end(), [](std::string const& t) { return t.empty() || t.size() > 24; }))
            return "bad value for traits: " + f.name;
        f.traitsJson = "[";
        for (std::size_t i = 0; i < traits.size(); ++i)
        {
            if (i)
                f.traitsJson += ",";
            JsonObject::AppendEscaped(f.traitsJson, traits[i]);
        }
        f.traitsJson += "]";
        if (row.backstory.size() > 255)
            return "bad value for backstory: " + f.name;
        f.backstory = row.backstory;
        founders.push_back(std::move(f));
    }
    out = std::move(founders);
    return "";
}

// Founders live on the world's dedicated founder accounts (GuildBridge.FounderAccounts, preflight C1), never on the
// population's RNDBOT accounts (the factory fills those). `accounts` = (account id, characters on it). One founder
// per account, the emptiest first (ties: the list's order); full accounts are skipped. False (and `out` empty) when
// fewer than `needed` accounts have a free slot.
inline bool PickFounderAccounts(std::vector<std::pair<uint32_t, uint32_t>> const& accounts, uint32_t perAccount,
                                std::size_t needed, std::vector<uint32_t>& out)
{
    out.clear();
    std::vector<std::pair<uint32_t, std::size_t>> free;  // (characters, position in the list)
    for (std::size_t i = 0; i < accounts.size(); ++i)
        if (accounts[i].second < perAccount)
            free.emplace_back(accounts[i].second, i);
    if (free.size() < needed)
        return false;
    std::stable_sort(free.begin(), free.end());
    for (std::size_t i = 0; i < needed; ++i)
        out.push_back(accounts[free[i].second].first);
    return true;
}

// Founders cut short (final review I1, narrowed by re-reviews N1). create_founders writes each founder's bot_profiles
// row right after making its character, and adds it to the population (the "add" record) once the character is in
// the database. A restart can stop it in between. At the next boot only the founders a create_founders order that
// was still running (the boot fails it as interrupted) journaled in bot_profiles are settled. No character is ever
// deleted: one without a profile (cut before its profile was written) only gets a warning, and the order sent again
// takes it back by name.
enum class FounderBootAction : uint8_t
{
    Keep,             // finished (profile + "add" record): placed as usual if it still waits for its guild
    Leave,            // looks unfinished but is not a cut order's own founder (raised, restored, ...): a warning only
    Adopt,            // profile, character, no "add" record: add it to the population; it joins its guild at login
    DropProfile,      // profile but the character never reached the database: forget the profile
};

struct FounderBootFacts
{
    bool hasProfile = false;
    bool characterExists = false;
    bool hasAddRecord = false;
    bool fromCutOrder = false;  // its profile's order_id is a create_founders order the boot fails as interrupted
    bool raising = false;       // has a playerbots_raisings row (old or new guid), finished or not
    bool restoring = false;     // in the restore recovery the boot queued
};

inline FounderBootAction SettleFounderAtBoot(FounderBootFacts const& f)
{
    bool const finished = f.hasProfile && f.characterExists && f.hasAddRecord;
    if (finished)
        return FounderBootAction::Keep;
    if (!f.hasProfile || !f.fromCutOrder || f.raising || f.restoring)
        return FounderBootAction::Leave;  // no journal of a cut order: a warning only, never a delete or drop
    if (!f.characterExists)
        return FounderBootAction::DropProfile;
    return f.hasAddRecord ? FounderBootAction::Keep : FounderBootAction::Adopt;
}

// A character that already has a founder's name (from the character cache and the bot_profiles rows).
struct ExistingCharacter
{
    bool onFounderAccount = false;  // one of this world's GuildBridge.FounderAccounts
    uint32_t guildId = 0;
    uint8_t race = 0, cls = 0, gender = 0;
    uint32_t profileGuild = 0;  // its founder profile's guild (0 = no profile)
};

// A create_founders sent again (after a restart or a "not saved in time") takes back the founders an earlier send
// already made, instead of failing "name taken": same race and class (and gender, when the order names one), on a
// founder account, guildless or already in this guild, with no profile or one for this guild.
inline bool CanReuseFounder(ExistingCharacter const& e, Founder const& f, uint32_t targetGuild)
{
    return e.onFounderAccount && e.race == f.race && e.cls == f.cls && (f.gender > 1 || e.gender == f.gender) &&
           (!e.guildId || e.guildId == targetGuild) && (!e.profileGuild || e.profileGuild == targetGuild);
}
}  // namespace GuildBridge

#endif
