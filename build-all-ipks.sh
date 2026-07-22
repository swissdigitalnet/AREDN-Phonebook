#!/bin/bash
# Build AREDN-Phonebook APKs for all supported architectures
# Supports: ath79, ipq40xx, x86-64
#
# Targets OpenWrt 25.12.x, which is the base for AREDN 4.x (4.26.7.0+).
# OpenWrt 25.12 replaced opkg/.ipk with the apk package manager (.apk),
# so side-loaded .ipk packages from the 23.05 era are no longer installable.

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Default settings
VERSION=$(git describe --tags --abbrev=0 2>/dev/null | sed 's/^v//' || echo "2.4.0")
# OpenWrt 25.12.x is the base for AREDN 4.x. Toolchain is gcc 14.3.0 and SDK
# tarballs are zstd-compressed (.tar.zst) rather than xz (.tar.xz).
OPENWRT_VERSION="25.12.4"
GCC_VERSION="14.3.0"
OUTPUT_DIR="$SCRIPT_DIR/build-output"
ARCHITECTURES=()

# SDK URLs
declare -A SDK_URLS=(
    ["ath79"]="https://downloads.openwrt.org/releases/${OPENWRT_VERSION}/targets/ath79/generic/openwrt-sdk-${OPENWRT_VERSION}-ath79-generic_gcc-${GCC_VERSION}_musl.Linux-x86_64.tar.zst"
    ["ipq40xx"]="https://downloads.openwrt.org/releases/${OPENWRT_VERSION}/targets/ipq40xx/generic/openwrt-sdk-${OPENWRT_VERSION}-ipq40xx-generic_gcc-${GCC_VERSION}_musl_eabi.Linux-x86_64.tar.zst"
    ["x86"]="https://downloads.openwrt.org/releases/${OPENWRT_VERSION}/targets/x86/64/openwrt-sdk-${OPENWRT_VERSION}-x86-64_gcc-${GCC_VERSION}_musl.Linux-x86_64.tar.zst"
)

# SDK directory names
declare -A SDK_DIRS=(
    ["ath79"]="ath79-sdk"
    ["ipq40xx"]="ipq40xx-sdk"
    ["x86"]="x86-sdk"
)

# Architecture names for IPK filenames
declare -A ARCH_NAMES=(
    ["ath79"]="mips_24kc"
    ["ipq40xx"]="arm_cortex-a7_neon-vfpv4"
    ["x86"]="x86_64"
)

usage() {
    echo "Usage: $0 [OPTIONS] [ARCHITECTURES...]"
    echo ""
    echo "Build AREDN-Phonebook APKs (OpenWrt 25.12 / AREDN 4.x) for specified architectures"
    echo ""
    echo "Options:"
    echo "  -h, --help              Show this help message"
    echo "  -v, --version VERSION   Override version (default: from git tag or 2.4.0)"
    echo "  -o, --output DIR        Output directory (default: ./build-output)"
    echo "  -c, --clean             Clean build directories before building"
    echo "  -a, --all               Build for all architectures (default)"
    echo ""
    echo "Architectures:"
    echo "  ath79                   Build for ath79 (MikroTik, Ubiquiti, TP-Link)"
    echo "  ipq40xx                 Build for ipq40xx (Qualcomm IPQ40xx devices)"
    echo "  x86                     Build for x86-64 (Virtual machines, PC engines)"
    echo ""
    echo "Examples:"
    echo "  $0                      Build for all architectures"
    echo "  $0 ath79 ipq40xx        Build for ath79 and ipq40xx only"
    echo "  $0 -v 2.5.0 -c          Build all with version 2.5.0, clean first"
    exit 0
}

log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

download_sdk() {
    local arch=$1
    local sdk_url="${SDK_URLS[$arch]}"
    local sdk_dir="${SDK_DIRS[$arch]}"

    # Reuse an existing SDK only if it matches the target OpenWrt version.
    # A leftover 23.05 SDK (opkg/.ipk) must not be reused for a 25.12 (.apk) build.
    if [ -d "$sdk_dir" ]; then
        if grep -q "$OPENWRT_VERSION" "$sdk_dir/include/version.mk" 2>/dev/null; then
            log_info "SDK for $arch already exists in $sdk_dir ($OPENWRT_VERSION)"
            return 0
        fi
        log_warn "SDK in $sdk_dir is not OpenWrt $OPENWRT_VERSION. Removing stale SDK."
        rm -rf "$sdk_dir"
    fi

    log_info "Downloading SDK for $arch..."
    local sdk_tarball="${arch}-sdk.tar.zst"

    if [ ! -f "$sdk_tarball" ]; then
        wget -q --show-progress "$sdk_url" -O "$sdk_tarball" || {
            log_error "Download failed for $arch from $sdk_url"
            rm -f "$sdk_tarball"
            return 1
        }
    fi

    if ! command -v zstd >/dev/null 2>&1; then
        log_error "zstd is required to extract OpenWrt 25.12 SDKs (.tar.zst). Install it: sudo apt-get install -y zstd"
        return 1
    fi

    log_info "Extracting SDK for $arch..."
    if ! tar --zstd -xf "$sdk_tarball"; then
        log_error "Failed to extract $sdk_tarball"
        return 1
    fi
    mv openwrt-sdk-* "$sdk_dir"

    # Sanity check: a real SDK must have rules.mk at its root.
    if [ ! -f "$sdk_dir/rules.mk" ]; then
        log_error "Extraction did not yield a valid SDK (no $sdk_dir/rules.mk)."
        rm -rf "$sdk_dir"
        return 1
    fi

    log_info "SDK for $arch ready"
}

