#include "LoggerHandler.h"

std::mutex logger::LoggerHandler::global_instance_lock;
std::map<std::shared_ptr<logger::LoggerSinkBase>, std::shared_ptr<logger::LoggerHandler>, std::owner_less<std::shared_ptr<logger::LoggerSinkBase>>> logger::LoggerHandler::global_logger_sink_store;

logger::LoggerHandler::LoggerHandler(const std::shared_ptr<logger::LoggerSinkBase> &sink)
    : logger_sink(sink), run_task(true)
{
    task_sink = std::thread(&LoggerHandler::task_handler_sink, this);
}

logger::LoggerHandler::~LoggerHandler()
{
    {
        std::lock_guard<std::mutex> lock(queue_lock);
        run_task = false;
    }
    block_thread_empty_queue.notify_all();

    if (task_sink.joinable()) {
        task_sink.join();
    }
}

std::shared_ptr<logger::LoggerHandler> logger::LoggerHandler::initLogger(
    const std::shared_ptr<logger::LoggerSinkBase> &sink)
{
    std::lock_guard<std::mutex> lock(global_instance_lock);

    auto it = global_logger_sink_store.find(sink);
    if (it != global_logger_sink_store.end()) {
        return it->second;
    }

    auto handler = std::make_shared<LoggerHandler>(sink);
    //auto handler = std::shared_ptr<LoggerHandler>(new LoggerHandler(sink));
    global_logger_sink_store.emplace(sink, handler);
    return handler;
}

void logger::LoggerHandler::setLogEntry(const std::shared_ptr<logger::LogEntry> &msg)
{
    {
        std::lock_guard<std::mutex> lock(queue_lock);
        log_msg_fifo.emplace_back(msg);
    }
    block_thread_empty_queue.notify_one();
}

void logger::LoggerHandler::task_handler_sink() noexcept
{
    while (true)
    {
        std::unique_lock<std::mutex> lock(queue_lock);

        block_thread_empty_queue.wait(lock, [&] {
            return !log_msg_fifo.empty() || !run_task;
        });

        if (!run_task && log_msg_fifo.empty())
            break;

        auto entry = log_msg_fifo.front();
        log_msg_fifo.pop_front();

        lock.unlock();

        if (entry) {
            logger_sink->setLogEntry(entry);
        }
    }
}