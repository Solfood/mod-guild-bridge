/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 */

#include "DcAdapter.h"

#include "TestRun/DcTestDungeonRegistry.h"

DcAdapter::DungeonInfo DcAdapter::Find(std::string const& token)
{
    DungeonInfo info;
    DcTestDungeonRegistry::Row const* row = DcTestDungeonRegistry::Find(token);
    if (!row || DcTestDungeonRegistry::IsScenario(*row))
        return info;
    info.found = true;
    info.token = row->token;
    info.name = row->name;
    info.mapId = row->mapId;
    info.recommendedLevel = row->recommendedLevel;
    info.heroicLevel = row->heroicLevel;
    return info;
}
