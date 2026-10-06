/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 */

#ifndef MOD_GUILD_BRIDGE_CHARACTERMAKER_H
#define MOD_GUILD_BRIDGE_CHARACTERMAKER_H

#include "Define.h"
#include <string>

// A fresh level-1 character with random looks at its race's start, made with the core's own steps (the same ones
// playerbots uses for random bots and the fork for raised death knights). World thread. The row is saved
// asynchronously: wait for it before logging the character in or adding it to the population.
namespace CharacterMaker
{
// gender: 0 male, 1 female, 2 random. Returns the new guid, 0 on failure (then `error` says why).
uint32 Create(uint32 account, std::string const& name, uint8 race, uint8 cls, uint8 gender, std::string& error);
}  // namespace CharacterMaker

#endif
