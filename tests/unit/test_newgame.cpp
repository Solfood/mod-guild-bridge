// tests/unit/test_newgame.cpp
#include "../../src/Core/NewGameRules.h"
#include "../../src/Core/OrderRules.h"
#include "check.h"

using namespace GuildBridge;

static FounderRow MakeRow(std::string name, std::string race, std::string cls)
{
    FounderRow row;
    row.name = std::move(name);
    row.race = std::move(race);
    row.cls = std::move(cls);
    row.profCount = "0";
    row.traitsType = "ARRAY";
    row.traits = "[\"Ambitious\", \"Loyal\"]";
    row.backstory = "Left the farm to see the world.";
    return row;
}

int main()
{
    // Names: 2-12 letters, normalised like the game does.
    CHECK_EQ(std::string("Tfounda"), NormalizeName("tFOUNDA"));
    CHECK_EQ(std::string(""), NormalizeName("A"));
    CHECK_EQ(std::string(""), NormalizeName("Bob2"));
    CHECK_EQ(std::string(""), NormalizeName("Thirteenchars"));

    // Factions and race/class pairs (WotLK).
    Faction f = Faction::Alliance;
    CHECK_TRUE(RaceFaction(6, f) && f == Faction::Horde);    // Tauren
    CHECK_TRUE(RaceFaction(11, f) && f == Faction::Alliance);  // Draenei
    CHECK_TRUE(!RaceFaction(9, f));                            // no race 9 in 3.3.5
    CHECK_TRUE(IsPlayableCombo(6, 11));                        // Tauren druid
    CHECK_TRUE(!IsPlayableCombo(6, 8));                        // no Tauren mage
    CHECK_EQ(1, static_cast<int>(DefaultLeaderRace(Faction::Alliance)));
    CHECK_EQ(2, static_cast<int>(DefaultLeaderRace(Faction::Horde)));

    // Trait arrays as MySQL prints them.
    std::vector<std::string> traits;
    CHECK_TRUE(ParseStringArray("[\"Ambitious\", \"Lo\\\"yal\"]", traits));
    CHECK_EQ(static_cast<std::size_t>(2), traits.size());
    CHECK_EQ(std::string("Lo\"yal"), traits[1]);
    CHECK_TRUE(!ParseStringArray("[\"a\", 3]", traits));
    CHECK_TRUE(!ParseStringArray("{\"a\":1}", traits));

    // A good party of five Horde founders.
    std::vector<FounderRow> rows = {MakeRow("tfounda", "2", "1"), MakeRow("Tfoundb", "8", "7"), MakeRow("Tfoundc", "5", "8"),
                                    MakeRow("Tfoundd", "10", "3"), MakeRow("Tfounde", "6", "11")};
    rows[0].profCount = "2";
    rows[0].prof1 = "186";
    rows[0].prof2 = "393";
    rows[1].gender = "female";
    std::vector<GuildBridge::Founder> out;
    CHECK_EQ(std::string(""), ParseFounders(rows, Faction::Horde, out));
    CHECK_EQ(static_cast<std::size_t>(5), out.size());
    CHECK_EQ(std::string("Tfounda"), out[0].name);
    CHECK_EQ(186, static_cast<int>(out[0].prof1));
    CHECK_EQ(1, static_cast<int>(out[1].gender));
    CHECK_EQ(2, static_cast<int>(out[2].gender));  // not given: random
    CHECK_EQ(std::string("[\"Ambitious\",\"Loyal\"]"), out[0].traitsJson);

    // Each refusal names the founder; nothing is accepted partly.
    std::vector<FounderRow> bad = rows;
    bad[2].race = "1";  // a Human in a Horde guild
    CHECK_EQ(std::string("wrong faction: Tfoundc"), ParseFounders(bad, Faction::Horde, out));
    CHECK_EQ(static_cast<std::size_t>(0), out.size());
    bad = rows;
    bad[3].cls = "6";
    CHECK_EQ(std::string("bad value for class: Tfoundd (death knights come only from the raisings)"),
             ParseFounders(bad, Faction::Horde, out));
    bad = rows;
    bad[4].cls = "8";  // Tauren mage
    CHECK_EQ(std::string("race and class don't match: Tfounde"), ParseFounders(bad, Faction::Horde, out));
    bad = rows;
    bad[1].name = "TFOUNDA";
    CHECK_EQ(std::string("name taken: Tfounda (twice in this order)"), ParseFounders(bad, Faction::Horde, out));
    bad = rows;
    bad[1].name = "X";
    CHECK_EQ(std::string("bad name: X"), ParseFounders(bad, Faction::Horde, out));
    bad = rows;
    bad[0].traitsType = "OBJECT";
    CHECK_EQ(std::string("bad value for traits: Tfounda"), ParseFounders(bad, Faction::Horde, out));
    bad = rows;
    bad[0].traits = "[\"a\", \"b\", \"c\", \"d\", \"e\", \"f\"]";
    CHECK_EQ(std::string("bad value for traits: Tfounda"), ParseFounders(bad, Faction::Horde, out));
    bad = rows;
    bad[0].backstory = std::string(256, 'x');
    CHECK_EQ(std::string("bad value for backstory: Tfounda"), ParseFounders(bad, Faction::Horde, out));
    bad = rows;
    bad[0].prof2 = "186";
    CHECK_EQ(std::string("bad value for professions: Tfounda"), ParseFounders(bad, Faction::Horde, out));
    CHECK_EQ(std::string("bad value for founders (1 to 5)"), ParseFounders({}, Faction::Horde, out));

    // Founder accounts (preflight C1): the per-world list, one founder per account, emptiest first, full ones skipped.
    std::vector<uint32_t> picked;
    CHECK_TRUE(PickFounderAccounts({{11, 0}, {12, 3}, {13, 10}, {14, 1}}, 10, 3, picked));
    CHECK_EQ(static_cast<std::size_t>(3), picked.size());
    CHECK_EQ(11u, picked[0]);
    CHECK_EQ(14u, picked[1]);
    CHECK_EQ(12u, picked[2]);
    CHECK_TRUE(!PickFounderAccounts({{11, 0}, {13, 10}}, 10, 2, picked));  // one free account, two founders
    CHECK_EQ(static_cast<std::size_t>(0), picked.size());
    CHECK_TRUE(!PickFounderAccounts({}, 10, 1, picked));

    // The order shapes.
    OrderRow guild;
    guild.id = 3;
    guild.type = "create_guild";
    ParsedOrder p;
    CHECK_EQ(std::string("missing field faction"), ParseOrder(guild, p));
    guild.faction = "scourge";
    CHECK_EQ(std::string("bad value for faction"), ParseOrder(guild, p));
    guild.faction = "horde";
    CHECK_EQ(std::string("missing field guild_name"), ParseOrder(guild, p));
    guild.guildName = "Proving Grounds";
    guild.leaderName = "testBOSS";
    CHECK_EQ(std::string(""), ParseOrder(guild, p));
    CHECK_EQ(std::string("Testboss"), p.leaderName);
    CHECK_EQ(std::string("user"), p.role);
    guild.leaderRace = "1";
    CHECK_EQ(std::string("bad value for leader_race"), ParseOrder(guild, p));  // a Human cannot lead a Horde guild
    guild.leaderRace = "";
    guild.guildName = "Proving_Grounds!";
    CHECK_EQ(std::string("bad value for guild_name"), ParseOrder(guild, p));

    OrderRow founders;
    founders.id = 4;
    founders.type = "create_founders";
    CHECK_EQ(std::string("missing field guild_id"), ParseOrder(founders, p));
    founders.guildId = "7";
    CHECK_EQ(std::string("missing field founders"), ParseOrder(founders, p));
    founders.foundersLength = "6";
    CHECK_EQ(std::string("bad value for founders (1 to 5)"), ParseOrder(founders, p));
    founders.foundersLength = "5";
    CHECK_EQ(std::string(""), ParseOrder(founders, p));
    CHECK_EQ(5u, p.founderCount);
    return UnitFailures();
}
