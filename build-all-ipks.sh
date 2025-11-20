#!/bin/bash
# Build AREDN-Phonebook IPKs for all supported architectures
# Supports: ath79, ipq40xx, x86-64

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
OPENWRT_VERSION="23.05.3"
OUTPUT_DIR="$SCRIPT_DIR/build-output"
ARCHITECTURES=()

# SDK URLs
declare -A SDK_URLS=(
    ["ath79"]="https://downloads.openwrt.org/releases/${OPENWRT_VERSION}/targets/ath79/generic/openwrt-sdk-${OPENWRT_VERSION}-ath79-generic_gcc-12.3.0_musl.Linux-x86_64.tar.xz"
    ["ipq40xx"]="https://downloads.openwrt.org/releases/${OPENWRT_VERSION}/targets/ipq40xx/generic/openwrt-sdk-${OPENWRT_VERSION}-ipq40xx-generic_gcc-12.3.0_musl_eabi.Linux-x86_64.tar.xz"
    ["x86"]="https://downloads.openwrt.org/releases/${OPENWRT_VERSION}/targets/x86/64/openwrt-sdk-${OPENWRT_VERSION}-x86-64_gcc-12.3.0_musl.Linux-x86_64.tar.xz"
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
    echo "Build AREDN-Phonebook IPKs for specified architectures"
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

    if [ -d "$sdk_dir" ]; then
        log_info "SDK for $arch already exists in $sdk_dir"
        return 0
    fi

    log_info "Downloading SDK for $arch..."
    local sdk_tarball="${arch}-sdk.tar.xz"

    if [ ! -f "$sdk_tarball" ]; then
        wget -q --show-progress "$sdk_url" -O "$sdk_tarball"
    fi

    log_info "Extracting SDK for $arch..."
    tar -xf "$sdk_tarball"
    mv openwrt-sdk-* "$sdk_dir"

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

    # Find the built IPK
    local ipk_file=$(find bin -name "*AREDN-Phonebook*.ipk" -type f | head -n 1)

    if [ -z "$ipk_file" ]; then
        log_error "Build failed for $arch! Check $sdk_dir/build.log"
        cd "$SCRIPT_DIR"
        return 1
    fi

    log_info "Build succeeded for $arch: $ipk_file"

    # Copy to output directory
    mkdir -p "$OUTPUT_DIR"
    local output_name="AREDN-Phonebook-${arch}-${VERSION}.ipk"
    cp "$ipk_file" "$OUTPUT_DIR/$output_name"

    log_info "IPK copied to: $OUTPUT_DIR/$output_name"

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
    log_info "IPK files created in: $OUTPUT_DIR"
    ls -lh "$OUTPUT_DIR"/*.ipk 2>/dev/null || true
    exit 0
else
    log_error "Some builds failed: ${FAILED_BUILDS[*]}"
    log_info ""
    log_info "Successful builds in: $OUTPUT_DIR"
    ls -lh "$OUTPUT_DIR"/*.ipk 2>/dev/null || true
    exit 1
fi
