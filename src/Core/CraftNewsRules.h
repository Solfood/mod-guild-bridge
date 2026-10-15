/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 * Pure (no AzerothCore includes): unit-tested on the Mac by tests/unit/run.sh.
 */

#ifndef MOD_GUILD_BRIDGE_CRAFTNEWSRULES_H
#define MOD_GUILD_BRIDGE_CRAFTNEWSRULES_H

#include <cstdint>

namespace GuildBridge
{
// World of Craft part 1 (spec 2026-10-14 §5 "App"): crafting news. A profession skill is news when it crosses a
// multiple of 75 (75, 150, ... 450: the ranks' caps).
constexpr uint32_t CRAFT_MILESTONE = 75;

inline bool CraftMilestone(uint32_t oldValue, uint32_t newValue)
{
    return newValue > oldValue && newValue / CRAFT_MILESTONE > oldValue / CRAFT_MILESTONE;
}

// The professions (SkillLine ids): the primary ones, cooking, first aid and fishing. Weapon and class skills never are.
inline bool IsCraftNewsSkill(uint32_t skill)
{
    switch (skill)
    {
        case 129: case 164: case 165: case 171: case 182: case 185: case 186: case 197: case 202: case 333:
        case 356: case 393: case 755: case 773:
            return true;
        default:
            return false;
    }
}
}  // namespace GuildBridge

#endif
