/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 */

#include "GuildRegistry.h"

#include "AccountMgr.h"
#include "BridgeConfig.h"
#include "CharacterCache.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "GuildmasterDatabase.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerbotsAdapter.h"
#include <ctime>

namespace
{
constexpr uint32 LeaderLoginTimeoutMs = 60000;
constexpr uint32 RevalidateDelayMs = 5000;
constexpr std::size_t MaxGuildNameLength = 24;  // guilds.name VARCHAR(24), the core's limit too
}  // namespace

GuildRegistry& GuildRegistry::Instance()
{
    static GuildRegistry instance;
    return instance;
}

void GuildRegistry::LoadAtStartup()
{
    _testAccount = AccountMgr::GetId(BridgeConfig::Get().testAccount);
    if (GuildmasterDatabaseReady)
    {
        if (QueryResult result = GuildmasterDatabase.Query("SELECT guild_id, role FROM guilds"))
            do
            {
                Field* fields = result->Fetch();
                uint32 const id = fields[0].Get<uint32>();
                GuildRole const role = RoleFromName(fields[1].Get<std::string>());
                // A guild disbanded behind the bridge's back is forgotten, row and all, so the role can be
                // founded again.
                if (!sGuildMgr->GetGuildById(id))
                {
                    LOG_ERROR("module.guildbridge", "GUILDBRIDGE {} guild {} no longer exists; forgetting it",
                              RoleName(role), id);
                    GuildmasterDatabase.Execute("DELETE FROM guilds WHERE guild_id = {}", id);
                    continue;
                }
                if (role == GuildRole::User)
                    _user = id;
                else if (role == GuildRole::Test)
                    _test = id;
            } while (result->NextRow());
    }
    if (_user.load())
        PlayerbotsAdapter::SetRaisingsUserGuild(_user.load());
    LOG_INFO("module.guildbridge", "GUILDBRIDGE guilds user={} test={} testAccount={}", _user.load(), _test.load(),
             _testAccount.load());
}

GuildRole GuildRegistry::RoleOf(uint32 guildId) const
{
    if (!guildId)
        return GuildRole::None;
    if (guildId == _user.load())
        return GuildRole::User;
    if (guildId == _test.load())
        return GuildRole::Test;
    return GuildRole::None;
}

uint32 GuildRegistry::GuildIdFor(GuildRole role) const
{
    return role == GuildRole::User ? _user.load() : role == GuildRole::Test ? _test.load() : 0;
}

bool GuildRegistry::IsRegisteredLeader(ObjectGuid guid) const
{
    for (uint32 id : {_user.load(), _test.load()})
        if (Guild* guild = id ? sGuildMgr->GetGuildById(id) : nullptr)
            if (guild->GetLeaderGUID() == guid)
                return true;
    return _pending && _pending->leader == guid;
}

char const* GuildRegistry::RoleName(GuildRole role)
{
    return role == GuildRole::User ? "user" : role == GuildRole::Test ? "test" : "none";
}

GuildRole GuildRegistry::RoleFromName(std::string const& name)
{
    return name == "user" ? GuildRole::User : name == "test" ? GuildRole::Test : GuildRole::None;
}

std::string GuildRegistry::BeginCreate(GuildRole role, std::string const& name, std::string const& leaderName,
                                       Created done)
{
    if (role == GuildRole::None)
        return "role must be user or test";
    if (!GuildmasterDatabaseReady)
        return "the guildmaster database is not ready";
    if (GuildIdFor(role))
        return std::string("a ") + RoleName(role) + " guild already exists";
    if (_pending)
        return "another guild is being created";
    if (name.empty() || name.size() > MaxGuildNameLength || !ObjectMgr::IsValidCharterName(name))
        return "not a valid guild name";
    if (sGuildMgr->GetGuildByName(name))
        return "a guild with that name exists";
    std::string leader = leaderName;
    normalizePlayerName(leader);
    ObjectGuid const guid = sCharacterCache->GetCharacterGuidByName(leader);
    CharacterCacheEntry const* cache = guid ? sCharacterCache->GetCharacterCacheByGuid(guid) : nullptr;
    if (!cache)
        return "no character named " + leader;
    if (cache->GuildId)
        return leader + " is already in a guild";
    // playerbots counts a guild as "real" (never auto-filled, members stay) only when its leader's account is
    // not a bot account.
    if (PlayerbotsAdapter::IsRandomBot(guid.GetCounter()) || PlayerbotsAdapter::IsBotAccount(cache->AccountId))
        return "the leader must be on a normal account, not a bot account";

    Pending pending;
    pending.role = role;
    pending.name = name;
    pending.leader = guid;
    pending.done = std::move(done);
    if (!ObjectAccessor::FindConnectedPlayer(guid))
    {
        // The fork keeps a masterless non-population character out of playerbots' bot guilds (preflight C4).
        PlayerbotsAdapter::LoginMasterless(guid);
        pending.weLoggedIn = true;
    }
    _pending = std::move(pending);
    return "";
}

