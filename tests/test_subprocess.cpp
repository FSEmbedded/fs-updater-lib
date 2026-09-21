#include <gtest/gtest.h>

#include "subprocess/subprocess.h"

#include <unistd.h>

namespace {

bool shell_available()
{
    return access("/bin/sh", X_OK) == 0;
}

TEST(SubprocessPopen, CapturesStdoutOfEcho)
{
    if (!shell_available()) {
        GTEST_SKIP() << "/bin/sh not available — skipping subprocess test";
    }

    subprocess::Popen p("echo hello");
    EXPECT_TRUE(p.successful());
    EXPECT_EQ(p.output(), "hello");
}

TEST(SubprocessPopen, TrimsTrailingWhitespaceFromOutput)
{
    if (!shell_available()) {
        GTEST_SKIP() << "/bin/sh not available — skipping subprocess test";
    }

    subprocess::Popen p("printf 'one\\ntwo\\n\\n'");
    EXPECT_TRUE(p.successful());
    EXPECT_EQ(p.output(), "one\ntwo");
}

TEST(SubprocessPopen, FailingCommandReportsUnsuccessful)
{
    if (!shell_available()) {
        GTEST_SKIP() << "/bin/sh not available — skipping subprocess test";
    }

    subprocess::Popen p("false");
    EXPECT_FALSE(p.successful());
}

TEST(SubprocessPopen, EmptyOutputCommandSucceeds)
{
    if (!shell_available()) {
        GTEST_SKIP() << "/bin/sh not available — skipping subprocess test";
    }

    subprocess::Popen p("true");
    EXPECT_TRUE(p.successful());
    EXPECT_TRUE(p.output().empty());
}

TEST(SubprocessError, ChildProcessWhatContainsPidAndCause)
{
    subprocess::ChildProcess err(static_cast<pid_t>(1234), "open pipe");
    const std::string msg(err.what());
    EXPECT_NE(msg.find("1234"), std::string::npos);
    EXPECT_NE(msg.find("open pipe"), std::string::npos);
}

TEST(SubprocessError, OpenPipeParentReportsErrno)
{
    subprocess::OpenPipeParent err(42);
    const std::string msg(err.what());
    EXPECT_NE(msg.find("42"), std::string::npos);
    EXPECT_NE(msg.find("parent"), std::string::npos);
}

TEST(SubprocessError, ExecuteSubprocessReportsCommandAndCause)
{
    subprocess::ExecuteSubprocess err("/bin/missing", "exit 127");
    const std::string msg(err.what());
    EXPECT_NE(msg.find("/bin/missing"), std::string::npos);
    EXPECT_NE(msg.find("exit 127"), std::string::npos);
}

} // namespace
