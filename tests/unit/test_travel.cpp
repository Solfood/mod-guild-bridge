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

    CHECK_EQ(static_cast<std::size_t>(0), RoleWarnings({1, 5, 3, 4, 8}).size());
    std::vector<std::string> const w = RoleWarnings({3, 4, 1, 1, 1});
    CHECK_EQ(static_cast<std::size_t>(2), w.size());
    CHECK_EQ(std::string("the tank slot has no tank class"), w[0]);
    CHECK_EQ(std::string("the healer slot has no healing class"), w[1]);

    // Roles as mod-dungeon-clear reads them (by talent spec): slot 0 must read tank, slot 1 heal.
    using Seat = std::pair<std::string, std::string>;
    CHECK_EQ(std::string(""), RoleProblem({Seat{"Tom", "tank"}, Seat{"Hal", "heal"}, Seat{"Dee", "dps"},
                                           Seat{"Dan", "dps"}, Seat{"Dot", "dps"}}));
    CHECK_EQ(std::string("no tank in this party: Tom has no tank spec"),
             RoleProblem({Seat{"Tom", "dps"}, Seat{"Hal", "heal"}, Seat{"Dee", "tank"}, Seat{"Dan", "dps"},
                          Seat{"Dot", "dps"}}));
    CHECK_EQ(std::string("no healer in this party: Hal has no healer spec"),
             RoleProblem({Seat{"Tom", "tank"}, Seat{"Hal", "dps"}, Seat{"Dee", "heal"}, Seat{"Dan", "dps"},
                          Seat{"Dot", "dps"}}));
    CHECK_EQ(std::string("no tank in this party: Tom has no tank spec"),  // the tank is named first
             RoleProblem({Seat{"Tom", "heal"}, Seat{"Hal", "tank"}, Seat{"Dee", "dps"}, Seat{"Dan", "dps"},
                          Seat{"Dot", "dps"}}));
    CHECK_EQ(std::string(""), RoleProblem({}));
    return UnitFailures();
}
