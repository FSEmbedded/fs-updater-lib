#include <gtest/gtest.h>

#include "logger/LoggerEntry.h"
#include "logger/LoggerLevel.h"

#include <chrono>

namespace {

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
