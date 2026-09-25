#pragma once

namespace logger
{
    /**
     * Define different states of log-levels.
     */
    enum class logLevel
    {
        ERROR,
        WARNING,
        INFO,
        DEBUG
    };

    /**
     * Verbosity rank: lower = more severe / less verbose.
     * Order: ERROR < WARNING < INFO < DEBUG. An explicit switch keeps the
     * ordering independent of the enum's declaration order and forces a
     * compile error (-Wswitch) if a level is added without a rank.
     */
    [[nodiscard]] constexpr int logLevelRank(logLevel level) noexcept
    {
        switch (level)
        {
            case logLevel::ERROR:   return 0;
            case logLevel::WARNING: return 1;
            case logLevel::INFO:    return 2;
            case logLevel::DEBUG:   return 3;
        }
        return 0;
    }

    /**
     * Single source of truth for level filtering shared by every sink:
     * a sink configured at `sink` emits an entry at `entry` iff the entry is
     * at or below the sink in verbosity (monotonic threshold). Keeping this
     * here guarantees all sinks filter identically and can never diverge.
     */
    [[nodiscard]] constexpr bool should_log(logLevel entry, logLevel sink) noexcept
    {
        return logLevelRank(entry) <= logLevelRank(sink);
    }
}