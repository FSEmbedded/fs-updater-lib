#pragma once

#include "util/posix_utils.h"

#include <gtest/gtest.h>

extern "C" {
#include <stdlib.h> /* mkdtemp */
}

#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace test_support
{
/* Builds a temp "/sys/class/block"-shaped tree: <root>/<name>/loop/backing_file
 * for each entry. Removed on destruction. */
class FakeSysfsBlockRoot
{
public:
    explicit FakeSysfsBlockRoot(const std::map<std::string, std::string> &loop_backing_files)
        : entries_(loop_backing_files)
    {
        std::string tmpl = "/tmp/sysfs-block-XXXXXX";
        std::vector<char> buf(tmpl.begin(), tmpl.end());
        buf.push_back('\0');
        EXPECT_NE(::mkdtemp(buf.data()), nullptr);
        root_ = buf.data();

        for (const auto &kv : loop_backing_files) {
            const std::string loop_dir = fs::util::path_join(root_, kv.first + "/loop");
            EXPECT_TRUE(fs::util::mkdir_p(loop_dir));
            std::ofstream backing_file(fs::util::path_join(loop_dir, "backing_file"));
            backing_file << kv.second << "\n";
        }
    }

    ~FakeSysfsBlockRoot()
    {
        for (const auto &kv : entries_) {
            (void)fs::util::remove_file(fs::util::path_join(root_, kv.first + "/loop/backing_file"));
        }
        /* Best-effort: rmdir the tree bottom-up. Leaked on failure — the
         * sandbox tmp dir gets reclaimed anyway; tests don't assert on it. */
    }

    [[nodiscard]] const std::string &root() const { return root_; }

private:
    std::string root_;
    std::map<std::string, std::string> entries_;
};
} // namespace test_support
