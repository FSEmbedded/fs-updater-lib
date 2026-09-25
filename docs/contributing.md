# Contributing

This is the one place the build is described: targets, options and
dependencies. Other documents link here.

## Build

```bash
./scripts/build.sh debug              # cross-compile Debug (default)
./scripts/build.sh release            # cross-compile Release (-Os, LTO, stripped)
./scripts/build.sh sanitize           # cross-compile Debug with ASan + UBSan, into build_san/
./scripts/build.sh test               # native build + unit tests (ctest), into build_test/
./scripts/build.sh test --sanitize    # the same suite under ASan/UBSan, into build_test_san/
./scripts/build.sh fuzz               # native libFuzzer build (clang) + a short smoke run
./scripts/build.sh fuzz-cross         # the fuzz targets for aarch64, run under qemu-aarch64
./scripts/build.sh clean              # remove every build directory
```

`SDK_ROOT` defaults to `/opt/fslc-xwayland/5.15-scarthgap`; override it with
`SDK_ROOT=/path/to/sdk ./scripts/build.sh debug`. The script sources the SDK
environment and unsets `LD_LIBRARY_PATH` itself. It has no `--help`: an
unknown argument prints the full list of targets and flags and exits 1.

For a real fuzzing session, run the built binaries directly with a work
corpus first and `fuzz/seed_corpus/<target>` second; libFuzzer writes new
inputs into the first directory, and the seed corpus stays the curated set.

## CMake options

`build.sh` flags in the last column; everything else is passed as
`-D<option>=<value>` to CMake.

