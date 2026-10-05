/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 */

void AddBridgeWorldScripts();
void AddBridgeCommandScripts();

// The build calls Add<folder name with - replaced by _>Scripts().
void Addmod_guild_bridgeScripts()
{
    AddBridgeWorldScripts();
    AddBridgeCommandScripts();
}
