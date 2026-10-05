// tests/unit/test_orders.cpp
#include "../../src/Core/OrderRules.h"
#include "check.h"

using namespace GuildBridge;

static OrderRow Row(std::string type)
{
    OrderRow row;
    row.id = 7;
    row.type = std::move(type);
    return row;
}

int main()
{
    ParsedOrder out;
    CHECK_EQ(std::string("unknown order type 'dance'"), ParseOrder(Row("dance"), out));

    OrderRow invite = Row("invite");
    CHECK_EQ(std::string("missing field bot"), ParseOrder(invite, out));
    invite.bot = "12x";
    CHECK_EQ(std::string("bad value for bot"), ParseOrder(invite, out));
    invite.bot = "1234";
    CHECK_EQ(std::string(""), ParseOrder(invite, out));
    CHECK_EQ(1234u, out.bot);
    CHECK_EQ(std::string("user"), out.guild);
    CHECK_EQ(-1, out.rank);
    invite.guild = "rivals";
    CHECK_EQ(std::string("bad value for guild"), ParseOrder(invite, out));
    invite.guild = "test";
    invite.rank = "0";
    CHECK_EQ(std::string("bad value for rank"), ParseOrder(invite, out));
    invite.rank = "3";
    invite.dryRun = "true";
    CHECK_EQ(std::string(""), ParseOrder(invite, out));
    CHECK_TRUE(out.dryRun);

    OrderRow focus = Row("focus");
    focus.bot = "5";
    focus.focus = "dancing";
    CHECK_EQ(std::string("bad value for focus"), ParseOrder(focus, out));
    focus.focus = "gathering";
    CHECK_EQ(std::string(""), ParseOrder(focus, out));
    CHECK_EQ(4, static_cast<int>(out.focus));

    OrderRow rank = Row("rank");
    rank.bot = "5";
    CHECK_EQ(std::string("missing field rank"), ParseOrder(rank, out));

    OrderRow prof = Row("preset_professions");
    prof.bot = "5";
    prof.first = "186";
    prof.second = "186";
    CHECK_EQ(std::string("bad value for second"), ParseOrder(prof, out));
    prof.second = "185";  // Cooking is secondary
    CHECK_EQ(std::string("bad value for second"), ParseOrder(prof, out));
    prof.second = "";
    CHECK_EQ(std::string(""), ParseOrder(prof, out));
    CHECK_EQ(0, static_cast<int>(out.second));

    OrderRow run = Row("run_dungeon");
    CHECK_EQ(std::string("missing field dungeon"), ParseOrder(run, out));
    run.dungeon = "rfc";
    run.partyLength = "4";
    CHECK_EQ(std::string("party must be 5 different bots"), ParseOrder(run, out));
    run.partyLength = "5";
    run.party = {"1", "2", "3", "4", "4"};
    CHECK_EQ(std::string("party must be 5 different bots"), ParseOrder(run, out));
    run.party = {"1", "2", "3", "4", "5"};
    run.approach = "fly";
    CHECK_EQ(std::string("bad value for approach"), ParseOrder(run, out));
    run.approach = "";
    CHECK_EQ(std::string(""), ParseOrder(run, out));
    CHECK_EQ(static_cast<int>(Approach::Auto), static_cast<int>(out.approach));

    OrderRow snap = Row("snapshot");
    CHECK_EQ(std::string("missing field bot"), ParseOrder(snap, out));
    snap.guild = "user";
    CHECK_EQ(std::string(""), ParseOrder(snap, out));
    snap.bot = "9";
    CHECK_EQ(std::string("give bot or guild, not both"), ParseOrder(snap, out));
    CHECK_EQ(std::string(""), ParseOrder(Row("snapshot_all"), out));
    CHECK_EQ(static_cast<int>(OrderType::SnapshotAll), static_cast<int>(out.type));
    CHECK_EQ(std::string("unknown order type 'profession'"), ParseOrder(Row("profession"), out));  // renamed

    OrderRow restore = Row("restore");
    restore.bot = "9";
    CHECK_EQ(std::string("missing field snapshot_id"), ParseOrder(restore, out));
    restore.snapshotId = "42";
    CHECK_EQ(std::string(""), ParseOrder(restore, out));
    CHECK_EQ(static_cast<uint64_t>(42), out.snapshotId);
    OrderRow restoreGuild = Row("restore");
    restoreGuild.guild = "test";
    CHECK_EQ(std::string("missing field taken_before"), ParseOrder(restoreGuild, out));

    // Values that would overflow their field are refused, never wrapped.
    OrderRow big = Row("invite");
    big.bot = "4294967296";
    CHECK_EQ(std::string("bad value for bot"), ParseOrder(big, out));
    big.bot = "4294967295";
    CHECK_EQ(std::string(""), ParseOrder(big, out));
    return UnitFailures();
}
