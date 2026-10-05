/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 */

#include "BotDumps.h"
#include "BridgeAsync.h"
#include "BridgeConfig.h"
#include "CharacterCache.h"
#include "Chat.h"
#include "CommandScript.h"
#include "DatabaseEnv.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "GuildRegistry.h"
#include "GuildmasterDatabase.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerbotsAdapter.h"
#include "QueryCallback.h"
#include "ScriptMgr.h"
#include "StringFormat.h"
#include <functional>
#include <sstream>
#include <string>
#include <vector>

using namespace Acore::ChatCommands;

namespace
{
std::vector<std::string> Words(char const* args)
{
    std::vector<std::string> out;
    std::istringstream in(args ? args : "");
    std::string word;
    while (in >> word)
        out.push_back(word);
    return out;
}

// Looks an account id up by name without a sync query on the world thread (AccountMgr::GetId is sync); `then`
// runs on the world thread with the id, 0 when there is no such account.
void WithAccountId(std::string accountName, std::function<void(uint32)> then)
{
    LoginDatabase.EscapeString(accountName);
    BridgeAsync::Add(
        LoginDatabase.AsyncQuery(Acore::StringFormat("SELECT id FROM account WHERE username = '{}'", accountName))
            .WithCallback([then = std::move(then)](QueryResult result) {
                then(result ? (*result)[0].Get<uint32>() : 0);
            }));
}

// Clone: dump `source` (saved first when online) and load it onto `accountName` as `target`, a new character.
void Clone(ObjectGuid source, std::string const& target, std::string const& accountName)
{
    WithAccountId(accountName, [source, target, accountName](uint32 account) {
        if (!account || PlayerbotsAdapter::IsBotAccount(account))
        {
            LOG_ERROR("module.guildbridge", "GUILDBRIDGE clone {} failed: no normal account {}", target, accountName);
            return;
        }
        BotDumps::Instance().Request(
            source.GetCounter(), "manual", 0,
            [account, target](bool ok, std::string const& error, std::string const& dump) {
                std::string why = error;
                if (ok && sCharacterCache->GetCharacterGuidByName(target))
                    why = "the name was taken meanwhile";
                else if (ok && BotDumps::LoadOnWorldThread(dump, account, target, 0, why))
                    why.clear();
                if (!why.empty() || !ok)
                {
                    LOG_ERROR("module.guildbridge", "GUILDBRIDGE clone {} failed: {}", target, why);
                    return;
                }
                BotDumps::ConfirmLoaded(target, [target](bool loaded, uint32 guid) {
                    if (loaded)
                        LOG_INFO("module.guildbridge", "GUILDBRIDGE clone {} guid={}", target, guid);
                    else
                        LOG_ERROR("module.guildbridge", "GUILDBRIDGE clone {} failed: the load was rolled back "
                                  "(see the DB errors log)", target);
                });
            });
    });
}
}  // namespace

