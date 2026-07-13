#!/bin/bash
set -e

SDK_ROOT="${SDK_ROOT:-/opt/fslc-xwayland/5.15-scarthgap}"
SDK_ENV="$SDK_ROOT/environment-setup-cortexa53-fslc-linux"
SDK_CMAKE="$SDK_ROOT/sysroots/x86_64-fslcsdk-linux/usr/bin/cmake"
SDK_CTEST="$SDK_ROOT/sysroots/x86_64-fslcsdk-linux/usr/bin/ctest"
PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"

usage() {
    cat <<'EOF'
Usage: build.sh <target> [options]

Targets:
  debug       Cross-compile Debug build (default)
  release     Cross-compile Release build (-Os, LTO)
  sanitize    Cross-compile Debug build with ASan + UBSan
  test        Native build + run unit tests
  fuzz        Native libFuzzer build (requires clang) + a short
              smoke run per target against fuzz/seed_corpus/; for an
              actual fuzzing session run the built binaries directly,
              e.g. build_fuzz/fuzz/fuzz_v2_container -max_total_time=300
              build_fuzz/corpus_v2_container fuzz/seed_corpus/v2_container
              (new inputs land in the first directory, never in
              seed_corpus/)
  clean       Remove all build directories

Options:
  --speed           Optimize for speed (-O2) instead of size (-Os)
  --nand <part>     U-Boot NAND env partition name (default: mtd5)
  --mmc <dev>       U-Boot MMC env device (default: mmcblk2boot0)
  --botan <path>    Manual path to Botan-2 headers
  --uint64          Use uint64 version type instead of string
  --no-dbus         Disable D-Bus RAUC support (BUILD_DBUS_SUPPORT=OFF)
                    On this branch D-Bus is the default; opt out only if you
                    need to build against a sysroot without libsystemd-dev.
  --no-scratch-override
                    Disable runtime rauc_scratch_path override on update_image()
                    (BUILD_RAUC_SCRATCH_OVERRIDE=OFF). Builds the lib as if the
                    override feature was never added; only the compile-time
                    DEFAULT_RAUC_SCRATCH_PATH is used.
  --scratch <path>  Set the compile-time DEFAULT_RAUC_SCRATCH_PATH (FSUP_RAUC_SCRATCH).
                    Useful for boards whose default /rw_fs is read-only and need
                    a writable subtree like /rw_fs/root/.cache/update.fw.
  --sanitize        With 'test': run the native suite under ASan/UBSan
                    (separate build_test_san/ dir; cross builds keep the
                    'sanitize' target).
EOF
    exit 1
}

TARGET=""
EXTRA_ARGS=()

while [ $# -gt 0 ]; do
    case "$1" in
    --speed)   EXTRA_ARGS+=("-DOPTIMIZE_FOR=SPEED") ;;
    --sanitize) EXTRA_ARGS+=("-DENABLE_SANITIZERS=ON"); TEST_SUFFIX="_san" ;;
    --uint64)  EXTRA_ARGS+=("-Dupdate_version_type=uint64") ;;
    --no-dbus) EXTRA_ARGS+=("-DBUILD_DBUS_SUPPORT=OFF") ;;
    --no-scratch-override) EXTRA_ARGS+=("-DBUILD_RAUC_SCRATCH_OVERRIDE=OFF") ;;
    --scratch) EXTRA_ARGS+=("-DFSUP_RAUC_SCRATCH=$2"); shift ;;
    --nand)    EXTRA_ARGS+=("-DUBOOT_ENV_NAND=$2"); shift ;;
    --mmc)     EXTRA_ARGS+=("-DUBOOT_ENV_MMC=$2"); shift ;;
    --botan)   EXTRA_ARGS+=("-DBOTAN2=$2"); shift ;;
    debug | release | sanitize | test | fuzz | clean)
        if [ -n "$TARGET" ]; then
            echo "Multiple targets specified: $TARGET and $1"
            usage
        fi
        TARGET="$1"
        ;;
    *)
        echo "Unknown option: $1"
        usage
        ;;
    esac
    shift
done

TARGET="${TARGET:-debug}"

if [ -n "${TEST_SUFFIX:-}" ] && [ "$TARGET" != "test" ]; then
    echo "--sanitize applies to the 'test' target only (cross builds: use the 'sanitize' target)"
    exit 1
fi

build_cross() {
    local build_dir="$PROJECT_ROOT/build"
    local cmake_args=("$@")

    unset LD_LIBRARY_PATH
    source "$SDK_ENV"

    mkdir -p "$build_dir" && cd "$build_dir"
    "$SDK_CMAKE" "${cmake_args[@]}" "$PROJECT_ROOT"
    make -j"$(nproc)"
}

