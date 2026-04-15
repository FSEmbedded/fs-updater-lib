# F&S Updater Library Architecture

## Overview

The F&S Updater Library provides a complete solution for managing firmware and application updates on embedded Linux systems. It integrates with RAUC for firmware updates and provides custom handling for application image updates.

## Design Principles

1. **A/B Updates**: Always maintain a working fallback configuration
2. **State Persistence**: Track update progress in U-Boot environment (survives reboots)
3. **Separation of Concerns**: Modular components with clear responsibilities
4. **Error Recovery**: Automatic rollback on failure detection
5. **Thread Safety**: Safe concurrent logging from multiple components

## Component Architecture

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              Application Layer                               │
│                           (fs-updater-cli, etc.)                            │
└─────────────────────────────────────────────────────────────────────────────┘
                                      │
                                      │ Uses
                                      ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                              fs::FSUpdate                                    │
│                           (Public API Class)                                 │
│                                                                              │
│  Responsibilities:                                                           │
│  - Entry point for all update operations                                    │
│  - Work directory management                                                │
│  - Coordinate firmware and application updates                              │
│  - Delegate to specialized handlers                                         │
└─────────────────────────────────────────────────────────────────────────────┘
                                      │
              ┌───────────────────────┼───────────────────────┐
              │                       │                       │
              ▼                       ▼                       ▼
┌─────────────────────┐  ┌─────────────────────┐  ┌─────────────────────┐
│  updater::Bootstate │  │ updater::firmware   │  │ updater::application│
│                     │  │      Update         │  │      Update         │
│  - State detection  │  │                     │  │                     │
│  - State transitions│  │  - RAUC install     │  │  - Cert chain verify│
│  - Rollback logic   │  │  - Slot management  │  │  - Signature verify │
│  - Confirm updates  │  │  - Version check    │  │  - Image copy       │
└─────────────────────┘  └─────────────────────┘  └─────────────────────┘
              │                       │                       │
              │                       │                       │
              ▼                       ▼                       │
┌─────────────────────┐  ┌─────────────────────┐              │
│   UBoot::UBoot      │  │  rauc::rauc_handler │              │
│                     │  │                     │              │
│  - Read variables   │  │  - Install bundle   │              │
│  - Write variables  │  │  - Mark partitions  │              │
│  - Slot detection   │  │  - Get status       │              │
└─────────────────────┘  └─────────────────────┘              │
              │                       │                       │
              │                       │                       │
              ▼                       ▼                       ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                            System Layer                                      │
│                                                                              │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐    │
│  │ libubootenv  │  │    rauc      │  │  libarchive  │  │  filesystem  │    │
│  │              │  │   (binary)   │  │              │  │  operations  │    │
│  └──────────────┘  └──────────────┘  └──────────────┘  └──────────────┘    │
└─────────────────────────────────────────────────────────────────────────────┘
```

## Module Descriptions

### FSUpdate (fsupdate.h/cpp)

**Purpose**: Public API and update orchestration

**Key Members**:
```cpp
class FSUpdate {
    shared_ptr<UBoot::UBoot> uboot_handler;      // U-Boot access
    shared_ptr<logger::LoggerHandler> logger;    // Logging
    updater::Bootstate update_handler;           // State management
    filesystem::path work_dir;                   // Temp directory
    filesystem::path tmp_app_path;               // Temp app location
};
```

**Responsibilities**:
- Initialize all sub-components
- Create/manage work directory
- Route update requests to appropriate handlers
- Coordinate multi-image updates (firmware + application)

### Bootstate (handleUpdate.h/cpp)

**Purpose**: Update state machine management

**State Detection Methods**:
```cpp
bool pendingApplicationUpdate();
bool pendingFirmwareUpdate();
bool pendingApplicationFirmwareUpdate();
bool failedFirmwareUpdate();
bool failedRebootFirmwareUpdate();
bool failedApplicationUpdate();
bool pendingFirmwareRollback();
```

**State Confirmation Methods**:
```cpp
void confirmPendingFirmwareUpdate();
void confirmPendingApplicationUpdate();
void confirmPendingApplicationFirmwareUpdate();
void confirmFailedFirmwareUpdate();
void confirmFailedApplicationeUpdate();
void confirmUpdateRollback();
```

**Reboot Detection**:
- Compares current boot slot with expected slot
- Checks boot attempt counters (BOOT_A_LEFT, BOOT_B_LEFT)
- Detects firmware update success/failure based on slot changes

### rauc_handler (rauc_handler.h/cpp)

**Purpose**: RAUC integration for firmware updates

**Commands Wrapped**:
```cpp
rauc_install_cmd    // "rauc install"
rauc_info_cmd       // "rauc info"
rauc_status         // "rauc status --output-format=json"
rauc_mark_good      // "rauc status mark-good"
rauc_mark_good_other// "rauc status mark-good other"
rauc_rollback       // Combination of mark operations
```

**Memory Type Handling**:
```cpp
enum class memory_type {
    eMMC,    // Uses mmcblk2boot0 for U-Boot env
    NAND,    // Uses mtd5 for U-Boot env
    None
};
```

### applicationUpdate (updateApplication.h/cpp)

**Purpose**: Application image update handling with X.509 certificate verification

**Update Process**:
1. Extract archive to temporary location
2. Verify certificate chain (X.509 path validation)
3. Verify codeSigning EKU on signing certificate
4. Verify certificate was valid at signing time
5. Verify header CRC32
6. Verify PSSR(SHA-256) signature over squashfs content + timestamp
7. Copy to target slot (A or B)
8. Update symlink to new image
9. Set state for reboot confirmation

**Image Format**:
```
[header:16B][squashfs content][sign.cert PEM][intermediate.cert PEM][timestamp:26B][signature]
 └─ squashfs_size(8B) + version(4B) + crc32(4B)   └─ optional
