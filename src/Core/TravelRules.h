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

// The party in role order. mod-dungeon-clear needs a tank (it elects its leader among tanks; without one `dc on`
// silently does nothing) and nothing else, so: no tank = problem; otherwise order = tank first (the given first seat
// if it is one), then a healer if there is one (the given second seat if it is one), then the rest as given.
// Warnings: no healer, and the new order when it changed.
struct PartyPlan
{
    std::string problem;
    std::vector<std::size_t> order;
    std::vector<std::string> warnings;
};

inline PartyPlan ArrangeParty(std::vector<SeatSpec> const& seats)
{
    PartyPlan plan;
    std::size_t const none = seats.size();
    std::size_t tank = none, heal = none;
    for (std::size_t i = 0; i < seats.size() && tank == none; ++i)
        if (ReadsAsTank(seats[i]))
            tank = i;
    if (tank == none)
    {
        plan.problem = "no tank in this party: no member has a tank spec";
        return plan;
    }
    if (seats.size() > 1 && tank != 1 && ReadsAsHealer(seats[1]))
        heal = 1;
    for (std::size_t i = 0; i < seats.size() && heal == none; ++i)
        if (i != tank && ReadsAsHealer(seats[i]))
            heal = i;
    plan.order.push_back(tank);
    if (heal != none)
        plan.order.push_back(heal);
    for (std::size_t i = 0; i < seats.size(); ++i)
        if (i != tank && i != heal)
            plan.order.push_back(i);
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
}  // namespace GuildBridge

#endif
