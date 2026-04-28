# F&S Updater Library

Core library for the F&S Update Framework. Consumed by `fs-updater-cli` and
any integrator service that needs A/B update orchestration. Writes the
U-Boot state variables that `dynamic-overlay` reads on the next boot.

## Overview

The F&S Updater Library (`fs_updater`) is a C++ library that provides:

- **Firmware Updates**: Integration with RAUC for A/B firmware updates
- **Application Updates**: SquashFS application image management
- **Boot State Management**: U-Boot environment handling for update state tracking
- **Rollback Support**: Automatic and manual rollback capabilities
- **Logging Framework**: Thread-safe logging with pluggable sinks

## Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                         Application                                 │
│                      (fs-updater-cli)                               │
└─────────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌──────────────────────────────────────────────────────────────────────┐
│                         FSUpdate                                     │
│                      (Public API)                                    │
│  - update_firmware()            - commit_update()                    │
│  - update_application()         - rollback_firmware()                │
│  - update_firmware_and_application()  - rollback_application()       │
│  - update_image()               - get_update_reboot_state()          │
│  - set/is_update_state_bad()    - get_firmware/application_version() │
└──────────────────────────────────────────────────────────────────────┘
                              │
         ┌────────────────────┼────────────────────┐
         │                    │                    │
         ▼                    ▼                    ▼
┌─────────────────┐  ┌─────────────────┐  ┌─────────────────────────┐
│   Bootstate     │  │  rauc_handler   │  │  applicationUpdate      │
│                 │  │                 │  │                         │
│ - State detect  │  │ - installBundle │  │ - CertificateVerifier   │
│ - Confirm/fail  │  │ - markGood      │  │   (X.509 chain + EKU)   │
│ - Reboot detect │  │ - rollback      │  │ - ImageVerifier         │
│ - Rollback      │  │ - getStatus     │  │   (header CRC + sig)    │
└────────┬────────┘  └────────┬────────┘  └────────┬────────────────┘
         │                    │                    │
         └────────────────────┼────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────────┐
│                         UBoot                                       │
│                   (Environment Access)                              │
│  - getVariable()  - addVariable()  - flushEnvironment()             │
└─────────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────────┐
│                      libubootenv                                    │
└─────────────────────────────────────────────────────────────────────┘
```

## Build Requirements

### Dependencies

| Library | Purpose |
|---------|---------|
| libubootenv | U-Boot environment access |
| libjsoncpp | JSON parsing for RAUC output |
| libarchive | Archive extraction for application updates |
| botan-2 | Cryptographic operations |

### Build Script

```bash
./scripts/build.sh debug           # cross-compile Debug (default)
./scripts/build.sh release         # cross-compile Release (-Os, LTO, stripped)
./scripts/build.sh sanitize        # cross-compile Debug with ASan + UBSan
./scripts/build.sh clean

# Platform-specific options
./scripts/build.sh debug --nand mtd7 --mmc mmcblk0boot0
./scripts/build.sh release --speed   # -O2 instead of -Os
```

The script handles `unset LD_LIBRARY_PATH` and SDK environment sourcing automatically.
Override the SDK location with `SDK_ROOT=/path/to/sdk ./scripts/build.sh debug`.

### Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `OPTIMIZE_FOR` | SIZE | Release optimization: `SIZE` (-Os) or `SPEED` (-O2) |
| `UBOOT_ENV_NAND` | mtd5 | MTD partition name for U-Boot env on NAND |
| `UBOOT_ENV_MMC` | mmcblk2boot0 | Block device for U-Boot env on eMMC |
| `update_version_type` | string | Version format: `string` or `uint64` |
| `fs_version_compare` | OFF | Enable F&S version comparison |
| `BOTAN2` | - | Manual include path for botan-2 headers |

## Installation

```bash
make install
```

Installs:
- `libfs_updater.a` — Static library
- `libfs_updater.so.1` — Shared library
- Headers to `include/fs_update_framework/`

## Quick Start

```cpp
#include <fs_update_framework/handle_update/fsupdate.h>
#include <fs_update_framework/logger/LoggerSinkStdout.h>

auto sink = std::make_shared<logger::LoggerSinkStdout>();
auto logger = logger::LoggerHandler::initLogger(sink);

fs::FSUpdate updater(logger);
updater.create_work_dir();

try {
    updater.update_firmware("/path/to/firmware.raucb");
    // reboot here
} catch (const fs::BaseFSUpdateException& e) {
    std::cerr << e.what() << std::endl;
}

// after reboot
updater.commit_update();
```

See [docs/getting-started.md](docs/getting-started.md) for the full call sequence including rollback and application updates.

## Documentation

| Document | Contents |
|----------|----------|
| [docs/getting-started.md](docs/getting-started.md) | Full update flow, error handling, build integration |
| [docs/architecture.md](docs/architecture.md) | Module design, bundle format, data flow |
| [docs/state-machine.md](docs/state-machine.md) | `update_reboot_state` values, transition diagram, stuck-state recovery |
| [docs/reference/api.md](docs/reference/api.md) | Full method signatures, exception hierarchy, thread safety |
| [docs/reference/bundle-format.md](docs/reference/bundle-format.md) | Old vs new bundle layouts, F&S header byte map, `fsupdate.json` schema |
| [docs/reference/uboot-variables.md](docs/reference/uboot-variables.md) | U-Boot variable lifecycle and write-batching contract |
| [docs/reference/rauc-contract.md](docs/reference/rauc-contract.md) | RAUC mark-good contract and counter-drain behaviour |
| [docs/integration/rauc-system-conf.md](docs/integration/rauc-system-conf.md) | `/etc/rauc/system.conf` configuration |
| [docs/contributing.md](docs/contributing.md) | Build, test, coding standard |

## Related Projects

- [RAUC](https://rauc.io/) — Robust Auto-Update Controller
- [dynamic-overlay](https://github.com/fsembedded/dynamic-overlay) — Overlay filesystem mounting
- [fs-updater-cli](https://github.com/fsembedded/fs-updater-cli) — Command-line interface

## License

MIT License
