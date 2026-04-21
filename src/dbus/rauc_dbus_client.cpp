#include <fus_updater_lib/config.h>
#include "rauc_dbus_client.h"

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

} // namespace rauc
