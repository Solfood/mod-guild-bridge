/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 * Pure. What the restore needs to read from AzerothCore player-dump text.
 */

#ifndef MOD_GUILD_BRIDGE_CORE_DUMPRULES_H
#define MOD_GUILD_BRIDGE_CORE_DUMPRULES_H

#include <cstdint>
#include <string>
#include <vector>

namespace GuildBridge
{
// The ids of the letters in a dump: lines "INSERT INTO `mail` (`id`, ...) VALUES ('<id>', ...". A restore empties
// exactly these letters before the delete (they come back with the dump); a letter that arrived after the dump is
// not in it, so it is left to the delete, which returns it to its sender.
inline std::vector<uint64_t> DumpMailIds(std::string const& dump)
{
    static std::string const head = "INSERT INTO `mail` (";
    static std::string const values = ") VALUES ('";
    std::vector<uint64_t> ids;
    std::size_t pos = 0;
    while ((pos = dump.find(head, pos)) != std::string::npos)
    {
        bool const lineStart = pos == 0 || dump[pos - 1] == '\n';
        std::size_t const eol = dump.find('\n', pos);
        std::size_t const v = dump.find(values, pos);
        pos += head.size();
        if (!lineStart || v == std::string::npos || (eol != std::string::npos && v > eol))
            continue;
        std::size_t i = v + values.size();
        uint64_t id = 0;
        std::size_t digits = 0;
        while (i < dump.size() && dump[i] >= '0' && dump[i] <= '9' && digits < 20)
        {
            id = id * 10 + static_cast<uint64_t>(dump[i] - '0');
            ++i;
            ++digits;
        }
        if (digits && i < dump.size() && dump[i] == '\'' && id)
            ids.push_back(id);
    }
    return ids;
}

inline std::string JoinIds(std::vector<uint64_t> const& ids)
{
    std::string out;
    for (uint64_t id : ids)
        out += (out.empty() ? "" : ",") + std::to_string(id);
    return out;
}
}  // namespace GuildBridge

#endif
