// tests/unit/test_dcrecord.cpp
#include "../../src/Core/DcRecord.h"
#include "check.h"

using namespace GuildBridge;

int main()
{
    std::string const text =
        "{\"schema\":13,\"runId\":\"r-1\",\"planId\":\"\",\"dungeon\":\"rfc\",\"durationS\":100,\"result\":\"success\","
        "\"failReason\":\"\",\"bossesTotal\":4,\"bossesKilled\":4}\n"
        "{\"schema\":13,\"runId\":\"r-2\",\"planId\":\"\",\"dungeon\":\"rfc\",\"durationS\":1830,\"result\":\"wipe\","
        "\"failReason\":\"party wiped on \\\"Taragaman\\\"\",\"bossesTotal\":4,\"bossesKilled\":1,"
        "\"pulls\":[{\"result\":\"nested\"}]}\n";
    DcOutcome o = FindDcRun(text, "r-2");
    CHECK_TRUE(o.found);
    CHECK_EQ(std::string("wipe"), o.result);
    CHECK_EQ(std::string("party wiped on \"Taragaman\""), o.failReason);
    CHECK_EQ(1830u, o.durationS);
    CHECK_EQ(1u, o.bossesKilled);
    CHECK_EQ(4u, o.bossesTotal);
    CHECK_EQ(std::string("success"), FindDcRun(text, "r-1").result);
    CHECK_TRUE(!FindDcRun(text, "r-3").found);
    CHECK_TRUE(!FindDcRun("", "r-1").found);
    CHECK_TRUE(FindDcRun("{\"runId\":\"r-1\"", "r-1").found);  // truncated line: found, fields empty
    CHECK_EQ(std::string("cleared"), std::string(RunResultFromDc("success")));
    CHECK_EQ(std::string("wiped"), std::string(RunResultFromDc("wipe")));
    CHECK_EQ(std::string("abandoned"), std::string(RunResultFromDc("no_progress")));
    return UnitFailures();
}
