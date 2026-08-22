#!/usr/bin/env python3

import subprocess
import select
import sys
import time


EXPECTED = "milestone 1: boot, stack, bss, uart console"
TIMEOUT_SECONDS = 5.0


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: boot_test.py <qemu-system-riscv64> <kernel.elf>", file=sys.stderr)
        return 2

    qemu = sys.argv[1]
    kernel = sys.argv[2]
    cmd = [
        qemu,
        "-machine",
        "virt",
        "-m",
        "128M",
        "-smp",
        "1",
        "-nographic",
        "-bios",
        "none",
        "-kernel",
        kernel,
    ]

    proc = subprocess.Popen(
        cmd,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1,
    )

    output = []
    deadline = time.monotonic() + TIMEOUT_SECONDS

    try:
        while time.monotonic() < deadline:
            if proc.stdout is not None:
                readable, _, _ = select.select([proc.stdout], [], [], 0.1)
                if readable:
                    line = proc.stdout.readline()
                    if line:
                        output.append(line)
                        if EXPECTED in line:
                            print("boot test: observed milestone banner")
                            return 0

            if proc.poll() is not None:
                break
    finally:
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=1)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait(timeout=1)

    print("boot test: did not observe milestone banner", file=sys.stderr)
    print("".join(output), file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