void GuildRegistry::Register(GuildRole role, uint32 guildId, std::string const& name, std::string const& faction,
                             uint32 leaderGuid)
{
    (role == GuildRole::User ? _user : _test) = guildId;
    if (role == GuildRole::User)
        PlayerbotsAdapter::SetRaisingsUserGuild(guildId);  // spec §3b: raisings limit the user's guild per wave
    GuildmasterPreparedStatement* stmt = GuildmasterDatabase.GetPreparedStatement(GM_REP_GUILD);
    stmt->SetData(0, guildId);
    stmt->SetData(1, std::string(RoleName(role)));
    stmt->SetData(2, name);
    stmt->SetData(3, faction);
    stmt->SetData(4, leaderGuid);
    stmt->SetData(5, static_cast<uint32>(std::time(nullptr)));
    GuildmasterDatabase.Execute(stmt);
}

void GuildRegistry::Update(uint32 diff)
{
    if (_revalidateInMs)
    {
        _revalidateInMs = diff >= _revalidateInMs ? 0 : _revalidateInMs - diff;
        if (!_revalidateInMs)
            PlayerbotsAdapter::ValidateGuildCache();
    }
    if (_lateLogout && PlayerbotsAdapter::IsMasterlessLoggedIn(_lateLogout))
    {
        PlayerbotsAdapter::Logout(_lateLogout);
        _lateLogout.Clear();
    }
    if (!_pending)
        return;
    Pending& pending = *_pending;
    pending.waitedMs += diff;
    Player* leader = ObjectAccessor::FindPlayer(pending.leader);
    // A leader we logged in must also have finished playerbots' login, or the logout below would miss it.
    bool const ready = leader && leader->IsInWorld() &&
                       (!pending.weLoggedIn || PlayerbotsAdapter::IsMasterlessLoggedIn(pending.leader));
    if (!ready)
    {
        if (pending.waitedMs > LeaderLoginTimeoutMs)
        {
            LOG_ERROR("module.guildbridge", "GUILDBRIDGE guild {}: the leader never came online", pending.name);
            if (pending.weLoggedIn)
                _lateLogout = pending.leader;  // its login may still finish: log it out then
            Created done = std::move(pending.done);
            _pending.reset();
            if (done)
                done(0, "the leader never came online");
        }
        return;
    }

    // Preflight C4, second line of defence: should anything have put the leader into another guild on its
    // login (playerbots' bot-guild assignment), take it out first. A guild it alone made is disbanded.
    if (uint32 const strayId = leader->GetGuildId())
        if (Guild* stray = sGuildMgr->GetGuildById(strayId))
        {
            LOG_WARN("module.guildbridge", "GUILDBRIDGE guild {}: leader {} was put into guild {} on login; removing",
                     pending.name, leader->GetName(), strayId);
            stray->DeleteMember(pending.leader, false, false, true);
        }

    uint32 createdId = 0;
    std::string error;
    Guild* guild = new Guild();
    if (leader->GetGuildId() || !guild->Create(leader, pending.name))
    {
        delete guild;
        error = "the game refused to create the guild";
        LOG_ERROR("module.guildbridge", "GUILDBRIDGE guild {}: the game refused to create it", pending.name);
    }
    else
    {
        sGuildMgr->AddGuild(guild);
        createdId = guild->GetId();
        std::string const faction = leader->GetTeamId() == TEAM_ALLIANCE ? "alliance" : "horde";
        Register(pending.role, createdId, pending.name, faction, pending.leader.GetCounter());
        PlayerbotsAdapter::ValidateGuildCache();
        _revalidateInMs = RevalidateDelayMs;
        LOG_INFO("module.guildbridge", "GUILDBRIDGE guild created role={} id={}", RoleName(pending.role), createdId);
    }
    if (pending.weLoggedIn)
        PlayerbotsAdapter::Logout(pending.leader);
    Created done = std::move(pending.done);
    _pending.reset();
    if (done)
        done(createdId, error);
}
