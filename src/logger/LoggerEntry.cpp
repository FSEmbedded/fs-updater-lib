#include "LoggerEntry.h"

logger::LogEntry::LogEntry(
                const std::string &logDomain,
                const std::string &logMessage,
                const logger::logLevel level
            ):
            logMessage(logMessage),
            logDomain(logDomain),
            time_of_occurance(std::chrono::system_clock::now()),
            logLevel(level)
{

}

logger::LogEntry::LogEntry(const logger::LogEntry &c):
            logMessage(c.logMessage),
            logDomain(c.logDomain),
            time_of_occurance(c.time_of_occurance),
            logLevel(c.logLevel)
{

}

logger::LogEntry::~LogEntry()
{

}

std::string logger::LogEntry::getLogMessage() const
{
    return this->logMessage;
}

std::chrono::time_point<std::chrono::system_clock> logger::LogEntry::getTimepoint() const
{
    return this->time_of_occurance;
}

logger::logLevel logger::LogEntry::getLogLevel() const
{
    return this->logLevel;
}

std::string logger::LogEntry::getLogDomain() const
{
    return this->logDomain;
}