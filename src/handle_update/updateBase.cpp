#include "updateBase.h"

updater::updateBase::updateBase(const std::shared_ptr<UBoot::IUBootEnv> & ptr, const std::shared_ptr<logger::LoggerHandler> &logger):
    uboot_handler(ptr),
    logger(logger)
{
    
}

updater::updateBase::~updateBase()
{

}