build_test() {
    local build_dir="$PROJECT_ROOT/build_test${TEST_SUFFIX:-}"
    local cmake_args=("$@")

    # Prefer SDK cmake/ctest; fall back to system cmake/ctest if SDK not present
    local cmake_bin="$SDK_CMAKE"
    local ctest_bin="$SDK_CTEST"
    if [ ! -x "$cmake_bin" ]; then
        cmake_bin="$(command -v cmake 2>/dev/null)" || { echo "cmake not found"; exit 1; }
        ctest_bin="$(command -v ctest 2>/dev/null)" || ctest_bin="$cmake_bin --build . --target test"
    fi

    mkdir -p "$build_dir" && cd "$build_dir"
    "$cmake_bin" \
        -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_C_COMPILER=gcc \
        -DCMAKE_CXX_COMPILER=g++ \
        -DBUILD_TESTING=ON \
        -DBUILD_MAIN_TARGET=OFF \
        "${cmake_args[@]}" \
        "$PROJECT_ROOT"
    make -j"$(nproc)"
    "$ctest_bin" --output-on-failure
}

build_fuzz() {
    local build_dir="$PROJECT_ROOT/build_fuzz"
    local cmake_args=("$@")

    command -v clang++ >/dev/null 2>&1 || { echo "clang++ not found — libFuzzer needs clang (gcc has no -fsanitize=fuzzer)"; exit 1; }

    # Same SDK-first-then-fallback resolution as build_test(): the SDK's
    # native x86_64 cmake works fine here too — cmake itself doesn't care
    # which compiler it's told to drive via -DCMAKE_CXX_COMPILER.
    local cmake_bin="$SDK_CMAKE"
    if [ ! -x "$cmake_bin" ]; then
        cmake_bin="$(command -v cmake 2>/dev/null)" || { echo "cmake not found"; exit 1; }
    fi

    # Neither fuzz target needs D-Bus (see fuzz/CMakeLists.txt); disabling it
    # here avoids a libsystemd-dev dependency for a build whose only real
    # purpose is the two parser fuzz binaries. Same --no-dbus escape hatch
    # build_cross already documents for "a sysroot without libsystemd-dev".
    mkdir -p "$build_dir" && cd "$build_dir"
    "$cmake_bin" \
        -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_C_COMPILER=clang \
        -DCMAKE_CXX_COMPILER=clang++ \
        -DBUILD_TESTING=ON \
        -DBUILD_MAIN_TARGET=OFF \
        -DBUILD_FUZZING=ON \
        -DBUILD_DBUS_SUPPORT=OFF \
        "${cmake_args[@]}" \
        "$PROJECT_ROOT"
    make -j"$(nproc)"

    echo
    echo "=== fuzz smoke run (10s/target against the seed corpus) ==="
    for t in fuzz_app_image_header fuzz_v2_container; do
        echo "--- $t ---"
        # First positional dir is libFuzzer's primary (read-write) corpus —
        # findings get written there, NOT into fuzz/seed_corpus/, which is
        # committed to git and must stay exactly the curated hand-crafted
        # set. seed_corpus/ is passed as a second, read-only seed source.
        local work_corpus="$build_dir/corpus_${t#fuzz_}"
        mkdir -p "$work_corpus"
        "./fuzz/$t" -max_total_time=10 "$work_corpus" "$PROJECT_ROOT/fuzz/seed_corpus/${t#fuzz_}" \
            || { echo "$t: FAILED (see output above)"; exit 1; }
    done
    echo "fuzz smoke run: PASS"
}

case "$TARGET" in
debug)
    build_cross -DCMAKE_BUILD_TYPE=Debug "${EXTRA_ARGS[@]}"
    ;;
release)
    build_cross -DCMAKE_BUILD_TYPE=Release "${EXTRA_ARGS[@]}"
    ;;
sanitize)
    build_cross -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all" \
        "${EXTRA_ARGS[@]}"
    ;;
test)
    build_test "${EXTRA_ARGS[@]}"
    ;;
fuzz)
    build_fuzz "${EXTRA_ARGS[@]}"
    ;;
clean)
    rm -rf "$PROJECT_ROOT/build" "$PROJECT_ROOT/build_test" "$PROJECT_ROOT/build_test_san" "$PROJECT_ROOT/build_fuzz"
    echo "Build directories removed."
    ;;
*)
    usage
    ;;
esac
