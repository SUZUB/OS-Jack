#!/usr/bin/env bash
# environment-check.sh — OS-Jackfruit preflight environment validator
# Usage: sudo ./environment-check.sh
#
# Checks:
#   1. Running as root
#   2. Not WSL
#   3. Kernel headers installed and matching uname -r
#   4. Toolchain: gcc, make, ld
#   5. Secure Boot state (if mokutil available)
#
# Exits 0 only if ALL checks pass.

set -uo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

PASS=0
FAIL=1
ERRORS=0

ok()   { echo -e "${GREEN}[OK]${NC}    $1"; }
fail() { echo -e "${RED}[FAIL]${NC}  $1"; ERRORS=$((ERRORS + 1)); }
warn() { echo -e "${YELLOW}[WARN]${NC}  $1"; }
info() { echo -e "        $1"; }

echo "================================================"
echo " OS-Jackfruit — Environment Preflight Check"
echo "================================================"
echo ""

# ── Check 1: Root privileges ──────────────────────────────────────────────────
if [ "$EUID" -eq 0 ]; then
    ok "Running as root"
else
    fail "Must run as root"
    info "Fix: sudo ./environment-check.sh"
    # Cannot continue reliably without root — exit immediately
    echo ""
    echo "Re-run with sudo and try again."
    exit 1
fi

# ── Check 2: Not WSL ──────────────────────────────────────────────────────────
PROC_VERSION=$(cat /proc/version 2>/dev/null || echo "")
if echo "$PROC_VERSION" | grep -qiE "microsoft|wsl"; then
    fail "WSL detected — kernel modules cannot be loaded in WSL"
    info "Fix: Use VirtualBox, VMware, or KVM with Ubuntu 22.04/24.04"
    info "     WSL does not support out-of-tree kernel module loading."
else
    ok "Not WSL (real Linux kernel)"
fi

# ── Check 3: Kernel headers ───────────────────────────────────────────────────
KERNEL_VER=$(uname -r)
HEADERS_PATH="/lib/modules/${KERNEL_VER}/build"
if [ -e "$HEADERS_PATH" ]; then
    ok "Kernel headers found: $HEADERS_PATH"
else
    fail "Kernel headers missing for running kernel: $KERNEL_VER"
    info "Fix: sudo apt install linux-headers-${KERNEL_VER}"
fi

# ── Check 4: Toolchain ────────────────────────────────────────────────────────
for tool in gcc make ld; do
    if command -v "$tool" > /dev/null 2>&1; then
        ok "$tool found ($(command -v $tool))"
    else
        fail "$tool not found on \$PATH"
        info "Fix: sudo apt install build-essential"
    fi
done

# ── Check 5: Secure Boot (optional — requires mokutil) ────────────────────────
if command -v mokutil > /dev/null 2>&1; then
    SB_STATE=$(mokutil --sb-state 2>/dev/null || echo "unknown")
    if echo "$SB_STATE" | grep -qi "enabled"; then
        fail "Secure Boot is ENABLED — unsigned kernel modules will be rejected"
        info "Fix: Disable Secure Boot in your VM BIOS/UEFI settings and reboot"
    else
        ok "Secure Boot is disabled ($SB_STATE)"
    fi
else
    warn "mokutil not installed — cannot check Secure Boot state"
    info "If insmod fails with 'Operation not permitted', disable Secure Boot in VM BIOS"
    info "Install mokutil: sudo apt install mokutil"
fi

# ── Summary ───────────────────────────────────────────────────────────────────
echo ""
echo "================================================"
if [ "$ERRORS" -eq 0 ]; then
    echo -e "${GREEN} All checks passed — environment is ready!${NC}"
    echo "================================================"
    echo ""
    echo "Next steps:"
    echo "  1. Prepare Alpine rootfs:  mkdir rootfs-base && wget <alpine-url> && tar -xzf ..."
    echo "  2. Build boilerplate:      cd boilerplate && make"
    exit 0
else
    echo -e "${RED} $ERRORS check(s) FAILED — fix the issues above before continuing${NC}"
    echo "================================================"
    exit 1
fi
