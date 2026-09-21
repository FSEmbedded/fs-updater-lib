#include "inspect_reply.h"

#include <cstring>

namespace rauc {

namespace {

/** Read the "compatible" entry out of the inner update dict; the cursor
 *  must sit inside the entered a{sv}. Returns empty when absent. */
std::string read_compatible_from_update_dict(sd_bus_message* m)
{
    std::string compatible;

    while (sd_bus_message_enter_container(m, SD_BUS_TYPE_DICT_ENTRY, "sv") > 0) {
        const char* key = nullptr;
        if (sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &key) < 0) {
            break;
        }

        if (key != nullptr && std::strcmp(key, "compatible") == 0 &&
            sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "s") > 0) {
            const char* value = nullptr;
            if (sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &value) >= 0 &&
                value != nullptr) {
                compatible = value;
            }
            sd_bus_message_exit_container(m);
        } else if (sd_bus_message_skip(m, "v") < 0) {
            break;
        }

        sd_bus_message_exit_container(m);
    }

    return compatible;
}

} // namespace

std::string parse_inspect_bundle_compatible(sd_bus_message* reply) noexcept
{
    std::string compatible;

    if (reply == nullptr) {
        return compatible;
    }

    if (sd_bus_message_enter_container(reply, SD_BUS_TYPE_ARRAY, "{sv}") <= 0) {
        return compatible;
    }

    while (sd_bus_message_enter_container(reply, SD_BUS_TYPE_DICT_ENTRY, "sv") > 0) {
        const char* key = nullptr;
        if (sd_bus_message_read_basic(reply, SD_BUS_TYPE_STRING, &key) < 0) {
            break;
        }

        // The manifest identity is nested: "update" holds its own a{sv}. Some
        // RAUC builds double-wrap it on the wire -- variant(v) ->
        // variant(a{sv}) -> the dict -- others don't. Try the single-wrap
        // form first, then unwrap one extra variant level. A differently
        // typed "update" value is skipped, not an error.
        if (key != nullptr && std::strcmp(key, "update") == 0) {
            bool single_wrap = sd_bus_message_enter_container(reply, SD_BUS_TYPE_VARIANT, "a{sv}") > 0;
            bool double_wrap = false;
            if (!single_wrap) {
                const bool outer = sd_bus_message_enter_container(reply, SD_BUS_TYPE_VARIANT, "v") > 0;
                if (outer) {
                    double_wrap = sd_bus_message_enter_container(reply, SD_BUS_TYPE_VARIANT, "a{sv}") > 0;
                    if (!double_wrap) {
                        sd_bus_message_exit_container(reply);
                    }
                }
            }
            if (single_wrap || double_wrap) {
                if (sd_bus_message_enter_container(reply, SD_BUS_TYPE_ARRAY, "{sv}") > 0) {
                    compatible = read_compatible_from_update_dict(reply);
                    sd_bus_message_exit_container(reply);
                }
                sd_bus_message_exit_container(reply);
                if (double_wrap) {
                    sd_bus_message_exit_container(reply);
                }
            } else if (sd_bus_message_skip(reply, "v") < 0) {
                break;
            }
        } else if (sd_bus_message_skip(reply, "v") < 0) {
            break;
        }

        sd_bus_message_exit_container(reply);

        if (!compatible.empty()) {
            break;
        }
    }

    sd_bus_message_exit_container(reply);

    return compatible;
}

} // namespace rauc
