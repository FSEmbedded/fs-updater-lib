# F&S Updater Library

Core library for the F&S Update Framework providing firmware and application update functionality for embedded Linux systems.

## Overview

The F&S Updater Library (`fs_updater`) is a C++ library that provides:

- **Firmware Updates**: Integration with RAUC for A/B firmware updates
- **Application Updates**: SquashFS application image management
- **Boot State Management**: U-Boot environment handling for update state tracking
- **Rollback Support**: Automatic and manual rollback capabilities
- **Logging Framework**: Thread-safe logging with pluggable sinks

## Features

- **A/B Update Support**: Maintains two copies of firmware/application for safe updates
- **RAUC Integration**: Uses RAUC for reliable firmware slot management
- **State Machine**: Tracks update progress through well-defined states
- **Thread-Safe Logging**: Asynchronous logging with multiple sink support
- **Rollback Detection**: Automatic detection of failed updates
- **Version Management**: Supports string and uint64 version formats

## Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                         Application                                  │
│                      (fs-updater-cli)                               │
└─────────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────────┐
│                         FSUpdate                                     │
│                      (Public API)                                    │
│  - update_firmware()            - commit_update()                    │
│  - update_application()         - rollback_firmware()                │
│  - update_firmware_and_application()  - rollback_application()       │
│  - update_image()               - get_update_reboot_state()          │
│  - set/is_update_state_bad()    - get_firmware/application_version() │
└─────────────────────────────────────────────────────────────────────┘
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
│                         UBoot                                        │
│                   (Environment Access)                               │
│  - getVariable()  - addVariable()  - flushEnvironment()              │
└─────────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────────┐
│                      libubootenv                                     │
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

### CMake Configuration

```bash
mkdir build && cd build
cmake .. \
    -DUBOOT_ENV_NAND=mtd5 \
    -DUBOOT_ENV_MMC=mmcblk2boot0 \
    -Dupdate_version_type=string
make
```

### Build Options

| Option | Default | Description |
|--------|---------|-------------|
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
- `libfs_updater.a` - Static library
- `libfs_updater.so.1` - Shared library
- Headers to `include/fs_update_framework/`

## Usage

### Basic Update Flow

```cpp
#include <fs_update_framework/handle_update/fsupdate.h>
#include <fs_update_framework/logger/LoggerSinkStdout.h>

// Initialize logger
auto sink = std::make_shared<logger::LoggerSinkStdout>();
auto logger = logger::LoggerHandler::initLogger(sink);

// Create update handler
fs::FSUpdate updater(logger);

// Create work directory
updater.create_work_dir();

// Perform firmware update
try {
    updater.update_firmware("/path/to/firmware.raucb");
    // Reboot required...
} catch (const std::exception& e) {
    // Handle error
}

// After reboot, commit the update
if (updater.commit_update()) {
    // Update committed successfully
}
```

### Update State Machine

The library tracks update state in the U-Boot variable `update_reboot_state` (enum `UBootBootstateFlags`, values 0–12 + sentinel 13). The diagram below is the single source of truth; it is mirrored identically in [`prd.md`](../prd.md) §3.2 and [`architecture.md`](documentation/architecture.md).

