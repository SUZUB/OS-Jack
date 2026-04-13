#!/usr/bin/env bash
# prepare-rootfs.sh — Download and extract Alpine minirootfs for OS-Jackfruit
# Usage: ./prepare-rootfs.sh
# Run from the repo root (parent of boilerplate/)

set -uo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

ok()   { echo -e "${GREEN}[OK]${NC}    $1"; }
fail() { echo -e "${RED}[FAIL]${NC}  $1"; exit 1; }
info() { echo -e "${YELLOW}[INFO]${NC}  $1"; }

ALPINE_VERSION="3.20.3"
ALPINE_ARCH="x86_64"
ALPINE_BASE_URL="https://dl-cdn.alpinelinux.org/alpine/v3.20/releases/${ALPINE_ARCH}"
TARBALL="alpine-minirootfs-${ALPINE_VERSION}-${ALPINE_ARCH}.tar.gz"
TARBALL_URL="${ALPINE_BASE_URL}/${TARBALL}"
ROOTFS_DIR="rootfs-base"

# Expected SHA256 for alpine-minirootfs-3.20.3-x86_64.tar.gz
# Verify at: https://dl-cdn.alpinelinux.org/alpine/v3.20/releases/x86_64/alpine-minirootfs-3.20.3-x86_64.tar.gz.sha256
EXPECTED_SHA256="a2d4b3c1e5f6789012345678901234567890abcdef1234567890abcdef123456"
# NOTE: Replace the above with the real SHA256 from Alpine's release page before use.
# Run: wget -q "${TARBALL_URL}.sha256" -O - | awk '{print $1}'

echo "================================================"
echo " OS-Jackfruit — Alpine Rootfs Preparation"
echo "================================================"
echo ""

# ── Step 1: Create rootfs-base directory (idempotent) ─────────────────────────
if [ -d "$ROOTFS_DIR" ]; then
    info "Directory '$ROOTFS_DIR' already exists — skipping mkdir"
else
    mkdir -p "$ROOTFS_DIR"
    ok "Created directory: $ROOTFS_DIR"
fi

# ── Step 2: Download tarball (skip if already present) ────────────────────────
if [ -f "$TARBALL" ]; then
    info "Tarball '$TARBALL' already downloaded — skipping wget"
else
    info "Downloading Alpine minirootfs ${ALPINE_VERSION}..."
    info "URL: $TARBALL_URL"
    if wget --progress=bar:force "$TARBALL_URL" -O "$TARBALL"; then
        ok "Downloaded: $TARBALL"
    else
        fail "Download failed: $TARBALL_URL\nCheck network connectivity and verify the URL is current."
    fi
fi

# ── Step 3: SHA256 checksum verification ──────────────────────────────────────
info "Fetching SHA256 checksum from Alpine CDN..."
REMOTE_SHA256=$(wget -q "${TARBALL_URL}.sha256" -O - 2>/dev/null | awk '{print $1}' || echo "")
if [ -n "$REMOTE_SHA256" ]; then
    LOCAL_SHA256=$(sha256sum "$TARBALL" | awk '{print $1}')
    if [ "$LOCAL_SHA256" = "$REMOTE_SHA256" ]; then
        ok "SHA256 checksum verified: $LOCAL_SHA256"
    else
        fail "SHA256 mismatch!\n  Expected: $REMOTE_SHA256\n  Got:      $LOCAL_SHA256\nDelete '$TARBALL' and re-run."
    fi
else
    info "Could not fetch remote checksum — skipping verification (check network)"
fi

# ── Step 4: Extract tarball ───────────────────────────────────────────────────
info "Extracting $TARBALL into $ROOTFS_DIR/..."
if tar -xzf "$TARBALL" -C "$ROOTFS_DIR"; then
    ok "Extraction complete"
else
    fail "Extraction failed — tarball may be corrupt. Delete '$TARBALL' and re-run."
fi

# ── Step 5: Verify rootfs integrity ──────────────────────────────────────────
VERIFY_ERRORS=0
for path in "bin/sh" "etc/alpine-release"; do
    if [ -e "${ROOTFS_DIR}/${path}" ]; then
        ok "Verified: ${ROOTFS_DIR}/${path}"
    else
        echo -e "${RED}[FAIL]${NC}  Missing: ${ROOTFS_DIR}/${path}"
        VERIFY_ERRORS=$((VERIFY_ERRORS + 1))
    fi
done

if [ "$VERIFY_ERRORS" -gt 0 ]; then
    fail "Rootfs verification failed — $VERIFY_ERRORS expected path(s) missing"
fi

# ── Step 6: Set permissions ───────────────────────────────────────────────────
chmod 755 "$ROOTFS_DIR"
ok "Permissions set on $ROOTFS_DIR (755)"

# ── Step 7: Create per-container copies ──────────────────────────────────────
echo ""
info "Creating per-container rootfs copies..."
for name in alpha beta; do
    if [ -d "rootfs-${name}" ]; then
        info "rootfs-${name} already exists — skipping"
    else
        cp -a "./${ROOTFS_DIR}" "./rootfs-${name}"
        ok "Created rootfs-${name}"
    fi
done

# ── Done ──────────────────────────────────────────────────────────────────────
echo ""
echo "================================================"
echo -e "${GREEN} Rootfs preparation complete!${NC}"
echo "================================================"
echo ""
echo "Directories created:"
echo "  ./$ROOTFS_DIR      (base — do not modify directly)"
echo "  ./rootfs-alpha     (container alpha copy)"
echo "  ./rootfs-beta      (container beta copy)"
echo ""
echo "IMPORTANT: Add these to .gitignore — do NOT commit rootfs directories!"
echo "  rootfs-base/"
echo "  rootfs-*/"
echo ""
echo "Next step: cd boilerplate && make"
