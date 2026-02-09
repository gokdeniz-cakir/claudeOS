#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
OS_BIN="${BUILD_DIR}/os.bin"
FAT32_IMG="${BUILD_DIR}/fat32.img"
SERIAL_LOG="${BUILD_DIR}/serial_task34_demo.log"
MONITOR_LOG="${BUILD_DIR}/monitor_task34_demo.log"

if [[ ! -f "${OS_BIN}" ]]; then
    echo "error: ${OS_BIN} not found; run 'make' first." >&2
    exit 1
fi

if [[ ! -f "${FAT32_IMG}" ]]; then
    echo "error: ${FAT32_IMG} not found; run 'make' first." >&2
    exit 1
fi

emit_console_command() {
    local command="$1"
    local i
    local ch

    for ((i = 0; i < ${#command}; i++)); do
        ch="${command:i:1}"
        printf 'sendkey %s\n' "${ch}"
    done
    printf 'sendkey ret\n'
}

run_qemu_demo() {
    {
        sleep 3
        emit_console_command "help"
        sleep 1
        emit_console_command "libctest"
        sleep 2
        emit_console_command "shell"
        sleep 3
        emit_console_command "uhello"
        sleep 1
        emit_console_command "ucat"
        sleep 1
        emit_console_command "uexec"
        sleep 1
        emit_console_command "appsdemo"
        sleep 4
        echo quit
    } | qemu-system-i386 \
        -drive format=raw,file="${OS_BIN}" \
        -drive format=raw,file="${FAT32_IMG}",if=ide,index=1 \
        -display none \
        -no-reboot \
        -no-shutdown \
        -serial "file:${SERIAL_LOG}" \
        -monitor stdio >"${MONITOR_LOG}" 2>&1
}

assert_marker() {
    local marker="$1"
    if ! grep -Fq "${marker}" "${SERIAL_LOG}"; then
        echo "error: missing marker: ${marker}" >&2
        return 1
    fi
}

echo "[task34-demo] running headless QEMU scenario..."
run_qemu_demo

echo "[task34-demo] verifying serial markers..."
assert_marker "[CONSOLE] help shown"
assert_marker "[ELF] spawned libc test process"
assert_marker "[LIBC] user C program started"
assert_marker "[SHELL] ClaudeOS userspace shell started"
assert_marker "[CONSOLE] uhello"
assert_marker "[APP] uhello: hello from standalone userspace program"
assert_marker "[CONSOLE] ucat"
assert_marker "[APP] ucat: reading /fat/HELLO.TXT"
assert_marker "Hello from ClaudeOS FAT32 via ATA PIO."
assert_marker "[CONSOLE] uexec"
assert_marker "[APP] uexec: exec /uhello.elf"
assert_marker "[CONSOLE] appsdemo"
assert_marker "[ELF] standalone apps demo started"

echo "[task34-demo] PASS"
echo "[task34-demo] serial log: ${SERIAL_LOG}"
