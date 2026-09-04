#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "handle_update/rauc_config_path.h"

namespace {
// A filesystem stand-in: the resolver takes its predicate as a parameter
// precisely so the search order can be proven without touching /etc, /run or
// /usr on the machine running the tests.
struct FakeFs {
    std::vector<std::string> present;
    bool operator()(const std::string &path) const
    {
        for (const std::string &entry : present) {
            if (entry == path) {
                return true;
            }
        }
        return false;
    }
};
} // namespace

TEST(RaucConfigSearchPaths, MirrorsTheDaemonsOwnOrder)
{
    // Not cosmetic: if this library searched a different order than RAUC does,
    // the two could load different configs and therefore different keyrings on
    // the same device, which is a split trust root.
    const std::vector<std::string> &paths = fs::rauc_config_search_paths();
    ASSERT_EQ(paths.size(), 3U);
    EXPECT_EQ(paths[0], "/etc/rauc/system.conf");
    EXPECT_EQ(paths[1], "/run/rauc/system.conf");
    EXPECT_EQ(paths[2], "/usr/lib/rauc/system.conf");
}

TEST(ResolveRaucConfig, TakesTheFirstCandidateThatExists)
{
    const FakeFs fs_stub{{"/run/rauc/system.conf", "/usr/lib/rauc/system.conf"}};
    EXPECT_EQ(fs::resolve_rauc_config(fs::rauc_config_search_paths(), fs_stub),
              "/run/rauc/system.conf");
}

TEST(ResolveRaucConfig, AnAdministratorOverrideBeatsTheShippedConfig)
{
    // The whole reason /etc stays first: a device with a deliberate override
    // must keep it, even once the image ships its own copy under /usr/lib.
    const FakeFs fs_stub{{"/etc/rauc/system.conf", "/usr/lib/rauc/system.conf"}};
    EXPECT_EQ(fs::resolve_rauc_config(fs::rauc_config_search_paths(), fs_stub),
              "/etc/rauc/system.conf");
}

TEST(ResolveRaucConfig, FindsTheShippedConfigWhenEtcIsEmpty)
{
    const FakeFs fs_stub{{"/usr/lib/rauc/system.conf"}};
    EXPECT_EQ(fs::resolve_rauc_config(fs::rauc_config_search_paths(), fs_stub),
              "/usr/lib/rauc/system.conf");
}

TEST(ResolveRaucConfig, ReportsNothingWhenNoCandidateExists)
{
    const FakeFs fs_stub{{}};
    EXPECT_TRUE(fs::resolve_rauc_config(fs::rauc_config_search_paths(), fs_stub).empty());
}

TEST(RaucConfigCandidatesText, NamesEveryCandidate)
{
    // The caller sees this string when nothing was found. Naming only the first
    // candidate would send the reader to the wrong directory.
    const std::string text = fs::rauc_config_candidates_text();
    EXPECT_NE(text.find("/etc/rauc/system.conf"), std::string::npos);
    EXPECT_NE(text.find("/run/rauc/system.conf"), std::string::npos);
    EXPECT_NE(text.find("/usr/lib/rauc/system.conf"), std::string::npos);
}

TEST(ResolveKeyringPath, RelativeKeyringFollowsTheConfigThatWasFound)
{
    // This is the pair that must not drift apart: a config found outside /etc
    // names its keyring beside itself, not back in /etc.
    EXPECT_EQ(fs::resolve_keyring_path("/usr/lib/rauc/system.conf", "ca.cert.pem"),
              "/usr/lib/rauc/ca.cert.pem");
    EXPECT_EQ(fs::resolve_keyring_path("/etc/rauc/system.conf", "ca.cert.pem"),
              "/etc/rauc/ca.cert.pem");
}

TEST(ResolveKeyringPath, AbsoluteKeyringIsUsedAsGiven)
{
    EXPECT_EQ(fs::resolve_keyring_path("/etc/rauc/system.conf", "/usr/lib/rauc/ca.cert.pem"),
              "/usr/lib/rauc/ca.cert.pem");
}

TEST(ResolveKeyringPath, ABareConfigNameResolvesAgainstTheWorkingDirectory)
{
    EXPECT_EQ(fs::resolve_keyring_path("system.conf", "ca.cert.pem"), "./ca.cert.pem");
}
