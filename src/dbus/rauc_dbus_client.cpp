#include <fus_updater_lib/config.h>
#include "rauc_dbus_client.h"

#include "../uboot_interface/allowed_uboot_variable_states.h"

#include <chrono>
#include <memory>
#include <string>

namespace {

constexpr const char* RAUC_BUS_NAME  = "de.pengutronix.rauc";
constexpr const char* RAUC_OBJ_PATH  = "/";
constexpr const char* RAUC_INTERFACE = "de.pengutronix.rauc.Installer";

// RAII guard for stack-allocated sd_bus_error
struct BusErrorGuard {
    sd_bus_error err = SD_BUS_ERROR_NULL;
    BusErrorGuard()                                = default;
    BusErrorGuard(const BusErrorGuard&)            = delete;
    BusErrorGuard& operator=(const BusErrorGuard&) = delete;
    ~BusErrorGuard() { sd_bus_error_free(&err); }
};

// RAII owner for heap-allocated sd_bus_message*
using MessagePtr = std::unique_ptr<sd_bus_message, decltype(&sd_bus_message_unref)>;

MessagePtr wrap_message(sd_bus_message* msg) noexcept
{
    return MessagePtr(msg, sd_bus_message_unref);
}

std::string format_bus_error(const sd_bus_error& err)
{
    std::string result;
    if (err.name)
        result = err.name;
    if (err.message) {
        result += ": ";
        result += err.message;
    }
    return result;
}

bool is_service_unavailable(const sd_bus_error& err)
{
    return sd_bus_error_has_name(&err, SD_BUS_ERROR_SERVICE_UNKNOWN)
        || sd_bus_error_has_name(&err, SD_BUS_ERROR_NAME_HAS_NO_OWNER);
}

// sd_bus_add_match filter: NameOwnerChanged for de.pengutronix.rauc only
constexpr const char* NAME_OWNER_MATCH =
    "type='signal',"
    "sender='org.freedesktop.DBus',"
    "path='/org/freedesktop/DBus',"
    "interface='org.freedesktop.DBus',"
    "member='NameOwnerChanged',"
    "arg0='de.pengutronix.rauc'";

} // namespace