```

**Image Locations**:
```
/rw_fs/root/application/
├── app_a.squashfs     # Slot A image
├── app_b.squashfs     # Slot B image
├── current -> app_a   # Symlink to active slot
└── tmp.app            # Temporary during install
```

### CertificateVerifier (updateApplication.h/cpp)

**Purpose**: X.509 certificate chain validation using Botan

**PKI Structure** (two signing modes):

| Mode | Chain in image | Keyring on device |
|------|----------------|-------------------|
| With intermediate CA | [signing, intermediate] | [root, intermediate] |
| Direct root signing | [signing] | [root] |

**Verification pipeline**:
1. `extract_certificates_from_image()` — read PEM certs from after squashfs
2. `verify_certificate_chain()` — split chain[0]=leaf, chain[1:]=intermediates
3. `load_trusted_certificates()` — parse keyring.pem (cached after first load)
4. `validate_certificate_chain()` — `Botan::x509_path_validate()` builds path from leaf to trusted root
5. Verify leaf has codeSigning EKU (`OID 1.3.6.1.5.5.7.3.3`)

**Keyring path**: read from `/etc/rauc/system.conf` `[keyring] path=`. Relative paths
are prefixed with `/etc/rauc/`, absolute paths are used as-is.

### ImageVerifier (updateApplication.h/cpp)

**Purpose**: Application image header and signature verification

**Header** (16 bytes, big-endian):
- Bytes 0-7: squashfs size (uint64)
- Bytes 8-11: version (uint32)
- Bytes 12-15: CRC32 over bytes 0-11

**Signature**: PSSR(SHA-256) with IEEE 1363 format over squashfs content + timestamp

### UBoot (UBoot.h/cpp)

**Purpose**: U-Boot environment variable access

**Key Variables Managed**:
| Variable | Purpose |
|----------|---------|
| `update` | 4-char slot state string `[FW_A][APP_A][FW_B][APP_B]`; each position: 0=committed, 1=uncommitted, 2=bad |
| `application` | Current app slot (A/B) |
| `BOOT_ORDER` | Boot priority |
| `BOOT_ORDER_OLD` | Previous boot order |
| `BOOT_A_LEFT` | Slot A boot attempts |
| `BOOT_B_LEFT` | Slot B boot attempts |
| `update_reboot_state` | Current state machine position |
| `rauc_cmd` | RAUC slot selection |

### LoggerHandler (LoggerHandler.h/cpp)

**Purpose**: Thread-safe asynchronous logging

**Architecture**:
```
┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│  Thread 1   │────▶│             │     │             │
├─────────────┤     │   FIFO      │────▶│    Sink     │
│  Thread 2   │────▶│   Queue     │     │  (stdout,   │
├─────────────┤     │             │     │   file,     │
│  Thread N   │────▶│  (mutex)    │     │   serial)   │
└─────────────┘     └─────────────┘     └─────────────┘
                          │
                          ▼
                    Worker Thread
                    (blocks on empty)
