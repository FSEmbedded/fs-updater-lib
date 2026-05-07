#include "handle_update/Descriptor.h"
#include "handle_update/fs_exceptions.h"

#include <json/json.h>

#include <cerrno>
#include <memory>
#include <string>
#include <utility>

namespace fs {

namespace {

MemberType type_from_string(const std::string& s) noexcept
{
    if (s == "firmware") return MemberType::Firmware;
    if (s == "app") return MemberType::Application;
    if (s == "manifest") return MemberType::Manifest;
    return MemberType::Unknown;
}

void require_member_field(const Json::Value& m, const char* key)
{
    if (!m.isMember(key)) {
        throw GenericException(
            std::string("v2.0 descriptor: member missing required field '") + key + "'",
            EINVAL);
    }
}

} // namespace

Descriptor parse_descriptor(std::string_view json)
{
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;
    auto reader = std::unique_ptr<Json::CharReader>(builder.newCharReader());
    if (!reader->parse(json.data(), json.data() + json.size(), &root, &errors)) {
        throw GenericException("v2.0 descriptor: malformed JSON: " + errors, EINVAL);
    }
    if (!root.isMember("version")) {
        throw GenericException("v2.0 descriptor: missing 'version' field", EINVAL);
    }
    if (!root.isMember("members")) {
        throw GenericException("v2.0 descriptor: missing 'members' array", EINVAL);
    }
    const Json::Value& members = root["members"];
    if (!members.isArray() || members.empty()) {
        throw GenericException("v2.0 descriptor: 'members' must be a non-empty array", EINVAL);
    }

    Descriptor d;
    d.version = root["version"].asString();
    if (root.isMember("fw_version")) {
        d.fw_version = root["fw_version"].asString();
    }
    if (root.isMember("app_version")) {
        d.app_version = root["app_version"].asString();
    }

    d.members.reserve(members.size());
    for (const auto& m : members) {
        require_member_field(m, "name");
        require_member_field(m, "type");
        require_member_field(m, "offset");
        require_member_field(m, "size");
        require_member_field(m, "sha256");
        Member out;
        out.name = m["name"].asString();
        out.type = type_from_string(m["type"].asString());
        out.offset = m["offset"].asUInt64();
        out.size = m["size"].asUInt64();
        out.sha256 = m["sha256"].asString();
        d.members.push_back(std::move(out));
    }
    return d;
}

} // namespace fs
