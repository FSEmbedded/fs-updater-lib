#include <gtest/gtest.h>

#include "rauc/rauc_exceptions.h"

#include <set>
#include <string>

// Pins the one property that decides whether a RAUC failure can be told apart
// at all: the report has to reach what().
//
// Every consumer of these exceptions -- updateFirmware.cpp, fsupdate.cpp, the
// service's real_updater.cpp and the CLI -- reads what() and nothing else, so
// a report kept in a second field that only report() exposes is a report no
// operator ever sees. Before this was composed, six distinct reasons from the
// install path collapsed into two consumer-visible sentences, and a wrong
// compatible looked exactly like a downgrade refusal.

namespace {

// The six shapes the install path can end in, with the reports the D-Bus
// client builds for them. The texts are examples, not a contract; what is
// pinned is that six causes stay six sentences.
std::set<std::string> install_path_texts()
{
    return {
        std::string(rauc::RaucServiceUnavailable("no bus name owner").what()),
        std::string(rauc::RaucInstallBundle("/tmp/a.raucb", "RAUC busy: installing").what()),
        std::string(rauc::RaucInstallBundle("/tmp/a.raucb",
                                            "org.freedesktop.DBus.Error.Failed: rejected").what()),
        std::string(rauc::RaucInstallBundle(
            "/tmp/a.raucb",
            "Completed signal: result=1 (Compatible mismatch)").what()),
        std::string(rauc::RaucServiceUnavailable("service vanished during install").what()),
        std::string(rauc::RaucServiceUnavailable(
            "bus connection lost while waiting: Transport endpoint is not connected").what()),
    };
}

} // namespace

TEST(RaucFailureTexts, InstallBundleCarriesTheReport)
{
    const rauc::RaucInstallBundle e("/tmp/a.raucb", "Compatible mismatch");
    const std::string             text = e.what();

    EXPECT_NE(text.find("Compatible mismatch"), std::string::npos)
        << "the reason is not in what(); no consumer reads report(): " << text;
    EXPECT_NE(text.find("/tmp/a.raucb"), std::string::npos) << text;
}

TEST(RaucFailureTexts, ServiceUnavailableCarriesTheReport)
{
    // Two situations throw this and call for different operator actions: the
    // service was never there, and it went away mid-install.
    const rauc::RaucServiceUnavailable never("no bus name owner");
    const rauc::RaucServiceUnavailable vanished("service vanished during install");

    EXPECT_NE(std::string(never.what()), std::string(vanished.what()));
    EXPECT_NE(std::string(vanished.what()).find("vanished"), std::string::npos)
        << vanished.what();
}

TEST(RaucFailureTexts, AnEmptyReportLeavesNoDanglingSeparator)
{
    const rauc::RaucInstallBundle e("/tmp/a.raucb", "");
    const std::string             text = e.what();

    EXPECT_EQ(text.back(), '"') << "an empty report must not add punctuation: " << text;
}

TEST(RaucFailureTexts, SixShapesStaySixSentences)
{
    // Before the report reached what() this set held two entries.
    EXPECT_EQ(install_path_texts().size(), 6u);
}

// The five classes below sit outside the install path (manifest read,
// mark-good, mark-other, rollback, get-status) and used to set error_msg
// directly instead of composing it with the report, so their what() carried
// no reason at all -- not a collapse of distinct reasons into one text, but
// no reason ever reaching the caller. Each gets its own test rather than
// joining install_path_texts(), since that set is scoped to the install path.

TEST(RaucFailureTexts, GetArtifactInformationCarriesTheReport)
{
    const rauc::RaucGetArtifactInformation e("/tmp/a.raucb", "signature verification failed");
    const std::string                      text = e.what();

    EXPECT_NE(text.find("signature verification failed"), std::string::npos)
        << "the reason is not in what(): " << text;
    EXPECT_NE(text.find("/tmp/a.raucb"), std::string::npos) << text;
}

TEST(RaucFailureTexts, MarkOtherPartitionCarriesTheReport)
{
    const rauc::RaucMarkOtherPartition e("no such slot");
    const std::string                  text = e.what();

    EXPECT_NE(text.find("no such slot"), std::string::npos)
        << "the reason is not in what(): " << text;
}

TEST(RaucFailureTexts, RollbackCarriesTheReport)
{
    const rauc::RaucRollback e("slot B has no valid image");
    const std::string        text = e.what();

    EXPECT_NE(text.find("slot B has no valid image"), std::string::npos)
        << "the reason is not in what(): " << text;
}

TEST(RaucFailureTexts, GetStatusCarriesTheReport)
{
    const rauc::RaucGetStatus e("no such interface");
    const std::string         text = e.what();

    EXPECT_NE(text.find("no such interface"), std::string::npos)
        << "the reason is not in what(): " << text;
}

TEST(RaucFailureTexts, MarkGoodCarriesTheReport)
{
    const rauc::RaucMarkGood e("mark-good called on a bad slot");
    const std::string        text = e.what();

    EXPECT_NE(text.find("mark-good called on a bad slot"), std::string::npos)
        << "the reason is not in what(): " << text;
}

// AnEmptyReportLeavesNoDanglingSeparator above pins this for RaucInstallBundle;
// these five classes share the same compose() call, so the same case applies
// to each of them: an empty report must leave what() exactly at the plain
// message, with no trailing ": ".
TEST(RaucFailureTexts, EmptyReportLeavesNoDanglingSeparatorForEachNewlyComposedClass)
{
    EXPECT_EQ(std::string(rauc::RaucGetArtifactInformation("/tmp/a.raucb", "").what()),
              "Error during gaining information: \"/tmp/a.raucb\"");
    EXPECT_EQ(std::string(rauc::RaucMarkOtherPartition("").what()), "Error during marking other image");
    EXPECT_EQ(std::string(rauc::RaucRollback("").what()), "Error during rollback");
    EXPECT_EQ(std::string(rauc::RaucGetStatus("").what()), "Error during getting status");
    EXPECT_EQ(std::string(rauc::RaucMarkGood("").what()), "Error during Mark(good, booted)");
}
