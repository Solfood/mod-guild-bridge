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
    // No tank at all: refused.
    p = ArrangeParty({seat("Arms", 1, 0), seat("Hal", 11, 2), seat("Cat", 11, 1, 30, true), seat("Dan", 8, 0),
                      seat("Dot", 8, 0)});
    CHECK_EQ(std::string("no tank in this party: no member has a tank spec"), p.problem);
    return UnitFailures();
}
