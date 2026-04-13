#!/usr/bin/env bash
# install-deps.sh — Install all dependencies required for OS-Jackfruit
# Must be run as root: sudo ./install-deps.sh

set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

ok()   { echo -e "${GREEN}[OK]${NC}    $1"; }
fail() { echo -e "${RED}[FAIL]${NC}  $1"; }
info() { echo -e "${YELLOW}[INFO]${NC}  $1"; }

# ── Root check ────────────────────────────────────────────────────────────────
if [ "$EUID" -ne 0 ]; then
    fail "This script must be run as root. Use: sudo ./install-deps.sh"
    exit 1
fi

KERNEL_VER=$(uname -r)
info "Running kernel: $KERNEL_VER"

# ── apt update ────────────────────────────────────────────────────────────────
info "Updating package lists..."
apt-get update -qq
ok "Package lists updated"

# ── Install packages ──────────────────────────────────────────────────────────
info "Installing build-essential..."
apt-get install -y build-essential
ok "build-essential installed"

info "Installing linux-headers-${KERNEL_VER}..."
apt-get install -y "linux-headers-${KERNEL_VER}"
ok "linux-headers-${KERNEL_VER} installed"

# ── Post-install verification ─────────────────────────────────────────────────
ERRORS=0

for tool in gcc make ld; do
    if command -v "$tool" > /dev/null 2>&1; then
        ok "$tool found at $(command -v $tool)"
    else
        fail "$tool not found on \$PATH after installation"
        ERRORS=$((ERRORS + 1))
    fi
done

HEADERS_PATH="/lib/modules/${KERNEL_VER}/build"
if [ -e "$HEADERS_PATH" ]; then
    ok "Kernel headers path exists: $HEADERS_PATH"
else
    fail "Kernel headers path missing: $HEADERS_PATH"
    fail "Try: sudo apt install --reinstall linux-headers-${KERNEL_VER}"
    ERRORS=$((ERRORS + 1))
fi

# ── Result ────────────────────────────────────────────────────────────────────
echo ""
if [ "$ERRORS" -eq 0 ]; then
    ok "All dependencies installed and verified successfully."
    exit 0
else
    fail "$ERRORS verification check(s) failed. Fix the issues above and re-run."
    exit 1
fi
