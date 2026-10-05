/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 * Pure. When a bot counts as stuck (GM-BOTAI stuck-bot toolkit, "find").
 */

#ifndef MOD_GUILD_BRIDGE_CORE_STUCKRULES_H
#define MOD_GUILD_BRIDGE_CORE_STUCKRULES_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace GuildBridge
{
enum class IncidentKind : uint8_t { DeadLong = 0, NoProgress = 1, PathFail = 2 };
constexpr int INCIDENT_KINDS = 3;

inline char const* IncidentKindName(IncidentKind kind)
{
    static char const* const names[] = {"dead_long", "no_progress", "path_fail"};
    return names[static_cast<int>(kind)];
}

struct Thresholds
{
    uint32_t deadS = 600;       // dead (or a ghost) this long
    uint32_t noProgressS = 900;  // no move beyond moveYards and no XP, level or money change this long
    float moveYards = 40.f;
    uint32_t pathFails = 3;      // the fork's "stuck -> teleport" fallback this many times ...
    uint32_t pathWindowS = 900;  // ... within this window
};

struct BotSample
{
    uint32_t nowS = 0;
    bool dead = false;
    bool exempt = false;  // in a run, held, restoring, raising or inside a dungeon: not judged for "no progress"
    uint32_t map = 0;
    float x = 0.f, y = 0.f;
    uint32_t xp = 0;
    uint32_t level = 0;
    uint64_t money = 0;
    uint32_t stuckTeleports = 0;  // the fork's counter: in memory, so it starts again at 0 at every login
    uint32_t loginS = 0;          // when this session logged in (a change = a relog)
};

struct BotTrack
{
    bool seen = false;
    bool deadSeen = false;  // dead since deadSinceS (preflight D2: 0 is a valid time, not "alive")
    uint32_t deadSinceS = 0;
    uint32_t anchorS = 0;  // last progress (or the last sample in which the bot was not judged)
    uint32_t anchorMap = 0;
    float anchorX = 0.f, anchorY = 0.f;
    uint32_t xp = 0;
    uint32_t level = 0;
    uint64_t money = 0;
    uint32_t lastStuck = 0;
    uint32_t loginS = 0;
    std::vector<uint32_t> stuckTimes;  // when each stuck teleport was first seen, within the window
    bool open[INCIDENT_KINDS] = {false, false, false};
};

struct Change
{
    IncidentKind kind;
    bool open;
};

inline void Anchor(BotTrack& t, BotSample const& s)
{
    t.anchorS = s.nowS;
    t.anchorMap = s.map;
    t.anchorX = s.x;
    t.anchorY = s.y;
    t.xp = s.xp;
    t.level = s.level;
    t.money = s.money;
}

inline std::vector<Change> Evaluate(BotTrack& t, BotSample const& s, Thresholds const& th)
{
    std::vector<Change> out;
    auto set = [&](IncidentKind kind, bool want) {
        bool& open = t.open[static_cast<int>(kind)];
        if (open != want)
        {
            open = want;
            out.push_back({kind, want});
        }
    };
    if (!t.seen)
    {
        t.seen = true;
        Anchor(t, s);
        t.lastStuck = s.stuckTeleports;
        t.loginS = s.loginS;
        t.deadSeen = s.dead;
        t.deadSinceS = s.nowS;
        return out;
    }

    if (s.dead && !t.deadSeen)
    {
        t.deadSeen = true;
        t.deadSinceS = s.nowS;
    }
    if (!s.dead)
        t.deadSeen = false;
    set(IncidentKind::DeadLong, s.dead && s.nowS - t.deadSinceS >= th.deadS);

    // Not judged while dead (dead_long covers it) or exempt: the clock starts again when judging resumes.
    bool const moved = s.map != t.anchorMap || std::hypot(s.x - t.anchorX, s.y - t.anchorY) > th.moveYards;
    if (s.dead || s.exempt || moved || s.xp != t.xp || s.level != t.level || s.money != t.money)
    {
        Anchor(t, s);
        set(IncidentKind::NoProgress, false);
    }
    else
        set(IncidentKind::NoProgress, s.nowS - t.anchorS >= th.noProgressS);

    if (s.loginS != t.loginS)
    {
        t.loginS = s.loginS;
        t.lastStuck = 0;  // the counter lives in memory: a relog restarted it at 0
    }
    if (s.stuckTeleports < t.lastStuck)
        t.lastStuck = s.stuckTeleports;  // a relog we did not see
    for (uint32_t i = t.lastStuck; i < s.stuckTeleports; ++i)
        t.stuckTimes.push_back(s.nowS);
    t.lastStuck = s.stuckTeleports;
    t.stuckTimes.erase(std::remove_if(t.stuckTimes.begin(), t.stuckTimes.end(),
                                      [&](uint32_t when) { return s.nowS - when > th.pathWindowS; }),
                       t.stuckTimes.end());
    if (th.pathFails && t.stuckTimes.size() >= th.pathFails)
        set(IncidentKind::PathFail, true);
    else if (t.stuckTimes.empty())
        set(IncidentKind::PathFail, false);
    return out;
}
}  // namespace GuildBridge

#endif
