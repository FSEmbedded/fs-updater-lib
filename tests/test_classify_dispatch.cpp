#include <gtest/gtest.h>

#include "handle_update/sources/UpdateSource.h"
#include "handle_update/fs_exceptions.h"

#include <cerrno>
#include <cstdint>

namespace {

fs::UpdateArtifacts both()
{
    fs::UpdateArtifacts a;
    a.firmware    = "/stage/update.fw";
    a.application = "/stage/update.app";
    return a;
}

TEST(ClassifyDispatch, FirmwareAndApplicationIsKind3)
{
    EXPECT_EQ(fs::classify_dispatch(both()), fs::DispatchKind::FirmwareAndApplication);
    EXPECT_EQ(static_cast<std::uint8_t>(fs::DispatchKind::FirmwareAndApplication), 3u);
}

TEST(ClassifyDispatch, FirmwareOnlyIsKind1)
{
    fs::UpdateArtifacts a;
    a.firmware = "/stage/update.fw";
    EXPECT_EQ(fs::classify_dispatch(a), fs::DispatchKind::Firmware);
    EXPECT_EQ(static_cast<std::uint8_t>(fs::DispatchKind::Firmware), 1u);
}

TEST(ClassifyDispatch, ApplicationOnlyIsKind2)
{
    fs::UpdateArtifacts a;
    a.application = "/stage/update.app";
    EXPECT_EQ(fs::classify_dispatch(a), fs::DispatchKind::Application);
    EXPECT_EQ(static_cast<std::uint8_t>(fs::DispatchKind::Application), 2u);
}

TEST(ClassifyDispatch, NoArtifactsThrowsInvalidUpdate)
{
    fs::UpdateArtifacts empty;
    EXPECT_THROW((void)fs::classify_dispatch(empty), fs::GenericException);
}

TEST(ClassifyDispatch, BootloaderOnlyIsKind4)
{
    fs::UpdateArtifacts a;
    a.bootloader = "/upload/bootloader.raucb";
    EXPECT_EQ(fs::classify_dispatch(a), fs::DispatchKind::Bootloader);
    EXPECT_EQ(static_cast<std::uint8_t>(fs::DispatchKind::Bootloader), 4u);
}

// A bootloader bundle installs without a slot switch, so it cannot share a
// dispatch with a payload that needs one. Refused with the same code as the
// empty set: the caller sees one "invalid update", not a new category.
TEST(ClassifyDispatch, BootloaderWithFirmwareIsRefusedWithEPERM)
{
    fs::UpdateArtifacts a;
    a.bootloader = "/upload/bootloader.raucb";
    a.firmware = "/stage/update.fw";
    try {
        (void)fs::classify_dispatch(a);
        FAIL() << "bootloader beside firmware was dispatched";
    } catch (const fs::GenericException &e) {
        EXPECT_EQ(e.errorno, EPERM);
    }
}

TEST(ClassifyDispatch, BootloaderWithApplicationIsRefusedWithEPERM)
{
    fs::UpdateArtifacts a;
    a.bootloader = "/upload/bootloader.raucb";
    a.application = "/stage/update.app";
    try {
        (void)fs::classify_dispatch(a);
        FAIL() << "bootloader beside application was dispatched";
    } catch (const fs::GenericException &e) {
        EXPECT_EQ(e.errorno, EPERM);
    }
}

} // namespace
