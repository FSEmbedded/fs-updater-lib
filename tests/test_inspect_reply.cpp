#include <gtest/gtest.h>

#include "dbus/inspect_reply.h"
#include "dbus/rauc_dbus_client.h"

#include <systemd/sd-bus.h>
#include <systemd/sd-id128.h>
#include <sys/socket.h>

#include <string>

namespace rauc {
struct ParseSvDictAccess {
    static SlotProperties parse(sd_bus_message* m) { return rauc_dbus_client::parse_sv_dict(m); }
};
} // namespace rauc

namespace {

// RAUC's InspectBundle reply is a nested a{sv}: the manifest identity lives
// in an inner "update" dict ("compatible", "version", ...), verified against
// RAUC's r_manifest_to_dict(). These tests build that shape in-process on a
// socketpair-backed peer bus (message creation needs a started bus) and seal
// it — no rauc daemon involved.
class InspectReply : public ::testing::Test {
protected:
    void SetUp() override
    {
        int fds[2] = {-1, -1};
        ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

        ASSERT_GE(sd_bus_new(&bus_), 0);
        ASSERT_GE(sd_bus_new(&peer_), 0);

        // Direct peer-to-peer pair (no daemon): both ends need explicit
        // client/server setup or the auth handshake never completes.
        ASSERT_GE(sd_bus_set_fd(bus_, fds[0], fds[0]), 0);
        ASSERT_GE(sd_bus_set_bus_client(bus_, false), 0);

        sd_id128_t server_id;
        ASSERT_GE(sd_id128_randomize(&server_id), 0);
        ASSERT_GE(sd_bus_set_fd(peer_, fds[1], fds[1]), 0);
        ASSERT_GE(sd_bus_set_server(peer_, 1, server_id), 0);
        ASSERT_GE(sd_bus_set_anonymous(peer_, true), 0);
        ASSERT_GE(sd_bus_set_trusted(peer_, true), 0);

        ASSERT_GE(sd_bus_start(bus_), 0);
        ASSERT_GE(sd_bus_start(peer_), 0);

        ASSERT_GE(sd_bus_message_new_method_call(bus_, &msg_, "de.test", "/de/test",
                                                 "de.test.Iface", "Method"),
                  0);
    }

    void TearDown() override
    {
        // close, don't flush: nothing was ever queued for sending, and a
        // flush would block on the never-draining socketpair peer.
        sd_bus_message_unref(msg_);
        sd_bus_close_unref(bus_);
        sd_bus_close_unref(peer_);
    }

    void seal_and_rewind()
    {
        ASSERT_GE(sd_bus_message_seal(msg_, 1, 0), 0);
        ASSERT_GE(sd_bus_message_rewind(msg_, 1), 0);
    }

    sd_bus*         bus_  = nullptr;
    sd_bus*         peer_ = nullptr;
    sd_bus_message* msg_  = nullptr;
};

TEST_F(InspectReply, ExtractsCompatibleFromNestedUpdateDict)
{
    ASSERT_GE(sd_bus_message_append(msg_, "a{sv}", 2,
                                    "manifest-hash", "s", "abc123",
                                    "update", "a{sv}", 2,
                                        "compatible", "s", "fus-update-board-appfs",
                                        "version", "s", "20260716"),
              0);
    seal_and_rewind();

    EXPECT_EQ(rauc::parse_inspect_bundle_compatible(msg_), "fus-update-board-appfs");
}

TEST_F(InspectReply, ExtractsCompatibleFromDoubleWrappedNestedUpdateDict)
{
    // Some RAUC builds double-wrap "update"'s value on the wire as
    // variant(v) -> variant(a{sv}) -> the manifest dict; the single-wrap
    // test above covers the plain form.
    ASSERT_GE(sd_bus_message_open_container(msg_, SD_BUS_TYPE_ARRAY, "{sv}"), 0);
    ASSERT_GE(sd_bus_message_open_container(msg_, SD_BUS_TYPE_DICT_ENTRY, "sv"), 0);
    ASSERT_GE(sd_bus_message_append(msg_, "s", "update"), 0);
    ASSERT_GE(sd_bus_message_open_container(msg_, SD_BUS_TYPE_VARIANT, "v"), 0);
    ASSERT_GE(sd_bus_message_open_container(msg_, SD_BUS_TYPE_VARIANT, "a{sv}"), 0);
    ASSERT_GE(sd_bus_message_append(msg_, "a{sv}", 2,
                                    "compatible", "s", "fus-update-board-appfs",
                                    "version", "s", "20260716"),
              0);
    ASSERT_GE(sd_bus_message_close_container(msg_), 0); // inner variant (a{sv})
    ASSERT_GE(sd_bus_message_close_container(msg_), 0); // outer variant (v)
    ASSERT_GE(sd_bus_message_close_container(msg_), 0); // dict entry
    ASSERT_GE(sd_bus_message_close_container(msg_), 0); // array
    seal_and_rewind();

    EXPECT_EQ(rauc::parse_inspect_bundle_compatible(msg_), "fus-update-board-appfs");
}

TEST_F(InspectReply, MissingUpdateDictYieldsEmpty)
{
    ASSERT_GE(sd_bus_message_append(msg_, "a{sv}", 1, "manifest-hash", "s", "abc123"), 0);
    seal_and_rewind();

    EXPECT_EQ(rauc::parse_inspect_bundle_compatible(msg_), "");
}

TEST_F(InspectReply, UpdateDictWithoutCompatibleYieldsEmpty)
{
    ASSERT_GE(sd_bus_message_append(msg_, "a{sv}", 1,
                                    "update", "a{sv}", 1, "version", "s", "20260716"),
              0);
    seal_and_rewind();

    EXPECT_EQ(rauc::parse_inspect_bundle_compatible(msg_), "");
}

TEST_F(InspectReply, NonDictUpdateValueYieldsEmpty)
{
    ASSERT_GE(sd_bus_message_append(msg_, "a{sv}", 2,
                                    "update", "s", "bogus",
                                    "manifest-hash", "s", "abc123"),
              0);
    seal_and_rewind();

    EXPECT_EQ(rauc::parse_inspect_bundle_compatible(msg_), "");
}

TEST_F(InspectReply, NullMessageYieldsEmpty)
{
    EXPECT_EQ(rauc::parse_inspect_bundle_compatible(nullptr), "");
}

} // namespace

// A struct-typed variant is skipped whole; the entries around it still parse.
TEST_F(InspectReply, ParseSvDictSkipsStructVariant)
{
    ASSERT_GE(sd_bus_message_append(msg_, "a{sv}", 3,
                                    "before", "s", "one",
                                    "pair", "(ii)", 7, 9,
                                    "after", "s", "two"),
              0);
    seal_and_rewind();
    ASSERT_GT(sd_bus_message_enter_container(msg_, SD_BUS_TYPE_ARRAY, "{sv}"), 0);

    const auto props = rauc::ParseSvDictAccess::parse(msg_);

    EXPECT_EQ(props.at("before"), "one");
    EXPECT_EQ(props.at("after"), "two");
}
