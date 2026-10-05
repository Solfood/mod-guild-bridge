// tests/unit/test_stuck.cpp
#include "../../src/Core/StuckRules.h"
#include "check.h"

using namespace GuildBridge;

static BotSample At(uint32_t t, float x, bool dead = false, uint32_t stuck = 0, uint32_t xp = 0)
{
    BotSample s;
    s.nowS = t;
    s.dead = dead;
    s.map = 0;
    s.x = x;
    s.y = 0.f;
    s.xp = xp;
    s.level = 10;
    s.money = 100;
    s.stuckTeleports = stuck;
    return s;
}

static std::string Str(std::vector<Change> const& changes)
{
    std::string out;
    for (Change const& c : changes)
        out += std::string(c.open ? "+" : "-") + IncidentKindName(c.kind) + " ";
    return out;
}

int main()
{
    Thresholds const th{600, 900, 40.f, 3, 900};

    BotTrack dead;
    CHECK_EQ(std::string(""), Str(Evaluate(dead, At(0, 0, true), th)));       // first sight: baseline only
    CHECK_EQ(std::string(""), Str(Evaluate(dead, At(599, 0, true), th)));
    CHECK_EQ(std::string("+dead_long "), Str(Evaluate(dead, At(600, 0, true), th)));  // dead since t=0 (D2)
    CHECK_EQ(std::string(""), Str(Evaluate(dead, At(660, 0, true), th)));     // stays open, no repeat
    CHECK_EQ(std::string("-dead_long "), Str(Evaluate(dead, At(700, 0, false, 0, 5), th)));

    BotTrack idle;
    Evaluate(idle, At(0, 0), th);
    CHECK_EQ(std::string(""), Str(Evaluate(idle, At(899, 30), th)));         // 30 yd is not progress; not 900 s yet
    CHECK_EQ(std::string("+no_progress "), Str(Evaluate(idle, At(900, 30), th)));
    CHECK_EQ(std::string("-no_progress "), Str(Evaluate(idle, At(960, 80), th)));  // moved 80 yards from the anchor

    BotTrack earner;
    Evaluate(earner, At(0, 0), th);
    CHECK_EQ(std::string(""), Str(Evaluate(earner, At(1000, 0, false, 0, 50), th)));  // XP changed: progress

    BotTrack exempt;
    Evaluate(exempt, At(0, 0), th);
    BotSample inRun = At(2000, 0);
    inRun.exempt = true;
    CHECK_EQ(std::string(""), Str(Evaluate(exempt, inRun, th)));
    // Judged again once the run is over: its clock starts then, not at the old anchor.
    CHECK_EQ(std::string(""), Str(Evaluate(exempt, At(2060, 0), th)));
    CHECK_EQ(std::string("+no_progress "), Str(Evaluate(exempt, At(2960, 0), th)));
    BotSample held = At(3000, 0);
    held.exempt = true;
    CHECK_EQ(std::string("-no_progress "), Str(Evaluate(exempt, held, th)));  // held now: no longer judged

    // A bot revived where it died, after a long death, is not at once "no progress": the clock restarts.
    BotTrack revived;
    Evaluate(revived, At(0, 0), th);
    Evaluate(revived, At(60, 0, true), th);
    CHECK_EQ(std::string("+dead_long "), Str(Evaluate(revived, At(660, 0, true), th)));
    CHECK_EQ(std::string("-dead_long "), Str(Evaluate(revived, At(1200, 0), th)));
    CHECK_EQ(std::string(""), Str(Evaluate(revived, At(1260, 0), th)));
    CHECK_EQ(std::string("+no_progress "), Str(Evaluate(revived, At(2100, 0), th)));

    BotTrack path;
    Evaluate(path, At(0, 0), th);
    Evaluate(path, At(60, 100, false, 1), th);
    Evaluate(path, At(120, 200, false, 2), th);
    CHECK_EQ(std::string("+path_fail "), Str(Evaluate(path, At(180, 300, false, 3), th)));
    CHECK_EQ(std::string("-path_fail "), Str(Evaluate(path, At(1200, 400, false, 3), th)));  // window passed
    CHECK_EQ(std::string(""), Str(Evaluate(path, At(1260, 500, false, 0), th)));             // relog reset the counter

    // A relog between two samples restarts the fork's counter at 0: every teleport since the login is new, even
    // when the new count is not below the old one.
    BotTrack relog;
    BotSample r0 = At(0, 0);
    r0.loginS = 1;
    Evaluate(relog, r0, th);
    BotSample r1 = At(60, 100, false, 2);
    r1.loginS = 1;
    CHECK_EQ(std::string(""), Str(Evaluate(relog, r1, th)));
    BotSample r2 = At(120, 200, false, 2);
    r2.loginS = 100;
    CHECK_EQ(std::string("+path_fail "), Str(Evaluate(relog, r2, th)));  // 2 before + 2 after the relog
    CHECK_EQ(4u, static_cast<unsigned>(relog.stuckTimes.size()));
    return UnitFailures();
}
