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
    // A truncated last line (the tail read caught dungeon-clear mid-write): not final, retry on the next poll.
    DcOutcome const cut = FindDcRun(text + "{\"runId\":\"r-3\",\"result\":\"succ", "r-3");
    CHECK_TRUE(!cut.found);
    CHECK_TRUE(cut.partial);
    CHECK_TRUE(!FindDcRun("{\"runId\":\"r-1\"", "r-1").found);
    CHECK_TRUE(FindDcRun("{\"runId\":\"r-1\"", "r-1").partial);
    CHECK_TRUE(!FindDcRun(text, "r-2").partial);
    CHECK_TRUE(!FindDcRun(text, "r-3").partial);  // absent: not partial either
    // Run ids match exactly, never by prefix: r-1 is not r-10, and r-10 is not r-1.
    std::string const tens =
        "{\"runId\":\"r-10\",\"result\":\"wipe\",\"durationS\":7}\n"
        "{\"runId\":\"r-1\",\"result\":\"success\",\"durationS\":5}\n"
        "{\"runId\":\"r-100\",\"result\":\"no_progress\",\"durationS\":9}\n";
    CHECK_EQ(std::string("success"), FindDcRun(tens, "r-1").result);
    CHECK_EQ(5u, FindDcRun(tens, "r-1").durationS);
    CHECK_EQ(std::string("wipe"), FindDcRun(tens, "r-10").result);
    CHECK_EQ(std::string("no_progress"), FindDcRun(tens, "r-100").result);
    CHECK_TRUE(!FindDcRun("{\"runId\":\"r-10\",\"result\":\"wipe\"}\n", "r-1").found);
    CHECK_TRUE(!FindDcRun("{\"runId\":\"r-1\",\"result\":\"wipe\"}\n", "r-10").found);
    CHECK_EQ(std::string("cleared"), std::string(RunResultFromDc("success")));
    CHECK_EQ(std::string("wiped"), std::string(RunResultFromDc("wipe")));
    CHECK_EQ(std::string("abandoned"), std::string(RunResultFromDc("no_progress")));
    return UnitFailures();
}
