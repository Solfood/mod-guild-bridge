/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 */

#include "DcAdapter.h"

#include "ObjectGuid.h"
#include "TestRun/DcTestDriver.h"
#include "TestRun/DcTestDungeonRegistry.h"
#include "TestRun/DcTestRunManager.h"
#include <cstdlib>
#include <limits>

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

DcAdapter::StartState DcAdapter::Start(std::string const& token, std::string const& names, bool heroic,
                                       std::string& message, std::string& runId)
{
    std::string why;
    switch (DcTestDriver::Ensure(&why))
    {
        case DcTestDriver::Readiness::Ready:
            break;
        case DcTestDriver::Readiness::PendingLogin:
            message = why;
            return StartState::Retry;
        case DcTestDriver::Readiness::Unavailable:
            // Includes dungeon-clear's first-use driver-account refusal, which stays until a restart (preflight C17).
            message = "driver unavailable: " + why;
            return StartState::Refused;
    }
    DcTestRunManager::StartErr error = DcTestRunManager::StartErr::None;
    if (DcTestRunManager::Instance().StartRoster(DcTestDriver::Get(), token, names, heroic, &message, "", &error, &runId))
        return StartState::Started;
    switch (error)
    {
        case DcTestRunManager::StartErr::CharacterOnline:
        case DcTestRunManager::StartErr::CharacterBusy:
        case DcTestRunManager::StartErr::CapHit:
        case DcTestRunManager::StartErr::PoolExhausted:
            return StartState::Retry;
        default:
            return StartState::Refused;
    }
}

uint32 DcAdapter::ConcurrentCap()
{
    uint32 const headroom = DcTestRunManager::Instance().CapHeadroom();
    return headroom == std::numeric_limits<uint32>::max() ? 0 : headroom;
}

bool DcAdapter::IsReserved(uint32 guid)
{
    return DcTestRunManager::Instance().IsReserved(ObjectGuid::Create<HighGuid::Player>(guid));
}

bool DcAdapter::Stop(std::string const& runId, std::string& message)
{
    if (runId.empty())
        return false;  // an empty selector would mean "the only run", which may be someone else's
    return DcTestRunManager::Instance().Stop(runId, &message);
}

std::string DcAdapter::RunsFilePath()
{
    if (char const* env = std::getenv("DC_TESTRUNS_FILE"))
        if (env[0])
            return env;
    return "dc_testruns.jsonl";
}
