/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 */

#include "BridgeAsync.h"

#include "AsyncCallbackProcessor.h"
#include "DatabaseEnvFwd.h"
#include "QueryCallback.h"

namespace
{
QueryCallbackProcessor& Processor()
{
    static QueryCallbackProcessor processor;
    return processor;
}
}  // namespace

void BridgeAsync::Add(QueryCallback&& callback) { Processor().AddCallback(std::move(callback)); }
void BridgeAsync::Process() { Processor().ProcessReadyCallbacks(); }
