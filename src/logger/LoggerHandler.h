#pragma once

#include "LoggerLevel.h"
#include "LoggerEntry.h"
#include "LoggerSinkBase.h"

#include <memory>
#include <mutex>
#include <thread>
#include <queue>
#include <atomic>
#include <condition_variable>

namespace logger
{
    class LoggerHandler
    {
        private:
            const std::shared_ptr<logger::LoggerSinkBase> logger_sink;
            const std::string loggerDomain;
            std::queue<logger::LogEntry> log_msg_fifio;
            std::mutex queue_lock, queue_empty;
            std::condition_variable block_thread_empty_queue; 
            std::atomic_bool run_task;
            std::thread task_sink;

            static std::mutex global_instance_lock;
            static std::shared_ptr<logger::LoggerHandler> global_logger;

            void task_handler_sink();

            LoggerHandler(
                const std::shared_ptr<logger::LoggerSinkBase> &sink
            );
            

        public:
            static std::shared_ptr<logger::LoggerHandler> initLogger(
                const std::shared_ptr<logger::LoggerSinkBase> &sink
            );

            ~LoggerHandler();

            void setLogEntry(const logger::LogEntry &msg);
    };
};

