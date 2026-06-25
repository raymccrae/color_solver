#!/usr/bin/env bash
set -euo pipefail

tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT
solver="$tmpdir/color_solver"

g++ -std=c++20 -O3 -Wall -Wextra -pedantic -o "$solver" color_solver.cpp
export SOLVER="$solver"

python3 - <<'PY'
import subprocess
import sys
import time
import os

SOLVER = os.environ["SOLVER"]

def puzzle(rows, header="12 4"):
    return header + "\n" + "\n".join(
        f"{len(row)}" + ("" if not row else " " + " ".join(str(x) for x in row))
        for row in rows
    ) + "\n"

def solved_rows():
    return [[color] * 4 for color in range(1, 11)] + [[], []]

TESTS = [
    {
        "name": "already solved",
        "input": puzzle(solved_rows()),
        "status": "SOLVABLE",
        "moves": 0,
    },
    {
        "name": "one move",
        "input": puzzle([[1, 1], [1, 1]] + [[color] * 4 for color in range(2, 11)] + [[]]),
        "status": "SOLVABLE",
        "moves": 1,
    },
    {
        "name": "multi-move",
        "input": puzzle([
            [1, 2, 1, 2],
            [2, 1, 2, 1],
            *([[color] * 4 for color in range(3, 11)]),
            [],
            [],
        ]),
        "status": "SOLVABLE",
    },
    {
        "name": "invalid fixed shape",
        "input": puzzle([[1] * 4 for _ in range(10)] + [[], []], header="10 4"),
        "status": "UNSOLVABLE",
    },
    {
        "name": "invalid input, tube too full",
        "input": puzzle([[1, 1, 1, 1, 1]] + [[] for _ in range(11)]),
        "status": "UNSOLVABLE",
    },
    {
        "name": "invalid color counts",
        "input": puzzle([[1] * 4, [2] * 3] + [[color] * 4 for color in range(3, 11)] + [[], []]),
        "status": "UNSOLVABLE",
    },
    {
        "name": "invalid color cardinality",
        "input": puzzle([[color] * 4 for color in range(1, 10)] + [[], [], []]),
        "status": "UNSOLVABLE",
    },
    {
        "name": "today's Reddit screenshot fixture",
        "input": puzzle([
            [1, 2, 3, 2],
            [4, 5, 2, 6],
            [3, 6, 7, 8],
            [5, 8, 5, 7],
            [9, 6, 10, 4],
            [6, 1, 4, 1],
            [10, 3, 9, 7],
            [8, 9, 5, 2],
            [4, 10, 10, 7],
            [3, 9, 1, 8],
            [],
            [],
        ]),
        "status": "SOLVABLE",
    },
    {
        "name": "duplicate empty tubes keep original indices",
        "input": puzzle([
            [1, 2, 1, 2],
            [2, 1, 2, 1],
            *([[color] * 4 for color in range(3, 11)]),
            [],
            [],
        ]),
        "status": "SOLVABLE",
    },
    {
        "name": "canonical duplicate states preserve legal output",
        "input": puzzle([
            [1, 2],
            [1, 2],
            [2, 1],
            [2, 1],
            *([[color] * 4 for color in range(3, 11)]),
        ]),
        "status": "SOLVABLE",
    },
]

def parse_puzzle(text):
    tokens = text.split()
    if len(tokens) < 2:
        raise AssertionError("missing dimensions")
    it = iter(tokens)
    n = int(next(it))
    c = int(next(it))
    state = []
    for _ in range(n):
        k = int(next(it))
        tube = [int(next(it)) for _ in range(k)]
        state.append(tube)
    return c, state

def is_mono(tube):
    return not tube or all(x == tube[0] for x in tube)

def is_goal(state):
    seen = set()
    for tube in state:
        if not tube:
            continue
        if not is_mono(tube):
            return False
        if tube[0] in seen:
            return False
        seen.add(tube[0])
    return True

def movable_amount(src, dst, cap):
    color = src[-1]
    run = 0
    i = len(src) - 1
    while i >= 0 and src[i] == color:
        run += 1
        i -= 1
    return min(run, cap - len(dst))

def replay(puzzle_input, output):
    lines = [line.strip() for line in output.splitlines() if line.strip()]
    if not lines:
        raise AssertionError("empty solver output")
    status = lines[0]
    if status == "UNSOLVABLE":
        return status, None
    if status != "SOLVABLE":
        raise AssertionError(f"bad status {status!r}")
    if len(lines) < 2:
        raise AssertionError("missing move count")
    try:
        count = int(lines[1])
    except ValueError as exc:
        raise AssertionError("move count is not an integer") from exc
    move_lines = lines[2:]
    if len(move_lines) != count:
        raise AssertionError(f"printed count {count}, but emitted {len(move_lines)} moves")

    cap, state = parse_puzzle(puzzle_input)
    for step, line in enumerate(move_lines):
        parts = line.split()
        if len(parts) != 2:
            raise AssertionError(f"move {step}: expected two indices")
        s, d = (int(x) - 1 for x in parts)
        if s == d:
            raise AssertionError(f"move {step}: source equals destination")
        if not (0 <= s < len(state) and 0 <= d < len(state)):
            raise AssertionError(f"move {step}: index out of range")
        src = state[s]
        dst = state[d]
        if not src:
            raise AssertionError(f"move {step}: empty source")
        if len(dst) >= cap:
            raise AssertionError(f"move {step}: full destination")
        color = src[-1]
        if dst and dst[-1] != color:
            raise AssertionError(f"move {step}: destination bottom color mismatch")
        amount = movable_amount(src, dst, cap)
        if amount <= 0:
            raise AssertionError(f"move {step}: no movable blocks")
        for _ in range(amount):
            dst.append(src.pop())

    if not is_goal(state):
        raise AssertionError("final state is not solved")
    return status, count

for test in TESTS:
    started = time.perf_counter()
    proc = subprocess.run(
        [SOLVER],
        input=test["input"],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=60,
        check=False,
    )
    elapsed = time.perf_counter() - started
    if proc.returncode != 0:
        sys.stderr.write(proc.stderr)
        raise AssertionError(f"{test['name']}: solver exited with {proc.returncode}")

    status, count = replay(test["input"], proc.stdout)
    if status != test["status"]:
        raise AssertionError(f"{test['name']}: expected {test['status']}, got {status}")
    if "moves" in test and count != test["moves"]:
        raise AssertionError(f"{test['name']}: expected {test['moves']} moves, got {count}")
    print(f"PASS {test['name']}: {status}" + (f" {count}" if count is not None else ""))
    if test["name"] == "today's Reddit screenshot fixture":
        print(f"BENCH today's Reddit screenshot fixture: {elapsed:.3f}s")

print("Tests: PASS")
PY
