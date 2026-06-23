#include <gtest/gtest.h>

#include "logger/LoggerEntry.h"
#include "logger/LoggerLevel.h"
#include "logger/LoggerSinkStdout.h"

#include <chrono>
#include <memory>
#include <string>

namespace {

// Emit one entry at `entry_level` through a stdout-sink configured at
// `sink_level`; return whether the message reached stdout. Exercises the
// sink's level matrix directly and synchronously (bypassing the async
// LoggerHandler). The sink writes to STDOUT_FILENO via POSIX ::write, so
// capture at the file-descriptor level rather than swapping std::cout's rdbuf.
bool sink_emits(logger::logLevel sink_level, logger::logLevel entry_level)
{
    logger::LoggerSinkStdout sink(sink_level);
    testing::internal::CaptureStdout();
    sink.setLogEntry(std::make_shared<logger::LogEntry>("dom", "needle", entry_level));
    const std::string captured = testing::internal::GetCapturedStdout();
    return captured.find("needle") != std::string::npos;
}

// A sink configured at level L emits exactly the entries at or below L in
// the monotonic verbosity order ERROR < WARNING < INFO < DEBUG. This is the
// "clean" contract: INFO hangs predictably on the configured level, and a
// release sink at WARNING suppresses INFO (no flood).
TEST(LoggerSinkStdoutMatrix, MonotonicThresholdAcrossAllLevels)
{
    using L = logger::logLevel;

    // sink = ERROR -> only ERROR
    EXPECT_TRUE (sink_emits(L::ERROR,   L::ERROR));
    EXPECT_FALSE(sink_emits(L::ERROR,   L::WARNING));
    EXPECT_FALSE(sink_emits(L::ERROR,   L::INFO));
    EXPECT_FALSE(sink_emits(L::ERROR,   L::DEBUG));

    // sink = WARNING -> ERROR + WARNING (fixes the legacy quirk where a
    // WARNING entry was dropped at sink==WARNING)
    EXPECT_TRUE (sink_emits(L::WARNING, L::ERROR));
    EXPECT_TRUE (sink_emits(L::WARNING, L::WARNING));
    EXPECT_FALSE(sink_emits(L::WARNING, L::INFO));
    EXPECT_FALSE(sink_emits(L::WARNING, L::DEBUG));

    // sink = INFO -> ERROR + WARNING + INFO (release milestones visible,
    // DEBUG still suppressed)
    EXPECT_TRUE (sink_emits(L::INFO,    L::ERROR));
    EXPECT_TRUE (sink_emits(L::INFO,    L::WARNING));
    EXPECT_TRUE (sink_emits(L::INFO,    L::INFO));
    EXPECT_FALSE(sink_emits(L::INFO,    L::DEBUG));

    // sink = DEBUG -> everything
    EXPECT_TRUE (sink_emits(L::DEBUG,   L::ERROR));
    EXPECT_TRUE (sink_emits(L::DEBUG,   L::WARNING));
    EXPECT_TRUE (sink_emits(L::DEBUG,   L::INFO));
    EXPECT_TRUE (sink_emits(L::DEBUG,   L::DEBUG));
}

// The shared threshold predicate every sink uses (single source of truth):
// emit iff the entry is at or below the sink in verbosity, in the order
// ERROR < WARNING < INFO < DEBUG.
TEST(LogLevelThreshold, ShouldLogIsMonotonic)
{
    using L = logger::logLevel;

    EXPECT_TRUE (logger::should_log(L::ERROR,   L::ERROR));
    EXPECT_FALSE(logger::should_log(L::WARNING, L::ERROR));
    EXPECT_FALSE(logger::should_log(L::INFO,    L::ERROR));
    EXPECT_FALSE(logger::should_log(L::DEBUG,   L::ERROR));

    EXPECT_TRUE (logger::should_log(L::ERROR,   L::WARNING));
    EXPECT_TRUE (logger::should_log(L::WARNING, L::WARNING));
    EXPECT_FALSE(logger::should_log(L::INFO,    L::WARNING));
    EXPECT_FALSE(logger::should_log(L::DEBUG,   L::WARNING));

    EXPECT_TRUE (logger::should_log(L::ERROR,   L::INFO));
    EXPECT_TRUE (logger::should_log(L::WARNING, L::INFO));
    EXPECT_TRUE (logger::should_log(L::INFO,    L::INFO));
    EXPECT_FALSE(logger::should_log(L::DEBUG,   L::INFO));

    EXPECT_TRUE (logger::should_log(L::ERROR,   L::DEBUG));
    EXPECT_TRUE (logger::should_log(L::WARNING, L::DEBUG));
    EXPECT_TRUE (logger::should_log(L::INFO,    L::DEBUG));
    EXPECT_TRUE (logger::should_log(L::DEBUG,   L::DEBUG));
}

TEST(LogEntry, ConstructionRoundTrip)
{
    const auto before = std::chrono::system_clock::now();
    logger::LogEntry entry("domain.x", "hello world", logger::logLevel::ERROR);
    const auto after = std::chrono::system_clock::now();

    EXPECT_EQ(entry.getLogDomain(), "domain.x");
    EXPECT_EQ(entry.getLogMessage(), "hello world");
    EXPECT_EQ(entry.getLogLevel(), logger::logLevel::ERROR);
    EXPECT_GE(entry.getTimepoint(), before);
    EXPECT_LE(entry.getTimepoint(), after);
}

TEST(LogEntry, AcceptsAllLogLevels)
{
    logger::LogEntry e1("d", "m", logger::logLevel::ERROR);
    logger::LogEntry e2("d", "m", logger::logLevel::WARNING);
    logger::LogEntry e3("d", "m", logger::logLevel::DEBUG);

    EXPECT_EQ(e1.getLogLevel(), logger::logLevel::ERROR);
    EXPECT_EQ(e2.getLogLevel(), logger::logLevel::WARNING);
    EXPECT_EQ(e3.getLogLevel(), logger::logLevel::DEBUG);
}

TEST(LogEntry, CopyPreservesAllFields)
{
    logger::LogEntry original("traceability", "migration step 4", logger::logLevel::WARNING);
    logger::LogEntry copy(original);

    EXPECT_EQ(copy.getLogDomain(), original.getLogDomain());
    EXPECT_EQ(copy.getLogMessage(), original.getLogMessage());
    EXPECT_EQ(copy.getLogLevel(), original.getLogLevel());
    EXPECT_EQ(copy.getTimepoint(), original.getTimepoint());
}

TEST(LogEntry, EmptyDomainAndMessageAreAccepted)
{
    logger::LogEntry entry("", "", logger::logLevel::DEBUG);
    EXPECT_TRUE(entry.getLogDomain().empty());
    EXPECT_TRUE(entry.getLogMessage().empty());
}

TEST(LogLevel, EnumValuesAreDistinct)
{
    EXPECT_NE(logger::logLevel::ERROR, logger::logLevel::WARNING);
    EXPECT_NE(logger::logLevel::ERROR, logger::logLevel::DEBUG);
    EXPECT_NE(logger::logLevel::WARNING, logger::logLevel::DEBUG);
}

} // namespace
