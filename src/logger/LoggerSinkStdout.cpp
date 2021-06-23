#include "LoggerSinkStdout.h"

logger::LoggerSinkStdout::LoggerSinkStdout(logger::logLevel level)
{
    this->log_level = level;
}

void logger::LoggerSinkStdout::setLogEntry(const std::shared_ptr<logger::LogEntry> &ptr)
{
    std::stringstream out;
    if(this->log_level == logger::logLevel::DEBUG)
    {
        out << "DEBUG: ";
    }
    else if (
        (ptr->getLogLevel() == logger::logLevel::INFO) &&
        (
            (this->log_level == logger::logLevel::INFO) ||
            (this->log_level == logger::logLevel::WARNING) ||
            (this->log_level == logger::logLevel::ERROR)
        )
    )
    {
        out << "INFO: ";
    }
    else if(
        (this->log_level == logger::logLevel::WARNING) &&
        (
            (ptr->getLogLevel() == logger::logLevel::WARNING) ||
            (ptr->getLogLevel() == logger::logLevel::ERROR)
        )
    )
    {
        out << "WARNING: ";
    }
    else if(
        (ptr->getLogLevel() == logger::logLevel::ERROR) ||
        (this->log_level == logger::logLevel::ERROR)
    )
    {
         out << "ERROR: ";
    }
    else
    {
        return;
    }

    std::time_t temp = std::chrono::system_clock::to_time_t(ptr->getTimepoint());
    struct tm buf;

    out << "[" <<  std::put_time(localtime_r(&temp,&buf), "%Y-%m-%d %X") << "]" << " - " << ptr->getLogDomain();
    out << ": " << ptr->getLogMessage();

    std::cout << out.str() << std::endl;
}
