# Contributing

## Build

```bash
./scripts/build.sh debug           # cross-compile Debug
./scripts/build.sh release         # cross-compile Release (-Os, LTO, stripped)
./scripts/build.sh sanitize        # cross-compile Debug with ASan + UBSan
./scripts/build.sh clean

# Override U-Boot partition names
./scripts/build.sh debug --nand mtd7 --mmc mmcblk0boot0
./scripts/build.sh release --speed  # -O2 instead of -Os
```

`SDK_ROOT` defaults to `/opt/fslc-xwayland/5.15-scarthgap`. Override with:

```bash
SDK_ROOT=/path/to/sdk ./scripts/build.sh debug
```

## CMake options

| Option | Values | Default | Effect |
|--------|--------|---------|--------|
| `OPTIMIZE_FOR` | `SIZE` / `SPEED` | `SIZE` | Release flags (`-Os` vs `-O2`) |
| `UBOOT_ENV_NAND` | MTD device name | `mtd5` | U-Boot env partition on NAND |
| `UBOOT_ENV_MMC` | block device name | `mmcblk2boot0` | U-Boot env partition on eMMC |
| `update_version_type` | `string` / `uint64` | `string` | Version field type in config header |
| `fs_version_compare` | `ON` / `OFF` | `OFF` | Enable F&S version comparison logic |
| `BOTAN2` | path | _(auto)_ | Manual include path for botan-2 headers |

## Tests

`fs-updater-lib` has no unit test suite. Integration testing requires a target
device or a QEMU image with U-Boot environment support and RAUC installed.

## Coding standard

Targeting C++17.

Key rules that apply to this library:

- **No `std::filesystem`** — use POSIX APIs (`::stat`, `::mkdir`, `::opendir`).
  Saves 50–100 KB binary size.
- **No `<iostream>` or `printf`** — use the `LoggerHandler` API. Saves ~100 KB.
- **No raw `new`/`delete`** — smart pointers and RAII only.
- **`[[nodiscard]]`** on all error-returning and `std::optional`-returning functions.
- **U-Boot writes must be batched** — always call `addVariable()` then
  `flushEnvironment()`. Never write individual variables; partial writes after
  a power failure leave the environment inconsistent (see
  [U-Boot Variables](reference/uboot-variables.md#write-batching-contract)).
- **Exception classes** — all new exception classes must derive from
  `fs::BaseFSUpdateException` or one of the existing base classes. Keep
  `what()` strings unique enough to identify the call site.
- **Exit codes are stable** — the CLI maps library exceptions to POSIX exit
  codes. Adding a new exception subclass does not change existing codes, but
  removing or renaming one can silently change the mapping. Grep for the class
  name in `fs-updater-cli` before renaming.
