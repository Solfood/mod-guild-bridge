// tests/unit/test_routes.cpp
#include "../../src/Core/RouteOrderRules.h"
#include "check.h"

using namespace GuildBridge;

int main()
{
    // The 30 s pass for a member with a stored head_to (contract §4): reached or no longer fitting -> clear; the game
    // forgot it (a relog) -> re-apply; otherwise keep.
    CHECK_TRUE(NextHeadToStep(0, 0, 85, true) == HeadToStep::Keep);
    CHECK_TRUE(NextHeadToStep(130, 130, 85, true) == HeadToStep::Keep);
    CHECK_TRUE(NextHeadToStep(130, 0, 85, true) == HeadToStep::Reapply);
    CHECK_TRUE(NextHeadToStep(130, 0, 130, true) == HeadToStep::Clear);   // it arrived
    CHECK_TRUE(NextHeadToStep(130, 130, 85, false) == HeadToStep::Clear); // it outlevelled the zone (or routes off)
    // Preflight D24: the exact spellings of the fork's Routes::StyleName (steady, curious, easygoing), lower case.
    CHECK_TRUE(RouteStyleValid("steady") && RouteStyleValid("curious") && RouteStyleValid("easygoing"));
    CHECK_TRUE(!RouteStyleValid("Steady") && !RouteStyleValid("lazy") && !RouteStyleValid(""));
    return UnitFailures();
}
