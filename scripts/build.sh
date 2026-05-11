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
EOF
    exit 1
}

TARGET=""
EXTRA_ARGS=()

while [ $# -gt 0 ]; do
    case "$1" in
    --speed)   EXTRA_ARGS+=("-DOPTIMIZE_FOR=SPEED") ;;
    --uint64)  EXTRA_ARGS+=("-Dupdate_version_type=uint64") ;;
    --no-dbus) EXTRA_ARGS+=("-DBUILD_DBUS_SUPPORT=OFF") ;;
    --nand)    EXTRA_ARGS+=("-DUBOOT_ENV_NAND=$2"); shift ;;
    --mmc)     EXTRA_ARGS+=("-DUBOOT_ENV_MMC=$2"); shift ;;
    --botan)   EXTRA_ARGS+=("-DBOTAN2=$2"); shift ;;
    debug | release | sanitize | test | clean)
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
    local build_dir="$PROJECT_ROOT/build_test"
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
clean)
    rm -rf "$PROJECT_ROOT/build" "$PROJECT_ROOT/build_test"
    echo "Build directories removed."
    ;;
*)
    usage
    ;;
esac