// `.bridge <sub> ...` (GM, console allowed). Status plus the test seams later tasks add. Every reply starts
// with a fixed tag (BRIDGE, BRIDGEOK, BRIDGEERR, ...) so the bash checks can parse it.
class BridgeCommandScript : public CommandScript
{
public:
    BridgeCommandScript() : CommandScript("BridgeCommandScript") {}

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable commandTable = {
            {"bridge", HandleBridge, SEC_ADMINISTRATOR, Console::Yes},
        };
        return commandTable;
    }

    static bool HandleBridge(ChatHandler* handler, char const* args)
    {
        std::vector<std::string> const words = Words(args);
        std::string const sub = words.empty() ? "status" : words[0];
        if (sub == "status")
        {
            handler->PSendSysMessage("BRIDGE enabled={} version={} db={} world={} snapshots={} snapfail={}",
                                     BridgeConfig::Get().enable ? 1 : 0, GUILDBRIDGE_VERSION,
                                     GuildmasterDatabaseReady ? 1 : 0, BridgeConfig::Get().worldId,
                                     BotDumps::Instance().Taken(), BotDumps::Instance().Failed());
            return true;
        }
        if ((sub == "snapshot" || sub == "clone" || sub == "testbots" || sub == "guild" || sub == "bot") &&
            !BridgeConfig::Get().enable)
        {
            handler->PSendSysMessage("BRIDGEERR the bridge is disabled (GuildBridge.Enable = 0)");
            return false;
        }
        if (sub == "snapshot" && words.size() > 1)
        {
            if (words[1] == "retention")
            {
                BotDumps::Instance().RunRetention();
                handler->PSendSysMessage("BRIDGEOK retention queued");
                return true;
            }
            std::string name = words[1];
            normalizePlayerName(name);
            ObjectGuid const guid = sCharacterCache->GetCharacterGuidByName(name);
            if (!guid)
            {
                handler->PSendSysMessage("BRIDGEERR no character named {}", name);
                return false;
            }
            BotDumps::Instance().Request(guid.GetCounter(), "manual", 0);
            handler->PSendSysMessage("BRIDGEOK snapshot queued for {}", name);
            return true;
        }
        if (sub == "clone" && words.size() > 2)
        {
            std::string source = words[1];
            std::string target = words[2];
            normalizePlayerName(source);
            normalizePlayerName(target);
            ObjectGuid const guid = sCharacterCache->GetCharacterGuidByName(source);
            if (!guid || sCharacterCache->GetCharacterGuidByName(target) ||
                ObjectMgr::CheckPlayerName(target, true) != CHAR_NAME_SUCCESS)
            {
                handler->PSendSysMessage("BRIDGEERR need an existing source and a free, valid new name");
                return false;
            }
            std::string const account = words.size() > 3 ? words[3] : BridgeConfig::Get().testAccount;
            if (PlayerbotsAdapter::IsBotAccountName(account))
            {
                handler->PSendSysMessage("BRIDGEERR clones only go to normal accounts");
                return false;
            }
            // The account is looked up asynchronously; a missing account is logged as a failed clone.
            Clone(guid, target, account);
            handler->PSendSysMessage("BRIDGEOK clone queued {} -> {}", source, target);
            return true;
        }
        if (sub == "testbots" && words.size() > 1 && (words[1] == "login" || words[1] == "logout"))
        {
            bool const login = words[1] == "login";
            WithAccountId(BridgeConfig::Get().testAccount, [login](uint32 account) {
                if (!account)
                    return;
                BridgeAsync::Add(CharacterDatabase.AsyncQuery(Acore::StringFormat(
                    "SELECT guid FROM characters WHERE account = {} AND deleteInfos_Account IS NULL", account))
                        .WithCallback([login](QueryResult result) {
                            if (!result)
                                return;
                            do
                            {
                                ObjectGuid const guid =
                                    ObjectGuid::Create<HighGuid::Player>((*result)[0].Get<uint32>());
                                // A registered guild's leader stays offline (spec §4.8: it never logs in).
                                if (login && !ObjectAccessor::FindConnectedPlayer(guid) &&
                                    !GuildRegistry::Instance().IsRegisteredLeader(guid))
                                    PlayerbotsAdapter::LoginMasterless(guid);
                                else if (!login)
                                    PlayerbotsAdapter::Logout(guid);
                            } while (result->NextRow());
                        }));
            });
            handler->PSendSysMessage("BRIDGEOK testbots {}", login ? "login" : "logout");
            return true;
        }
        if (sub == "guild" && words.size() > 4 && words[1] == "create")
        {
            std::string name;
            for (std::size_t i = 4; i < words.size(); ++i)
                name += (i > 4 ? " " : "") + words[i];
            std::string const error =
                GuildRegistry::Instance().BeginCreate(GuildRegistry::RoleFromName(words[2]), name, words[3]);
            if (!error.empty())
            {
                handler->PSendSysMessage("BRIDGEERR {}", error);
                return false;
            }
            handler->PSendSysMessage("BRIDGEOK creating {}", name);
            return true;
        }
        if (sub == "guild" && words.size() > 2 && words[1] == "show")
        {
            GuildRole const role = GuildRegistry::RoleFromName(words[2]);
            uint32 const id = GuildRegistry::Instance().GuildIdFor(role);
            Guild* guild = id ? sGuildMgr->GetGuildById(id) : nullptr;
            handler->PSendSysMessage("BRIDGEGUILD role={} id={} members={} real={} raisings={}",
                                     GuildRegistry::RoleName(role), id, guild ? guild->GetMemberCount() : 0,
                                     guild && PlayerbotsAdapter::IsRealGuild(id) ? 1 : 0,
                                     PlayerbotsAdapter::RaisingsUserGuild());
            return true;
        }
        if (sub == "bot" && words.size() > 1)
        {
            std::string name = words[1];
            normalizePlayerName(name);
            ObjectGuid const guid = sCharacterCache->GetCharacterGuidByName(name);
            CharacterCacheEntry const* cache = guid ? sCharacterCache->GetCharacterCacheByGuid(guid) : nullptr;
            if (!cache)
            {
                handler->PSendSysMessage("BRIDGEERR no character named {}", name);
                return false;
            }
            Player* bot = ObjectAccessor::FindConnectedPlayer(guid);
            handler->PSendSysMessage("BRIDGEBOT name={} guid={} online={} random={} held={} guild={} role={}", name,
                                     guid.GetCounter(), bot ? 1 : 0,
                                     PlayerbotsAdapter::IsRandomBot(guid.GetCounter()) ? 1 : 0,
                                     PlayerbotsAdapter::IsHeld(guid.GetCounter()) ? 1 : 0, cache->GuildId,
                                     GuildRegistry::RoleName(GuildRegistry::Instance().RoleOf(cache->GuildId)));
            return true;
        }
        handler->PSendSysMessage("BRIDGEERR unknown sub-command {}", sub);
        return false;
    }
};

void AddBridgeCommandScripts() { new BridgeCommandScript(); }
