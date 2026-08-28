#!/usr/bin/env python3

import select
import subprocess
import sys
import time


SCENARIOS = {
    "allocator": {
        "expected_sequence": [
            "scenario: allocator",
            "milestone 11: physical page allocator",
        ],
        "required_trace_types": [],
        "success": "physical page allocator scenario",
    },
    "heap": {
        "expected_sequence": [
            "scenario: heap",
            "milestone 12: kernel heap",
        ],
        "required_trace_types": [],
        "success": "kernel heap scenario",
    },
    "vm": {
        "expected_sequence": [
            "scenario: vm",
            "milestone 13: sv39 page table primitives",
        ],
        "required_trace_types": [],
        "success": "sv39 page table primitives scenario",
    },
    "scheduler-sync": {
        "expected_sequence": [
            "scenario: scheduler-sync",
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
        ],
        "required_trace_types": [
            "context_switch",
            "wait_timeout",
            "mutex_timeout",
            "idle",
        ],
        "success": "scheduler synchronization scenario",
    },
}
TIMEOUT_SECONDS = 15.0


def is_relevant_line(line: str) -> bool:
    return (
        line.startswith("scenario:")
        or line.startswith("thread: mutex-")
        or line.startswith("milestone 11:")
        or line.startswith("milestone 12:")
        or line.startswith("milestone 13:")
        or line.startswith("milestone 10:")
        or line.startswith("trace:")
        or line.startswith("thread: null idle")
    )


def main() -> int:
    if len(sys.argv) != 4:
        print(
            "usage: qemu_smoke_test.py <qemu-system-riscv64> <kernel.elf> <scenario>",
            file=sys.stderr,
        )
        print(f"scenarios: {', '.join(sorted(SCENARIOS))}", file=sys.stderr)
        return 2

    qemu = sys.argv[1]
    kernel = sys.argv[2]
    scenario = sys.argv[3]
    scenario_config = SCENARIOS.get(scenario)
    if scenario_config is None:
        print(f"unknown scenario: {scenario}", file=sys.stderr)
        print(f"scenarios: {', '.join(sorted(SCENARIOS))}", file=sys.stderr)
        return 2

    expected_sequence = scenario_config["expected_sequence"]
    required_trace_types = scenario_config["required_trace_types"]
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
                            if sequence_line != expected_sequence[expected_index]:
                                print(
                                    "qemu smoke test: scenario sequence mismatch",
                                    file=sys.stderr,
                                )
                                print(
                                    f"scenario: {scenario}",
                                    file=sys.stderr,
                                )
                                print(
                                    f"expected: {expected_sequence[expected_index]}",
                                    file=sys.stderr,
                                )
                                print(f"observed: {stripped_line}", file=sys.stderr)
                                print("".join(output), file=sys.stderr)
                                return 1
                            expected_index += 1
                        if expected_index == len(expected_sequence):
                            missing_trace_types = [
                                trace_type for trace_type in required_trace_types
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
                                f"qemu smoke test: observed {scenario_config['success']}"
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

    print("qemu smoke test: did not observe scenario sequence", file=sys.stderr)
    print(f"scenario: {scenario}", file=sys.stderr)
    print(
        f"next expected: {expected_sequence[expected_index]}",
        file=sys.stderr,
    )
    print(f"observed sequence: {observed_sequence}", file=sys.stderr)
    print(f"observed trace types: {sorted(observed_trace_types)}", file=sys.stderr)
    print("".join(output), file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
