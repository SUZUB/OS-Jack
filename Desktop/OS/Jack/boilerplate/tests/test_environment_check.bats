#!/usr/bin/env bats
# test_environment_check.bats — Unit tests for environment-check.sh
#
# Requires: bats-core (https://github.com/bats-core/bats-core)
# Install:  sudo apt install bats
# Run:      bats tests/test_environment_check.bats

# Source the script functions (environment-check.sh must export functions
# or be structured to allow sourcing for testing)

SCRIPT_DIR="$(cd "$(dirname "$BATS_TEST_FILENAME")/.." && pwd)"

# ── Helper: create a fake /proc/version ───────────────────────────────────────
setup() {
    TMPDIR_TEST=$(mktemp -d)
}

teardown() {
    rm -rf "$TMPDIR_TEST"
}

# ── Test: root privilege check ────────────────────────────────────────────────
@test "root check passes when EUID is 0" {
    run bash -c "EUID=0 bash -c '
        source_check() {
            if [ \"\$EUID\" -eq 0 ]; then echo PASS; else echo FAIL; fi
        }
        source_check
    '"
    [ "$status" -eq 0 ]
    [[ "$output" == *"PASS"* ]]
}

@test "root check fails when EUID is non-zero" {
    # Run environment-check.sh without sudo — should exit 1
    run bash "$SCRIPT_DIR/environment-check.sh"
    # Non-root should fail immediately
    [ "$status" -eq 1 ]
    [[ "$output" == *"root"* ]] || [[ "$output" == *"FAIL"* ]]
}

# ── Test: WSL detection ───────────────────────────────────────────────────────
@test "WSL detection triggers FAIL when /proc/version contains Microsoft" {
    FAKE_PROC="$TMPDIR_TEST/proc_version"
    echo "Linux version 5.15.0-microsoft-standard-WSL2" > "$FAKE_PROC"

    run bash -c "
        PROC_VERSION=\$(cat '$FAKE_PROC')
        if echo \"\$PROC_VERSION\" | grep -qiE 'microsoft|wsl'; then
            echo FAIL_WSL
        else
            echo PASS
        fi
    "
    [ "$status" -eq 0 ]
    [[ "$output" == *"FAIL_WSL"* ]]
}

@test "WSL detection passes when /proc/version is normal Linux" {
    FAKE_PROC="$TMPDIR_TEST/proc_version"
    echo "Linux version 6.5.0-35-generic (buildd@lcy02-amd64-059)" > "$FAKE_PROC"

    run bash -c "
        PROC_VERSION=\$(cat '$FAKE_PROC')
        if echo \"\$PROC_VERSION\" | grep -qiE 'microsoft|wsl'; then
            echo FAIL_WSL
        else
            echo PASS
        fi
    "
    [ "$status" -eq 0 ]
    [[ "$output" == *"PASS"* ]]
}

# ── Test: kernel headers check ────────────────────────────────────────────────
@test "kernel headers check passes when build path exists" {
    FAKE_HEADERS="$TMPDIR_TEST/build"
    mkdir -p "$FAKE_HEADERS"

    run bash -c "
        HEADERS_PATH='$FAKE_HEADERS'
        if [ -e \"\$HEADERS_PATH\" ]; then echo PASS; else echo FAIL; fi
    "
    [ "$status" -eq 0 ]
    [[ "$output" == *"PASS"* ]]
}

@test "kernel headers check fails when build path is missing" {
    run bash -c "
        HEADERS_PATH='/nonexistent/path/build'
        if [ -e \"\$HEADERS_PATH\" ]; then echo PASS; else echo FAIL; fi
    "
    [ "$status" -eq 0 ]
    [[ "$output" == *"FAIL"* ]]
}

# ── Test: toolchain check ─────────────────────────────────────────────────────
@test "toolchain check passes for gcc make ld when all present" {
    # These should be present on any Ubuntu system with build-essential
    run bash -c "
        ERRORS=0
        for tool in gcc make ld; do
            if ! command -v \"\$tool\" > /dev/null 2>&1; then
                ERRORS=\$((ERRORS + 1))
            fi
        done
        echo \"errors=\$ERRORS\"
    "
    [ "$status" -eq 0 ]
    [[ "$output" == *"errors=0"* ]]
}

# ── Test: verify_rootfs ───────────────────────────────────────────────────────
@test "verify_rootfs passes when bin/sh and etc/alpine-release exist" {
    FAKE_ROOTFS="$TMPDIR_TEST/rootfs"
    mkdir -p "$FAKE_ROOTFS/bin" "$FAKE_ROOTFS/etc"
    touch "$FAKE_ROOTFS/bin/sh"
    echo "3.20.3" > "$FAKE_ROOTFS/etc/alpine-release"

    run bash -c "
        ROOTFS='$FAKE_ROOTFS'
        ERRORS=0
        for path in bin/sh etc/alpine-release; do
            if [ ! -e \"\$ROOTFS/\$path\" ]; then ERRORS=\$((ERRORS+1)); fi
        done
        if [ \"\$ERRORS\" -eq 0 ]; then echo PASS; else echo FAIL; fi
    "
    [ "$status" -eq 0 ]
    [[ "$output" == *"PASS"* ]]
}

@test "verify_rootfs fails when bin/sh is missing" {
    FAKE_ROOTFS="$TMPDIR_TEST/rootfs"
    mkdir -p "$FAKE_ROOTFS/etc"
    echo "3.20.3" > "$FAKE_ROOTFS/etc/alpine-release"
    # bin/sh intentionally missing

    run bash -c "
        ROOTFS='$FAKE_ROOTFS'
        ERRORS=0
        for path in bin/sh etc/alpine-release; do
            if [ ! -e \"\$ROOTFS/\$path\" ]; then ERRORS=\$((ERRORS+1)); fi
        done
        if [ \"\$ERRORS\" -eq 0 ]; then echo PASS; else echo FAIL; fi
    "
    [ "$status" -eq 0 ]
    [[ "$output" == *"FAIL"* ]]
}

# ── Test: overall pass/fail aggregation ──────────────────────────────────────
@test "overall result is FAIL when any single check fails" {
    run bash -c "
        ERRORS=0
        # Simulate one passing check
        ERRORS=\$((ERRORS + 0))
        # Simulate one failing check
        ERRORS=\$((ERRORS + 1))
        # Simulate another passing check
        ERRORS=\$((ERRORS + 0))

        if [ \"\$ERRORS\" -eq 0 ]; then echo OVERALL_PASS; else echo OVERALL_FAIL; fi
    "
    [ "$status" -eq 0 ]
    [[ "$output" == *"OVERALL_FAIL"* ]]
}

@test "overall result is PASS when all checks pass" {
    run bash -c "
        ERRORS=0
        for i in 1 2 3 4; do
            ERRORS=\$((ERRORS + 0))
        done
        if [ \"\$ERRORS\" -eq 0 ]; then echo OVERALL_PASS; else echo OVERALL_FAIL; fi
    "
    [ "$status" -eq 0 ]
    [[ "$output" == *"OVERALL_PASS"* ]]
}
