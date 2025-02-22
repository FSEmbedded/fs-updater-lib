/**
 * Create the logger handler for all log messages.
 * Connect to a sink and inject there all messages.
 * The creation, destruction and injection functions are thread-safe.
 */

#pragma once

#include "LoggerLevel.h"
#include "LoggerEntry.h"
#include "LoggerSinkBase.h"

#include <memory>
#include <mutex>
#include <thread>
#include <list>
#include <atomic>
#include <condition_variable>
#include <map>

namespace logger
{
    /**
     * Will create a subprocess that continously collect all log-entries.
     * The subprocess block until a log entry is added to the chain.
     * Multiple reference points can add at the same times log entries.
     * The functions of adding log entries is thread-safe.
     * Only one logger can be instanced at one time, return allways a reference.
     */
    class LoggerHandler
    {
        private:
            const std::shared_ptr<logger::LoggerSinkBase> logger_sink;
            const std::string loggerDomain;
            std::list<logger::LogEntry> log_msg_fifio;
            std::mutex queue_lock, queue_empty;
            std::condition_variable block_thread_empty_queue; 
            std::atomic_bool run_task;
            std::thread task_sink;

            static std::mutex global_instance_lock;
            static std::map<logger::LoggerSinkBase*, std::shared_ptr<logger::LoggerHandler>> global_logger_sink_store;

            /**
             * "Thread"-function, that will call the target function for the given sink.
             */
            void task_handler_sink() noexcept;

            /**
             * Private constructor for using singelton pattern.
             * Instance thread for logger.
             * @param sink Set Sink where the logger will transfer all log entries.
             */
            explicit LoggerHandler(
                const std::shared_ptr<logger::LoggerSinkBase> &sink
            );
            

        public:
            /**
             * Static function that returns the object for logger.
             * If no object is instanced a object will created and returned,
             * else a reference will be returend.
             * It exists only one pair of the same sink - handler pair.
             * @param sink Reference of a derived object from logger::LoggerSinkBase.
             */
            static std::shared_ptr<logger::LoggerHandler> initLogger(
                const std::shared_ptr<logger::LoggerSinkBase> &sink
            );

            ~LoggerHandler();

            /**
             * Add logEntry to the chain of all enqued entries.
             * @param msg LogEntry of the current event.
             */
            void setLogEntry(const logger::LogEntry &msg);
    };
}