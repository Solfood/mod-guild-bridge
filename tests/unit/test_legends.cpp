// tests/unit/test_legends.cpp
#include "../../src/Core/LegendsTiming.h"
#include "check.h"

using namespace GuildBridge;

int main()
{
    // A world that booted before has its playerbots database: the view is made at once, so a problem stops the boot.
    CHECK_TRUE(LegendsViewTiming(true) == LegendsViewWhen::Now);
    // A fresh world: mod-playerbots creates its database after the bridge's hook, so the view waits for it.
    CHECK_TRUE(LegendsViewTiming(false) == LegendsViewWhen::AfterAllDatabases);
    CHECK_EQ(std::string("now"), std::string(LegendsViewWhenName(LegendsViewWhen::Now)));
    CHECK_EQ(std::string("after all databases"), std::string(LegendsViewWhenName(LegendsViewWhen::AfterAllDatabases)));
    return UnitFailures();
}
