#include <gtest/gtest.h>

#include "handle_update/app_bundle_install.h"
#include "handle_update/fs_exceptions.h"
#include "handle_update/fsupdate.h"
#include "handle_update/updateApplication.h"
#include "handle_update/updater_exceptions.h"
#include "logger/LoggerHandler.h"
#include "logger/LoggerSinkEmpty.h"
#include "support/fake_sysfs_block_root.h"
#include "support/fake_uboot_env.h"
#include "uboot_interface/uboot_exceptions.h"
#include "util/posix_utils.h"

extern "C" {
#include <stdlib.h> /* mkdtemp */
}

#include <cerrno>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

/* The rollback verbs as they behave today, pinned cell by cell: a starting
 * board, one verb, and what the verb raised and left in the environment. This
 * is a record of observed behaviour, not a specification. Rows marked as
 * pinning known-wrong behaviour are expected to be inverted when that
 * behaviour is fixed; every other row changing is a regression. */

namespace
{

using test_support::FakeSysfsBlockRoot;
using test_support::FakeUBootEnv;

std::shared_ptr<logger::LoggerHandler> quiet_logger()
{
    static auto handler =
        logger::LoggerHandler::initLogger(std::make_shared<logger::LoggerSinkEmpty>(logger::logLevel::ERROR));
    return handler;
}

/* Which application image the loop devices carry, relative to the
 * environment's 'application' variable at the time of the call. */
enum class Mount { Active, Other, None };

/* Starting boards. Slot A is the proven side unless the row mirrors it.
 * Digits of 'update' are indexed fw_a, app_a, fw_b, app_b. */
enum class Shape {
    S0,         /* idle */
    S1,         /* legacy failed-reboot marker after a fallback */
    S2Pre,      /* firmware installed, reboot not taken */
    S2Post,     /* firmware installed, running the new slot */
    S2Fallback, /* firmware installed, bootloader fell back */
    S3Pre,      /* application installed, old image still mounted */
    S3Post,     /* application installed, new image mounted */
    S4Pre,      /* both installed, reboot not taken */
    S4Post,     /* both installed, running the new slot */
    S5,         /* firmware install failed */
    S6,         /* application install failed */
    S7Pre,      /* firmware switch prepared, reboot not taken */
    S7Post,     /* firmware switch prepared, running the other slot */
    S8Pre,      /* application switch prepared, old image still mounted */
    S8Post,     /* application switch prepared, new image mounted */
    S9Pre,      /* combined rollback taken before the update's reboot */
    S9Post,     /* combined rollback taken after the update's reboot */
    S10,        /* legacy firmware rollback awaiting commit */
    S11,        /* legacy application rollback awaiting commit */
    S12         /* legacy combined rollback awaiting commit */
};

/* One axis moved away from the shape at a time. */
enum class Variant {
    Base,
    Mirror,    /* every slot-relative value swapped A <-> B */
    Settled,   /* uncommitted bit cleared on every digit */
    TargetBad, /* bad bit set on the digit the verb would switch to */
    BoToggle,  /* BOOT_ORDER_OLD made equal to / different from BOOT_ORDER */
    Budget23,  /* BOOT_A_LEFT=2 BOOT_B_LEFT=3 */
    Budget03,  /* BOOT_A_LEFT=0 BOOT_B_LEFT=3 */
    Budget30,  /* BOOT_A_LEFT=3 BOOT_B_LEFT=0 */
    MountActive,
    MountOther,
    MountNone,
    NoConfig,  /* no RAUC configuration at the configured path */
    AppFlipped /* 'application' names the other slot */
};

enum class Verb {
    Fw,    /* rollback_firmware() */
    App,   /* rollback_application() */
    Cli,   /* the command-line sequence for a combined update: application, then firmware */
    FwFw,  /* rollback_firmware() twice */
    AppApp /* rollback_application() twice */
};

struct Board {
    std::string state;
    std::string update;
    char slot;
    char app;
    std::string boot_order;
    std::string boot_order_old;
    std::string a_left;
    std::string b_left;
    Mount mount;
    bool config;
};

Board shape_board(Shape shape)
{
    switch (shape) {
    case Shape::S0:
        return {"0", "0000", 'A', 'A', "A B", "A B", "3", "3", Mount::Active, true};
    case Shape::S1:
        return {"1", "0010", 'A', 'A', "B A", "A B", "3", "0", Mount::Active, true};
    case Shape::S2Pre:
        return {"2", "0010", 'A', 'A', "B A", "A B", "3", "3", Mount::Active, true};
    case Shape::S2Post:
        return {"2", "0010", 'B', 'A', "B A", "A B", "3", "3", Mount::Active, true};
    case Shape::S2Fallback:
        return {"2", "0010", 'A', 'A', "B A", "A B", "3", "0", Mount::Active, true};
    case Shape::S3Pre:
        return {"3", "0001", 'A', 'B', "A B", "A B", "3", "3", Mount::Other, true};
    case Shape::S3Post:
        return {"3", "0001", 'A', 'B', "A B", "A B", "3", "3", Mount::Active, true};
    case Shape::S4Pre:
        return {"4", "0011", 'A', 'B', "B A", "A B", "3", "3", Mount::Other, true};
    case Shape::S4Post:
        return {"4", "0011", 'B', 'B', "B A", "A B", "3", "3", Mount::Active, true};
    case Shape::S5:
        return {"5", "0010", 'A', 'A', "A B", "A B", "3", "3", Mount::Active, true};
    case Shape::S6:
        return {"6", "0001", 'A', 'A', "A B", "A B", "3", "3", Mount::Active, true};
    case Shape::S7Pre:
        return {"7", "0000", 'A', 'A', "B A", "A B", "3", "3", Mount::Active, true};
    case Shape::S7Post:
        return {"7", "0000", 'B', 'A', "B A", "A B", "3", "3", Mount::Active, true};
    case Shape::S8Pre:
        return {"8", "0000", 'A', 'B', "A B", "A B", "3", "3", Mount::Other, true};
    case Shape::S8Post:
        return {"8", "0000", 'A', 'B', "A B", "A B", "3", "3", Mount::Active, true};
    case Shape::S9Pre:
        return {"9", "0001", 'A', 'A', "A B", "A B", "3", "3", Mount::Active, true};
    case Shape::S9Post:
        return {"9", "0011", 'B', 'A', "B A", "A B", "3", "0", Mount::Other, true};
    case Shape::S10:
        return {"10", "0000", 'A', 'A', "A B", "B A", "3", "3", Mount::Active, true};
    case Shape::S11:
        return {"11", "0000", 'A', 'A', "A B", "A B", "3", "3", Mount::Active, true};
    case Shape::S12:
        return {"12", "0000", 'A', 'A', "A B", "B A", "3", "3", Mount::Active, true};
    }
    return {};
}

char other(char slot)
{
    return (slot == 'A') ? 'B' : 'A';
}

std::string swapped_order(const std::string &order)
{
    return (order == "A B") ? "B A" : "A B";
}

Board apply_variant(Board board, Variant variant, Verb verb)
{
    const bool fw_dimension = (verb != Verb::App) && (verb != Verb::AppApp);
    const bool app_dimension = (verb != Verb::Fw) && (verb != Verb::FwFw);

    switch (variant) {
    case Variant::Base:
        break;
    case Variant::Mirror:
        board.slot = other(board.slot);
        board.app = other(board.app);
        board.boot_order = swapped_order(board.boot_order);
        board.boot_order_old = swapped_order(board.boot_order_old);
        board.update = board.update.substr(2, 2) + board.update.substr(0, 2);
        std::swap(board.a_left, board.b_left);
        break;
    case Variant::Settled:
        for (char &digit : board.update) {
            digit = static_cast<char>('0' + ((digit - '0') & ~1));
        }
        break;
    case Variant::TargetBad:
        if (fw_dimension) {
            char &digit = board.update.at((board.slot == 'A') ? 2U : 0U);
            digit = static_cast<char>('0' + ((digit - '0') | 2));
        }
        if (app_dimension) {
            char &digit = board.update.at((board.app == 'A') ? 3U : 1U);
            digit = static_cast<char>('0' + ((digit - '0') | 2));
        }
        break;
    case Variant::BoToggle:
        board.boot_order_old =
            (board.boot_order == board.boot_order_old) ? swapped_order(board.boot_order) : board.boot_order;
        break;
    case Variant::Budget23:
        board.a_left = "2";
        board.b_left = "3";
        break;
    case Variant::Budget03:
        board.a_left = "0";
        board.b_left = "3";
        break;
    case Variant::Budget30:
        board.a_left = "3";
        board.b_left = "0";
        break;
    case Variant::MountActive:
        board.mount = Mount::Active;
        break;
    case Variant::MountOther:
        board.mount = Mount::Other;
        break;
    case Variant::MountNone:
        board.mount = Mount::None;
        break;
    case Variant::NoConfig:
        board.config = false;
        break;
    case Variant::AppFlipped:
        board.app = other(board.app);
        break;
    }
    return board;
}

std::string errno_name(int err)
{
    switch (err) {
    case 0:
        return "0";
    case EPERM:
        return "EPERM";
    case ENOENT:
        return "ENOENT";
    case EINVAL:
        return "EINVAL";
    case ECANCELED:
        return "ECANCELED";
    default:
        return std::to_string(err);
    }
}

/* Most-derived first; RTTI is not available to ask the type directly. */
template <typename F> std::string outcome_of(F &&call)
{
    try {
        call();
        return "ok";
    } catch (const updater::RebootStateNotInterpretable &) {
        return "NotInterpretable";
    } catch (const updater::GetLoopDevices &) {
        return "GetLoopDevices";
    } catch (const fs::NotAllowedUpdateState &) {
        return "NotAllowedUpdateState";
    } catch (const fs::GenericException &e) {
        return "Generic(" + errno_name(e.errorno) + ")";
    } catch (const fs::BaseFSUpdateException &) {
        return "BaseFSUpdateException";
    } catch (const UBoot::UBootError &) {
        return "UBootError";
    } catch (const std::runtime_error &) {
        return "runtime_error";
    } catch (const std::exception &) {
        return "exception";
    }
}

const std::vector<std::string> &watched_variables()
{
    static const std::vector<std::string> names = {"update_reboot_state", "update",      "application", "BOOT_ORDER",
                                                   "BOOT_ORDER_OLD",      "BOOT_A_LEFT", "BOOT_B_LEFT"};
    return names;
}

std::string compact(const std::string &value)
{
    std::string out;
    for (const char c : value) {
        if (c != ' ') {
            out.push_back(c);
        }
    }
    return out;
}

/* Fixture trees shared by every row: two loop-device trees, one per mounted
 * image, an empty one, a RAUC configuration and an image store holding both
 * slots' images. The mount trees name images in the compiled-in store, which
 * is what the mount probe compares against; the injected store only answers
 * whether a slot is provisioned. */
struct Trees {
    FakeSysfsBlockRoot a_mounted{{{"loop0", fs::app_slot_image_path(updater::config::STANDARD_APP_IMG_STORE, 'A')}}};
    FakeSysfsBlockRoot b_mounted{{{"loop0", fs::app_slot_image_path(updater::config::STANDARD_APP_IMG_STORE, 'B')}}};
    FakeSysfsBlockRoot nothing_mounted{{}};
    std::string dir;
    std::string config;
    std::string store;

