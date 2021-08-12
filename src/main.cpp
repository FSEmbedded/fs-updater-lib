#include "logger/LoggerHandler.h"
#include "logger/LoggerSinkStdout.h"
#include "logger/LoggerEntry.h"
#include "handle_update/fsupdate.h"
#include <iostream>

int main(void)
{   
    std::shared_ptr<logger::LoggerSinkBase> sink = std::make_shared<logger::LoggerSinkStdout>(logger::logLevel::DEBUG);
    std::shared_ptr<logger::LoggerHandler> handler = logger::LoggerHandler::initLogger(sink);

    fs::FSUpdate updater(handler);

    try
    {
        updater.update_firmware("/tmp/test_fw");
        std::cout << "Firmware update successfull" << std::endl;
    }
    catch(const fs::UpdateInProgress &e)
    {
        std::cerr  << "Exception Update Handler: " << e.what() << std::endl;
        updater.commit_update();
    }
    catch(const fs::BaseFSUpdateException &e)
    {
        std::cerr << "Exception Firmware Image: " << e.what() << std::endl;
    }

    return 0;
}