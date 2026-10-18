/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 * Pure. How a run gets to its dungeon, the level band, and role warnings (spec §4.3 steps 1 and 3).
 */

#ifndef MOD_GUILD_BRIDGE_CORE_TRAVELRULES_H
#define MOD_GUILD_BRIDGE_CORE_TRAVELRULES_H

#include "OrderRules.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace GuildBridge
{
struct Spot
{
    uint32_t map = 0;
    float x = 0.f, y = 0.f, z = 0.f;
};

// Eastern Kingdoms, Kalimdor, Outland, Northrend: the maps a party can walk and ride on.
inline bool IsContinent(uint32_t map) { return map == 0 || map == 1 || map == 530 || map == 571; }

struct ApproachChoice
{
    Approach approach = Approach::Teleport;
    std::string reason;   // for a teleport: "far" or "requested"
    float distance = -1;  // largest 2D distance of a member to the entrance; -1 when not all on its map
};

// Travel when every member is on the entrance's continent and (unless travel was asked for) within maxDistance.
inline ApproachChoice ChooseApproach(std::vector<Spot> const& party, Spot const& entrance, float maxDistance,
                                     Approach requested)
{
    ApproachChoice choice;
    bool sameMap = IsContinent(entrance.map) && !party.empty();
    float farthest = 0.f;
    for (Spot const& spot : party)
    {
        if (spot.map != entrance.map)
            sameMap = false;
        farthest = std::max(farthest, std::hypot(spot.x - entrance.x, spot.y - entrance.y));
    }
    choice.distance = sameMap ? farthest : -1.f;
    if (requested == Approach::Teleport)
    {
        choice.reason = "requested";
        return choice;
    }
    if (!sameMap || (requested == Approach::Auto && farthest > maxDistance))
    {
        choice.reason = "far";
        return choice;
    }
    choice.approach = Approach::Travel;
    return choice;
}

struct LevelBand
{
    uint32_t min = 1, max = 80;
};

inline LevelBand BandFor(uint32_t recommended, uint32_t below, uint32_t above)
{
    return {recommended > below ? recommended - below : 1u, recommended + above};
}

// "" when every member is inside the band; else the first one outside it.
inline std::string LevelProblem(std::vector<std::pair<std::string, uint32_t>> const& members, LevelBand band)
{
    for (auto const& [name, level] : members)
        if (level < band.min || level > band.max)
            return "level out of range: " + name + " is " + std::to_string(level) + " (" + std::to_string(band.min) +
                   "-" + std::to_string(band.max) + ")";
    return "";
}

// One party member as playerbots' strategy assignment sees it (AiFactory::AddDefault*Strategies, which
// mod-dungeon-clear triggers with ResetStrategies at roster time). WotLK class ids; tab = AiFactory::GetPlayerSpecTab
// (most points; below level 10 or with no talents a per-class default).
struct SeatSpec
{
    std::string name;
    uint8_t cls = 0;
    uint8_t tab = 0;
    uint32_t level = 0;
    bool hasCatForm = false;    // knows Cat Form (768)
    bool hasThickHide = false;  // has the Thick Hide aura (16931)
    uint32_t guid = 0;          // tie-break between equal choices (mod-dungeon-clear elects the lowest guid)
};

// A tank strategy (STRATEGY_TYPE_TANK in its combat or non-combat engine): what mod-dungeon-clear's leader election
// asks for. Form-independent. Warrior protection (tab 2), paladin protection (1), death knight blood (0), feral druid
// (1) unless it fights as a cat: combat "bear" when it lacks Cat Form or has Thick Hide, non-combat "tank assist" below
// level 20 or with Thick Hide.
inline bool ReadsAsTank(SeatSpec const& s)
{
    switch (s.cls)
    {
        case 1: return s.tab == 2;
        case 2: return s.tab == 1;
        case 6: return s.tab == 0;
        case 11: return s.tab == 1 && (!s.hasCatForm || s.hasThickHide || s.level < 20);
        default: return false;
    }
}

// A healing strategy: priest discipline/holy, shaman restoration, paladin holy, druid restoration.
inline bool ReadsAsHealer(SeatSpec const& s)
{
    switch (s.cls)
    {
        case 5: return s.tab != 2;
        case 7: return s.tab == 2;
        case 2: return s.tab == 0;
        case 11: return s.tab == 2;
        default: return false;
    }
}

enum class Role { Dps, Tank, Heal };

inline char const* RoleWord(Role r) { return r == Role::Tank ? "tank" : r == Role::Heal ? "heal" : "dps"; }

// The role a member is set up for now (what mod-dungeon-clear would see if nothing is changed).
inline Role CurrentRole(SeatSpec const& s)
{
    return ReadsAsTank(s) ? Role::Tank : ReadsAsHealer(s) ? Role::Heal : Role::Dps;
}

// What the CLASS can do, whatever the spec. Warrior, paladin, death knight tank; a druid tanks in Bear Form (level 10).
// Priest, paladin, druid, shaman heal. Order of preference (lower = first): tank warrior, paladin, druid, death
// knight; heal priest, paladin, druid, shaman.
inline int TankRank(SeatSpec const& s)
{
    switch (s.cls)
    {
        case 1: return 0;
        case 2: return 1;
        case 11: return s.level >= 10 ? 2 : -1;
        case 6: return 3;
        default: return -1;
    }
}

inline int HealRank(SeatSpec const& s)
{
    switch (s.cls)
    {
        case 5: return 0;
        case 2: return 1;
        case 11: return 2;
        case 7: return 3;
        default: return -1;
    }
}

inline char const* ClassWord(uint8_t cls)
{
    static char const* const names[12] = {"", "warrior", "paladin", "hunter", "rogue", "priest", "death knight",
                                          "shaman", "mage", "warlock", "", "druid"};
    // WotLK ids: 1 warrior 2 paladin 3 hunter 4 rogue 5 priest 6 death knight 7 shaman 8 mage 9 warlock 11 druid
    return cls < 12 ? names[cls] : "";
}

// The party in role order. "Role assignment" (GM-BRIDGE-0073): the roles go by what each member's CLASS can do, not
// by its current spec, but the current spec is preferred when it already fits. mod-dungeon-clear needs a tank (it
// elects its leader among tanks; without one `dc on` silently does nothing) and nothing else, so: nobody's class can
// tank = problem. Otherwise the pair (tank, healer) is chosen to have a healer if one can be had, then the fewest
// members switching away from what they are set up for, then class preference (tank warrior > paladin > druid >
// death knight; healer priest > paladin > druid > shaman), then the lowest guid (mod-dungeon-clear's own tie-break).
// The rest are dps. order = tank, healer (if any), the rest as given.
// Warnings: "<name> tanks|heals (<class>, switching from dps)" for each member who has to change role (their
// strategies are switched for the run, talents are not touched), no healer, and the new order when it changed.
struct PartyPlan
{
    std::string problem;
    std::vector<std::size_t> order;
    std::vector<std::string> warnings;
    std::vector<Role> roles;  // by input seat
    std::vector<bool> switching;  // by input seat: the member's strategies must change for its role
};

inline PartyPlan ArrangeParty(std::vector<SeatSpec> const& seats)
{
    PartyPlan plan;
    std::size_t const n = seats.size();
    std::size_t const none = n;
    plan.roles.assign(n, Role::Dps);
    plan.switching.assign(n, false);
    std::size_t bestTank = none, bestHeal = none;
    // score: lower is better. (no healer, switches, tank rank, heal rank, tank guid, heal guid)
    std::array<long long, 6> bestScore{};
    for (std::size_t t = 0; t < n; ++t)
    {
        if (TankRank(seats[t]) < 0)
            continue;
        for (std::size_t h = 0; h <= n; ++h)
        {
            if (h == t || (h < n && HealRank(seats[h]) < 0))
                continue;
            long long switches = (CurrentRole(seats[t]) != Role::Tank) + (h < n && CurrentRole(seats[h]) != Role::Heal);
            std::array<long long, 6> score = {h == n ? 1 : 0, switches, TankRank(seats[t]),
                                              h < n ? HealRank(seats[h]) : 0, seats[t].guid,
                                              h < n ? seats[h].guid : 0};
            if (bestTank == none || score < bestScore)
            {
                bestTank = t;
                bestHeal = h;
                bestScore = score;
            }
        }
    }
    if (bestTank == none)
    {
        plan.problem = "no tank in this party: no member can tank";
        return plan;
    }
    std::size_t const tank = bestTank;
    std::size_t const heal = bestHeal;  // n = none
    plan.roles[tank] = Role::Tank;
    plan.switching[tank] = CurrentRole(seats[tank]) != Role::Tank;
    if (heal != none)
    {
        plan.roles[heal] = Role::Heal;
        plan.switching[heal] = CurrentRole(seats[heal]) != Role::Heal;
    }
    plan.order.push_back(tank);
    if (heal != none)
        plan.order.push_back(heal);
    for (std::size_t i = 0; i < n; ++i)
        if (i != tank && i != heal)
            plan.order.push_back(i);
    for (std::size_t i : {tank, heal})
        if (i != none && plan.switching[i])
            plan.warnings.push_back(seats[i].name + (i == tank ? " tanks (" : " heals (") + ClassWord(seats[i].cls) +
                                    ", switching from " + RoleWord(CurrentRole(seats[i])) + ")");
    if (heal == none)
        plan.warnings.push_back("no healer in this party");
    for (std::size_t i = 0; i < plan.order.size(); ++i)
        if (plan.order[i] != i)
        {
            plan.warnings.push_back("party reordered: tank " + seats[tank].name +
                                    (heal == none ? std::string() : ", healer " + seats[heal].name));
            break;
        }
    return plan;
}

// "The guild's summons": a member who is dead or in combat when a run starts is revived / pulled out of combat
// instead of refusing the order. This is the warning line about one member. A dry run says what the summons will
// do; a real run says what it did (the bridge does it just before the party sets out).
inline std::string SummonWarning(std::string const& name, bool dead, bool fighting, bool dryRun)
{
    if (dead)
        return name + (dryRun ? " is dead: the summons revives them" : " was dead: the summons revived them");
    if (fighting)
        return name + (dryRun ? " is fighting: the summons pulls them out" : " was fighting: the summons pulled them out");
    return "";
}
}  // namespace GuildBridge

#endif
