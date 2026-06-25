# Reddit Color Puzzle Solver

An optimal C++20 solver for the Reddit-style Color Puzzle / water-sort variant where colors move from the **bottom** of each tube.

The solver reads a puzzle from standard input and prints either an optimal sequence of legal moves or `UNSOLVABLE`.

## Features

- Finds a minimum-length solution using IDA*.
- Uses one-based tube numbers in output, matching the input tube order.
- Optimized for the fixed Reddit screenshot shape: 12 tubes, capacity 4, and 10 colors.
- Moves bottom color runs, not top color runs.
- Verifies invalid or unsolvable inputs as `UNSOLVABLE`.
- Includes a replay-based verification script.

## Build

```bash
g++ -std=c++20 -O3 -Wall -Wextra -pedantic -o color_solver color_solver.cpp
```

## Run

```bash
./color_solver < puzzle.txt
```

Example:

```bash
cat <<'EOF' | ./color_solver
12 4
4 1 2 3 2
4 4 5 2 6
4 3 6 7 8
4 5 8 5 7
4 9 6 10 4
4 6 1 4 1
4 10 3 9 7
4 8 9 5 2
4 4 10 10 7
4 3 9 1 8
0
0
EOF
```

Output:

```text
SOLVABLE
M
s0 d0
s1 d1
...
```

Move indices are **one-based** and refer to the original input tube order.

## Input Format

```text
12 4
k0 color color ...
k1 color color ...
...
k11 color color ...
```

Where:

- The first line must be exactly `12 4`.
- Each tube line starts with `k`, the number of blocks in that tube.
- The following `k` integers are color IDs.
- Tube contents are listed from **top to bottom**.
- The final listed color is the tube bottom and is the movable end.
- There must be exactly 10 distinct colors, each appearing exactly 4 times.

Example:

```text
12 4
4 1 2 3 2
4 4 5 2 6
4 3 6 7 8
4 5 8 5 7
4 9 6 10 4
4 6 1 4 1
4 10 3 9 7
4 8 9 5 2
4 4 10 10 7
4 3 9 1 8
0
0
```

This means tube 1 has top `1`, then `2`, then `3`, and bottom `2`.

## Rules Solved

A move transfers blocks from the bottom of one tube to the bottom of another tube.

A move is legal when:

- The source tube is non-empty.
- The destination tube is not full.
- The destination is empty, or its bottom color matches the source bottom color.

The move transfers the maximal contiguous bottom run of that color, limited by destination capacity.

The goal is reached when every non-empty tube contains exactly one color. Empty tubes are allowed.

## Output Format

For a solvable puzzle:

```text
SOLVABLE
M
s0 d0
s1 d1
...
sM-1 dM-1
```

For an invalid or unsolvable puzzle:

```text
UNSOLVABLE
```

The move count `M` is optimal under the implemented rules.

## Verification

Run:

```bash
./verify.sh
```

The verification script:

- Builds `color_solver.cpp`.
- Runs required regression tests.
- Checks expected solver status.
- Checks known optimal move counts for small tests.
- Replays emitted moves using one-based original input tube indices.
- Confirms every move is legal and the final state is solved.

## Algorithm

The solver uses IDA*:

- Iteratively deepens on `g + h`.
- Uses a transposition table within each iteration.
- Canonicalizes state keys internally so equivalent tube permutations prune duplicate search.
- Keeps the live state in original tube order so output indices remain user-friendly.
- Stores each tube as one packed 16-bit value with four 4-bit color slots.
- Stores a full game state as 12 packed tubes, for a 24-byte state.

The heuristic is admissible:

```text
h = max(non_monochromatic_tubes, split_color_lower_bound)
```

`split_color_lower_bound` counts how many extra tubes each color is split across. One move can reduce that split count for at most one color by one, so the heuristic does not overestimate.

## Notes

- The program has no command-line flags.
- Color IDs may be any non-negative integers.
- The implementation remaps colors internally to values `1..10`; `0` is reserved for empty slots.
- Generated binaries such as `color_solver` are build artifacts and should not be committed.
