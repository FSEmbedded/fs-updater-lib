#include "LoggerHandler.h"

std::mutex logger::LoggerHandler::global_instance_lock;
std::map<logger::LoggerSinkBase*, std::shared_ptr<logger::LoggerHandler>> logger::LoggerHandler::global_logger_sink_store;


logger::LoggerHandler::LoggerHandler(
    const std::shared_ptr<logger::LoggerSinkBase> &sink):
    logger_sink(sink)
{
    this->run_task = true;
    this->task_sink = std::thread(&LoggerHandler::task_handler_sink, this);
}

std::shared_ptr<logger::LoggerHandler> logger::LoggerHandler::initLogger(
    const std::shared_ptr<logger::LoggerSinkBase> &sink)

{
    std::lock_guard<std::mutex> lock(global_instance_lock);

    if (global_logger_sink_store.find(sink.get()) == global_logger_sink_store.end())
    {
        global_logger_sink_store[sink.get()] = std::shared_ptr<logger::LoggerHandler>(new logger::LoggerHandler(sink));
    }

    return   global_logger_sink_store[sink.get()];
}

logger::LoggerHandler::~LoggerHandler()
{
    this->run_task = false;
    std::lock_guard<std::mutex> lock(global_instance_lock);

    this->block_thread_empty_queue.notify_one();
    // Check if default constructed
    if (this->task_sink.get_id() != std::thread::id())
    {
        this->task_sink.join();
    }

    if (global_logger_sink_store.find(this->logger_sink.get()) == global_logger_sink_store.end())
    {
        global_logger_sink_store.erase(this->logger_sink.get());
    }
}

void logger::LoggerHandler::setLogEntry(const logger::LogEntry &msg)
{
    std::lock_guard<std::mutex> lock(this->queue_lock);
    this->log_msg_fifio.push(msg);
}

void logger::LoggerHandler::task_handler_sink()
{
    while (true)
    {
        std::shared_ptr<logger::LogEntry> entry;
        {
            std::lock_guard<std::mutex> lock(this->queue_lock);
            if (!this->log_msg_fifio.empty())
            {
                entry = std::make_shared<logger::LogEntry>(this->log_msg_fifio.front());
                this->log_msg_fifio.pop();
            }
            else if (this->run_task == false)
            {
                break;
            }
        }

        if (entry)
        {
            this->logger_sink->setLogEntry(entry);
        }
        else
        {
            std::unique_lock<std::mutex> cv_lock(this->queue_empty);
            block_thread_empty_queue.wait(cv_lock);
        }
    }
}