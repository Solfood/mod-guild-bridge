/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 */

#pragma once

// When the bridge makes its legends view on the world's playerbots database (pure logic, unit-tested).
namespace GuildBridge
{
enum class LegendsViewWhen
{
    Now,               // while the guildmaster database loads: a problem stops the boot at once
    AfterAllDatabases  // once every module database is loaded (a fresh world's first boot)
};

// Modules load their databases in name order, so mod-playerbots creates a fresh world's playerbots database after the
// bridge's hook has run. When its raisings table does not exist yet, the view waits until all databases are loaded.
inline LegendsViewWhen LegendsViewTiming(bool raisingsTableExists)
{
    return raisingsTableExists ? LegendsViewWhen::Now : LegendsViewWhen::AfterAllDatabases;
}

inline char const* LegendsViewWhenName(LegendsViewWhen w)
{
    return w == LegendsViewWhen::Now ? "now" : "after all databases";
}
}  // namespace GuildBridge
