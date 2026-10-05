// tests/unit/test_payloads.cpp
#include "../../src/Core/EventPayloads.h"
#include "check.h"
#include <clocale>
#include <cmath>

using namespace GuildBridge;

int main()
{
    Where const w{389, 2437, 1.26f, -2.0f, 3.0f};

    CHECK_EQ(std::string("{\"a\":\"q\\\"b\\\\c\\n\",\"n\":-3,\"f\":1.3,\"t\":true,\"l\":[1,2]}"),
             JsonObject().Str("a", "q\"b\\c\n").Int("n", -3).Num("f", 1.26).Bool("t", true)
                 .UIntArray("l", {1, 2}).Build());
    CHECK_EQ(std::string("{\"c\":\"\\u0001\"}"), JsonObject().Str("c", std::string(1, '\x01')).Build());

    CHECK_EQ(std::string("level"), std::string(EventTypeName(EventType::Level)));
    CHECK_EQ(std::string("pvp_kill"), std::string(EventTypeName(EventType::PvpKill)));
    CHECK_EQ(std::string("run_teleport"), std::string(EventTypeName(EventType::RunTeleport)));
    CHECK_EQ(std::string("fake"), std::string(EventTypeName(EventType::Fake)));

    CHECK_EQ(std::string("{\"level\":12,\"old_level\":11,\"class\":1,\"race\":2,\"map\":389,\"zone\":2437,"
                         "\"x\":1.3,\"y\":-2.0,\"z\":3.0}"),
             LevelPayload(12, 11, 1, 2, w));
    CHECK_EQ(std::string("{\"item_entry\":1234,\"item_name\":\"Sabatons\",\"quality\":3,\"item_level\":20,\"count\":1,"
                         "\"source\":\"roll\",\"map\":389,\"zone\":2437,\"x\":1.3,\"y\":-2.0,\"z\":3.0}"),
             LootPayload(1234, "Sabatons", 3, 20, 1, "roll", w));
    CHECK_EQ(std::string("{\"killer_type\":\"creature\",\"killer_entry\":11520,\"killer_guid\":0,\"killer_name\":"
                         "\"Taragaman\",\"killer_level\":16,\"map\":389,\"zone\":2437,\"x\":1.3,\"y\":-2.0,\"z\":3.0}"),
             DeathPayload(DeathInfo{"creature", 11520, 0, "Taragaman", 16, w}));
    CHECK_EQ(std::string("{\"victim_guid\":555,\"victim_name\":\"Bob\",\"victim_level\":30,\"map\":389,\"zone\":2437,"
                         "\"x\":1.3,\"y\":-2.0,\"z\":3.0}"),
             PvpKillPayload(555, "Bob", 30, w));
    CHECK_EQ(std::string("{\"achievement_id\":6,\"name\":\"Level 10\",\"points\":10}"),
             AchievementPayload(6, "Level 10", 10));
    CHECK_EQ(std::string("{\"map\":389,\"map_name\":\"Ragefire Chasm\",\"credit_entry\":11520,\"boss_name\":"
                         "\"Taragaman\",\"difficulty\":0,\"dungeon_completed\":false,\"participants\":[1,2]}"),
             BossKillPayload(389, "Ragefire Chasm", 11520, "Taragaman", 0, false, {1, 2}));
    CHECK_EQ(std::string("{\"map\":389,\"boss_id\":1,\"participants\":[1]}"), BossWipePayload(389, 1, {1}));
    CHECK_EQ(std::string("{\"guild_name\":\"Proving Grounds\",\"rank\":4}"), GuildJoinPayload("Proving Grounds", 4));
    CHECK_EQ(std::string("{\"guild_name\":\"PG\",\"kicked\":true}"), GuildLeavePayload("PG", true));
    CHECK_EQ(std::string("{\"rank\":3,\"promoted\":true}"), GuildRankPayload(3, true));
    CHECK_EQ(std::string("{\"guild_name\":\"PG\",\"faction\":\"horde\",\"leader_name\":\"Testboss\"}"),
             GuildFoundedPayload("PG", "horde", "Testboss"));
    CHECK_EQ(std::string("guild_founded"), std::string(EventTypeName(EventType::GuildFounded)));
    CHECK_EQ(std::string("{\"kind\":\"level\",\"ref\":20,\"label\":\"First to level 20\"}"),
             FirstPayload("level", 20, "First to level 20"));
    CHECK_EQ(std::string("{\"run_id\":17,\"dungeon\":\"rfc\",\"party\":[1,2,3,4,5],\"approach\":\"travel\"}"),
             RunStartPayload(17, "rfc", {1, 2, 3, 4, 5}, "travel"));
    CHECK_EQ(std::string("{\"run_id\":17,\"distance\":812.0}"), RunTravelPayload(17, 812.0f));
    CHECK_EQ(std::string("{\"run_id\":17,\"reason\":\"far\"}"), RunTeleportPayload(17, "far"));
    CHECK_EQ(std::string("{\"run_id\":17,\"killer_name\":\"Spy\",\"pvp\":false}"), RunAmbushPayload(17, "Spy", false));
    CHECK_EQ(std::string("{\"run_id\":17,\"result\":\"wiped\",\"fail_reason\":\"\",\"bosses_killed\":1,"
                         "\"bosses_total\":4,\"duration_s\":1830,\"wipes\":2}"),
             RunEndPayload(17, "wiped", "", 1, 4, 1830, 2));
    CHECK_EQ(std::string("{\"snapshot_id\":42,\"ok\":true,\"reason\":\"\"}"), RestorePayload(42, true, ""));
    CHECK_EQ(std::string("{\"note\":\"test\"}"), FakePayload("test"));
    // UTF-8: valid multi-byte text unchanged; invalid bytes become U+FFFD.
    CHECK_EQ(std::string("{\"n\":\"Zo\xC3\xAB \xE5\x90\x8D\xE5\x89\x8D \xF0\x9F\x98\x80\"}"),
             JsonObject().Str("n", "Zo\xC3\xAB \xE5\x90\x8D\xE5\x89\x8D \xF0\x9F\x98\x80").Build());
    CHECK_EQ(std::string("{\"n\":\"a\xEF\xBF\xBD" "b\"}"), JsonObject().Str("n", "a\xE9" "b").Build());  // Latin-1
    // Cut off 3-byte sequence: each leftover byte becomes one U+FFFD.
    CHECK_EQ(std::string("{\"n\":\"a\xEF\xBF\xBD\xEF\xBF\xBD\"}"), JsonObject().Str("n", "a\xE5\x90").Build());
    // Overlong form.
    CHECK_EQ(std::string("{\"n\":\"\xEF\xBF\xBD\xEF\xBF\xBD\"}"), JsonObject().Str("n", "\xC0\x80").Build());
    CHECK_EQ(std::string("{\"n\":\"\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD\"}"),
             JsonObject().Str("n", "\xED\xA0\x80").Build());  // surrogate
    // Other escapes: \r \t NUL, 0x1f escaped, 0x7f not, escaped key, empty array.
    CHECK_EQ(std::string("{\"k\\\"\":\"\\r\\t\\u0000\\u001f\x7f\",\"e\":[]}"),
             JsonObject().Str("k\"", std::string("\r\t\0\x1f\x7f", 5)).UIntArray("e", {}).Build());
    // Numbers: non-finite becomes null; rounding to one decimal; negative.
    CHECK_EQ(std::string("{\"a\":null,\"b\":null,\"c\":null,\"d\":-0.5,\"e\":1.3}"),
             JsonObject().Num("a", std::nan("")).Num("b", HUGE_VAL).Num("c", -HUGE_VAL).Num("d", -0.5)
                 .Num("e", 1.26f).Build());
    // Comma-decimal locale must not change the output (skipped silently if the locale is not installed).
    if (std::setlocale(LC_NUMERIC, "de_DE.UTF-8"))
        CHECK_EQ(std::string("{\"f\":1.3}"), JsonObject().Num("f", 1.26).Build());
    // EventTypeName: all 19 in order; out of range is safe.
    char const* const expected[] = {"level", "loot", "death", "pvp_kill", "achievement", "boss_kill", "boss_wipe",
                                    "guild_join", "guild_leave", "guild_rank", "guild_founded", "first", "run_start",
                                    "run_travel", "run_teleport", "run_ambush", "run_end", "restore", "fake"};
    for (int i = 0; i < 19; ++i)
        CHECK_EQ(std::string(expected[i]), std::string(EventTypeName(static_cast<EventType>(i))));
    CHECK_EQ(std::string("?"), std::string(EventTypeName(EventType::Count)));
    CHECK_EQ(std::string("?"), std::string(EventTypeName(static_cast<EventType>(200))));
    return UnitFailures();
}
