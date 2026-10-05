/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 */

#ifndef MOD_GUILD_BRIDGE_ORDERRUNNER_H
#define MOD_GUILD_BRIDGE_ORDERRUNNER_H

#include "Define.h"
#include "OrderRules.h"
#include <string>
#include <unordered_set>

struct OrderResult
{
    bool ok = false;
    std::string text;  // orders.result: one plain sentence
    std::string data;  // orders.result_data: JSON or ""
};

// Polls guildmaster.orders every 5 s (async), checks, claims, dispatches, finishes. World thread.
// Holds: the runner holds no bot. An order that is running at a restart is finished as failed at the next boot
// (RecoverAtStartup), so nothing it started needs to survive the restart.
class OrderRunner
{
public:
    static OrderRunner& Instance();
    void RecoverAtStartup();  // OnStartup: nothing stays "running" across a restart
    void Update(uint32 diff);
    void PollNow() { _timer = POLL_MS; }
    static void Finish(uint64 id, OrderResult const& result);
    uint64 Finished() const { return _finished; }

private:
    static constexpr uint32 POLL_MS = 5000;
    void Poll();
    void Handle(GuildBridge::OrderRow const& row);

    uint32 _timer = 0;
    bool _polling = false;
    std::unordered_set<uint64> _running;
    static uint64 _finished;
};

#endif