| Option | Default | Effect | `build.sh` |
|--------|---------|--------|------------|
| `OPTIMIZE_FOR` | `SIZE` | Release optimisation: `SIZE` (`-Os`) or `SPEED` (`-O2`) | `--speed` |
| `update_version_type` | `string` | `version_t` is `std::string` or (`uint64`) `uint64_t` | `--uint64` |
| `FUS_LEGACY_IMAGE_SUPPORT` | `ON` | Raw F&S application images and the `.fs` container, the only users of botan-2 and zlib. `OFF` leaves both formats out and refuses them as unsupported; raw RAUC bundles are unaffected ([Bundle Format](reference/bundle-format.md#accepted-inputs)) | `--no-legacy` |
| `FSUP_RAUC_SCRATCH` | `/rw_fs/.cache/update.fw` | Compile-time scratch path; its directory is where `update_image()` stages container members | `--scratch <path>` |
| `BUILD_RAUC_SCRATCH_OVERRIDE` | `ON` | Honour `update_image()`'s runtime `rauc_scratch_path` argument; `OFF` always uses `FSUP_RAUC_SCRATCH` | `--no-scratch-override` |
| `FSUP_APP_IMG_STORE` | `/rw_fs/root/application/` | Directory holding `app_a.squashfs` / `app_b.squashfs`, their verity sidecars and the `.incoming.squashfs` a RAUC application bundle's install hook writes. Trailing slash required; must match the BSP's hook | — |
| `FSUP_APP_VERSION_FILE` | `/etc/app_version` | File `get_application_version()` reads. Override per-BSP where the application ships its own version file instead, e.g. inside its own mount | — |
| `UBOOT_CONFIG_PATH` | empty (the header's `/etc/fw_env.config`) | The `fw_env.config` the default constructor opens; for test harnesses | `--env-config <path>` |
| `FUS_SOURCE_ID` | empty (`git describe`, else `unknown`) | Revision reported by `library_source_id()`; a recipe passes its `SRCREV` | — |
| `BUILD_DBUS_SUPPORT` | `ON` | Must be `ON`: the D-Bus client is the only RAUC backend. `OFF` stops at configure time with a `FATAL_ERROR` | `--no-dbus` (fails) |
| `BOTAN2` | empty | Manual include path for botan-2 headers when pkg-config does not find them | `--botan <path>` |
| `BUILD_TESTING` | `OFF` | Build the unit tests | set by `test` |
| `BUILD_MAIN_TARGET` | `ON` | Build the library; `OFF` builds only the natively testable sources | set by `test` |
| `FSUP_WERROR` | `ON` | `-Werror` on the library targets | — |
| `ENABLE_SANITIZERS` | `OFF` | ASan + UBSan | `--sanitize`, `sanitize` |
| `BUILD_FUZZING` | `OFF` | libFuzzer targets (clang) | set by `fuzz` |
| `UBOOT_ENV_NAND` | `mtd5` | **No effect.** Written into the generated `config.h` as `FUS_LIB_UBOOT_ENV_NAND`, which nothing reads; the environment location comes from `fw_env.config` | `--nand <part>` |
| `UBOOT_ENV_MMC` | `mmcblk2boot0` | **No effect**, as `UBOOT_ENV_NAND` | `--mmc <dev>` |
| `fs_version_compare` | `OFF` | **No effect.** Declared, but nothing in the build or the sources reads it | — |

For native tests on a host without `libsystemd-dev`, `--libsystemd <dir>`
points the test build at a vendored libsystemd (`include/` + `lib/`).

## Dependencies

| Dependency | Needed for | Required |
|------------|------------|----------|
| libubootenv | U-Boot environment access | yes |
| libsystemd (sd-bus) | D-Bus client for RAUC | yes — found with pkg-config and linked by the library targets |
| jsoncpp | `.fs` descriptor parsing; `<json/json.h>` is included by `fsupdate.h` | yes |
| Boost.PropertyTree (headers only) | Reading RAUC's `system.conf` | yes |
| botan-2 | Certificate and signature checks of raw application images; SHA-256 of container members | only with `FUS_LEGACY_IMAGE_SUPPORT=ON` |
| zlib | Header CRC of raw application images | only with `FUS_LEGACY_IMAGE_SUPPORT=ON` |

Only libsystemd (and botan-2, when found through pkg-config) is recorded on the
library targets. A consumer links the rest itself — as `fs-updater-cli` does
with `ubootenv`, `jsoncpp`, `z` and botan-2.

## Install

```bash
cmake --install build
```

Installs `libfs_updater.a`, `libfs_updater.so.1`, the public headers under
`include/fs_update_framework/`, and the generated
`include/fus_updater_lib/config.h`, which the headers include. There is no
CMake package config: consumers add the include directory and link
`fs_updater` directly.

## Tests

`tests/` holds a GoogleTest suite (38 test sources) that runs natively, without
a device: `./scripts/build.sh test`. It drives the state machine through an
in-memory U-Boot environment and covers the container reader, format
detection, the RAUC D-Bus reply parsing and the error mapping, among others.

`ctest` registers the test binary, not its cases, so a case that stops being
compiled would vanish silently. `tests/expected-cases` declares the number of
cases the suite must run, and `scripts/check-case-count.sh` compares it with
gtest's own summary. A `--no-legacy` build compiles fewer cases and fails that
check by design.

Behaviour that needs a real U-Boot environment, the RAUC daemon or a reboot
is not covered here; it needs a target device.

## Coding standard

C++17. Rules that apply to this library:

- **No `std::filesystem`, no `<cstdio>`** — use the POSIX wrappers in
  `src/util/posix_utils.h`.
- **No `<iostream>` or `printf`** — log through `logger::LoggerHandler`.
- **No raw `new`/`delete`** — smart pointers and RAII only.
- **`[[nodiscard]]`** on functions whose result must not be dropped.
- **U-Boot writes are batched** — stage with `addVariable()`, write once with
  `flushEnvironment()` (see
  [U-Boot Variables](reference/uboot-variables.md#write-batching)).
- **New exception classes** derive from `fs::BaseFSUpdateException` or an
  existing base; keep `what()` specific enough to identify the call site.
  `fs-updater-cli` maps exception types to exit codes, so renaming or removing
  one can change a caller's exit code — check that repository before you do.

Deliberate deviations from the framework's coding standard are recorded in
[coding-standard-deviations.md](coding-standard-deviations.md).
