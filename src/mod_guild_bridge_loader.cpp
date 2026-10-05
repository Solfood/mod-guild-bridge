/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 */

void AddBridgeDatabaseScripts();
void AddBridgeWorldScripts();
void AddBridgeCommandScripts();
void AddBridgeEventHooks();

// The build calls Add<folder name with - replaced by _>Scripts().
void Addmod_guild_bridgeScripts()
{
    AddBridgeDatabaseScripts();  // first: the guildmaster database must open before anything uses it
    AddBridgeWorldScripts();
    AddBridgeCommandScripts();
    AddBridgeEventHooks();
}
