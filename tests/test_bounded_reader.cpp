#include <gtest/gtest.h>

#include "handle_update/BoundedReader.h"

#include <sstream>
#include <string>

namespace {

std::string drain(fs::BoundedReader& r)
{
    std::string out;
    char buf[64];
    while (auto got = r.read(buf, sizeof(buf))) {
        out.append(buf, static_cast<std::size_t>(got));
    }
    return out;
}

} // namespace

TEST(BoundedReader, ReadsExactlySizeBytesFromOffset)
{
    std::istringstream src("ABCDEFGHIJKLMNOP", std::ios::binary);
    fs::BoundedReader reader(src, 4, 5);
    EXPECT_EQ(drain(reader), "EFGHI");
}

TEST(BoundedReader, RemainingDecreasesAsBytesAreRead)
{
    std::istringstream src("ABCDEFGH", std::ios::binary);
    fs::BoundedReader reader(src, 0, 6);
    EXPECT_EQ(reader.remaining(), 6);

    char buf[3];
    EXPECT_EQ(reader.read(buf, 3), 3);
    EXPECT_EQ(reader.remaining(), 3);
}

TEST(BoundedReader, EofWhenExhausted)
{
    std::istringstream src("XYZ", std::ios::binary);
    fs::BoundedReader reader(src, 0, 3);
    EXPECT_FALSE(reader.eof());

    char buf[10];
    (void)reader.read(buf, 10);
    EXPECT_TRUE(reader.eof());
    EXPECT_EQ(reader.read(buf, 10), 0);
}

TEST(BoundedReader, OverreadIsClampedToRemainingNotSourceLength)
{
    std::string source(100, 'A');
    std::istringstream src(source, std::ios::binary);
    fs::BoundedReader reader(src, 10, 4);

    char buf[10];
    EXPECT_EQ(reader.read(buf, 10), 4);
    EXPECT_EQ(reader.read(buf, 10), 0);
}

TEST(BoundedReader, ZeroSizeReadsNothing)
{
    std::istringstream src("hello", std::ios::binary);
    fs::BoundedReader reader(src, 0, 0);

    char buf[10];
    EXPECT_EQ(reader.read(buf, 10), 0);
    EXPECT_TRUE(reader.eof());
}

TEST(BoundedReader, NonContiguousReadersOnSameSource)
{
    std::istringstream src("ABCDEFGHIJ", std::ios::binary);
    {
        fs::BoundedReader r1(src, 2, 3);
        EXPECT_EQ(drain(r1), "CDE");
    }
    {
        fs::BoundedReader r2(src, 7, 3);
        EXPECT_EQ(drain(r2), "HIJ");
    }
}

TEST(BoundedReader, ReadHonorsBufferSizeArgument)
{
    std::istringstream src("0123456789", std::ios::binary);
    fs::BoundedReader reader(src, 0, 10);

    char buf[3];
    EXPECT_EQ(reader.read(buf, 3), 3);
    EXPECT_EQ(std::string(buf, 3), "012");
    EXPECT_EQ(reader.read(buf, 3), 3);
    EXPECT_EQ(std::string(buf, 3), "345");
}
