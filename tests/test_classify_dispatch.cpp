#include <gtest/gtest.h>

#include "handle_update/sources/UpdateSource.h"
#include "handle_update/fs_exceptions.h"

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

} // namespace
