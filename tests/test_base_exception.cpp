#include <gtest/gtest.h>

#include "BaseException.h"

#include <exception>
#include <string>

namespace {

class TestException : public fs::BaseFSUpdateException
{
public:
    explicit TestException(const std::string &msg)
    {
        this->error_msg = msg;
    }
};

TEST(BaseFSUpdateException, WhatReturnsErrorMsg)
{
    TestException ex("update bundle missing signature");
    EXPECT_STREQ(ex.what(), "update bundle missing signature");
}

TEST(BaseFSUpdateException, WhatIsEmptyByDefault)
{
    TestException ex("");
    EXPECT_STREQ(ex.what(), "");
}

TEST(BaseFSUpdateException, IsCatchableAsStdException)
{
    bool caught = false;
    std::string seen;
    try {
        throw TestException("rauc bundle verify failed");
    } catch (const std::exception &e) {
        caught = true;
        seen = e.what();
    }
    EXPECT_TRUE(caught);
    EXPECT_EQ(seen, "rauc bundle verify failed");
}

} // namespace
