/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 */

#include "BridgeConfig.h"

#include "Config.h"
#include <algorithm>
#include <cctype>

BridgeConfig& BridgeConfig::Get()
{
    static BridgeConfig config;
    return config;
}

void BridgeConfig::Load()
{
    enable = sConfigMgr->GetOption<bool>("GuildBridge.Enable", true);
    worldId = sConfigMgr->GetOption<std::string>("GuildBridge.WorldId", "w1");
    charactersDb = DatabaseNameOf(sConfigMgr->GetOption<std::string>("CharacterDatabaseInfo", ""));
    playerbotsDb = DatabaseNameOf(sConfigMgr->GetOption<std::string>("PlayerbotsDatabaseInfo", ""));
}

std::string BridgeConfig::DatabaseNameOf(std::string const& info)
{
    std::size_t pos = 0;
    for (int field = 0; field < 4; ++field)
    {
        pos = info.find(';', pos);
        if (pos == std::string::npos)
            return "";
        ++pos;
    }
    std::size_t const end = info.find(';', pos);
    return info.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
}

bool BridgeConfig::IsValidWorldId(std::string const& id)
{
    return !id.empty() && id.size() <= 32 && std::all_of(id.begin(), id.end(), [](unsigned char c) {
        return std::islower(c) || std::isdigit(c) || c == '_' || c == '-';
    });
}
