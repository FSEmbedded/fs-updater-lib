# Getting Started

## Prerequisites

- The F&S Yocto SDK (`SDK_ROOT`, default `/opt/fslc-xwayland/5.15-scarthgap`).
- A target with an A/B U-Boot environment (`/etc/fw_env.config`) and a RAUC
  daemon configured as described in
  [RAUC system.conf](integration/rauc-system-conf.md).

## Build and install

```bash
./scripts/build.sh release
cmake --install build --prefix <staging-prefix>
```

Targets, options, dependencies and what gets installed are in
[Contributing](contributing.md).

## Link

The library installs no CMake package config. Add the installed `include/`
directory and link `fs_updater` together with the dependencies listed in
[Contributing](contributing.md#dependencies):

```cmake
target_include_directories(my_app PRIVATE <staging-prefix>/include)
target_link_directories(my_app PRIVATE <staging-prefix>/lib)
target_link_libraries(my_app PRIVATE fs_updater ubootenv jsoncpp systemd
                      botan-2 z)   # botan-2 and z only with FUS_LEGACY_IMAGE_SUPPORT=ON
```

To build `fs-updater-cli` against a local build of this library, use that
repository's `--lib` option; its README describes it.

## First use

```cpp
#include <fs_update_framework/handle_update/fsupdate.h>
#include <fs_update_framework/logger/LoggerSinkStdout.h>

// 1. A logger; the sink takes the most verbose level it prints.
auto sink   = std::make_shared<logger::LoggerSinkStdout>(logger::logLevel::INFO);
auto logger = logger::LoggerHandler::initLogger(sink);

// 2. The update handler; opens the U-Boot environment.
fs::FSUpdate updater(logger);

// 3. Install a bundle. An empty type lets the library detect the format.
std::string path      = "/mnt/usb/update.fs";
std::string type      = "";
uint8_t     installed = 0;
updater.update_image(path, type, installed);
// installed == 1 (firmware), 2 (application) or 3 (both)

// --- reboot the device here ---

// 4. After the reboot, commit.
updater.commit_update();
```

`commit_update()` throws if the evidence does not support the stored state —
most commonly `updater::MissingReboot` when it is called before the reboot.
Its return value only says whether it wrote anything; with nothing pending it
still restores the running slot's boot counter, which is why a system that
relies on this library for boot confirmation calls it once per boot. Details:
[API Reference](reference/api.md#commit).

Not everything the library throws derives from `fs::BaseFSUpdateException`;
see [Exceptions](reference/api.md#exceptions) for the catch order.

## Query installed versions

```cpp
version_t fw_ver  = updater.get_firmware_version();     // /etc/fw_version
version_t app_ver = updater.get_application_version();  // /etc/app_version
```

## Roll back

```cpp
// An installed update you do not want (state 2, 3 or 4):
updater.rollback_firmware();      // or rollback_application() for state 3
```

Whether that takes effect at once or needs a reboot and `commit_update()`
depends on where the update stands; see [Rollback](reference/api.md#rollback).

A combined update (state 4) always goes to state 9, and rolled back before its
reboot that 9 cannot be committed; see
[Rolling back a combined update before its reboot](state-machine.md#rolling-back-a-combined-update-before-its-reboot).

A **failed** install (state 5 or 6) is not rolled back — the device never left
the proven slot. `commit_update()` acknowledges it. The recovery for every
state is listed in [Stale and stuck states](state-machine.md#stale-and-stuck-states).

## Next steps

- [State Machine](state-machine.md) — states, transitions, recovery
- [API Reference](reference/api.md) — every call, its exceptions, thread safety
- [Bundle Format](reference/bundle-format.md) — what `update_image()` accepts
- [U-Boot Variables](reference/uboot-variables.md) — the environment the library reads and writes
- [Architecture](architecture.md) — how the pieces fit
