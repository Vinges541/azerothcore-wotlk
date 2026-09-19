/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 * Released under GNU GPL v2 or (at your option) any later version.
 */

#ifndef ACORE_MODULEPREPAREDSTATEMENTREGISTRY_H
#define ACORE_MODULEPREPAREDSTATEMENTREGISTRY_H

#include <algorithm>
#include <cstdint>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Acore
{
    // Trusted module code registers SQL before pool preparation. No runtime/config-supplied SQL.
    // One registry per database backend, owned by that backend (not by a module/shared-library copy).
    template <typename Flags>
    class ModulePreparedStatementRegistry
    {
    public:
        struct Definition
        {
            std::uint32_t index;
            std::string name;
            std::string sql;
            Flags flags;
            bool operator==(Definition const&) const = default;
        };

        ModulePreparedStatementRegistry(std::uint32_t firstIndex, Flags allowedFlags)
            : firstIndex(firstIndex), allowedFlags(allowedFlags) { }

        std::optional<std::uint32_t> Register(std::string name, std::string sql, Flags flags)
        {
            std::lock_guard lock(mutex);
            if (name.empty() || sql.empty() || name.find('\0') != std::string::npos ||
                sql.find('\0') != std::string::npos || !static_cast<std::uint32_t>(flags) ||
                (static_cast<std::uint32_t>(flags) & ~static_cast<std::uint32_t>(allowedFlags)))
                return {};
            auto found = std::find_if(entries.begin(), entries.end(), [&name](auto const& entry)
            {
                return entry.name == name;
            });
            if (found != entries.end())
                return found->sql == sql && found->flags == flags ? std::optional(found->index) : std::nullopt;
            if (frozen || entries.size() >= 4096 ||
                entries.size() >= std::numeric_limits<std::uint32_t>::max() - firstIndex)
                return {};
            auto const index = firstIndex + static_cast<std::uint32_t>(entries.size());
            entries.push_back({index, std::move(name), std::move(sql), flags});
            return index;
        }

        // Owned snapshot: concurrent prepares/reconnects see the same IDs, strings and flags forever.
        std::vector<Definition> Freeze()
        {
            std::lock_guard lock(mutex);
            frozen = true;
            return entries;
        }

    private:
        std::mutex mutex;
        std::uint32_t const firstIndex;
        Flags const allowedFlags;
        std::vector<Definition> entries;
        bool frozen = false;
    };
}

#endif
