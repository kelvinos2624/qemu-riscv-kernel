#!/usr/bin/env python3

import select
import subprocess
import sys
import time


EXPECTED_SEQUENCE = [
    "milestone 11: physical page allocator",
    "thread: mutex-a locking",
    "thread: mutex-a acquired",
    "thread: mutex-b timed wait",
    "thread: null idle",
    "thread: mutex-b timed out",
    "thread: mutex-a unlocking",
    "thread: mutex-c locking",
    "thread: mutex-c acquired",
    "milestone 10: scheduler tracing",
    "trace: begin",
    "trace: end",
]
REQUIRED_TRACE_TYPES = [
    "context_switch",
    "wait_timeout",
    "mutex_timeout",
    "idle",
]
TIMEOUT_SECONDS = 10.0


def is_relevant_line(line: str) -> bool:
    return (
        line.startswith("thread: mutex-")
        or line.startswith("milestone 11:")
        or line.startswith("milestone 10:")
        or line.startswith("trace:")
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
    observed_trace_types = set()
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
                            if stripped_line.startswith("trace: seq="):
                                marker = " type="
                                type_start = stripped_line.find(marker)
                                if type_start >= 0:
                                    type_start += len(marker)
                                    type_end = stripped_line.find(" ", type_start)
                                    observed_trace_types.add(stripped_line[type_start:type_end])
                                continue

                            if stripped_line.startswith("trace: begin"):
                                sequence_line = "trace: begin"
                            else:
                                sequence_line = stripped_line

                            observed_sequence.append(stripped_line)
                            if sequence_line != EXPECTED_SEQUENCE[expected_index]:
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
                            missing_trace_types = [
                                trace_type for trace_type in REQUIRED_TRACE_TYPES
                                if trace_type not in observed_trace_types
                            ]
                            if missing_trace_types:
                                print(
                                    "qemu smoke test: missing trace types",
                                    file=sys.stderr,
                                )
                                print(
                                    f"missing: {missing_trace_types}",
                                    file=sys.stderr,
                                )
                                print("".join(output), file=sys.stderr)
                                return 1

                            print(
                                "qemu smoke test: observed physical page allocator "
                                "and scheduler trace sequence"
                            )
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

    print("qemu smoke test: did not observe scheduler trace sequence", file=sys.stderr)
    print(
        f"next expected: {EXPECTED_SEQUENCE[expected_index]}",
        file=sys.stderr,
    )
    print(f"observed sequence: {observed_sequence}", file=sys.stderr)
    print(f"observed trace types: {sorted(observed_trace_types)}", file=sys.stderr)
    print("".join(output), file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