```
Phase 1 — Install (from IDLE)
──────────────────────────────
                           NO_UPDATE_REBOOT_PENDING (0)  [IDLE]
                                      │
           ┌──────────────────────────┼──────────────────────────┐
  update_firmware()          update_application()     update_firmware_and_application()
           │                          │                          │
           ▼                          ▼                          ▼
  INCOMPLETE_FW_UPDATE (2)   INCOMPLETE_APP_UPDATE (3)   INCOMPLETE_APP_FW_UPDATE (4)
           │ install fail             │ install fail             │ install fail
           ▼                          ▼                          ▼
  FAILED_FW_UPDATE (5)       FAILED_APP_UPDATE (6)       FAILED_FW / FAILED_APP (5/6)

Phase 2 — Reboot & verify (INCOMPLETE_* → commit or fail)
──────────────────────────────────────────────────────────
  INCOMPLETE_FW_UPDATE (2) ──reboot──▶ bootloader selects new FW slot
                                         │
                                ┌────────┴────────┐
                          booted new        booted old (BOOT_X_LEFT = 0)
                                │                  │
                                ▼                  ▼
                        commit_update()   FW_UPDATE_REBOOT_FAILED (1)
                              → IDLE (0)

  INCOMPLETE_APP_UPDATE (3) ──reboot──▶ dynamic-overlay mounts new APP slot
                                         │
                                ┌────────┴────────┐
                              success            failure
                                │                  │
                                ▼                  ▼
                        commit_update()    FAILED_APP_UPDATE (6)
                              → IDLE (0)

  INCOMPLETE_APP_FW_UPDATE (4) ──reboot──▶ both verifications run
                                         │
                                ┌────────┴────────┐
                              success      any component fails
                                │                  │
                                ▼                  ▼
                        commit_update()    FAILED_FW / FAILED_APP (5/6)
                              → IDLE (0)

Phase 3 — Rollback initiation (from FAILED_* or FW_UPDATE_REBOOT_FAILED)
─────────────────────────────────────────────────────────────────────────
  FAILED_FW_UPDATE (5)            ──rollback_firmware()────▶   ROLLBACK_FW_REBOOT_PENDING (7)
  FAILED_APP_UPDATE (6)           ──rollback_application()─▶   ROLLBACK_APP_REBOOT_PENDING (8)
  combined (5 + 6)                ──rollback_*──────────────▶  ROLLBACK_APP_FW_REBOOT_PENDING (9)
  FW_UPDATE_REBOOT_FAILED (1)     ──rollback_firmware()────▶   ROLLBACK_FW_REBOOT_PENDING (7)

Phase 4 — Rollback verify (post-reboot)
────────────────────────────────────────
  ROLLBACK_FW_REBOOT_PENDING (7)      ──reboot──▶ INCOMPLETE_FW_ROLLBACK (10)
  ROLLBACK_APP_REBOOT_PENDING (8)     ──reboot──▶ INCOMPLETE_APP_ROLLBACK (11)
  ROLLBACK_APP_FW_REBOOT_PENDING (9)  ──reboot──▶ INCOMPLETE_APP_FW_ROLLBACK (12)
                                                         │
                                                         │ commit_update()
                                                         ▼
                                                   IDLE (0)

Sentinel
────────
  UNKNOWN_STATE (13) — returned when update_reboot_state holds an invalid value.
                       Not a reachable phase; indicates environment corruption.
```

### State Definitions

| State | Value | Description |
|-------|-------|-------------|
| `NO_UPDATE_REBOOT_PENDING` | 0 | Normal operation, no pending updates |
| `FW_UPDATE_REBOOT_FAILED` | 1 | Firmware installed but bootloader fell back to old slot (`BOOT_X_LEFT` drained) |
| `INCOMPLETE_FW_UPDATE` | 2 | Firmware installed, awaiting reboot verification |
| `INCOMPLETE_APP_UPDATE` | 3 | Application installed, awaiting reboot verification |
| `INCOMPLETE_APP_FW_UPDATE` | 4 | Both installed, awaiting reboot verification |
| `FAILED_FW_UPDATE` | 5 | Firmware installation failed (installer-level) |
| `FAILED_APP_UPDATE` | 6 | Application installation or post-reboot mount failed |
| `ROLLBACK_FW_REBOOT_PENDING` | 7 | Firmware rollback requested, reboot pending |
| `ROLLBACK_APP_REBOOT_PENDING` | 8 | Application rollback requested, reboot pending |
| `ROLLBACK_APP_FW_REBOOT_PENDING` | 9 | Both rollbacks requested, reboot pending |
| `INCOMPLETE_FW_ROLLBACK` | 10 | Firmware rolled back, awaiting commit |
| `INCOMPLETE_APP_ROLLBACK` | 11 | Application rolled back, awaiting commit |
| `INCOMPLETE_APP_FW_ROLLBACK` | 12 | Both rolled back, awaiting commit |
| `UNKNOWN_STATE` | 13 | Sentinel: invalid `update_reboot_state` value |

## Module Reference

### FSUpdate (fsupdate.h)

Main public interface for update operations.

```cpp
namespace fs {
class FSUpdate {
    void update_firmware(const string& path);
    void update_application(const string& path);
    void update_firmware_and_application(const string& fw, const string& app);
    bool commit_update();
    void rollback_firmware();
    void rollback_application();
    UBootBootstateFlags get_update_reboot_state();
    version_t get_application_version();
    version_t get_firmware_version();
};
}
```

### rauc_handler (rauc_handler.h)

RAUC integration for firmware updates.

```cpp
namespace rauc {
class rauc_handler {
    void installBundle(const string& path);
    void markUpdateAsSuccessful();
    void markOtherPartition();
    void rollback();
    Json::Value getStatus();
};
}
```

### UBoot (UBoot.h)

U-Boot environment access via libubootenv. Thread-safe (mutex-protected).

Writes use a batch pattern: stage variables with `addVariable()`, then
write all at once with `flushEnvironment()`. This minimizes flash writes
and provides atomicity via libubootenv's redundant environment copies.

```cpp
namespace UBoot {
class UBoot {
    // Read variable directly from U-Boot environment
    string getVariable(const string& name);
    // Typed overloads with allowed-value validation
    uint8_t getVariable(const string& name, const vector<uint8_t>& allowed);
    string  getVariable(const string& name, const vector<string>& allowed);
    char    getVariable(const string& name, const vector<char>& allowed);

    // Stage a variable for writing (does not touch flash)
    void addVariable(const string& key, const string& value);
    // Write all staged variables to flash in one operation
    void flushEnvironment();
    // Discard all staged variables without writing
    void freeVariables();
};
}
```