namespace rauc {

rauc_dbus_client::rauc_dbus_client(
    const std::shared_ptr<UBoot::UBoot>&          uboot,
    const std::shared_ptr<logger::LoggerHandler>& logger)
    : bus_{}
    , uboot_(uboot)
    , logger_(logger)
{
    logger_->setLogEntry(std::make_shared<logger::LogEntry>(
        RAUC_DOMAIN, "rauc_dbus_client constructed", logger::logLevel::DEBUG));
}

// -------------------------------------------------------------------------
// Private helpers
// -------------------------------------------------------------------------

void rauc_dbus_client::call_mark(
    const char* state, const char* slot_id, MarkExceptionKind kind)
{
    BusErrorGuard   guard;
    sd_bus_message* raw_reply = nullptr;

    const int r = sd_bus_call_method(
        bus_.get(),
        RAUC_BUS_NAME, RAUC_OBJ_PATH, RAUC_INTERFACE,
        "Mark",
        &guard.err, &raw_reply,
        "ss", state, slot_id);

    MessagePtr reply = wrap_message(raw_reply);

    if (r < 0) {
        const std::string report   = format_bus_error(guard.err);
        const bool        svc_gone = is_service_unavailable(guard.err);

        logger_->setLogEntry(std::make_shared<logger::LogEntry>(
            RAUC_DOMAIN,
            std::string("call_mark(") + state + "," + slot_id + "): " + report,
            logger::logLevel::ERROR));

        if (svc_gone)
            throw RaucServiceUnavailable(report);

        switch (kind) {
        case MarkExceptionKind::MarkGood:
            throw RaucMarkGood(report);
        case MarkExceptionKind::MarkOtherPartition:
            throw RaucMarkOtherPartition(report);
        case MarkExceptionKind::Rollback:
            throw RaucRollback(report);
        }
    }
}

// static
SlotProperties rauc_dbus_client::parse_sv_dict(sd_bus_message* msg)
{
    SlotProperties props;

    while (sd_bus_message_enter_container(msg, SD_BUS_TYPE_DICT_ENTRY, "sv") > 0) {
        const char* key = nullptr;
        sd_bus_message_read_basic(msg, 's', &key);

        // enter the variant to inspect its content type
        sd_bus_message_enter_container(msg, SD_BUS_TYPE_VARIANT, nullptr);

        char        inner_type     = '\0';
        const char* inner_contents = nullptr;
        sd_bus_message_peek_type(msg, &inner_type, &inner_contents);

        std::string value;
        switch (inner_type) {
        case SD_BUS_TYPE_STRING:
        case SD_BUS_TYPE_OBJECT_PATH: {
            const char* s = nullptr;
            sd_bus_message_read_basic(msg, inner_type, &s);
            if (s) value = s;
            break;
        }
        case SD_BUS_TYPE_UINT32: {
            uint32_t u = 0;
            sd_bus_message_read_basic(msg, 'u', &u);
            value = std::to_string(u);
            break;
        }
        case SD_BUS_TYPE_INT32: {
            int32_t i = 0;
            sd_bus_message_read_basic(msg, 'i', &i);
            value = std::to_string(i);
            break;
        }
        case SD_BUS_TYPE_BOOLEAN: {
            int b = 0;
            sd_bus_message_read_basic(msg, 'b', &b);
            value = b ? "true" : "false";
            break;
        }
        case SD_BUS_TYPE_UINT64: {
            uint64_t u = 0;
            sd_bus_message_read_basic(msg, 't', &u);
            value = std::to_string(u);
            break;
        }
        default: {
            // Skip unknown types (e.g. arrays in InspectBundle result).
            // Build the full type signature for containers so sd_bus_message_skip
            // can traverse them correctly (arrays need element type, structs need
            // content + closing paren).
            std::string skip_sig(1, inner_type);
            if (inner_type == SD_BUS_TYPE_ARRAY) {
                if (inner_contents)
                    skip_sig += inner_contents;
            } else if (inner_type == SD_BUS_TYPE_STRUCT_BEGIN) {
                if (inner_contents)
                    skip_sig += inner_contents;
                skip_sig += SD_BUS_TYPE_STRUCT_END;
            }
            if (!skip_sig.empty())
                sd_bus_message_skip(msg, skip_sig.c_str());
            break;
        }
        }

        sd_bus_message_exit_container(msg); // exit variant

        if (key)
            props[key] = std::move(value);

        sd_bus_message_exit_container(msg); // exit dict entry
    }

    return props;
}

// -------------------------------------------------------------------------
// Synchronous public methods
// -------------------------------------------------------------------------

void rauc_dbus_client::markUpdateAsSuccessful()
{
    logger_->setLogEntry(std::make_shared<logger::LogEntry>(
        RAUC_DOMAIN, "markUpdateAsSuccessful: Mark(good,booted)", logger::logLevel::DEBUG));
    call_mark("good", "booted", MarkExceptionKind::MarkGood);
}

void rauc_dbus_client::markOtherPartition()
{
    logger_->setLogEntry(std::make_shared<logger::LogEntry>(
        RAUC_DOMAIN, "markOtherPartition: Mark(good,other)", logger::logLevel::DEBUG));
    call_mark("good", "other", MarkExceptionKind::MarkOtherPartition);
}

void rauc_dbus_client::rollback()
{
    logger_->setLogEntry(std::make_shared<logger::LogEntry>(
        RAUC_DOMAIN, "rollback: Mark(active,other)", logger::logLevel::DEBUG));
    call_mark("active", "other", MarkExceptionKind::Rollback);

    logger_->setLogEntry(std::make_shared<logger::LogEntry>(
        RAUC_DOMAIN, "rollback: Mark(good,other)", logger::logLevel::DEBUG));
    call_mark("good", "other", MarkExceptionKind::MarkOtherPartition);
}

SlotStatusList rauc_dbus_client::getSlotStatus()
{
    logger_->setLogEntry(std::make_shared<logger::LogEntry>(
        RAUC_DOMAIN, "getSlotStatus: GetSlotStatus()", logger::logLevel::DEBUG));

    BusErrorGuard   guard;
    sd_bus_message* raw_reply = nullptr;

    const int r = sd_bus_call_method(
        bus_.get(),
        RAUC_BUS_NAME, RAUC_OBJ_PATH, RAUC_INTERFACE,
        "GetSlotStatus",
        &guard.err, &raw_reply,
        "");

    MessagePtr reply = wrap_message(raw_reply);

    if (r < 0) {
        const std::string report   = format_bus_error(guard.err);
        const bool        svc_gone = is_service_unavailable(guard.err);

        logger_->setLogEntry(std::make_shared<logger::LogEntry>(
            RAUC_DOMAIN, "getSlotStatus: " + report, logger::logLevel::ERROR));

        if (svc_gone)
            throw RaucServiceUnavailable(report);
        throw RaucGetStatus(report);
    }

    // parse a(sa{sv})
    SlotStatusList result;
    sd_bus_message_enter_container(reply.get(), SD_BUS_TYPE_ARRAY, "(sa{sv})");

    while (sd_bus_message_enter_container(reply.get(), SD_BUS_TYPE_STRUCT, "sa{sv}") > 0) {
        const char* slot_name = nullptr;
        sd_bus_message_read_basic(reply.get(), 's', &slot_name);

        sd_bus_message_enter_container(reply.get(), SD_BUS_TYPE_ARRAY, "{sv}");
        SlotProperties props = parse_sv_dict(reply.get());
        sd_bus_message_exit_container(reply.get()); // exit a{sv}

        if (slot_name)
            result.emplace_back(slot_name, std::move(props));

        sd_bus_message_exit_container(reply.get()); // exit (sa{sv})
    }

    sd_bus_message_exit_container(reply.get()); // exit outer a(sa{sv})

    return result;
}

BundleInfo rauc_dbus_client::getInfoAboutBundle(const std::string& path)
{
    logger_->setLogEntry(std::make_shared<logger::LogEntry>(
        RAUC_DOMAIN, "getInfoAboutBundle: InspectBundle(" + path + ")", logger::logLevel::DEBUG));

    BusErrorGuard   guard;
    sd_bus_message* raw_reply = nullptr;

    // Pass empty options dict (0 entries) as required by the D-Bus signature "sa{sv}"
    const int r = sd_bus_call_method(
        bus_.get(),
        RAUC_BUS_NAME, RAUC_OBJ_PATH, RAUC_INTERFACE,
        "InspectBundle",
        &guard.err, &raw_reply,
        "sa{sv}", path.c_str(), 0);

    MessagePtr reply = wrap_message(raw_reply);

    if (r < 0) {
        const std::string report   = format_bus_error(guard.err);
        const bool        svc_gone = is_service_unavailable(guard.err);

        logger_->setLogEntry(std::make_shared<logger::LogEntry>(
            RAUC_DOMAIN, "getInfoAboutBundle: " + report, logger::logLevel::ERROR));

        if (svc_gone)
            throw RaucServiceUnavailable(report);
        throw RaucGetArtifactInformation(path, report);
    }

    // parse a{sv}
    sd_bus_message_enter_container(reply.get(), SD_BUS_TYPE_ARRAY, "{sv}");
    BundleInfo info = parse_sv_dict(reply.get());
    sd_bus_message_exit_container(reply.get());

    return info;
}

// -------------------------------------------------------------------------
// Async install
// -------------------------------------------------------------------------

void rauc_dbus_client::installBundle(const std::string& path)
{
    install_path_  = path;
    install_state_ = {};

    // 1. Verify Operation == "idle" (advisory; TOCTOU between check and call).
    {
        BusErrorGuard guard;
        char*         raw = nullptr;
        const int r = sd_bus_get_property_string(
            bus_.get(),
            RAUC_BUS_NAME, RAUC_OBJ_PATH, RAUC_INTERFACE,
            "Operation", &guard.err, &raw);
        auto op = std::unique_ptr<char, decltype(&free)>(raw, free);

        if (r < 0) {
            const std::string report   = format_bus_error(guard.err);
            const bool        svc_gone = is_service_unavailable(guard.err);
            logger_->setLogEntry(std::make_shared<logger::LogEntry>(
                RAUC_DOMAIN,
                "installBundle: Operation check failed: " + report,
                logger::logLevel::ERROR));
            if (svc_gone)
                throw RaucServiceUnavailable(report);
            throw RaucInstallBundle(path, "Operation check failed: " + report);
        }

        if (raw && std::string(raw) != "idle") {
            const std::string msg = std::string("RAUC not idle: ") + raw;
            logger_->setLogEntry(std::make_shared<logger::LogEntry>(
                RAUC_DOMAIN, "installBundle: " + msg, logger::logLevel::ERROR));
            throw RaucInstallBundle(path, msg);
        }
    }

    // 2. Subscribe Completed signal BEFORE calling InstallBundle (fast-fail race).
    {
        sd_bus_slot* raw = nullptr;
        const int r = sd_bus_match_signal(
            bus_.get(), &raw,
            RAUC_BUS_NAME, RAUC_OBJ_PATH, RAUC_INTERFACE,
            "Completed", on_completed, this);
        if (r < 0) {
            const std::string msg = "Failed to subscribe Completed: " + std::to_string(r);
            logger_->setLogEntry(std::make_shared<logger::LogEntry>(
                RAUC_DOMAIN, "installBundle: " + msg, logger::logLevel::ERROR));
            throw RaucInstallBundle(path, msg);
        }
        completed_slot_ = dbus::SdBusMatchSlot(raw);
    }

    // 3. Subscribe NameOwnerChanged watchdog for service restart detection.
    {
        sd_bus_slot* raw = nullptr;
        const int r = sd_bus_add_match(
            bus_.get(), &raw, NAME_OWNER_MATCH, on_name_owner_changed, this);
        if (r < 0) {
            const std::string msg = "Failed to subscribe NameOwnerChanged: " + std::to_string(r);
            logger_->setLogEntry(std::make_shared<logger::LogEntry>(
                RAUC_DOMAIN, "installBundle: " + msg, logger::logLevel::ERROR));
            completed_slot_.reset();
            throw RaucInstallBundle(path, msg);
        }
        name_owner_slot_ = dbus::SdBusMatchSlot(raw);
    }

    // 4. Issue InstallBundle(sa{sv}) — returns immediately; install runs async.
    {
        BusErrorGuard guard;
        const int r = sd_bus_call_method(
            bus_.get(),
            RAUC_BUS_NAME, RAUC_OBJ_PATH, RAUC_INTERFACE,
            "InstallBundle",
            &guard.err, nullptr,
            "sa{sv}", path.c_str(), 0);

        if (r < 0) {
            const std::string report   = format_bus_error(guard.err);
            const bool        svc_gone = is_service_unavailable(guard.err);
            logger_->setLogEntry(std::make_shared<logger::LogEntry>(
                RAUC_DOMAIN,
                "installBundle(" + path + "): " + report,
                logger::logLevel::ERROR));
            completed_slot_.reset();
            name_owner_slot_.reset();
            revert_boot_order();
            if (svc_gone)
                throw RaucServiceUnavailable(report);
            throw RaucInstallBundle(path, report);
        }
    }

    logger_->setLogEntry(std::make_shared<logger::LogEntry>(
        RAUC_DOMAIN,
        "installBundle(" + path + "): accepted by RAUC",
        logger::logLevel::DEBUG));
}

bool rauc_dbus_client::waitForCompletion(uint64_t timeout_ms,
                                         std::function<void(int)> progress_cb)
{
    using clock    = std::chrono::steady_clock;
    using ms       = std::chrono::milliseconds;
    using us       = std::chrono::microseconds;

    const bool has_timeout = (timeout_ms > 0);
    const auto deadline    = has_timeout ? clock::now() + ms(timeout_ms)
                                         : clock::time_point::max();

    while (!install_state_.completed && !install_state_.svc_lost) {
        while (sd_bus_process(bus_.get(), nullptr) > 0) {}   // drain pending events

        if (install_state_.completed || install_state_.svc_lost)
            break;

        if (progress_cb) {
            auto p = getProgress();
            progress_cb(p.percent);
        }

        if (has_timeout && clock::now() >= deadline) {
            logger_->setLogEntry(std::make_shared<logger::LogEntry>(
                RAUC_DOMAIN,
                "waitForCompletion: timed out after " + std::to_string(timeout_ms) + " ms",
                logger::logLevel::WARNING));
            return false;
        }

        const uint64_t wait_usec = has_timeout
            ? static_cast<uint64_t>(std::max(int64_t{0},
                std::chrono::duration_cast<us>(deadline - clock::now()).count()))
            : UINT64_MAX;
        sd_bus_wait(bus_.get(), wait_usec);
    }

    completed_slot_.reset();
    name_owner_slot_.reset();

    if (install_state_.svc_lost) {
        logger_->setLogEntry(std::make_shared<logger::LogEntry>(
            RAUC_DOMAIN,
            "waitForCompletion: RAUC service vanished during install",
            logger::logLevel::ERROR));
        revert_boot_order();
        throw RaucServiceUnavailable("RAUC service vanished during install of " + install_path_);
    }

    if (install_state_.result != 0) {
        const std::string report =
            "Completed signal: result=" + std::to_string(install_state_.result);
        logger_->setLogEntry(std::make_shared<logger::LogEntry>(
            RAUC_DOMAIN, "waitForCompletion: " + report, logger::logLevel::ERROR));
        revert_boot_order();
        throw RaucInstallBundle(install_path_, report);
    }

    logger_->setLogEntry(std::make_shared<logger::LogEntry>(
        RAUC_DOMAIN, "waitForCompletion: install completed successfully",
        logger::logLevel::DEBUG));
    if (progress_cb)
        progress_cb(100);
    return true;
}

// -------------------------------------------------------------------------
// Progress / status polls
// -------------------------------------------------------------------------

bool rauc_dbus_client::isInstalling()
{
    BusErrorGuard guard;
    char*         raw = nullptr;

    const int r = sd_bus_get_property_string(
        bus_.get(),
        RAUC_BUS_NAME, RAUC_OBJ_PATH, RAUC_INTERFACE,
        "Operation", &guard.err, &raw);

    auto op = std::unique_ptr<char, decltype(&free)>(raw, free);

    if (r < 0) {
        // Non-fatal per progress-monitor contract; waitForCompletion() handles recovery
        logger_->setLogEntry(std::make_shared<logger::LogEntry>(
            RAUC_DOMAIN,
            "isInstalling: " + format_bus_error(guard.err),
            logger::logLevel::DEBUG));
        return false;
    }

    return raw && std::string(raw) != "idle";
}

RaucInstallProgress rauc_dbus_client::getProgress()
{
    BusErrorGuard   guard;
    sd_bus_message* raw_reply = nullptr;

    const int r = sd_bus_get_property(
        bus_.get(),
        RAUC_BUS_NAME, RAUC_OBJ_PATH, RAUC_INTERFACE,
        "Progress", &guard.err, &raw_reply, "(isi)");

    MessagePtr reply = wrap_message(raw_reply);

    if (r < 0) {
        // Non-fatal per progress-monitor contract; caller continues polling loop
        logger_->setLogEntry(std::make_shared<logger::LogEntry>(
            RAUC_DOMAIN,
            "getProgress: " + format_bus_error(guard.err),
            logger::logLevel::DEBUG));
        return {};
    }

    RaucInstallProgress progress{};
    const char*         message = nullptr;
    sd_bus_message_read(reply.get(), "(isi)", &progress.percent, &message, &progress.depth);
    if (message)
        progress.message = message;
    return progress;
}

// -------------------------------------------------------------------------
// Signal callbacks (static)
// -------------------------------------------------------------------------

int rauc_dbus_client::on_completed(sd_bus_message* msg, void* userdata, sd_bus_error* /*ret_err*/)
{
    auto*   self   = static_cast<rauc_dbus_client*>(userdata);
    int32_t result = 0;
    sd_bus_message_read(msg, "i", &result);
    self->install_state_.completed = true;
    self->install_state_.result    = static_cast<int>(result);
    return 0;
}

int rauc_dbus_client::on_name_owner_changed(
    sd_bus_message* msg, void* userdata, sd_bus_error* /*ret_err*/)
{
    // Match filter constrains arg0='de.pengutronix.rauc'; arg2 is the new owner.
    auto*       self      = static_cast<rauc_dbus_client*>(userdata);
    const char* name      = nullptr;
    const char* old_owner = nullptr;
    const char* new_owner = nullptr;
    sd_bus_message_read(msg, "sss", &name, &old_owner, &new_owner);
    if (new_owner && new_owner[0] == '\0')   // empty new owner → service vanished
        self->install_state_.svc_lost = true;
    return 0;
}

// -------------------------------------------------------------------------
// U-Boot helpers
// -------------------------------------------------------------------------

void rauc_dbus_client::revert_boot_order() noexcept
{
    try {
        UBoot::UBoot::EnvTransaction txn(*uboot_);
        const std::string boot_order =
            uboot_->getVariable("BOOT_ORDER", allowed_boot_order_variables);
        const std::string boot_order_old =
            uboot_->getVariable("BOOT_ORDER_OLD", allowed_boot_order_variables);

        if (boot_order != boot_order_old) {
            uboot_->addVariable("BOOT_ORDER", boot_order_old);
            uboot_->flushEnvironment();
            logger_->setLogEntry(std::make_shared<logger::LogEntry>(
                RAUC_DOMAIN,
                "revert_boot_order: BOOT_ORDER " + boot_order + " -> " + boot_order_old,
                logger::logLevel::WARNING));
        }
    } catch (const std::exception& ex) {
        logger_->setLogEntry(std::make_shared<logger::LogEntry>(
            RAUC_DOMAIN,
            std::string("revert_boot_order: failed: ") + ex.what(),
            logger::logLevel::ERROR));
    } catch (...) {
        logger_->setLogEntry(std::make_shared<logger::LogEntry>(
            RAUC_DOMAIN, "revert_boot_order: unknown exception",
            logger::logLevel::ERROR));
    }
}

} // namespace rauc
