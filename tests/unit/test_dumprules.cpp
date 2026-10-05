// tests/unit/test_dumprules.cpp
#include "../../src/Core/DumpRules.h"
#include "check.h"

using namespace GuildBridge;

int main()
{
    std::string const dump =
        "IMPORTANT NOTE: THIS DUMPFILE IS MADE FOR USE WITH THE 'PDUMP' COMMAND ONLY\n"
        "INSERT INTO `characters` (`guid`, `account`) VALUES ('1631', '7');\n"
        "INSERT INTO `mail` (`id`, `messageType`, `stationery`) VALUES ('19', '0', '61');\n"
        "INSERT INTO `mail_items` (`mail_id`, `item_guid`, `receiver`) VALUES ('19', '4915300', '1631');\n"
        "INSERT INTO `mail` (`id`, `messageType`, `stationery`) VALUES ('4294967290', '0', '41');\n"
        "INSERT INTO `mailbox` (`id`) VALUES ('5');\n";
    std::vector<uint64_t> const ids = DumpMailIds(dump);
    CHECK_EQ(static_cast<std::size_t>(2), ids.size());
    CHECK_EQ(static_cast<uint64_t>(19), ids[0]);
    CHECK_EQ(static_cast<uint64_t>(4294967290u), ids[1]);
    CHECK_EQ(static_cast<std::size_t>(0), DumpMailIds("INSERT INTO `characters` VALUES ('1');\n").size());
    CHECK_EQ(static_cast<std::size_t>(0), DumpMailIds("").size());
    // A broken mail line is skipped, never read as id 0.
    CHECK_EQ(static_cast<std::size_t>(0), DumpMailIds("INSERT INTO `mail` (`id`) VALUES ('x');").size());
    CHECK_EQ(std::string("19,4294967290"), JoinIds(ids));
    return UnitFailures();
}
