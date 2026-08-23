#!/usr/bin/env python3

import select
import subprocess
import sys
import time


EXPECTED_SEQUENCE = [
    "thread: A0",
    "thread: B0",
    "thread: C0",
    "thread: A1",
    "thread: B1",
    "thread: C1",
    "thread: A2",
    "thread: B2",
    "thread: C2",
    "milestone 4: bounded ready queue scheduler core",
    "thread: null idle",
]
TIMEOUT_SECONDS = 5.0


def is_relevant_line(line: str) -> bool:
    return (
        line.startswith("thread: A")
        or line.startswith("thread: B")
        or line.startswith("thread: C")
        or line.startswith("milestone 4:")
        or line.startswith("thread: null idle")
    )


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: qemu_smoke_test.py <qemu-system-riscv64> <kernel.elf>", file=sys.stderr)
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
    observed_sequence = []
    expected_index = 0
    deadline = time.monotonic() + TIMEOUT_SECONDS

    try:
        while time.monotonic() < deadline:
            if proc.stdout is not None:
                readable, _, _ = select.select([proc.stdout], [], [], 0.1)
                if readable:
                    line = proc.stdout.readline()
                    if line:
                        output.append(line)
                        stripped_line = line.strip()
                        if is_relevant_line(stripped_line):
                            observed_sequence.append(stripped_line)
                            if stripped_line != EXPECTED_SEQUENCE[expected_index]:
                                print(
                                    "qemu smoke test: scheduler sequence mismatch",
                                    file=sys.stderr,
                                )
                                print(
                                    f"expected: {EXPECTED_SEQUENCE[expected_index]}",
                                    file=sys.stderr,
                                )
                                print(f"observed: {stripped_line}", file=sys.stderr)
                                print("".join(output), file=sys.stderr)
                                return 1
                            expected_index += 1
                        if expected_index == len(EXPECTED_SEQUENCE):
                            print("qemu smoke test: observed FIFO ready-queue round-robin sequence")
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

    print("qemu smoke test: did not observe FIFO ready-queue round-robin sequence", file=sys.stderr)
    print(
        f"next expected: {EXPECTED_SEQUENCE[expected_index]}",
        file=sys.stderr,
    )
    print(f"observed sequence: {observed_sequence}", file=sys.stderr)
    print("".join(output), file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
