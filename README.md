# fweisz

A small, fast C++23 library for the weighted 1-median problem in the plane,
solved by Weiszfeld's fixed-point iteration. Ships with a CLI driver
(`fweisz`), a `Parser` for the input format, and a SIMD lane abstraction that
lights up AVX2 on x86_64, NEON on AArch64, and falls back to a scalar
implementation everywhere else.

Given demand points $B_j = (x_j, y_j)$ with positive weights $b_j$, fweisz
finds a point $S^* \in \mathbb{R}^2$ minimizing

$$
K(S) \;=\; \sum_{j} b_j \, \lVert S - B_j \rVert_2 .
$$

The classical Weiszfeld update is iterated:

$$
S^{(r+1)} \;=\;
  \frac{\sum_j (b_j / d_j^{(r)}) \, B_j}
       {\sum_j  b_j / d_j^{(r)}},
\qquad d_j^{(r)} = \lVert S^{(r)} - B_j \rVert_2,
$$

starting from the weighted center of gravity (itself the squared-distance
optimum and a good warm start).

## Highlights

- **One inner loop, three back-ends.** The Weiszfeld step is written once
  against a thin `SimdLane` concept; the compiler picks `Avx2Lane` (4×
  `double`), `NeonLane` (2× `double`), or `ScalarLane` (1× `double`) at
  build time.
- **Zero-cost abstractions.** Every lane method is force-inlined and wraps a
  single intrinsic vector member, so the abstraction compiles down to the
  same code a hand-written intrinsic loop would produce.
- **C++23 throughout.** `std::span` for non-owning inputs, `std::expected`
  for the parser, `std::format` for diagnostics, concepts for the lane
  contract.
- **No exceptions, no allocations in the hot path.** Inputs are passed as
  `std::span<const double>`; the solver is stateless beyond those views.
- **Cross-platform CI.** Builds and tests on Linux (x86_64 + ARM64), macOS
  (Intel + Apple Silicon), and Windows MSVC on every push.

## Repository layout

```
fweisz/
├── README.md                ← this file
├── MODULE.bazel             ← Bzlmod module definition
├── .bazelrc                 ← C++23 + -O3 + -march=native + LTO + -fno-exceptions
├── .github/
│   └── workflows/
│       └── build_test.yml   ← multi-platform Build & Test workflow
├── examples/
│   └── eiselt.txt           ← Eiselt & Sandblom (2004) pathological input
└── fweisz/
    ├── algorithm.h          ← Solver, Result, Config, Solve()
    ├── algorithm_test.cc    ← GoogleTest unit tests for the solver
    ├── lanes.h              ← SimdLane concept + ScalarLane / Avx2Lane / NeonLane
    ├── lanes_test.cc        ← GoogleTest per-lane + differential tests
    ├── parser.h             ← Parser class for the `x y b` text format
    ├── parser.cc            ← Parser implementation
    ├── parser_test.cc       ← GoogleTest unit tests for the parser
    ├── console.cc           ← CLI driver
    └── base/
        ├── platform.h       ← Compiler-attribute shims (force-inline, restrict, ...)
        └── simd.h           ← ISA detection (AVX2 / NEON) + intrinsic-header gating
```

## Build

Requires [Bazel](https://bazel.build/) (Bzlmod) and a C++23 toolchain
(GCC ≥ 13, Clang ≥ 17, Apple Clang ≥ 16, or MSVC ≥ 19.37 — all needed for
`std::expected` and `std::format`).

```sh
# Build the CLI driver.
bazel build //fweisz:console

# Build just the libraries (sanity check).
bazel build //fweisz:algorithm //fweisz:lanes //fweisz:parser

# Run the test suite (GoogleTest).
bazel test //fweisz:parser_test //fweisz:algorithm_test //fweisz:lanes_test
```

The resulting binary lives at `bazel-bin/fweisz/console`.

## Input format

The CLI and the `Parser` both consume the same plain-text format:

- **One demand point per line**, written as three whitespace-separated
  numbers `x y b`.
  - `x`, `y` — the point's coordinates (any finite `double`; sign and
    magnitude unrestricted).
  - `b` — the point's weight, **strictly positive** (`b > 0`). A zero or
    negative weight is rejected with a `NonPositiveWeight` error.
- **Fields may be separated by spaces or tabs**, in any combination. Leading
  whitespace on a data line is tolerated.
- **Blank lines are ignored** (including lines that contain only spaces or
  tabs).
- **Comment lines are ignored.** A line is a comment if its first
  non-whitespace character is `#` — useful for headers, source attributions,
  and inline annotations. Inline trailing comments (mid-line `#`) are **not**
  supported.
- **No mandatory trailing newline.** The final line may end with or without
  one.

The CLI reports parse problems with a 1-based physical line number,
including blank and comment lines in the count:

```
parse error on line 4: expected three whitespace-separated numbers `x y b` ("bad bad bad")
non-positive weight on line 7: b must be strictly positive ("1 2 -0.5")
parse error on line 0: cannot open /no/such/file
```

(Line `0` is used for file-open errors, since they are not attributable to
any line of input.)

### Worked example

`examples/eiselt.txt` — Eiselt & Sandblom (2004)'s pathological case:

```text
# Eiselt & Sandblom (2004) — Weiszfeld's pathological example.
# Format: x y b
100  100  4
  0    0  1
  0    1  1
  1    0  1
  1    1  1
```

Five demand points: one heavy (`b=4`) point at `(100, 100)` and four
unit-weight points clustered near the origin. The dominant weight pulls the
iterate slowly across the gap, which is why the run below hits the
iteration cap.