    Trees()
    {
        std::string tmpl = "/tmp/rollback-golden-XXXXXX";
        std::vector<char> buf(tmpl.begin(), tmpl.end());
        buf.push_back('\0');
        EXPECT_NE(::mkdtemp(buf.data()), nullptr);
        dir = buf.data();

        config = fs::util::path_join(dir, "system.conf");
        std::ofstream(config) << "[keyring]\npath = keyring.pem\n";

        store = fs::util::path_join(dir, "images/");
        EXPECT_TRUE(fs::util::mkdir_p(store));
        std::ofstream(fs::app_slot_image_path(store, 'A')) << "a";
        std::ofstream(fs::app_slot_image_path(store, 'B')) << "b";
    }

    const std::string &sysfs_for(Mount mount, char app) const
    {
        switch (mount) {
        case Mount::Active:
            return (app == 'A') ? a_mounted.root() : b_mounted.root();
        case Mount::Other:
            return (app == 'A') ? b_mounted.root() : a_mounted.root();
        case Mount::None:
        default:
            return nothing_mounted.root();
        }
    }
};

const Trees &trees()
{
    static const Trees instance;
    return instance;
}

std::shared_ptr<FakeUBootEnv> env_of(const Board &board)
{
    return std::make_shared<FakeUBootEnv>(std::map<std::string, std::string>{
        {"update_reboot_state", board.state},
        {"update", board.update},
        {"rauc_cmd", std::string("rauc.slot=") + board.slot},
        {"application", std::string(1, board.app)},
        {"BOOT_ORDER", board.boot_order},
        {"BOOT_ORDER_OLD", board.boot_order_old},
        {"BOOT_A_LEFT", board.a_left},
        {"BOOT_B_LEFT", board.b_left},
    });
}

std::unique_ptr<fs::FSUpdate> updater_on(const std::shared_ptr<FakeUBootEnv> &env, const Board &board)
{
    const Trees &t = trees();
    return std::make_unique<fs::FSUpdate>(env, quiet_logger(), t.sysfs_for(board.mount, board.app),
                                          board.config ? t.config : fs::util::path_join(t.dir, "absent.conf"), t.store);
}

std::map<std::string, std::string> snapshot(const FakeUBootEnv &env)
{
    std::map<std::string, std::string> values;
    for (const std::string &name : watched_variables()) {
        values[name] = env.at(name);
    }
    return values;
}

/* "<outcome per call> | <w if anything was staged, - if not> | <variables that changed, = if none>" */
std::string run_cell(const Board &board, Verb verb)
{
    auto env = env_of(board);
    auto updater = updater_on(env, board);
    std::map<std::string, std::string> before = snapshot(*env);

    auto fw = [&]() { updater->rollback_firmware(); };
    auto app = [&]() { updater->rollback_application(); };

    std::string outcome;
    switch (verb) {
    case Verb::Fw:
        outcome = outcome_of(fw);
        break;
    case Verb::App:
        outcome = outcome_of(app);
        break;
    case Verb::Cli:
        /* Stops at the first refusal, as the command-line tool does. */
        outcome = outcome_of(app);
        if (outcome == "ok") {
            outcome += ", " + outcome_of(fw);
        }
        break;
    case Verb::FwFw:
        outcome = outcome_of(fw);
        outcome += ", " + outcome_of(fw);
        break;
    case Verb::AppApp:
        outcome = outcome_of(app);
        outcome += ", " + outcome_of(app);
        break;
    }

    std::string changed;
    for (const std::string &name : watched_variables()) {
        const std::string now = env->at(name);
        if (now != before[name]) {
            changed += (changed.empty() ? "" : " ") + name + ":" + compact(before[name]) + ">" + compact(now);
        }
    }

    return outcome + " | " + (env->nothing_staged() ? "-" : "w") + " | " + (changed.empty() ? "=" : changed);
}

const char *shape_name(Shape shape)
{
    switch (shape) {
    case Shape::S0:
        return "S0";
    case Shape::S1:
        return "S1";
    case Shape::S2Pre:
        return "S2Pre";
    case Shape::S2Post:
        return "S2Post";
    case Shape::S2Fallback:
        return "S2Fallback";
    case Shape::S3Pre:
        return "S3Pre";
    case Shape::S3Post:
        return "S3Post";
    case Shape::S4Pre:
        return "S4Pre";
    case Shape::S4Post:
        return "S4Post";
    case Shape::S5:
        return "S5";
    case Shape::S6:
        return "S6";
    case Shape::S7Pre:
        return "S7Pre";
    case Shape::S7Post:
        return "S7Post";
    case Shape::S8Pre:
        return "S8Pre";
    case Shape::S8Post:
        return "S8Post";
    case Shape::S9Pre:
        return "S9Pre";
    case Shape::S9Post:
        return "S9Post";
    case Shape::S10:
        return "S10";
    case Shape::S11:
        return "S11";
    case Shape::S12:
        return "S12";
    }
    return "S?";
}

const char *variant_name(Variant variant)
{
    switch (variant) {
    case Variant::Base:
        return "Base";
    case Variant::Mirror:
        return "Mirror";
    case Variant::Settled:
        return "Settled";
    case Variant::TargetBad:
        return "TargetBad";
    case Variant::BoToggle:
        return "BoToggle";
    case Variant::Budget23:
        return "Budget23";
    case Variant::Budget03:
        return "Budget03";
    case Variant::Budget30:
        return "Budget30";
    case Variant::MountActive:
        return "MountActive";
    case Variant::MountOther:
        return "MountOther";
    case Variant::MountNone:
        return "MountNone";
    case Variant::NoConfig:
        return "NoConfig";
    case Variant::AppFlipped:
        return "AppFlipped";
    }
    return "V?";
}

const char *verb_name(Verb verb)
{
    switch (verb) {
    case Verb::Fw:
        return "Fw";
    case Verb::App:
        return "App";
    case Verb::Cli:
        return "Cli";
    case Verb::FwFw:
        return "FwFw";
    case Verb::AppApp:
        return "AppApp";
    }
    return "?";
}

struct Cell {
    Shape shape;
    Variant variant;
    Verb verb;
    const char *expected;
};

class RollbackGolden : public ::testing::TestWithParam<Cell>
{
};

TEST_P(RollbackGolden, MatchesTheRecordedOutcome)
{
    const Cell &cell = GetParam();
    const std::string actual = run_cell(apply_variant(shape_board(cell.shape), cell.variant, cell.verb), cell.verb);
    EXPECT_EQ(actual, cell.expected) << "ROW {Shape::" << shape_name(cell.shape)
                                     << ", Variant::" << variant_name(cell.variant)
                                     << ", Verb::" << verb_name(cell.verb) << ", \"" << actual << "\"},";
}

/* One row per cell. Rows are the informative subset of the full grid (every
 * shape x variant x single verb, the command-line sequence on the combined
 * shapes, and the repeated verbs on the base shapes): a variant row is kept
 * only where it changed the outcome against the base row of its shape and
 * verb, so an axis missing below had no effect there when this was recorded.
 *
 * KNOWN-WRONG: pins behaviour already recorded as faulty; the fix inverts it.
 * SUSPECT: pins behaviour that looks faulty but is not recorded as such yet. */
// clang-format off
const Cell kCells[] = {
    {Shape::S0, Variant::Base, Verb::Fw, "ok | w | update_reboot_state:0>7 BOOT_ORDER:AB>BA"},
    {Shape::S0, Variant::Base, Verb::App, "ok | w | update_reboot_state:0>8 application:A>B"},
    {Shape::S0, Variant::Base, Verb::FwFw, "ok, ok | w | update_reboot_state:0>7 BOOT_ORDER:AB>BA"},
    {Shape::S0, Variant::Base, Verb::AppApp, "ok, ok | w | update_reboot_state:0>8"}, // SUSPECT: second call undoes the first and keeps 8
    {Shape::S0, Variant::Mirror, Verb::Fw, "ok | w | update_reboot_state:0>7 BOOT_ORDER:BA>AB"},
    {Shape::S0, Variant::Mirror, Verb::App, "ok | w | update_reboot_state:0>8 application:B>A"},
    {Shape::S0, Variant::TargetBad, Verb::Fw, "Generic(EPERM) | - | ="},
    {Shape::S0, Variant::TargetBad, Verb::App, "Generic(EPERM) | - | ="},
    {Shape::S0, Variant::BoToggle, Verb::Fw, "ok | w | update_reboot_state:0>7 BOOT_ORDER:AB>BA BOOT_ORDER_OLD:BA>AB"},
    {Shape::S0, Variant::NoConfig, Verb::App, "runtime_error | - | ="},
    {Shape::S0, Variant::AppFlipped, Verb::App, "ok | w | update_reboot_state:0>8 application:B>A"},

    {Shape::S1, Variant::Base, Verb::Fw, "Generic(ECANCELED) | - | ="},
    {Shape::S1, Variant::Base, Verb::App, "ok | w | update_reboot_state:1>8 application:A>B"}, // KNOWN-WRONG: switch path taken where the state should refuse
    {Shape::S1, Variant::Base, Verb::FwFw, "Generic(ECANCELED), Generic(ECANCELED) | - | ="},
    {Shape::S1, Variant::Base, Verb::AppApp, "ok, ok | w | update_reboot_state:1>8"}, // KNOWN-WRONG: switch path taken where the state should refuse
    {Shape::S1, Variant::Mirror, Verb::Fw, "Generic(ECANCELED) | - | ="},
    {Shape::S1, Variant::Mirror, Verb::App, "ok | w | update_reboot_state:1>8 application:B>A"}, // KNOWN-WRONG: switch path taken where the state should refuse
    {Shape::S1, Variant::Settled, Verb::Fw, "ok | w | update_reboot_state:1>7"}, // KNOWN-WRONG: switch path taken where the state should refuse
    {Shape::S1, Variant::TargetBad, Verb::App, "Generic(EPERM) | - | ="},
    {Shape::S1, Variant::NoConfig, Verb::App, "runtime_error | - | ="},
    {Shape::S1, Variant::AppFlipped, Verb::App, "ok | w | update_reboot_state:1>8 application:B>A"}, // KNOWN-WRONG: switch path taken where the state should refuse

    {Shape::S2Pre, Variant::Base, Verb::Fw, "ok | w | update_reboot_state:2>0 update:0010>0000 BOOT_ORDER:BA>AB"},
    {Shape::S2Pre, Variant::Base, Verb::App, "ok | w | update_reboot_state:2>8 application:A>B"}, // SUSPECT: application rollback overwrites a pending firmware update
    {Shape::S2Pre, Variant::Base, Verb::FwFw, "ok, ok | w | update_reboot_state:2>7 update:0010>0000"}, // SUSPECT: second call switches into the slot just abandoned
    {Shape::S2Pre, Variant::Base, Verb::AppApp, "ok, ok | w | update_reboot_state:2>8"}, // SUSPECT: application rollback overwrites a pending firmware update
    {Shape::S2Pre, Variant::Mirror, Verb::Fw, "ok | w | update_reboot_state:2>0 update:1000>0000 BOOT_ORDER:AB>BA"},
    {Shape::S2Pre, Variant::Mirror, Verb::App, "ok | w | update_reboot_state:2>8 application:B>A"}, // SUSPECT: application rollback overwrites a pending firmware update
    {Shape::S2Pre, Variant::Settled, Verb::Fw, "ok | w | update_reboot_state:2>7"}, // SUSPECT: switch path overwrites the pending update
    {Shape::S2Pre, Variant::TargetBad, Verb::Fw, "ok | w | update_reboot_state:2>0 update:0030>0020 BOOT_ORDER:BA>AB"},
    {Shape::S2Pre, Variant::TargetBad, Verb::App, "Generic(EPERM) | - | ="},
    {Shape::S2Pre, Variant::BoToggle, Verb::Fw, "ok | - | ="}, // KNOWN-WRONG: silent no-op
    {Shape::S2Pre, Variant::Budget23, Verb::Fw, "ok | - | ="}, // KNOWN-WRONG: silent no-op
    {Shape::S2Pre, Variant::Budget03, Verb::Fw, "ok | - | ="}, // KNOWN-WRONG: silent no-op
    {Shape::S2Pre, Variant::Budget30, Verb::Fw, "ok | - | ="}, // KNOWN-WRONG: silent no-op
    {Shape::S2Pre, Variant::NoConfig, Verb::App, "runtime_error | - | ="},
    {Shape::S2Pre, Variant::AppFlipped, Verb::App, "ok | w | update_reboot_state:2>8 application:B>A"}, // SUSPECT: application rollback overwrites a pending firmware update

    {Shape::S2Post, Variant::Base, Verb::Fw, "ok | w | update_reboot_state:2>7 BOOT_B_LEFT:3>0"},
    {Shape::S2Post, Variant::Base, Verb::App, "ok | w | update_reboot_state:2>8 application:A>B"}, // SUSPECT: application rollback overwrites a pending firmware update
    {Shape::S2Post, Variant::Base, Verb::FwFw, "ok, Generic(0) | w | update_reboot_state:2>7 BOOT_B_LEFT:3>0"}, // SUSPECT: second call switches into the slot just abandoned
    {Shape::S2Post, Variant::Base, Verb::AppApp, "ok, ok | w | update_reboot_state:2>8"}, // SUSPECT: application rollback overwrites a pending firmware update
    {Shape::S2Post, Variant::Mirror, Verb::Fw, "ok | w | update_reboot_state:2>7 BOOT_A_LEFT:3>0"},
    {Shape::S2Post, Variant::Mirror, Verb::App, "ok | w | update_reboot_state:2>8 application:B>A"}, // SUSPECT: application rollback overwrites a pending firmware update
    {Shape::S2Post, Variant::Settled, Verb::Fw, "ok | w | update_reboot_state:2>7 BOOT_ORDER:BA>AB BOOT_ORDER_OLD:AB>BA"}, // SUSPECT: switch path overwrites the pending update
    {Shape::S2Post, Variant::TargetBad, Verb::App, "Generic(EPERM) | - | ="},
    {Shape::S2Post, Variant::BoToggle, Verb::Fw, "ok | - | ="}, // KNOWN-WRONG: silent no-op
    {Shape::S2Post, Variant::Budget30, Verb::Fw, "ok | w | update_reboot_state:2>7"},
    {Shape::S2Post, Variant::NoConfig, Verb::App, "runtime_error | - | ="},
    {Shape::S2Post, Variant::AppFlipped, Verb::App, "ok | w | update_reboot_state:2>8 application:B>A"}, // SUSPECT: application rollback overwrites a pending firmware update

    {Shape::S2Fallback, Variant::Base, Verb::Fw, "ok | - | ="}, // KNOWN-WRONG: silent no-op
    {Shape::S2Fallback, Variant::Base, Verb::App, "ok | w | update_reboot_state:2>8 application:A>B"}, // SUSPECT: application rollback overwrites a pending firmware update
    {Shape::S2Fallback, Variant::Base, Verb::FwFw, "ok, ok | - | ="},
    {Shape::S2Fallback, Variant::Base, Verb::AppApp, "ok, ok | w | update_reboot_state:2>8"}, // SUSPECT: application rollback overwrites a pending firmware update
    {Shape::S2Fallback, Variant::Mirror, Verb::Fw, "ok | - | ="}, // KNOWN-WRONG: silent no-op
    {Shape::S2Fallback, Variant::Mirror, Verb::App, "ok | w | update_reboot_state:2>8 application:B>A"}, // SUSPECT: application rollback overwrites a pending firmware update
    {Shape::S2Fallback, Variant::Settled, Verb::Fw, "ok | w | update_reboot_state:2>7"}, // SUSPECT: switch path overwrites the pending update
    {Shape::S2Fallback, Variant::TargetBad, Verb::App, "Generic(EPERM) | - | ="},
    {Shape::S2Fallback, Variant::NoConfig, Verb::App, "runtime_error | - | ="},
    {Shape::S2Fallback, Variant::AppFlipped, Verb::App, "ok | w | update_reboot_state:2>8 application:B>A"}, // SUSPECT: application rollback overwrites a pending firmware update

    {Shape::S3Pre, Variant::Base, Verb::Fw, "ok | w | update_reboot_state:3>7 BOOT_ORDER:AB>BA"}, // SUSPECT: firmware rollback overwrites a pending application update
    {Shape::S3Pre, Variant::Base, Verb::App, "ok | w | update_reboot_state:3>0 update:0001>0000 application:B>A"},
    {Shape::S3Pre, Variant::Base, Verb::FwFw, "ok, ok | w | update_reboot_state:3>7 BOOT_ORDER:AB>BA"}, // SUSPECT: firmware rollback overwrites a pending application update
    {Shape::S3Pre, Variant::Base, Verb::AppApp, "ok, ok | w | update_reboot_state:3>8 update:0001>0000"}, // SUSPECT: second call switches into the slot just abandoned
    {Shape::S3Pre, Variant::Mirror, Verb::Fw, "ok | w | update_reboot_state:3>7 BOOT_ORDER:BA>AB"}, // SUSPECT: firmware rollback overwrites a pending application update
    {Shape::S3Pre, Variant::Mirror, Verb::App, "ok | w | update_reboot_state:3>0 update:0100>0000 application:A>B"},
    {Shape::S3Pre, Variant::Settled, Verb::App, "ok | w | update_reboot_state:3>8 application:B>A"}, // SUSPECT: switch path overwrites the pending update
    {Shape::S3Pre, Variant::TargetBad, Verb::Fw, "Generic(EPERM) | - | ="},
    {Shape::S3Pre, Variant::TargetBad, Verb::App, "ok | w | update_reboot_state:3>0 update:0201>0200 application:B>A"},
    {Shape::S3Pre, Variant::BoToggle, Verb::Fw, "ok | w | update_reboot_state:3>7 BOOT_ORDER:AB>BA BOOT_ORDER_OLD:BA>AB"}, // SUSPECT: firmware rollback overwrites a pending application update
    {Shape::S3Pre, Variant::MountActive, Verb::App, "ok | w | update_reboot_state:3>8 application:B>A"},
    {Shape::S3Pre, Variant::NoConfig, Verb::App, "runtime_error | - | ="},
    {Shape::S3Pre, Variant::AppFlipped, Verb::App, "Generic(ECANCELED) | - | ="},

    {Shape::S3Post, Variant::Base, Verb::Fw, "ok | w | update_reboot_state:3>7 BOOT_ORDER:AB>BA"}, // SUSPECT: firmware rollback overwrites a pending application update
    {Shape::S3Post, Variant::Base, Verb::App, "ok | w | update_reboot_state:3>8 application:B>A"},
    {Shape::S3Post, Variant::Base, Verb::FwFw, "ok, ok | w | update_reboot_state:3>7 BOOT_ORDER:AB>BA"}, // SUSPECT: firmware rollback overwrites a pending application update
    {Shape::S3Post, Variant::Base, Verb::AppApp, "ok, Generic(ECANCELED) | w | update_reboot_state:3>8 application:B>A"},
    {Shape::S3Post, Variant::Mirror, Verb::Fw, "ok | w | update_reboot_state:3>7 BOOT_ORDER:BA>AB"}, // SUSPECT: firmware rollback overwrites a pending application update
    {Shape::S3Post, Variant::Mirror, Verb::App, "ok | w | update_reboot_state:3>8 application:A>B"},
    {Shape::S3Post, Variant::TargetBad, Verb::Fw, "Generic(EPERM) | - | ="},
    {Shape::S3Post, Variant::BoToggle, Verb::Fw, "ok | w | update_reboot_state:3>7 BOOT_ORDER:AB>BA BOOT_ORDER_OLD:BA>AB"}, // SUSPECT: firmware rollback overwrites a pending application update
    {Shape::S3Post, Variant::MountOther, Verb::App, "ok | w | update_reboot_state:3>0 update:0001>0000 application:B>A"},
    {Shape::S3Post, Variant::MountNone, Verb::App, "ok | w | update_reboot_state:3>0 update:0001>0000 application:B>A"},
    {Shape::S3Post, Variant::NoConfig, Verb::App, "runtime_error | - | ="},
    {Shape::S3Post, Variant::AppFlipped, Verb::App, "Generic(ECANCELED) | - | ="},

    {Shape::S4Pre, Variant::Base, Verb::Fw, "ok | w | update_reboot_state:4>9 update:0011>0001 application:B>A BOOT_ORDER:BA>AB"}, // KNOWN-WRONG: combined rollback leaves a 9 that cannot be committed
    {Shape::S4Pre, Variant::Base, Verb::App, "ok | w | ="}, // KNOWN-WRONG: silent no-op, the staged writes are discarded
    {Shape::S4Pre, Variant::Base, Verb::Cli, "ok, ok | w | update_reboot_state:4>9 update:0011>0001 application:B>A BOOT_ORDER:BA>AB"}, // KNOWN-WRONG: combined rollback leaves a 9 that cannot be committed
    {Shape::S4Pre, Variant::Base, Verb::FwFw, "ok, ok | w | update_reboot_state:4>7 update:0011>0001 application:B>A"}, // KNOWN-WRONG: second call switches into the abandoned firmware
    {Shape::S4Pre, Variant::Base, Verb::AppApp, "ok, ok | w | ="}, // KNOWN-WRONG: silent no-op, the staged writes are discarded
    {Shape::S4Pre, Variant::Mirror, Verb::Fw, "ok | w | update_reboot_state:4>9 update:1100>0100 application:A>B BOOT_ORDER:AB>BA"}, // KNOWN-WRONG: combined rollback leaves a 9 that cannot be committed
    {Shape::S4Pre, Variant::Mirror, Verb::App, "ok | w | ="}, // KNOWN-WRONG: silent no-op, the staged writes are discarded
    {Shape::S4Pre, Variant::Mirror, Verb::Cli, "ok, ok | w | update_reboot_state:4>9 update:1100>0100 application:A>B BOOT_ORDER:AB>BA"}, // KNOWN-WRONG: combined rollback leaves a 9 that cannot be committed
    {Shape::S4Pre, Variant::Settled, Verb::Fw, "ok | w | update_reboot_state:4>7"}, // SUSPECT: switch path overwrites the pending update
    {Shape::S4Pre, Variant::Settled, Verb::App, "ok | w | update_reboot_state:4>8 application:B>A"}, // SUSPECT: switch path overwrites the pending update
    {Shape::S4Pre, Variant::Settled, Verb::Cli, "ok, Generic(0) | w | update_reboot_state:4>8 application:B>A"}, // SUSPECT: half-applied, the application half is written before the refusal
    {Shape::S4Pre, Variant::TargetBad, Verb::Fw, "ok | w | update_reboot_state:4>9 update:0031>0021 application:B>A BOOT_ORDER:BA>AB"}, // KNOWN-WRONG: combined rollback leaves a 9 that cannot be committed
    {Shape::S4Pre, Variant::TargetBad, Verb::Cli, "ok, ok | w | update_reboot_state:4>9 update:0231>0221 application:B>A BOOT_ORDER:BA>AB"}, // KNOWN-WRONG: combined rollback leaves a 9 that cannot be committed
    {Shape::S4Pre, Variant::BoToggle, Verb::Fw, "ok | w | update_reboot_state:4>9 application:B>A"}, // KNOWN-WRONG: stores 9 but leaves the boot order on the new firmware
    {Shape::S4Pre, Variant::BoToggle, Verb::Cli, "ok, ok | w | update_reboot_state:4>9 application:B>A"}, // KNOWN-WRONG: stores 9 but leaves the boot order on the new firmware
    {Shape::S4Pre, Variant::Budget23, Verb::Fw, "ok | w | update_reboot_state:4>9 application:B>A"}, // KNOWN-WRONG: stores 9 but leaves the boot order on the new firmware
    {Shape::S4Pre, Variant::Budget23, Verb::Cli, "ok, ok | w | update_reboot_state:4>9 application:B>A"}, // KNOWN-WRONG: stores 9 but leaves the boot order on the new firmware
    {Shape::S4Pre, Variant::Budget03, Verb::Fw, "ok | w | update_reboot_state:4>9 application:B>A"}, // KNOWN-WRONG: stores 9 but leaves the boot order on the new firmware
    {Shape::S4Pre, Variant::Budget03, Verb::Cli, "ok, ok | w | update_reboot_state:4>9 application:B>A"}, // KNOWN-WRONG: stores 9 but leaves the boot order on the new firmware
    {Shape::S4Pre, Variant::Budget30, Verb::Fw, "ok | w | update_reboot_state:4>9 application:B>A"}, // KNOWN-WRONG: stores 9 but leaves the boot order on the new firmware
    {Shape::S4Pre, Variant::Budget30, Verb::Cli, "ok, ok | w | update_reboot_state:4>9 application:B>A"}, // KNOWN-WRONG: stores 9 but leaves the boot order on the new firmware
    {Shape::S4Pre, Variant::NoConfig, Verb::Fw, "runtime_error | w | ="},
    {Shape::S4Pre, Variant::NoConfig, Verb::App, "runtime_error | - | ="},
    {Shape::S4Pre, Variant::NoConfig, Verb::Cli, "runtime_error | - | ="},
    {Shape::S4Pre, Variant::AppFlipped, Verb::Fw, "ok | w | update_reboot_state:4>9 update:0011>0001 application:A>B BOOT_ORDER:BA>AB"}, // KNOWN-WRONG: combined rollback leaves a 9 that cannot be committed
    {Shape::S4Pre, Variant::AppFlipped, Verb::Cli, "ok, ok | w | update_reboot_state:4>9 update:0011>0001 application:A>B BOOT_ORDER:BA>AB"}, // KNOWN-WRONG: combined rollback leaves a 9 that cannot be committed

    {Shape::S4Post, Variant::Base, Verb::Fw, "ok | w | update_reboot_state:4>9 application:B>A BOOT_B_LEFT:3>0"},
    {Shape::S4Post, Variant::Base, Verb::App, "ok | w | ="}, // KNOWN-WRONG: silent no-op, the staged writes are discarded
    {Shape::S4Post, Variant::Base, Verb::Cli, "ok, ok | w | update_reboot_state:4>9 application:B>A BOOT_B_LEFT:3>0"},
    {Shape::S4Post, Variant::Base, Verb::FwFw, "ok, ok | w | update_reboot_state:4>7 application:B>A BOOT_ORDER:BA>AB BOOT_ORDER_OLD:AB>BA BOOT_B_LEFT:3>0"}, // KNOWN-WRONG: second call switches into the abandoned firmware
    {Shape::S4Post, Variant::Base, Verb::AppApp, "ok, ok | w | ="}, // KNOWN-WRONG: silent no-op, the staged writes are discarded
    {Shape::S4Post, Variant::Mirror, Verb::Fw, "ok | w | update_reboot_state:4>9 application:A>B BOOT_A_LEFT:3>0"},
    {Shape::S4Post, Variant::Mirror, Verb::App, "ok | w | ="}, // KNOWN-WRONG: silent no-op, the staged writes are discarded
    {Shape::S4Post, Variant::Mirror, Verb::Cli, "ok, ok | w | update_reboot_state:4>9 application:A>B BOOT_A_LEFT:3>0"},
    {Shape::S4Post, Variant::Settled, Verb::Fw, "ok | w | update_reboot_state:4>7 BOOT_ORDER:BA>AB BOOT_ORDER_OLD:AB>BA"}, // SUSPECT: switch path overwrites the pending update
    {Shape::S4Post, Variant::Settled, Verb::App, "ok | w | update_reboot_state:4>8 application:B>A"}, // SUSPECT: switch path overwrites the pending update
    {Shape::S4Post, Variant::Settled, Verb::Cli, "ok, ok | w | update_reboot_state:4>7 application:B>A BOOT_ORDER:BA>AB BOOT_ORDER_OLD:AB>BA"}, // SUSPECT: switch path overwrites the pending update
    {Shape::S4Post, Variant::BoToggle, Verb::Fw, "ok | w | update_reboot_state:4>9 application:B>A"}, // SUSPECT: unreachable shape (equal boot orders in 4); the firmware half does nothing
    {Shape::S4Post, Variant::BoToggle, Verb::Cli, "ok, ok | w | update_reboot_state:4>9 application:B>A"}, // SUSPECT: unreachable shape (equal boot orders in 4); the firmware half does nothing
    {Shape::S4Post, Variant::Budget30, Verb::Fw, "ok | w | update_reboot_state:4>9 application:B>A"},
    {Shape::S4Post, Variant::Budget30, Verb::Cli, "ok, ok | w | update_reboot_state:4>9 application:B>A"},
    {Shape::S4Post, Variant::NoConfig, Verb::Fw, "runtime_error | w | ="},
    {Shape::S4Post, Variant::NoConfig, Verb::App, "runtime_error | - | ="},
    {Shape::S4Post, Variant::NoConfig, Verb::Cli, "runtime_error | - | ="},
    {Shape::S4Post, Variant::AppFlipped, Verb::Fw, "ok | w | update_reboot_state:4>7 BOOT_ORDER:BA>AB BOOT_ORDER_OLD:AB>BA"}, // SUSPECT: switch path overwrites the pending update
    {Shape::S4Post, Variant::AppFlipped, Verb::App, "Generic(ECANCELED) | - | ="},
    {Shape::S4Post, Variant::AppFlipped, Verb::Cli, "Generic(ECANCELED) | - | ="},

    {Shape::S5, Variant::Base, Verb::Fw, "Generic(ECANCELED) | - | ="},
    {Shape::S5, Variant::Base, Verb::App, "ok | w | update_reboot_state:5>8 application:A>B"}, // KNOWN-WRONG: switch path taken where the state should refuse
    {Shape::S5, Variant::Base, Verb::FwFw, "Generic(ECANCELED), Generic(ECANCELED) | - | ="},
    {Shape::S5, Variant::Base, Verb::AppApp, "ok, ok | w | update_reboot_state:5>8"}, // KNOWN-WRONG: switch path taken where the state should refuse
    {Shape::S5, Variant::Mirror, Verb::Fw, "Generic(ECANCELED) | - | ="},
    {Shape::S5, Variant::Mirror, Verb::App, "ok | w | update_reboot_state:5>8 application:B>A"}, // KNOWN-WRONG: switch path taken where the state should refuse
    {Shape::S5, Variant::Settled, Verb::Fw, "ok | w | update_reboot_state:5>7 BOOT_ORDER:AB>BA"}, // KNOWN-WRONG: switch path taken where the state should refuse
    {Shape::S5, Variant::TargetBad, Verb::App, "Generic(EPERM) | - | ="},
    {Shape::S5, Variant::NoConfig, Verb::App, "runtime_error | - | ="},
    {Shape::S5, Variant::AppFlipped, Verb::App, "ok | w | update_reboot_state:5>8 application:B>A"}, // KNOWN-WRONG: switch path taken where the state should refuse

    {Shape::S6, Variant::Base, Verb::Fw, "ok | w | update_reboot_state:6>7 BOOT_ORDER:AB>BA"}, // KNOWN-WRONG: switch path taken where the state should refuse
    {Shape::S6, Variant::Base, Verb::App, "Generic(ECANCELED) | - | ="},
    {Shape::S6, Variant::Base, Verb::FwFw, "ok, ok | w | update_reboot_state:6>7 BOOT_ORDER:AB>BA"}, // KNOWN-WRONG: switch path taken where the state should refuse
    {Shape::S6, Variant::Base, Verb::AppApp, "Generic(ECANCELED), Generic(ECANCELED) | - | ="},
    {Shape::S6, Variant::Mirror, Verb::Fw, "ok | w | update_reboot_state:6>7 BOOT_ORDER:BA>AB"}, // KNOWN-WRONG: switch path taken where the state should refuse
    {Shape::S6, Variant::Mirror, Verb::App, "Generic(ECANCELED) | - | ="},
    {Shape::S6, Variant::Settled, Verb::App, "ok | w | update_reboot_state:6>8 application:A>B"}, // KNOWN-WRONG: switch path taken where the state should refuse
    {Shape::S6, Variant::TargetBad, Verb::Fw, "Generic(EPERM) | - | ="},
    {Shape::S6, Variant::BoToggle, Verb::Fw, "ok | w | update_reboot_state:6>7 BOOT_ORDER:AB>BA BOOT_ORDER_OLD:BA>AB"}, // KNOWN-WRONG: switch path taken where the state should refuse
    {Shape::S6, Variant::AppFlipped, Verb::App, "ok | w | update_reboot_state:6>8 application:B>A"}, // KNOWN-WRONG: switch path taken where the state should refuse

    {Shape::S7Pre, Variant::Base, Verb::Fw, "ok | w | ="}, // KNOWN-WRONG: switch path taken where the state should refuse
    {Shape::S7Pre, Variant::Base, Verb::App, "ok | w | update_reboot_state:7>8 application:A>B"}, // KNOWN-WRONG: switch path taken where the state should refuse
    {Shape::S7Pre, Variant::Base, Verb::FwFw, "ok, ok | w | ="}, // KNOWN-WRONG: switch path taken where the state should refuse
    {Shape::S7Pre, Variant::Base, Verb::AppApp, "ok, ok | w | update_reboot_state:7>8"}, // KNOWN-WRONG: switch path taken where the state should refuse
    {Shape::S7Pre, Variant::Mirror, Verb::Fw, "ok | w | ="}, // KNOWN-WRONG: switch path taken where the state should refuse
    {Shape::S7Pre, Variant::Mirror, Verb::App, "ok | w | update_reboot_state:7>8 application:B>A"}, // KNOWN-WRONG: switch path taken where the state should refuse
    {Shape::S7Pre, Variant::TargetBad, Verb::Fw, "Generic(EPERM) | - | ="},
    {Shape::S7Pre, Variant::TargetBad, Verb::App, "Generic(EPERM) | - | ="},
    {Shape::S7Pre, Variant::BoToggle, Verb::Fw, "ok | w | BOOT_ORDER_OLD:BA>AB"}, // KNOWN-WRONG: switch path taken where the state should refuse
    {Shape::S7Pre, Variant::Budget03, Verb::Fw, "Generic(0) | - | ="},
    {Shape::S7Pre, Variant::Budget03, Verb::App, "Generic(0) | - | ="},
    {Shape::S7Pre, Variant::Budget30, Verb::Fw, "Generic(0) | - | ="},
    {Shape::S7Pre, Variant::Budget30, Verb::App, "Generic(0) | - | ="},
    {Shape::S7Pre, Variant::NoConfig, Verb::App, "runtime_error | - | ="},
    {Shape::S7Pre, Variant::AppFlipped, Verb::App, "ok | w | update_reboot_state:7>8 application:B>A"}, // KNOWN-WRONG: switch path taken where the state should refuse

    {Shape::S7Post, Variant::Base, Verb::Fw, "ok | w | BOOT_ORDER:BA>AB BOOT_ORDER_OLD:AB>BA"}, // KNOWN-WRONG: switch path taken where the state should refuse
    {Shape::S7Post, Variant::Base, Verb::App, "ok | w | update_reboot_state:7>8 application:A>B"}, // KNOWN-WRONG: switch path taken where the state should refuse
    {Shape::S7Post, Variant::Base, Verb::FwFw, "ok, ok | w | BOOT_ORDER:BA>AB BOOT_ORDER_OLD:AB>BA"}, // KNOWN-WRONG: switch path taken where the state should refuse
    {Shape::S7Post, Variant::Base, Verb::AppApp, "ok, ok | w | update_reboot_state:7>8"}, // KNOWN-WRONG: switch path taken where the state should refuse
    {Shape::S7Post, Variant::Mirror, Verb::Fw, "ok | w | BOOT_ORDER:AB>BA BOOT_ORDER_OLD:BA>AB"}, // KNOWN-WRONG: switch path taken where the state should refuse
    {Shape::S7Post, Variant::Mirror, Verb::App, "ok | w | update_reboot_state:7>8 application:B>A"}, // KNOWN-WRONG: switch path taken where the state should refuse
    {Shape::S7Post, Variant::TargetBad, Verb::Fw, "Generic(EPERM) | - | ="},
    {Shape::S7Post, Variant::TargetBad, Verb::App, "Generic(EPERM) | - | ="},
    {Shape::S7Post, Variant::BoToggle, Verb::Fw, "ok | w | BOOT_ORDER:BA>AB"}, // KNOWN-WRONG: switch path taken where the state should refuse
    {Shape::S7Post, Variant::NoConfig, Verb::App, "runtime_error | - | ="},
    {Shape::S7Post, Variant::AppFlipped, Verb::App, "ok | w | update_reboot_state:7>8 application:B>A"}, // KNOWN-WRONG: switch path taken where the state should refuse

    {Shape::S8Pre, Variant::Base, Verb::Fw, "ok | w | update_reboot_state:8>7 BOOT_ORDER:AB>BA"}, // KNOWN-WRONG: switch path taken where the state should refuse
    {Shape::S8Pre, Variant::Base, Verb::App, "ok | w | application:B>A"}, // KNOWN-WRONG: switch path taken where the state should refuse
    {Shape::S8Pre, Variant::Base, Verb::FwFw, "ok, ok | w | update_reboot_state:8>7 BOOT_ORDER:AB>BA"}, // KNOWN-WRONG: switch path taken where the state should refuse
    {Shape::S8Pre, Variant::Base, Verb::AppApp, "ok, Generic(0) | w | application:B>A"}, // KNOWN-WRONG: switch path taken where the state should refuse
    {Shape::S8Pre, Variant::Mirror, Verb::Fw, "ok | w | update_reboot_state:8>7 BOOT_ORDER:BA>AB"}, // KNOWN-WRONG: switch path taken where the state should refuse
    {Shape::S8Pre, Variant::Mirror, Verb::App, "ok | w | application:A>B"}, // KNOWN-WRONG: switch path taken where the state should refuse
    {Shape::S8Pre, Variant::TargetBad, Verb::Fw, "Generic(EPERM) | - | ="},
    {Shape::S8Pre, Variant::TargetBad, Verb::App, "Generic(EPERM) | - | ="},
    {Shape::S8Pre, Variant::BoToggle, Verb::Fw, "ok | w | update_reboot_state:8>7 BOOT_ORDER:AB>BA BOOT_ORDER_OLD:BA>AB"}, // KNOWN-WRONG: switch path taken where the state should refuse
    {Shape::S8Pre, Variant::MountActive, Verb::Fw, "Generic(0) | - | ="},
    {Shape::S8Pre, Variant::MountActive, Verb::App, "Generic(0) | - | ="},
    {Shape::S8Pre, Variant::MountNone, Verb::Fw, "Generic(0) | - | ="},
    {Shape::S8Pre, Variant::MountNone, Verb::App, "Generic(0) | - | ="},
    {Shape::S8Pre, Variant::NoConfig, Verb::App, "runtime_error | - | ="},
    {Shape::S8Pre, Variant::AppFlipped, Verb::App, "ok | w | application:A>B"}, // KNOWN-WRONG: switch path taken where the state should refuse

    {Shape::S8Post, Variant::Base, Verb::Fw, "Generic(0) | - | ="},
    {Shape::S8Post, Variant::Base, Verb::App, "Generic(0) | - | ="},
    {Shape::S8Post, Variant::Base, Verb::FwFw, "Generic(0), Generic(0) | - | ="},
    {Shape::S8Post, Variant::Base, Verb::AppApp, "Generic(0), Generic(0) | - | ="},
    {Shape::S8Post, Variant::Mirror, Verb::Fw, "Generic(0) | - | ="},
    {Shape::S8Post, Variant::Mirror, Verb::App, "Generic(0) | - | ="},
    {Shape::S8Post, Variant::MountOther, Verb::Fw, "ok | w | update_reboot_state:8>7 BOOT_ORDER:AB>BA"}, // KNOWN-WRONG: switch path taken where the state should refuse
    {Shape::S8Post, Variant::MountOther, Verb::App, "ok | w | application:B>A"}, // KNOWN-WRONG: switch path taken where the state should refuse

    {Shape::S9Pre, Variant::Base, Verb::Fw, "ok | w | update_reboot_state:9>7 BOOT_ORDER:AB>BA"}, // KNOWN-WRONG: switches into the firmware the rollback abandoned
    {Shape::S9Pre, Variant::Base, Verb::App, "Generic(ECANCELED) | - | ="},
    {Shape::S9Pre, Variant::Base, Verb::FwFw, "ok, ok | w | update_reboot_state:9>7 BOOT_ORDER:AB>BA"}, // KNOWN-WRONG: switches into the firmware the rollback abandoned
    {Shape::S9Pre, Variant::Base, Verb::AppApp, "Generic(ECANCELED), Generic(ECANCELED) | - | ="},
    {Shape::S9Pre, Variant::Mirror, Verb::Fw, "ok | w | update_reboot_state:9>7 BOOT_ORDER:BA>AB"}, // KNOWN-WRONG: switches into the firmware the rollback abandoned
    {Shape::S9Pre, Variant::Mirror, Verb::App, "Generic(ECANCELED) | - | ="},
    {Shape::S9Pre, Variant::Settled, Verb::App, "ok | w | update_reboot_state:9>8 application:A>B"}, // KNOWN-WRONG: switch path taken where the state should refuse
    {Shape::S9Pre, Variant::TargetBad, Verb::Fw, "Generic(EPERM) | - | ="},
    {Shape::S9Pre, Variant::BoToggle, Verb::Fw, "ok | w | update_reboot_state:9>7 BOOT_ORDER:AB>BA BOOT_ORDER_OLD:BA>AB"}, // KNOWN-WRONG: switches into the firmware the rollback abandoned
    {Shape::S9Pre, Variant::AppFlipped, Verb::App, "ok | w | update_reboot_state:9>8 application:B>A"}, // KNOWN-WRONG: switch path taken where the state should refuse

    {Shape::S9Post, Variant::Base, Verb::Fw, "ok | w | update_reboot_state:9>7 BOOT_ORDER:BA>AB BOOT_ORDER_OLD:AB>BA"}, // KNOWN-WRONG: switch path in 9; committing the 7 re-adopts the rolled-back firmware
    {Shape::S9Post, Variant::Base, Verb::App, "Generic(ECANCELED) | - | ="},
    {Shape::S9Post, Variant::Base, Verb::FwFw, "ok, Generic(0) | w | update_reboot_state:9>7 BOOT_ORDER:BA>AB BOOT_ORDER_OLD:AB>BA"}, // KNOWN-WRONG: switch path in 9; committing the 7 re-adopts the rolled-back firmware
    {Shape::S9Post, Variant::Base, Verb::AppApp, "Generic(ECANCELED), Generic(ECANCELED) | - | ="},
    {Shape::S9Post, Variant::Mirror, Verb::Fw, "ok | w | update_reboot_state:9>7 BOOT_ORDER:AB>BA BOOT_ORDER_OLD:BA>AB"}, // KNOWN-WRONG: switch path in 9; committing the 7 re-adopts the rolled-back firmware
    {Shape::S9Post, Variant::Mirror, Verb::App, "Generic(ECANCELED) | - | ="},
    {Shape::S9Post, Variant::Settled, Verb::App, "ok | w | update_reboot_state:9>8 application:A>B"}, // KNOWN-WRONG: switch path taken where the state should refuse
    {Shape::S9Post, Variant::TargetBad, Verb::Fw, "Generic(EPERM) | - | ="},
    {Shape::S9Post, Variant::BoToggle, Verb::Fw, "ok | w | update_reboot_state:9>7 BOOT_ORDER:BA>AB"}, // KNOWN-WRONG: switch path in 9; committing the 7 re-adopts the rolled-back firmware
    {Shape::S9Post, Variant::AppFlipped, Verb::Fw, "Generic(0) | - | ="},
    {Shape::S9Post, Variant::AppFlipped, Verb::App, "Generic(0) | - | ="},

    {Shape::S10, Variant::Base, Verb::Fw, "Generic(0) | - | ="},
    {Shape::S10, Variant::Base, Verb::App, "Generic(0) | - | ="},
    {Shape::S10, Variant::Base, Verb::FwFw, "Generic(0), Generic(0) | - | ="},
    {Shape::S10, Variant::Base, Verb::AppApp, "Generic(0), Generic(0) | - | ="},
    {Shape::S10, Variant::Mirror, Verb::Fw, "Generic(0) | - | ="},
    {Shape::S10, Variant::Mirror, Verb::App, "Generic(0) | - | ="},

    {Shape::S11, Variant::Base, Verb::Fw, "Generic(0) | - | ="},
    {Shape::S11, Variant::Base, Verb::App, "Generic(0) | - | ="},
    {Shape::S11, Variant::Base, Verb::FwFw, "Generic(0), Generic(0) | - | ="},
    {Shape::S11, Variant::Base, Verb::AppApp, "Generic(0), Generic(0) | - | ="},
    {Shape::S11, Variant::Mirror, Verb::Fw, "Generic(0) | - | ="},
    {Shape::S11, Variant::Mirror, Verb::App, "Generic(0) | - | ="},

    {Shape::S12, Variant::Base, Verb::Fw, "Generic(0) | - | ="},
    {Shape::S12, Variant::Base, Verb::App, "Generic(0) | - | ="},
    {Shape::S12, Variant::Base, Verb::FwFw, "Generic(0), Generic(0) | - | ="},
    {Shape::S12, Variant::Base, Verb::AppApp, "Generic(0), Generic(0) | - | ="},
    {Shape::S12, Variant::Mirror, Verb::Fw, "Generic(0) | - | ="},
    {Shape::S12, Variant::Mirror, Verb::App, "Generic(0) | - | ="},
};
// clang-format on

INSTANTIATE_TEST_SUITE_P(Cells, RollbackGolden, ::testing::ValuesIn(kCells),
                         [](const ::testing::TestParamInfo<Cell> &info) {
                             return std::string(shape_name(info.param.shape)) + "_" + variant_name(info.param.variant) +
                                    "_" + verb_name(info.param.verb);
                         });

/* A verb whose environment write fails leaves nothing behind, and the same
 * call on the same object then lands exactly where an undisturbed call does.
 * Only shapes whose verb reaches the flush are listed. */
class RollbackAfterFailedFlush : public ::testing::TestWithParam<std::pair<Shape, Verb>>
{
};

TEST_P(RollbackAfterFailedFlush, WritesNothingAndCanBeRepeated)
{
    const Board board = shape_board(GetParam().first);
    const bool firmware = (GetParam().second == Verb::Fw);

    auto env = env_of(board);
    auto updater = updater_on(env, board);
    auto call = [&]() {
        if (firmware) {
            updater->rollback_firmware();
        } else {
            updater->rollback_application();
        }
    };
    const auto before = snapshot(*env);

    env->fail_next_flush();
    EXPECT_EQ(outcome_of(call), "UBootError");
    EXPECT_EQ(snapshot(*env), before) << "a failed flush left a partial write";

    EXPECT_EQ(outcome_of(call), "ok") << "the verb cannot be repeated after a failed flush";

    auto reference_env = env_of(board);
    auto reference = updater_on(reference_env, board);
    if (firmware) {
        reference->rollback_firmware();
    } else {
        reference->rollback_application();
    }
    EXPECT_EQ(snapshot(*env), snapshot(*reference_env));
}

INSTANTIATE_TEST_SUITE_P(Verbs, RollbackAfterFailedFlush,
                         ::testing::Values(std::make_pair(Shape::S0, Verb::Fw), std::make_pair(Shape::S0, Verb::App),
                                           std::make_pair(Shape::S2Pre, Verb::Fw),
                                           std::make_pair(Shape::S3Post, Verb::App),
                                           std::make_pair(Shape::S4Pre, Verb::Fw)),
                         [](const ::testing::TestParamInfo<std::pair<Shape, Verb>> &info) {
                             return std::string(shape_name(info.param.first)) + "_" + verb_name(info.param.second);
                         });

} // namespace
