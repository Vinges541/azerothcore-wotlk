/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it under the terms of the GNU General
 * Public License as published by the Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#ifndef WORLD_STATE_SAVE_QUEUE_H
#define WORLD_STATE_SAVE_QUEUE_H

#include <algorithm>
#include <cstdint>
#include <map>

// World-thread-only bookkeeping. Database completion must be delivered back to the world thread.
// Kept separate from the DB adapter so failures and overlapping snapshots can be exercised deterministically.
class WorldStateSaveQueue
{
public:
    using Values = std::map<std::uint32_t, std::uint32_t>;

    void Enqueue(Values const& values)
    {
        for (auto const& [key, value] : values)
            _pending[key] = value;
    }

    Values const* Begin(bool ignoreDelay = false)
    {
        if (!_writing.empty() || _pending.empty() || (_retryDelayMs && !ignoreDelay))
            return nullptr;

        _writing.swap(_pending);
        return &_writing;
    }

    void Complete(bool success)
    {
        if (_writing.empty())
            return;

        if (!success)
            for (auto const& [key, value] : _writing)
                _pending.try_emplace(key, value); // Newer pending values win over failed older values.

        _writing.clear();
        _retryDelayMs = success ? 0 : 5000;
    }

    void Update(std::uint32_t diffMs)
    {
        _retryDelayMs -= std::min(_retryDelayMs, diffMs);
    }

    [[nodiscard]] bool IsDrained() const { return _pending.empty() && _writing.empty(); }

private:
    Values _pending;
    Values _writing;
    std::uint32_t _retryDelayMs = 0;
};

#endif
