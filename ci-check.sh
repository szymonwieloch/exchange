#!/usr/bin/env bash
#
# ci-check.sh — Run the same checks locally that CI performs.
#
# Usage:
#   ./ci-check.sh              # Run all checks
#   ./ci-check.sh format       # Format check only
#   ./ci-check.sh lint         # Lint only (clang-tidy)
#   ./ci-check.sh build        # Build & test only (Release + GCC)
#   ./ci-check.sh sanitizers   # Sanitizers only (ASan + UBSan)
#
# Requires: cmake, clang-format, clang-tidy, gcc/g++ or clang/clang++

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# ── Helpers ───────────────────────────────────────────────────────

print_header() {
    echo -e "${YELLOW}============================================================${NC}"
    echo -e "${YELLOW}  $*${NC}"
    echo -e "${YELLOW}============================================================${NC}"
}

print_pass()  { echo -e "${GREEN}[PASS]${NC} $*"; }
print_fail() { echo -e "${RED}[FAIL]${NC} $*"; }

require_cmd() {
    if ! command -v "$1" &>/dev/null; then
        echo "Error: '$1' is required but not found in PATH." >&2
        exit 1
    fi
}

# Collect all .cpp and .h files (same scope as CI: src/ and tests/)
source_files() {
    find src/ tests/ -name '*.cpp' -o -name '*.h'
}

# ── Format Check ──────────────────────────────────────────────────

run_format_check() {
    print_header "Format Check (clang-format)"
    require_cmd clang-format

    # Prefer clang-format-18 (matching CI container), fall back to clang-format
    local fmt="clang-format"
    if command -v clang-format-18 &>/dev/null; then
        fmt="clang-format-18"
    fi

    local files
    files=$(source_files)
    if [ -z "$files" ]; then
        echo "No source files found."
        return 0
    fi

    if echo "$files" | xargs "$fmt" --dry-run --Werror 2>&1; then
        print_pass "All files are properly formatted."
        return 0
    else
        print_fail "Some files need formatting. Run:"
        echo "  $fmt -i \$(find src/ tests/ -name '*.cpp' -o -name '*.h')"
        return 1
    fi
}

# ── Lint (clang-tidy) ─────────────────────────────────────────────

run_lint() {
    print_header "Lint (clang-tidy)"

    # Prefer clang-tidy-18, fall back to clang-tidy
    local tidy="clang-tidy"
    if command -v clang-tidy-18 &>/dev/null; then
        tidy="clang-tidy-18"
    fi

    require_cmd "$tidy"
    require_cmd cmake

    # Use a separate build directory for lint to avoid generator conflicts
    local lint_build_dir="build/lint"
    if [ ! -f "$lint_build_dir/compile_commands.json" ]; then
        # Prefer Clang for compile_commands.json (best for clang-tidy), fall back to GCC
        local cc="clang"
        local cxx="clang++"
        if ! command -v "$cc" &>/dev/null; then
            if command -v clang-18 &>/dev/null; then
                cc="clang-18"
                cxx="clang++-18"
            else
                cc="gcc"
                cxx="g++"
            fi
        fi
        echo "Generating compile_commands.json (using $cxx)..."
        cmake -B "$lint_build_dir" \
            -DCMAKE_C_COMPILER="$cc" \
            -DCMAKE_CXX_COMPILER="$cxx" \
            -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
            -DCMAKE_BUILD_TYPE=Release
    fi

    local files
    files=$(source_files)
    if [ -z "$files" ]; then
        echo "No source files found."
        return 0
    fi

    if echo "$files" | xargs "$tidy" -p "$lint_build_dir" --warnings-as-errors='*' 2>&1; then
        print_pass "clang-tidy found no issues."
        return 0
    else
        print_fail "clang-tidy reported warnings/errors."
        return 1
    fi
}

# ── Build & Test ──────────────────────────────────────────────────

run_build_and_test() {
    local compiler="${1:-gcc}"
    local build_type="${2:-Release}"

    local cc="gcc"
    local cxx="g++"
    if [ "$compiler" = "clang" ]; then
        cc="clang"
        cxx="clang++"
    fi

    print_header "Build & Test ($compiler, $build_type)"
    require_cmd cmake
    require_cmd "$cc"
    require_cmd "$cxx"

    # Clean build directory for a fresh start
    rm -rf build
    mkdir -p build

    echo "Configuring CMake..."
    cmake -B build \
        -DCMAKE_BUILD_TYPE="$build_type" \
        -DCMAKE_C_COMPILER="$cc" \
        -DCMAKE_CXX_COMPILER="$cxx"

    local nproc
    nproc=$(nproc 2>/dev/null || echo 4)

    echo "Building..."
    cmake --build build --parallel "$nproc"

    echo "Running tests..."
    cd build
    if ctest --output-on-failure --verbose; then
        print_pass "All tests passed."
        cd ..
        return 0
    else
        print_fail "Some tests failed."
        cd ..
        return 1
    fi
}

# ── Sanitizers ────────────────────────────────────────────────────

run_sanitizers() {
    print_header "Sanitizers (ASan + UBSan)"
    require_cmd cmake
    require_cmd clang
    require_cmd clang++

    # Clean build directory
    rm -rf build
    mkdir -p build

    echo "Configuring CMake with sanitizers..."
    cmake -B build \
        -DCMAKE_C_COMPILER=clang \
        -DCMAKE_CXX_COMPILER=clang++ \
        -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
        -DCMAKE_LINKER_FLAGS="-fsanitize=address,undefined" \
        -DCMAKE_BUILD_TYPE=Debug

    local nproc
    nproc=$(nproc 2>/dev/null || echo 4)

    echo "Building..."
    cmake --build build --parallel "$nproc"

    echo "Running tests with sanitizers..."
    cd build
    if ctest --output-on-failure --verbose; then
        print_pass "Sanitizer run clean — no issues detected."
        cd ..
        return 0
    else
        print_fail "Sanitizers detected issues."
        cd ..
        return 1
    fi
}

# ── Full CI Pipeline ──────────────────────────────────────────────

run_all() {
    local failed=0

    run_format_check     || { failed=1; echo; }
    run_lint             || { failed=1; echo; }
    run_build_and_test gcc Release || { failed=1; echo; }
    run_sanitizers       || { failed=1; echo; }

    echo
    if [ "$failed" -eq 0 ]; then
        echo -e "${GREEN}============================================================${NC}"
        echo -e "${GREEN}  All CI checks passed!${NC}"
        echo -e "${GREEN}============================================================${NC}"
    else
        echo -e "${RED}============================================================${NC}"
        echo -e "${RED}  Some CI checks FAILED. See above for details.${NC}"
        echo -e "${RED}============================================================${NC}"
    fi

    return "$failed"
}

# ── Main ──────────────────────────────────────────────────────────

case "${1:-all}" in
    format)     run_format_check ;;
    lint)       run_lint ;;
    build)      run_build_and_test "${2:-gcc}" "${3:-Release}" ;;
    sanitizers) run_sanitizers ;;
    all)        run_all ;;
    *)
        echo "Usage: $0 {format|lint|build [gcc|clang] [Debug|Release]|sanitizers|all}"
        exit 1
        ;;
esac