### CertificateVerifier / ImageVerifier (updateApplication.h)

X.509 certificate chain verification and image signature validation for application updates.

```cpp
namespace updater {
class CertificateVerifier {
    // Verify full certificate chain from image against keyring
    bool verify_certificate_chain(const vector<Botan::X509_Certificate>& chain);
    // Extract PEM certificates embedded after squashfs content
    vector<Botan::X509_Certificate> extract_certificates_from_image(const path& image);
};

class ImageVerifier {
    // Verify 16-byte header (squashfs_size, version, CRC32)
    bool verify_header(const vector<uint8_t>& data, uint64_t& size,
                       uint32_t& version, uint32_t& crc);
    // Verify PSSR(SHA-256) signature over squashfs + timestamp
    bool verify_signature(const Botan::X509_Certificate& cert,
                         applicationImage& app, uint64_t squashfs_size,
                         const vector<uint8_t>& timestamp,
                         const vector<uint8_t>& signature);
};
}
```

**PKI model:** Supports signing via intermediate CA or directly by root CA.
- Keyring (on device): root cert + optional intermediate cert
- Chain (in image): signing cert + optional intermediate cert
- Validates: chain path, codeSigning EKU on leaf, cert validity at signing time

### LoggerHandler (LoggerHandler.h)

Thread-safe logging framework.

```cpp
namespace logger {
class LoggerHandler {
    static shared_ptr<LoggerHandler> initLogger(shared_ptr<LoggerSinkBase>& sink);
    void setLogEntry(const shared_ptr<LogEntry>& msg);
};
}
```

## File Structure

```
fs-updater-lib/
├── CMakeLists.txt
├── README.md
├── cmake/
│   └── config.h.in
├── documentation/
│   ├── main_page.md
│   └── architecture.md
└── src/
    ├── BaseException.h
    ├── handle_update/
    │   ├── fsupdate.cpp/h         # Public API
    │   ├── handleUpdate.cpp/h     # Bootstate management
    │   ├── updateApplication.cpp/h # Application updates
    │   ├── updateFirmware.cpp/h   # Firmware updates
    │   ├── updateBase.cpp/h       # Base update class
    │   ├── updateDefinitions.cpp/h # State definitions
    │   ├── applicationImage.cpp/h # Image handling
    │   ├── UpdateStore.cpp/h      # Update storage
    │   ├── LibArchiveHandle.cpp/h # Archive extraction
    │   ├── utils.cpp/h            # Utilities
    │   ├── fs_consts.h            # Constants
    │   └── fs_exceptions.h        # Exception definitions
    ├── rauc/
    │   └── rauc_handler.cpp/h     # RAUC integration
    ├── uboot_interface/
    │   ├── UBoot.cpp/h            # U-Boot access
    │   └── allowed_uboot_variable_states.h
    ├── subprocess/
    │   └── subprocess.cpp/h       # Process execution
    └── logger/
        ├── LoggerHandler.cpp/h    # Logger core
        ├── LoggerEntry.cpp/h      # Log entry
        ├── LoggerLevel.h          # Log levels
        ├── LoggerSinkBase.h       # Sink interface
        ├── LoggerSinkStdout.cpp/h # Stdout sink
        └── LoggerSinkEmpty.cpp/h  # Null sink
```

## Error Handling

The library uses exceptions for error reporting. All exceptions derive from base classes:

- `fs::BaseFSUpdateException` - Base for update-related errors
- `rauc::RaucBaseException` - Base for RAUC-related errors
- `UBoot::UBootException` - Base for U-Boot access errors

Example error handling:

```cpp
try {
    updater.update_firmware("/path/to/bundle.raucb");
} catch (const rauc::RaucInstallBundle& e) {
    std::cerr << "Install failed: " << e.what() << std::endl;
    std::cerr << "RAUC report: " << e.report() << std::endl;
} catch (const fs::BaseFSUpdateException& e) {
    std::cerr << "Update error: " << e.what() << std::endl;
}
```

## Thread Safety

- `LoggerHandler`: Fully thread-safe, uses mutex and condition variables
- `FSUpdate`: Not thread-safe, use one instance per thread
- `UBoot`: All operations mutex-protected (reads and writes)
- `rauc_handler`: Not thread-safe, blocking subprocess calls
- `Bootstate`: Not thread-safe (uses UBoot internally)

## Related Projects

- [RAUC](https://rauc.io/) - Robust Auto-Update Controller
- [dynamic-overlay](../dynamic-overlay/) - Overlay filesystem mounting
- [fs-updater-cli](../fs-updater-cli/) - Command-line interface

## License

Proprietary - F&S Elektronik Systeme GmbH
