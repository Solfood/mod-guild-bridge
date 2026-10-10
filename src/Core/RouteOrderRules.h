/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 * Pure. The quest-route orders' rules (contract §4 head_to, route_style; Plan 5a).
 */

#ifndef MOD_GUILD_BRIDGE_CORE_ROUTEORDERRULES_H
#define MOD_GUILD_BRIDGE_CORE_ROUTEORDERRULES_H

#include <cstdint>
#include <string>

namespace GuildBridge
{
enum class HeadToStep
{
    Keep,
    Reapply,
    Clear
};

// A stored head_to holds until the bot reaches the zone or outlevels it (spec §7). `inGame` is the fork's current
// target (0 after a relog or once the fork saw it arrive); `fits` is "the fork's HeadToProblem is empty". The caller
// does not ask at all (Keep) while the bot is held, in a dungeon run or in an instance (preflight C7).
inline HeadToStep NextHeadToStep(uint32_t stored, uint32_t inGame, uint32_t botZone, bool fits)
{
    if (!stored)
        return HeadToStep::Keep;
    if (botZone == stored || !fits)
        return HeadToStep::Clear;
    return inGame == stored ? HeadToStep::Keep : HeadToStep::Reapply;
}

// The fork's Routes::StyleName spellings (preflight D24: the bridge's pure tests cannot include fork headers, so the
// names are repeated here and pinned in test_routes.cpp).
inline bool RouteStyleValid(std::string const& style)
{
    return style == "steady" || style == "curious" || style == "easygoing";
}
}  // namespace GuildBridge

#endif
