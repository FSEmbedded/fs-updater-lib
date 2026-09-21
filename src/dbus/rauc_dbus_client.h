#pragma once

#include "../rauc/rauc_exceptions.h"
#include "sd_bus_connection.h"
#include "sd_bus_match.h"
#include "../uboot_interface/IUBootEnv.h"
#include "../logger/LoggerHandler.h"
#include "../logger/LoggerEntry.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace rauc {

// -------------------------------------------------------------------------
// Typed return types
// -------------------------------------------------------------------------

using SlotProperties = std::map<std::string, std::string>;
using SlotStatusList = std::vector<std::pair<std::string, SlotProperties>>;
using BundleInfo     = std::map<std::string, std::string>;

struct RaucInstallProgress {
    int32_t     percent = 0;
    std::string message;
    int32_t     depth   = 0;
};

// -------------------------------------------------------------------------
// rauc_dbus_client
// -------------------------------------------------------------------------

class rauc_dbus_client {
public:
    rauc_dbus_client(const std::shared_ptr<UBoot::IUBootEnv>& /*uboot*/,
                     const std::shared_ptr<logger::LoggerHandler>& /*logger*/);

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

    /** Call InspectBundle(path, {}) and return the manifest `compatible`
     *  (nested in the reply's inner "update" dict).
     *  @return compatible string; empty when the manifest carries none
     *  @throw RaucGetArtifactInformation on failure
     *  @throw RaucServiceUnavailable if RAUC service is not running
     */
    std::string getBundleCompatible(const std::string& path);

    /** Call InstallBundle(path, {}) — returns immediately.
     *  Subscribes Completed and NameOwnerChanged before issuing the call.
     *  @throw RaucInstallBundle if RAUC rejects the call or Operation != "idle"
     *  @throw RaucServiceUnavailable if RAUC service is not running
     */
    void installBundle(const std::string& path);

    /** Block until the Completed signal fires or timeout_ms elapses.
     *  timeout_ms == 0 means wait indefinitely.
     *  If progress_cb is set it is called with the current RAUC Progress
     *  percentage (0-100) on each event-loop iteration; 100 is guaranteed
     *  to be called on success before this method returns.
     *  @return true if Completed was received; false if timed out
     *  @throw RaucInstallBundle if Completed result != 0
     *  @throw RaucServiceUnavailable if RAUC service vanished during install
     */
    bool waitForCompletion(uint64_t timeout_ms = 0,
                           std::function<void(int)> progress_cb = nullptr);

    /** Read the Operation property.
     *  @return true if Operation != "idle"; false on read error (non-fatal, logs DEBUG)
     */
    bool isInstalling();

    /** Read the Progress property (isi).
     *  @return Current progress; returns zeroed struct on read error (non-fatal, logs DEBUG)
     */
    RaucInstallProgress getProgress();

private:
    enum class MarkExceptionKind { MarkGood, MarkOtherPartition, Rollback };

    struct InstallState {
        bool completed = false;
        int  result    = 0;
        /* False when the Completed signal arrived but its body could not be
         * read. Kept beside the value rather than encoded in it: the value
         * domain belongs to RAUC. */
        bool result_readable = true;
        bool svc_lost        = false;
    };

    /** Read RAUC's LastError property.
     *  @return RAUC's own sentence about the last failure, or an empty string
     *          when the property is empty or cannot be read. Never throws:
     *          it is called while an install failure is already being
     *          reported, and losing that report to a second failure would
     *          trade the cause for a worse one.
     */
    std::string lastError();

    /** Issue a single Mark D-Bus call; throw on failure. */
    void call_mark(const char* state, const char* slot_id, MarkExceptionKind kind);

    friend struct ParseSvDictAccess; // wire-shape tests

    /** Parse an a{sv} container already entered in msg into a SlotProperties map. */
    static SlotProperties parse_sv_dict(sd_bus_message* msg);

    /** Fired by RAUC's Completed(i) signal. */
    static int on_completed(sd_bus_message* msg, void* userdata, sd_bus_error* ret_err);

    /** Fired by org.freedesktop.DBus.NameOwnerChanged for de.pengutronix.rauc. */
    static int on_name_owner_changed(sd_bus_message* msg, void* userdata, sd_bus_error* ret_err);

    /** Revert BOOT_ORDER to BOOT_ORDER_OLD; logs but never throws. */
    void revert_boot_order() noexcept;

    dbus::SdBusConnection                  bus_;
    std::shared_ptr<UBoot::IUBootEnv>      uboot_;
    std::shared_ptr<logger::LoggerHandler> logger_;

    InstallState         install_state_;
    dbus::SdBusMatchSlot completed_slot_;
    dbus::SdBusMatchSlot name_owner_slot_;
    std::string          install_path_;
};

} // namespace rauc