## CLI usage

```
Usage: fweisz [options] [input-file]

Reads demand points from <input-file> or stdin: one point per line as
`x y b`, where (x, y) is the location and b > 0 is its weight. Lines
starting with `#` and blank lines are ignored.

Options:
  --tol VAL        Convergence tolerance on |S^(r+1) - S^(r)|  (default 1e-8)
  --max-iter N     Hard iteration cap                          (default 500)
  --eps2 VAL       Floor for d^2 against demand-point singularity (default 1e-20)
  --scalar         Force the scalar lane (default: widest SIMD)
  --reps N         Solve N times; report mean wall time        (default 1)
  --quiet          Machine-readable output: `x y K iters converged`
  --help, -h       Show this message and exit

Exit code: 0 if converged, 1 if max-iter hit, 2 on bad input.
```

`--quiet` is intended for scripted use: it prints a single line of
space-separated values `x y K iters converged`, with `converged` as `0`/`1`.

### Sample runs

The pathological Eiselt input illustrates Weiszfeld's slow convergence on
adversarial weight ratios:

```sh
$ bazel-bin/fweisz/console examples/eiselt.txt
n          = 5
lane       = ActiveLane
S          = (50.5620808176, 50.5620808176)
K(S)       = 562.8640600350
iterations = 500
converged  = no
time       = 0.040 ms (mean of 1 run)
```

A well-behaved triangle of three unit-weight points converges quickly (its
optimum is the Fermat point):

```sh
$ printf '0 0 1\n10 0 1\n5 8 1\n' | bazel-bin/fweisz/console
n          = 3
lane       = ActiveLane
S          = (5.0000000000, 2.8867513371)
K(S)       = 16.6602540378
iterations = 26
converged  = yes
```

## Library usage

```cpp
#include "fweisz/algorithm.h"

#include <span>
#include <vector>

int main()
{
    std::vector<double> xs = { 0.0, 10.0,  5.0 };
    std::vector<double> ys = { 0.0,  0.0,  8.0 };
    std::vector<double> bs = { 1.0,  1.0,  1.0 };

    // One-shot:
    const fweisz::Result r = fweisz::Solve(xs, ys, bs);

    // Or, for repeated solves / explicit lane choice:
    const fweisz::Solver solver(xs, ys, bs);
    const fweisz::Result r_simd   = solver.Run();
    const fweisz::Result r_scalar = solver.RunWith<fweisz::ScalarLane>();
}
```

For reading the standard `x y b` input format programmatically, use
`fweisz::Parser`. The API is built on C++23 `std::expected`, so success
and failure are values you destructure with `if (result)` — no
out-parameters, no `Release()`, no mutable state on the parser:

```cpp
#include "fweisz/parser.h"

#include <fstream>

const fweisz::Parser parser;
std::ifstream in("examples/eiselt.txt");
const auto result = parser.Parse(in);
if (!result) {
    std::fprintf(stderr, "%s\n", result.error().Format().c_str());
    return 2;
}
const fweisz::Result r = fweisz::Solve(result->xs, result->ys, result->bs);
```

`Parser::Error::kind` is an enum (`ParseFailure` / `NonPositiveWeight`) for
callers that want to react to the failure category without parsing the
message. A `Parser` instance is stateless — every parse function is `const`
and returns a fresh `std::expected<Points, Error>`, so one instance can be
reused across any number of inputs.

For tweaking solver knobs:

```cpp
const fweisz::Config cfg{
    .max_iterations = 10'000,
    .tolerance      = 1e-12,
    .singular_eps2  = 1e-24,
};
const fweisz::Result r = fweisz::Solve(xs, ys, bs, cfg);
```

The full API is documented inline with Doxygen — see `fweisz/algorithm.h`,
`fweisz/lanes.h`, and `fweisz/parser.h`.

## Continuous integration

Every push and pull request runs the full build + test matrix defined in
`.github/workflows/build_test.yml`:

| OS | Architecture | Toolchain |
| --- | --- | --- |
| Linux | x86_64 | GCC |
| Linux | x86_64 | Clang |
| Linux | ARM64 | GCC |
| macOS | x86_64 (Intel) | Apple Clang |
| macOS | ARM64 (Apple Silicon) | Apple Clang |
| Windows | x86_64 | MSVC |

Each cell builds and tests in both `--compilation_mode=dbg` and
`--compilation_mode=opt`. Bazel's bazelisk cache, disk cache (keyed per
OS/toolchain), and repository cache are all wired through
`bazel-contrib/setup-bazel`, so warm runs are fast.

## Performance notes

The default Bazel flags (see `.bazelrc`) compile with `-O3 -march=native
-flto -fno-exceptions`. The inner loop is structured to be friendly to
auto-vectorization and FMA generation:

- Hot pointer arguments are `FWEISZ_RESTRICT`-qualified to license
  no-aliasing assumptions.
- The accumulator pattern uses `acc.Fma(b, c) == acc + b * c` so the
  compiler emits a hardware FMA when available and falls back to
  multiply+add otherwise.
- The `d² ≥ singular_eps2` clamp is implemented as a vector `max`, keeping
  the loop branch-free even when the iterate coincides with a demand point.
- The SIMD body and the scalar tail (at most `kLanes − 1` iterations) are
  each force-inlined into `Step()`, so the codegen matches a single
  open-coded function.

If a change suggests itself, please measure before and after — the inner
loop is small enough that minor source rearrangements can perturb codegen
noticeably.

## License

MIT. See the SPDX-style header at the top of each source file.
