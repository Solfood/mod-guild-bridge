// tests/unit/test_travel.cpp
#include "../../src/Core/TravelRules.h"
#include "check.h"

using namespace GuildBridge;

int main()
{
    Spot const entrance{1, 1800.f, -4400.f, -15.f};  // Ragefire Chasm, Orgrimmar
    std::vector<Spot> near = {{1, 1700.f, -4400.f, 0.f}, {1, 1650.f, -4300.f, 0.f}};
    std::vector<Spot> mixed = {{1, 1700.f, -4400.f, 0.f}, {0, 0.f, 0.f, 0.f}};
    std::vector<Spot> farOff = {{1, 1800.f, -2000.f, 0.f}};

    ApproachChoice c = ChooseApproach(near, entrance, 1500.f, Approach::Auto);
    CHECK_EQ(static_cast<int>(Approach::Travel), static_cast<int>(c.approach));
    CHECK_TRUE(c.distance > 180.f && c.distance < 185.f);
    c = ChooseApproach(mixed, entrance, 1500.f, Approach::Auto);
    CHECK_EQ(static_cast<int>(Approach::Teleport), static_cast<int>(c.approach));
    CHECK_EQ(std::string("far"), c.reason);
    c = ChooseApproach(farOff, entrance, 1500.f, Approach::Auto);
    CHECK_EQ(std::string("far"), c.reason);
    c = ChooseApproach(farOff, entrance, 1500.f, Approach::Travel);  // asked to travel, same continent: travel
    CHECK_EQ(static_cast<int>(Approach::Travel), static_cast<int>(c.approach));
    c = ChooseApproach(mixed, entrance, 1500.f, Approach::Travel);   // other continent: cannot walk there
    CHECK_EQ(std::string("far"), c.reason);
    c = ChooseApproach(near, entrance, 1500.f, Approach::Teleport);
    CHECK_EQ(std::string("requested"), c.reason);
    c = ChooseApproach(near, Spot{389, 0.f, 0.f, 0.f}, 1500.f, Approach::Auto);  // entrance not on a continent
    CHECK_EQ(std::string("far"), c.reason);

    LevelBand const band = BandFor(17, 5, 5);
    CHECK_EQ(12u, band.min);
    CHECK_EQ(22u, band.max);
    CHECK_EQ(1u, BandFor(3, 5, 5).min);
    CHECK_EQ(std::string(""), LevelProblem({{"Ann", 12}, {"Bob", 22}}, band));
    CHECK_EQ(std::string("level out of range: Cid is 9 (12-22)"), LevelProblem({{"Ann", 15}, {"Cid", 9}}, band));

    // Roles as playerbots' strategy assignment (what mod-dungeon-clear's leader election reads) makes them.
    // cls, tab: 1 warrior (2 prot), 2 paladin (0 holy, 1 prot), 5 priest (2 shadow), 8 mage, 11 druid (1 feral, 2 resto).
    auto seat = [](std::string n, uint8_t cls, uint8_t tab, uint32_t level = 15, bool cat = false, bool hide = false) {
        return SeatSpec{n, cls, tab, level, cat, hide};
    };
    CHECK_TRUE(ReadsAsTank(seat("Bear", 11, 1, 15)));                     // low-level feral, no Cat Form: tank
    CHECK_TRUE(ReadsAsTank(seat("Bear", 11, 1, 15, true)));               // below 20 the non-combat tank assist counts
    CHECK_TRUE(!ReadsAsTank(seat("Cat", 11, 1, 30, true)));               // a cat at 30: dps
    CHECK_TRUE(ReadsAsTank(seat("Hide", 11, 1, 30, true, true)));         // Thick Hide: tank, whatever its form
    CHECK_TRUE(!ReadsAsTank(seat("Arms", 1, 0)));                         // talentless warrior = arms
    CHECK_TRUE(ReadsAsTank(seat("Prot", 2, 1)));
    CHECK_TRUE(ReadsAsHealer(seat("Holy", 2, 0)) && !ReadsAsHealer(seat("Shadow", 5, 2)));

    PartyPlan p = ArrangeParty({seat("Tom", 1, 2), seat("Hal", 11, 2), seat("Dee", 8, 0), seat("Dan", 8, 0),
                                seat("Dot", 8, 0)});
    CHECK_EQ(std::string(""), p.problem);
    CHECK_EQ(static_cast<std::size_t>(0), p.warnings.size());
    CHECK_TRUE((p.order == std::vector<std::size_t>{0, 1, 2, 3, 4}));
    // The tank in seat 3 and the healer in seat 5: reordered, accepted, with a warning naming the new order.
    p = ArrangeParty({seat("Dee", 8, 0), seat("Dan", 8, 0), seat("Bear", 11, 1), seat("Dot", 8, 0), seat("Pri", 5, 1)});
    CHECK_EQ(std::string(""), p.problem);
    CHECK_TRUE((p.order == std::vector<std::size_t>{2, 4, 0, 1, 3}));
    CHECK_EQ(static_cast<std::size_t>(1), p.warnings.size());
    CHECK_EQ(std::string("party reordered: tank Bear, healer Pri"), p.warnings[0]);
    // No healer: accepted (dungeon-clear needs none), with a warning.
    p = ArrangeParty({seat("Tom", 1, 2), seat("Dee", 8, 0), seat("Dan", 8, 0), seat("Dot", 8, 0), seat("Don", 8, 0)});
    CHECK_EQ(std::string(""), p.problem);
    CHECK_TRUE((p.order == std::vector<std::size_t>{0, 1, 2, 3, 4}));
    CHECK_EQ(std::string("no healer in this party"), p.warnings.at(0));
    CHECK_EQ(static_cast<std::size_t>(1), p.warnings.size());
    // A tank-and-healer pair given the wrong way round is swapped, not refused.
    p = ArrangeParty({seat("Hal", 11, 2), seat("Tom", 1, 2), seat("Dee", 8, 0), seat("Dan", 8, 0), seat("Dot", 8, 0)});
    CHECK_EQ(std::string(""), p.problem);
    CHECK_TRUE((p.order == std::vector<std::size_t>{1, 0, 2, 3, 4}));
    // Role assignment (GM-BRIDGE-0073): roles by what the CLASS can do; the current spec preferred when it fits.
    // No class that can tank (priest, mage, shaman, rogue, hunter): refused.
    p = ArrangeParty({seat("Pri", 5, 1), seat("Hal", 7, 2), seat("Mia", 8, 0), seat("Dan", 8, 0), seat("Rue", 4, 0)});
    CHECK_EQ(std::string("no tank in this party: no member can tank"), p.problem);
    // A druid below level 10 has no Bear Form: it cannot tank.
    p = ArrangeParty({seat("Pri", 5, 1), seat("Kit", 11, 1, 8), seat("Mia", 8, 0), seat("Dan", 8, 0), seat("Rue", 4, 0)});
    CHECK_EQ(std::string("no tank in this party: no member can tank"), p.problem);
    // An arms warrior is a tank by class (switching from dps); the resto druid already heals.
    p = ArrangeParty({seat("Arms", 1, 0), seat("Hal", 11, 2), seat("Cat", 11, 1, 30, true), seat("Dan", 8, 0),
                      seat("Dot", 8, 0)});
    CHECK_EQ(std::string(""), p.problem);
    CHECK_TRUE((p.order == std::vector<std::size_t>{0, 1, 2, 3, 4}));
    CHECK_TRUE(p.roles[0] == Role::Tank && p.roles[1] == Role::Heal && p.roles[2] == Role::Dps);
    CHECK_TRUE(p.switching[0] && !p.switching[1]);
    CHECK_EQ(static_cast<std::size_t>(1), p.warnings.size());
    CHECK_EQ(std::string("Arms tanks (warrior, switching from dps)"), p.warnings.at(0));
    // All-dps hybrids: warrior + paladin (ret) + priest (shadow): the warrior tanks (class preference, one switch each
    // way), the priest heals. Nobody fits, so the preference decides.
    p = ArrangeParty({seat("Ret", 2, 2), seat("Shad", 5, 2), seat("War", 1, 0), seat("Mia", 8, 0), seat("Dan", 8, 0)});
    CHECK_TRUE(p.roles[2] == Role::Tank && p.roles[1] == Role::Heal && p.roles[0] == Role::Dps);
    CHECK_TRUE((p.order == std::vector<std::size_t>{2, 1, 0, 3, 4}));
    CHECK_EQ(static_cast<std::size_t>(3), p.warnings.size());  // War tanks, Shad heals, reordered
    CHECK_EQ(std::string("War tanks (warrior, switching from dps)"), p.warnings.at(0));
    CHECK_EQ(std::string("Shad heals (priest, switching from dps)"), p.warnings.at(1));
    // Two possible tanks: the warrior beats the paladin by class preference...
    p = ArrangeParty({seat("Pal", 2, 2), seat("War", 1, 0), seat("Dee", 8, 0), seat("Dan", 8, 0), seat("Dot", 8, 0)});
    CHECK_TRUE(p.roles[1] == Role::Tank && p.roles[0] == Role::Heal);  // ...and the paladin heals (one switch fewer than nothing)
    // ...but a protection paladin that already tanks keeps the job over a dps warrior (current spec preferred).
    p = ArrangeParty({seat("War", 1, 0), seat("Prot", 2, 1), seat("Dee", 8, 0), seat("Pri", 5, 0), seat("Dot", 8, 0)});
    CHECK_TRUE(p.roles[1] == Role::Tank && p.roles[3] == Role::Heal && p.roles[0] == Role::Dps);
    CHECK_TRUE(!p.switching[1] && !p.switching[3]);
    // Tank and heal only possible from one paladin: it tanks, no healer, a warning (not a refusal).
    p = ArrangeParty({seat("Pal", 2, 2), seat("Dee", 8, 0), seat("Dan", 8, 0), seat("Dot", 8, 0), seat("Rue", 4, 0)});
    CHECK_EQ(std::string(""), p.problem);
    CHECK_TRUE(p.roles[0] == Role::Tank);
    CHECK_EQ(std::string("Pal tanks (paladin, switching from dps)"), p.warnings.at(0));
    CHECK_EQ(std::string("no healer in this party"), p.warnings.at(1));
    // A paladin that heals now keeps healing when a druid can tank (a druid at 30 with Cat Form: bear is a switch).
    p = ArrangeParty({seat("Holy", 2, 0), seat("Cat", 11, 1, 30, true), seat("Dee", 8, 0), seat("Dan", 8, 0), seat("Dot", 8, 0)});
    CHECK_TRUE(p.roles[1] == Role::Tank && p.roles[0] == Role::Heal);
    CHECK_TRUE(p.switching[1] && !p.switching[0]);
    CHECK_EQ(std::string("Cat tanks (druid, switching from dps)"), p.warnings.at(0));
    // Druid: a feral-tank druid tanks and a priest heals (nobody switches); a lone resto druid with a dps warrior:
    // the warrior tanks, the druid heals.
    p = ArrangeParty({seat("Bear", 11, 1, 15), seat("Pri", 5, 1), seat("Dee", 8, 0), seat("Dan", 8, 0), seat("Dot", 8, 0)});
    CHECK_TRUE(p.roles[0] == Role::Tank && p.roles[1] == Role::Heal && !p.switching[0] && !p.switching[1]);
    CHECK_EQ(static_cast<std::size_t>(0), p.warnings.size());
    p = ArrangeParty({seat("Dee", 8, 0), seat("Resto", 11, 2), seat("War", 1, 0), seat("Dan", 8, 0), seat("Dot", 8, 0)});
    CHECK_TRUE(p.roles[2] == Role::Tank && p.roles[1] == Role::Heal);
    // Death knight: blood tanks as is; a frost DK is the tank of last resort (after warrior, paladin, druid).
    p = ArrangeParty({seat("Dk", 6, 0), seat("Pri", 5, 1), seat("Dee", 8, 0), seat("Dan", 8, 0), seat("Dot", 8, 0)});
    CHECK_TRUE(p.roles[0] == Role::Tank && !p.switching[0]);
    p = ArrangeParty({seat("Dk", 6, 1), seat("Pri", 5, 1), seat("Dee", 8, 0), seat("Dan", 8, 0), seat("Dot", 8, 0)});
    CHECK_TRUE(p.roles[0] == Role::Tank && p.switching[0]);
    CHECK_EQ(std::string("Dk tanks (death knight, switching from dps)"), p.warnings.at(0));
    // Equal choices: the lowest guid (mod-dungeon-clear's own tie-break).
    SeatSpec w1 = seat("W1", 1, 2), w2 = seat("W2", 1, 2);
    w1.guid = 20;
    w2.guid = 10;
    p = ArrangeParty({w1, w2, seat("Dee", 8, 0), seat("Dan", 8, 0), seat("Dot", 8, 0)});
    CHECK_TRUE(p.roles[1] == Role::Tank && p.roles[0] == Role::Dps);
    // The summons: dead beats fighting; a healthy member gets no line.
    CHECK_EQ(std::string("Buddy is fighting: the summons pulls them out"), SummonWarning("Buddy", false, true, true));
    CHECK_EQ(std::string("John is dead: the summons revives them"), SummonWarning("John", true, false, true));
    CHECK_EQ(std::string("John was dead: the summons revived them"), SummonWarning("John", true, true, false));
    CHECK_EQ(std::string("Buddy was fighting: the summons pulled them out"), SummonWarning("Buddy", false, true, false));
    CHECK_EQ(std::string(""), SummonWarning("Pip", false, false, false));
    return UnitFailures();
}