inject_package() {
    local sdk_dir=$1

    log_info "Injecting AREDN-Phonebook package into $sdk_dir..."
    rm -rf "$sdk_dir/package/AREDN-Phonebook"
    mkdir -p "$sdk_dir/package/AREDN-Phonebook"
    cp -r Phonebook/* "$sdk_dir/package/AREDN-Phonebook/"
}

build_package() {
    local arch=$1
    local sdk_dir="${SDK_DIRS[$arch]}"

    log_info "Building AREDN-Phonebook for $arch..."

    cd "$sdk_dir"

    # Configure SDK
    if [ ! -f .config ]; then
        make defconfig > /dev/null 2>&1
    fi

    # Build with version override
    log_info "Compiling for $arch (version: $VERSION)..."
    PKG_VERSION_OVERRIDE="$VERSION" make package/AREDN-Phonebook/compile V=s -j$(nproc) > build.log 2>&1

    # Find the built APK (OpenWrt 25.12 emits .apk instead of .ipk)
    local pkg_file=$(find bin -name "*AREDN-Phonebook*.apk" -type f | head -n 1)

    if [ -z "$pkg_file" ]; then
        log_error "Build failed for $arch! Check $sdk_dir/build.log"
        cd "$SCRIPT_DIR"
        return 1
    fi

    log_info "Build succeeded for $arch: $pkg_file"

    # Copy to output directory
    mkdir -p "$OUTPUT_DIR"
    local output_name="AREDN-Phonebook-${arch}-${VERSION}.apk"
    cp "$pkg_file" "$OUTPUT_DIR/$output_name"

    log_info "APK copied to: $OUTPUT_DIR/$output_name"

    cd "$SCRIPT_DIR"
    return 0
}

clean_sdk() {
    local arch=$1
    local sdk_dir="${SDK_DIRS[$arch]}"

    if [ -d "$sdk_dir" ]; then
        log_info "Cleaning SDK for $arch..."
        cd "$sdk_dir"
        make clean > /dev/null 2>&1 || true
        cd "$SCRIPT_DIR"
    fi
}

# Parse arguments
CLEAN=false
BUILD_ALL=true

while [[ $# -gt 0 ]]; do
    case $1 in
        -h|--help)
            usage
            ;;
        -v|--version)
            VERSION="$2"
            shift 2
            ;;
        -o|--output)
            OUTPUT_DIR="$2"
            shift 2
            ;;
        -c|--clean)
            CLEAN=true
            shift
            ;;
        -a|--all)
            BUILD_ALL=true
            shift
            ;;
        ath79|ipq40xx|x86)
            ARCHITECTURES+=("$1")
            BUILD_ALL=false
            shift
            ;;
        *)
            log_error "Unknown option: $1"
            usage
            ;;
    esac
done

# If no architectures specified, build all
if [ "$BUILD_ALL" = true ] || [ ${#ARCHITECTURES[@]} -eq 0 ]; then
    ARCHITECTURES=("ath79" "ipq40xx" "x86")
fi

log_info "AREDN-Phonebook Multi-Architecture Build"
log_info "=========================================="
log_info "Version: $VERSION"
log_info "Architectures: ${ARCHITECTURES[*]}"
log_info "Output directory: $OUTPUT_DIR"
echo ""

# Clean output directory
mkdir -p "$OUTPUT_DIR"

# Build each architecture
FAILED_BUILDS=()

for arch in "${ARCHITECTURES[@]}"; do
    echo ""
    log_info "========================================"
    log_info "Processing architecture: $arch"
    log_info "========================================"

    # Clean if requested
    if [ "$CLEAN" = true ]; then
        clean_sdk "$arch"
    fi

    # Download SDK if needed
    download_sdk "$arch" || {
        log_error "Failed to download SDK for $arch"
        FAILED_BUILDS+=("$arch")
        continue
    }

    # Inject package
    inject_package "${SDK_DIRS[$arch]}" || {
        log_error "Failed to inject package for $arch"
        FAILED_BUILDS+=("$arch")
        continue
    }

    # Build
    build_package "$arch" || {
        FAILED_BUILDS+=("$arch")
        continue
    }
done

# Summary
echo ""
log_info "=========================================="
log_info "Build Summary"
log_info "=========================================="

if [ ${#FAILED_BUILDS[@]} -eq 0 ]; then
    log_info "All builds completed successfully!"
    log_info ""
    log_info "APK files created in: $OUTPUT_DIR"
    ls -lh "$OUTPUT_DIR"/*.apk 2>/dev/null || true
    exit 0
else
    log_error "Some builds failed: ${FAILED_BUILDS[*]}"
    log_info ""
    log_info "Successful builds in: $OUTPUT_DIR"
    ls -lh "$OUTPUT_DIR"/*.apk 2>/dev/null || true
    exit 1
fi
