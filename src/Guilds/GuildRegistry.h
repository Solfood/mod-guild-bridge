/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 */

#ifndef MOD_GUILD_BRIDGE_GUILDREGISTRY_H
#define MOD_GUILD_BRIDGE_GUILDREGISTRY_H

#include "Define.h"
#include "ObjectGuid.h"
#include <atomic>
#include <functional>
#include <optional>
#include <string>

enum class GuildRole : uint8
{
    None,
    User,
    Test
};

// The two guilds the bridge manages. RoleOf/GuildIdFor/IsTestAccount are lock-free and safe from map threads.
// Holds: none. A leader is never a population bot (BeginCreate refuses one), so the population manager never
// touches it; a restart mid-creation leaves no guild and no row, and creation can simply be asked for again.
class GuildRegistry
{
public:
    static GuildRegistry& Instance();
    void LoadAtStartup();  // OnStartup only (sync queries before the world runs)
    GuildRole RoleOf(uint32 guildId) const;
    uint32 GuildIdFor(GuildRole role) const;
    bool IsTestAccount(uint32 accountId) const { return accountId && accountId == _testAccount.load(); }
    // A registered guild's leader (world thread): `testbots login` leaves it offline (spec §4.8).
    bool IsRegisteredLeader(ObjectGuid guid) const;
    static char const* RoleName(GuildRole role);
    static GuildRole RoleFromName(std::string const& name);

    using Created = std::function<void(uint32 guildId, std::string const& error)>;
    // World thread. "" when creation started; else why not (then `done` is never called).
    std::string BeginCreate(GuildRole role, std::string const& name, std::string const& leaderName, Created done = {});
    void Update(uint32 diff);

private:
    struct Pending
    {
        GuildRole role = GuildRole::None;
        std::string name;
        ObjectGuid leader;
        uint32 waitedMs = 0;
        bool weLoggedIn = false;
        Created done;
    };

    void Register(GuildRole role, uint32 guildId, std::string const& name, std::string const& faction,
                  uint32 leaderGuid);

    std::atomic<uint32> _user{0};
    std::atomic<uint32> _test{0};
    std::atomic<uint32> _testAccount{0};
    std::optional<Pending> _pending;
    // Guild::Create writes the guild row asynchronously; playerbots' cache reads that table, so it is read
    // again once the write has surely landed (until then a new guild may not count as "real").
    uint32 _revalidateInMs = 0;
};

#endif
