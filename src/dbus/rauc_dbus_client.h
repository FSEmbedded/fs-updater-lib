#pragma once

#include "../rauc/rauc_handler.h"
#include "sd_bus_connection.h"
#include "../uboot_interface/UBoot.h"
#include "../logger/LoggerHandler.h"
#include "../logger/LoggerEntry.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace rauc {

// -------------------------------------------------------------------------
// Additional exception classes for D-Bus-specific failures
// -------------------------------------------------------------------------

class RaucMarkGood : public RaucBaseException {
public:
    explicit RaucMarkGood(const std::string& error_report)
    {
        this->error_msg    = "Error during Mark(good, booted)";
        this->error_report = error_report;
    }
};

class RaucServiceUnavailable : public RaucBaseException {
public:
    explicit RaucServiceUnavailable(const std::string& error_report)
    {
        this->error_msg    = "RAUC D-Bus service unavailable (de.pengutronix.rauc)";
        this->error_report = error_report;
    }
};

// -------------------------------------------------------------------------
// Typed return types
// -------------------------------------------------------------------------

using SlotProperties = std::map<std::string, std::string>;
using SlotStatusList = std::vector<std::pair<std::string, SlotProperties>>;
using BundleInfo     = std::map<std::string, std::string>;

// -------------------------------------------------------------------------
// rauc_dbus_client
// -------------------------------------------------------------------------

class rauc_dbus_client {
public:
    rauc_dbus_client(const std::shared_ptr<UBoot::UBoot>&,
                     const std::shared_ptr<logger::LoggerHandler>&);

    rauc_dbus_client(const rauc_dbus_client&)            = delete;
    rauc_dbus_client& operator=(const rauc_dbus_client&) = delete;
    rauc_dbus_client(rauc_dbus_client&&)                 = delete;
    rauc_dbus_client& operator=(rauc_dbus_client&&)      = delete;

    /** Call Mark("good","booted"). NOT used by the 13-state commit flow.
     *  @throw RaucMarkGood on failure
     *  @throw RaucServiceUnavailable if RAUC service is not running
     */
    void markUpdateAsSuccessful();

    /** Call Mark("good","other").
     *  @throw RaucMarkOtherPartition on failure
     *  @throw RaucServiceUnavailable if RAUC service is not running
     */
    void markOtherPartition();

    /** Call Mark("active","other") then Mark("good","other").
     *  @throw RaucRollback if the first Mark fails
     *  @throw RaucMarkOtherPartition if the second Mark fails
     *  @throw RaucServiceUnavailable if RAUC service is not running
     */
    void rollback();

    /** Call GetSlotStatus().
     *  @return List of (slot_name, property_map) pairs; values are strings.
     *  @throw RaucGetStatus on failure
     *  @throw RaucServiceUnavailable if RAUC service is not running
     */
    SlotStatusList getSlotStatus();

    /** Call InspectBundle(path, {}).
     *  @return Map of bundle metadata; values are strings.
     *  @throw RaucGetArtifactInformation on failure
     *  @throw RaucServiceUnavailable if RAUC service is not running
     */
    BundleInfo getInfoAboutBundle(const std::string& path);

private:
    enum class MarkExceptionKind { MarkGood, MarkOtherPartition, Rollback };

    /** Issue a single Mark D-Bus call; throw on failure. */
    void call_mark(const char* state, const char* slot_id, MarkExceptionKind kind);

    /** Parse an a{sv} container already entered in msg into a SlotProperties map. */
    static SlotProperties parse_sv_dict(sd_bus_message* msg);

    dbus::SdBusConnection                  bus_;
    std::shared_ptr<UBoot::UBoot>          uboot_;
    std::shared_ptr<logger::LoggerHandler> logger_;
};

} // namespace rauc
