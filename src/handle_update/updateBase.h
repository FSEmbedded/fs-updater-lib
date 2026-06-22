#pragma once
#include "updateDefinitions.h"
#include "../uboot_interface/UBoot.h"
#include "../logger/LoggerHandler.h"
#include "../logger/LoggerEntry.h"
#include "./../BaseException.h"
#include <functional>
#include <memory>
#include <string>

constexpr char BASE_UPDATE[] = "base update";

namespace updater
{
    using ProgressCb = std::function<void(int)>;

    ///////////////////////////////////////////////////////////////////////////
    /// updateBase declaration
    ///////////////////////////////////////////////////////////////////////////
    /**
     * Base class of Updater. Interface all needed functions.
     */
    class updateBase
    {
    protected:
        std::shared_ptr<UBoot::UBoot> uboot_handler;
        std::shared_ptr<logger::LoggerHandler> logger;
        ProgressCb progress_cb_;

    public:
        updateBase(const std::shared_ptr<UBoot::UBoot> & /*ptr*/, const std::shared_ptr<logger::LoggerHandler> & /*logger*/);
        virtual ~updateBase();

        void setProgressCallback(ProgressCb cb) { progress_cb_ = std::move(cb); }

        // Disable copy and move operations
        updateBase(const updateBase &) = delete;
        updateBase &operator=(const updateBase &) = delete;
        updateBase(updateBase &&) = delete;
        updateBase &operator=(updateBase &&) = delete;

        /**
         * Abstract install function. Interface for install image.
         * @param path_object Path to the update image
         */
        virtual void install(const std::string &) = 0;

        /**
         * Abstract rollback function. Interface for roll to next older version back.
         */
        virtual void rollback() = 0;

        /**
         * Return current software version.
         * @return Software version of current state.
         */
        virtual version_t getCurrentVersion() = 0;
    };
}
