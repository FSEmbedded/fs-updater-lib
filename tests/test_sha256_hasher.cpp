#include <gtest/gtest.h>

#include "handle_update/Sha256Hasher.h"

#include <cctype>
#include <string>

TEST(Sha256Hasher, EmptyInputProducesEmptyStringDigest)
{
    fs::Sha256Hasher h;
    EXPECT_EQ(h.hex_digest(),
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST(Sha256Hasher, AbcMatchesKnownDigest)
{
    fs::Sha256Hasher h;
    h.update("abc", 3);
    EXPECT_EQ(h.hex_digest(),
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST(Sha256Hasher, ChunkedUpdatesProduceSameDigestAsOneShot)
{
    fs::Sha256Hasher single;
    single.update("hello world", 11);

    fs::Sha256Hasher chunked;
    chunked.update("hello ", 6);
    chunked.update("world", 5);

    EXPECT_EQ(single.hex_digest(), chunked.hex_digest());
}

TEST(Sha256Hasher, HexDigestIsLowercaseHex)
{
    fs::Sha256Hasher h;
    h.update("XYZ", 3);
    const auto digest = h.hex_digest();
    EXPECT_EQ(digest.size(), 64u);
    for (char c : digest) {
        const auto uc = static_cast<unsigned char>(c);
        EXPECT_TRUE(std::isxdigit(uc));
        EXPECT_FALSE(std::isupper(uc));
    }
}