```

**Sink Interface**:
```cpp
class LoggerSinkBase {
    virtual void log(const shared_ptr<LogEntry>& entry) = 0;
};

// Implementations:
class LoggerSinkStdout : public LoggerSinkBase;  // Console output
class LoggerSinkEmpty : public LoggerSinkBase;   // Null sink
```

## Update State Machine

### State Transition Diagram

See **[state-machine.md](state-machine.md)** for the canonical state table,
full transition diagram (Phases 1–4 + Sentinel), `UBootBootstateFlags` enum,
and FR-LIB-SM-01..12 traceability.

Requirements authority: [`prd.md` §3.2](../../prd.md#32-fs-updater-lib).

## Update Bundle (`.fs`) and `update_image()` Contract

`FSUpdate::update_image(path, type, installed_update_type)` is the primary entry point used by the CLI for `--update_file` and `--automatic`. It accepts a single `.fs` bundle that may carry firmware, application, or both, and dispatches internally to `update_firmware()`, `update_application()`, or `update_firmware_and_application()`.

### Bundle format

```
.fs bundle
 ├── F&S header (64 bytes, fs_header_v1_0)
 │     • magic "FSLX" + version + file size
 │     • type = "UPDATE"
 └── tar.bz2 payload
       ├── fsupdate.json      # manifest (see below)
       ├── update.fw          # RAUC bundle, optional
       └── update.app         # application image, optional
```

At least one of `update.fw` / `update.app` must be present. Payload filenames are fixed (`app_store_name`, `fw_store_name` in `UpdateStore.h`).

### `fsupdate.json` manifest

```json
{
  "images": {
    "updates": [
      { "version": "<version>", "handler": "<handler>", "file": "update.fw",  "hashes": { "sha256": "<hex>" } },
      { "version": "<version>", "handler": "<handler>", "file": "update.app", "hashes": { "sha256": "<hex>" } }
    ]
  }
}
```

All four fields (`version`, `handler`, `file`, `hashes`) are required per `UpdateStore::CheckUpdateSha256Sum`. At least one entry must be present.

SHA-256 hashes are case-insensitive hex strings, verified by `UpdateStore::CheckUpdateSha256Sum` before any slot write.

### Install flow

```
FSUpdate::update_image(path, type, &installed_update_type)
    │
    ├─ decorator_update_state()            // require update_reboot_state == 0
    ├─ UpdateStore::ExtractUpdateStore()   // strip F&S header, extract tar.bz2 to work dir
    ├─ UpdateStore::ReadUpdateConfiguration() // parse fsupdate.json
    ├─ UpdateStore::CheckUpdateSha256Sum() // fail closed on any hash mismatch
    ├─ Decide dispatch from manifest + type filter:
    │     fw only        → update_firmware()                  → installed_update_type = 1
    │     app only       → update_application()               → installed_update_type = 2
    │     both           → update_firmware_and_application()  → installed_update_type = 3
    └─ Set update_reboot_state = INCOMPLETE_FW / INCOMPLETE_APP / INCOMPLETE_APP_FW
```

The `type` argument filters the manifest: passing `"fw"` on a bundle that carries both installs only the firmware payload (same for `"app"`). Empty `type` installs everything the manifest declares. `installed_update_type` is an out-parameter used by the CLI to pick the correct return-code enum (see §3.3.7 in `prd.md`).

### Staging paths

| Path | Purpose |
|------|---------|
| `TEMP_ADU_WORK_DIR` (default `/tmp/adu/.work`) | Bundle extraction, temp files |
| `TEMP_ADU_WORK_DIR/tmp.app` | Staged application image before copy to `/rw_fs/root/application/` |
| `/rw_fs/root/application/app_{a,b}.squashfs` | Application slot storage |
| `/rw_fs/root/application/current` | Symlink to active slot |

## Data Flow

### Firmware Update Flow

```
Application
    │
    │ update_firmware("/path/to/bundle.raucb")
    ▼
