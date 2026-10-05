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

// classes in role order (tank, heal, dps, dps, dps). WotLK class ids: 1 warrior, 2 paladin, 5 priest, 6 death knight,
// 7 shaman, 11 druid. Warnings only: the run still goes.
inline std::vector<std::string> RoleWarnings(std::array<uint8_t, 5> const& classes)
{
    std::vector<std::string> warnings;
    auto in = [](uint8_t c, std::initializer_list<uint8_t> set) {
        return std::find(set.begin(), set.end(), c) != set.end();
    };
    if (!in(classes[0], {1, 2, 6, 11}))
        warnings.push_back("the tank slot has no tank class");
    if (!in(classes[1], {2, 5, 7, 11}))
        warnings.push_back("the healer slot has no healing class");
    return warnings;
}

// Roles in role order (tank, heal, dps, dps, dps) as mod-dungeon-clear reads them: by talent spec ("tank", "heal" or
// "dps"; playerbots' IsTank/IsHeal by spec). dungeon-clear elects its leader among tanks only, so a party whose tank
// does not read as a tank never starts ("dc on did not take"). "" when the tank and healer seats read right.
inline std::string RoleProblem(std::vector<std::pair<std::string, std::string>> const& seats)
{
    if (seats.size() > 0 && seats[0].second != "tank")
        return "no tank in this party: " + seats[0].first + " has no tank spec";
    if (seats.size() > 1 && seats[1].second != "heal")
        return "no healer in this party: " + seats[1].first + " has no healer spec";
    return "";
}
}  // namespace GuildBridge

#endif
