/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 */

#include "BridgeConfig.h"
#include "Chat.h"
#include "CommandScript.h"
#include "ScriptMgr.h"
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
            handler->PSendSysMessage("BRIDGE enabled={} version={}", BridgeConfig::Get().enable ? 1 : 0,
                                     GUILDBRIDGE_VERSION);
            return true;
        }
        handler->PSendSysMessage("BRIDGEERR unknown sub-command {}", sub);
        return false;
    }
};

void AddBridgeCommandScripts() { new BridgeCommandScript(); }