FSUpdate
    │
    ├─── Check: noUpdateProcessing() ?
    │         │
    │         └─── If false: throw UpdateInProgress
    │
    ├─── Create work directory
    │
    ├─── Call firmwareUpdate::install()
    │         │
    │         ├─── rauc_handler::installBundle()
    │         │         │
    │         │         └─── subprocess: "rauc install /path/to/bundle.raucb"
    │         │
    │         └─── Set update_reboot_state = INCOMPLETE_FW_UPDATE
    │
    └─── Return (reboot required)

=== REBOOT ===

Bootstate::confirmPendingFirmwareUpdate()
    │
    ├─── Detect boot slot (cmdline: rauc.slot=A/B)
    │
    ├─── Compare with expected slot
    │
    ├─── If successful:
    │         │
    │         ├─── rauc_handler::markUpdateAsSuccessful()
    │         │
    │         └─── Set update_reboot_state = NO_UPDATE_REBOOT_PENDING
    │
    └─── If failed:
              │
              └─── Set update_reboot_state = FW_UPDATE_REBOOT_FAILED
```

### Application Update Flow

```
Application
    │
    │ update_application("/path/to/app.tar.gz")
    ▼
FSUpdate
    │
    ├─── Check: noUpdateProcessing() ?
    │
    ├─── Create work directory
    │
    ├─── Call applicationUpdate::install()
    │         │
    │         ├─── LibArchiveHandle::extract()
    │         │         │
    │         │         └─── Extract to work_dir
    │         │
    │         ├─── Validate image
    │         │
    │         ├─── Determine target slot (opposite of current)
    │         │
    │         ├─── Copy to /rw_fs/root/application/app_[a|b].squashfs
    │         │
    │         └─── Set update_reboot_state = INCOMPLETE_APP_UPDATE
    │
    └─── Return (reboot required)

=== REBOOT (handled by dynamic-overlay) ===

Bootstate::confirmPendingApplicationUpdate()
    │
    ├─── Verify new application is mounted
    │
    ├─── Update symlink: current -> app_[a|b]
    │
    └─── Set update_reboot_state = NO_UPDATE_REBOOT_PENDING
```

## Exception Hierarchy

See [`prd.md` §9](../../prd.md) for the full tree and the CLI catch-to-return-code mapping. Summary:

- `fs::BaseFSUpdateException` — thrown by `fs::*`, `updater::*` classes. Catch-all base for update-framework errors (commit, rollback, install, verification, version mismatch, state transitions).
- `rauc::RaucBaseException` — thrown by `rauc_handler` (install bundle, mark partition, get status, parse JSON).
- `UBoot::UBootError` — thrown by `UBoot` (env read/write, value-validation failures).
- `subprocess::SubprocessError` — internal to the subprocess runner; converted to `rauc::RaucBaseException` before surfacing.

When adding an exception class, grep the source to regenerate the canonical list in `prd.md` §9 rather than editing this file.

## Thread Safety Model

| Component | Thread Safety | Notes |
|-----------|---------------|-------|
| LoggerHandler | Full | Mutex-protected queue, worker thread |
| FSUpdate | None | Single-threaded use only |
| UBoot | Full | All operations mutex-protected (`std::lock_guard`) |
| rauc_handler | None | Subprocess calls are blocking |
| Bootstate | None | Single-threaded use only |
| CertificateVerifier | None | Called from applicationUpdate |
| ImageVerifier | None | Called from applicationUpdate |

## Memory Management

- **Shared pointers** for logger and UBoot handlers (shared across components)
- **RAII** for file handles and archive operations
- **Scoped allocations** for temporary buffers
- **No raw `new`/`delete`** - smart pointers throughout

## Configuration

Build-time configuration via `config.h.in`:

```cpp
// Generated from CMake options
#define UBOOT_ENV_NAND "@UBOOT_ENV_NAND@"
#define UBOOT_ENV_MMC "@UBOOT_ENV_MMC@"

// Version type selection (exactly one is 1)
#define UPDATE_VERSION_TYPE_STRING @UPDATE_VERSION_TYPE_STRING@
#define UPDATE_VERSION_TYPE_UINT64 @UPDATE_VERSION_TYPE_UINT64@
```

Runtime configuration:
- Work directory: `TEMP_ADU_WORK_DIR` (default: `/tmp/adu/.work`)
- Application path: Hardcoded to `/rw_fs/root/application/`
