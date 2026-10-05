/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 */

#ifndef MOD_GUILD_BRIDGE_STUCKDETECTOR_H
#define MOD_GUILD_BRIDGE_STUCKDETECTOR_H

#include "Define.h"
#include "StuckRules.h"
#include <string>
#include <unordered_map>
#include <vector>

class Player;

// guildmaster.incidents: stuck bots (GM-BOTAI stuck-bot toolkit, "find"). World thread only.
// Every ScanS seconds it looks at every online bot (population and guild, decided Q6; the test account's clones
// only in test mode), PerTick bots per world tick, from memory (never characters.online). Tracks live in memory:
// a restart closes whatever was open. Writes are async (one ordered writer).
class StuckDetector
{
public:
    static StuckDetector& Instance();
    void CloseAllAtStartup();  // OnStartup: nothing carries over a restart (tracks are in memory)
    void Update(uint32 diff);
    void ScanNow();  // a whole look at once (console, checks)
    // Test mode: judge only the bot with this name, with these thresholds (MoveYards/PathWindowS from config).
    void SetTest(std::string const& name, GuildBridge::Thresholds const& thresholds);
    void Reset();  // back to normal: every bot, configured thresholds; what is open is closed
    uint32 OpenCount() const;
    std::size_t Tracked() const { return _tracks.size(); }
    // Cost of the last whole look: world-thread microseconds summed over its ticks, bots judged, ticks used;
    // and the most one tick of it has cost since startup.
    uint32 LastScanUs() const { return _lastScanUs; }
    uint32 LastScanBots() const { return _lastScanBots; }
    uint32 LastScanTicks() const { return _lastScanTicks; }
    uint32 MaxStepUs() const { return _maxStepUs; }

private:
    struct Entry
    {
        GuildBridge::BotTrack track;
        uint32 pass = 0;  // the last look that saw this bot online
    };

    struct ProgressSpot  // where and when the bot last made progress
    {
        uint32 sinceS;
        uint32 map;
        float x, y;
    };

    void StartScan();
    void Step(std::size_t budget);  // judges up to `budget` bots of the current look
    void FinishScan();
    void Judge(uint32 guid, uint32 now);
    void Open(Player* bot, GuildBridge::IncidentKind kind, GuildBridge::BotSample const& sample,
              GuildBridge::BotTrack const& track, ProgressSpot const& before, char const* exempt);
    void Close(uint32 guid, GuildBridge::IncidentKind kind, uint32 now, char const* why);
    void CloseAll(char const* why);  // every open incident in the database; tracks are forgotten
    void LoadThresholds();

    std::unordered_map<uint32, Entry> _tracks;
    GuildBridge::Thresholds _thresholds;
    std::string _onlyName;
    uint32 _timer = 0;
    uint32 _pass = 0;
    bool _scanning = false;
    std::vector<uint32> _pending;  // guids of the current look, judged from _next on
    std::size_t _next = 0;
    uint64 _scanUs = 0;
    uint32 _scanTicks = 0;
    uint32 _scanBots = 0;
    uint32 _lastScanUs = 0;
    uint32 _lastScanBots = 0;
    uint32 _lastScanTicks = 0;
    uint32 _maxStepUs = 0;
};

#endif
