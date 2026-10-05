/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 */

#ifndef MOD_GUILD_BRIDGE_ASYNC_H
#define MOD_GUILD_BRIDGE_ASYNC_H

#include "DatabaseEnvFwd.h"

// One place for every async read the bridge makes (guildmaster, characters, world). Add() from the world
// thread; Process() runs the ready callbacks once per world tick (BridgeWorldScript::OnUpdate).
namespace BridgeAsync
{
void Add(QueryCallback&& callback);
void Process();
}  // namespace BridgeAsync

#endif
