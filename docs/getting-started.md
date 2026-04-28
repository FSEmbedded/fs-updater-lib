# Getting Started

## Prerequisites

- SDK sourced: `/opt/fslc-xwayland/5.15-scarthgap/environment-setup-cortexa53-fslc-linux`
- Target device with U-Boot environment configured for A/B updates

## Build

```bash
./scripts/build.sh debug           # cross-compile Debug
./scripts/build.sh release         # cross-compile Release (-Os, LTO)
./scripts/build.sh sanitize        # cross-compile Debug with ASan + UBSan
./scripts/build.sh clean

# Override U-Boot partition names (NAND and eMMC defaults)
./scripts/build.sh debug --nand mtd7 --mmc mmcblk0boot0
```

Output lands in `build/`. See [Contributing](../docs/contributing.md) for the
full CMake option reference.

## Install

```bash
make -C build install
```

Installs to the SDK sysroot:
- `libfs_updater.so.1` and `libfs_updater.a`
- Headers under `include/fs_update_framework/`

## Link

Add to your `CMakeLists.txt`:

```cmake
find_package(fs_updater REQUIRED)
target_link_libraries(my_app PRIVATE fs_updater::fs_updater)
```

Or with the CLI's `--lib` flag when building `fs-updater-cli` against a local
build of the library:

```bash
cd ../fs-updater-cli
./scripts/build.sh debug --lib ../fs-updater-lib/build
```

## First use

```cpp
#include <fs_update_framework/handle_update/fsupdate.h>
#include <fs_update_framework/logger/LoggerSinkStdout.h>

// 1. Set up a logger (required by FSUpdate constructor)
auto sink   = std::make_shared<logger::LoggerSinkStdout>();
auto logger = logger::LoggerHandler::initLogger(sink);

// 2. Create the update handler
fs::FSUpdate updater(logger);

// 3. Ensure the work directory exists (needed for bundle extraction)
updater.create_work_dir();

// 4. Install a .fs bundle; reboot is required before commit
std::string path        = "/mnt/usb/update.fs";
std::string type        = "";   // empty → install all components in the bundle
uint8_t     installed   = 0;
updater.update_image(path, type, installed);

// installed == 1 (fw), 2 (app), or 3 (both) depending on bundle contents

// --- reboot the device here ---

// 5. After rebooting into the new slot, commit
bool committed = updater.commit_update();
// committed == true  → IDLE
// committed == false → already idle, nothing to commit
```

## Query installed versions

```cpp
version_t fw_ver  = updater.get_firmware_version();
version_t app_ver = updater.get_application_version();
```

## Roll back on failure

```cpp
// If get_update_reboot_state() returns FAILED_FW_UPDATE or FW_UPDATE_REBOOT_FAILED:
updater.rollback_firmware();
// Then reboot and call commit_update() to finalise.
```

## Next steps

- [Architecture](architecture.md) — component diagram, bundle format, update flows
- [State Machine](state-machine.md) — `UBootBootstateFlags` enum and transition diagram
- [API Reference](reference/api.md) — full method signatures and error model
- [U-Boot Variables](reference/uboot-variables.md) — variable lifecycle table
