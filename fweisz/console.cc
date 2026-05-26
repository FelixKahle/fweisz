// Copyright (c) 2026 Felix Kahle.
//
// Permission is hereby granted, free of charge, to any person obtaining
// a copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to
// the following conditions:
//
// The above copyright notice and this permission notice shall be
// included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
// WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

/// @file console.cc
/// @brief Command-line driver for the fweisz 1-median solver.
///
/// Reads weighted demand points from a file or stdin, runs Weiszfeld's
/// iteration, and prints either a human-readable summary or a machine-readable
/// one-liner. Exit codes:
///   - `0` — solver converged within @ref fweisz::Config::max_iterations.
///   - `1` — solver hit the iteration cap.
///   - `2` — input or argument error.

#include "fweisz/algorithm.h"
#include "fweisz/parser.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <expected>
#include <fstream>
#include <iostream>
#include <string>

namespace wz = fweisz;

namespace
{
    // -----------------------------------------------------------------------------------------------------------------
    // Exit codes
    // -----------------------------------------------------------------------------------------------------------------

    /// @brief Process exited because the solver converged.
    constexpr int kExitConverged = 0;
    /// @brief Process exited because the solver hit the iteration cap.
    constexpr int kExitMaxIter = 1;
    /// @brief Process exited due to bad input or arguments.
    constexpr int kExitBadInput = 2;

    /// @brief Help text printed by `--help` / `-h`.
    constexpr auto kUsage =
        "Usage: fweisz [options] [input-file]\n"
        "\n"
        "Solves the weighted 1-median problem (Weiszfeld's algorithm).\n"
        "Reads demand points from <input-file> or stdin: one point per line as\n"
        "`x y b`, where (x, y) is the location and b > 0 is its weight. Lines\n"
        "starting with `#` and blank lines are ignored.\n"
        "\n"
        "Options:\n"
        "  --tol VAL        Convergence tolerance on |S^(r+1) - S^(r)|  (default 1e-8)\n"
        "  --max-iter N     Hard iteration cap                          (default 500)\n"
        "  --eps2 VAL       Floor for d^2 against demand-point singularity (default 1e-20)\n"
        "  --scalar         Force the scalar lane (default: widest SIMD)\n"
        "  --reps N         Solve N times; report mean wall time        (default 1)\n"
        "  --quiet          Machine-readable output: `x y K iters converged`\n"
        "  --help, -h       Show this message and exit\n"
        "\n"
        "Exit code: 0 if converged, 1 if max-iter hit, 2 on bad input.\n";

    // -----------------------------------------------------------------------------------------------------------------
    // Argument parsing
    // -----------------------------------------------------------------------------------------------------------------

    /// @brief Parsed command-line options.
    struct Args
    {
        wz::Config config{};              ///< Iteration / numerical-guard parameters.
        bool scalar = false;              ///< When true, force the scalar lane regardless of host capabilities.
        int reps = 1;                     ///< Number of repeated solves used to compute the reported mean time.
        bool quiet = false;               ///< When true, emit machine-readable output instead of the human summary.
        const char* input_path = nullptr; ///< Path to the input file; `nullptr` means read from stdin.
    };

    /// @brief Parse a strictly formatted double (full-string consumption).
    /// @param s   Null-terminated input.
    /// @param out Result on success.
    /// @return `true` on success, `false` on any parse error or trailing garbage.
    bool ParseDouble(const char* s, double& out)
    {
        char* end = nullptr;
        const double v = std::strtod(s, &end);
        if (end == s || *end != '\0')
        {
            return false;
        }
        out = v;
        return true;
    }

    /// @brief Parse a strictly formatted signed integer that fits in `int`.
    /// @param s   Null-terminated input.
    /// @param out Result on success.
    /// @return `true` on success, `false` on parse error, trailing garbage, or out-of-range.
    bool ParseInt(const char* s, int& out)
    {
        char* end = nullptr;
        const long v = std::strtol(s, &end, 10);
        if (end == s || *end != '\0' || v > 2'000'000'000L || v < -2'000'000'000L)
        {
            return false;
        }
        out = static_cast<int>(v);
        return true;
    }

    /// @brief Consume the next argv entry as the value of @p name; emit a diagnostic if missing.
    /// @param name Flag name, used only in the error message.
    /// @param argc Total argument count.
    /// @param argv Argument vector.
    /// @param i    Index of the flag; advanced past the value on success.
    /// @return Pointer to the value, or `nullptr` if no value follows.
    const char* NextValue(const char* name, const int argc, char** argv, int& i)
    {
        if (++i >= argc)
        {
            std::fprintf(stderr, "%s requires a value\n", name);
            return nullptr;
        }
        return argv[i];
    }

    /// @brief Parse the entire argv into an @ref Args struct.
    /// @return `true` on success, `false` if any flag was malformed.
    ///         May call `std::exit(0)` directly when `--help` is supplied.
    bool ParseArgs(const int argc, char** argv, Args& args)
    {
        for (int i = 1; i < argc; ++i)
        {
            const char* a = argv[i];

            if (std::strcmp(a, "--help") == 0 || std::strcmp(a, "-h") == 0)
            {
                std::fputs(kUsage, stdout);
                std::exit(kExitConverged);
            }
            else if (std::strcmp(a, "--scalar") == 0)
            {
                args.scalar = true;
            }
            else if (std::strcmp(a, "--quiet") == 0)
            {
                args.quiet = true;
            }
            else if (std::strcmp(a, "--tol") == 0)
            {
                const char* v = NextValue("--tol", argc, argv, i);
                if (!v || !ParseDouble(v, args.config.tolerance))
                {
                    return false;
                }
            }
            else if (std::strcmp(a, "--max-iter") == 0)
            {
                const char* v = NextValue("--max-iter", argc, argv, i);
                if (!v || !ParseInt(v, args.config.max_iterations) || args.config.max_iterations < 1)
                {
                    std::fprintf(stderr, "Bad --max-iter value\n");
                    return false;
                }
            }
            else if (std::strcmp(a, "--eps2") == 0)
            {
                const char* v = NextValue("--eps2", argc, argv, i);
                if (!v || !ParseDouble(v, args.config.singular_eps2))
                {
                    return false;
                }
            }
            else if (std::strcmp(a, "--reps") == 0)
            {
                const char* v = NextValue("--reps", argc, argv, i);
                if (!v || !ParseInt(v, args.reps) || args.reps < 1)
                {
                    std::fprintf(stderr, "Bad --reps value\n");
                    return false;
                }
            }
            else if (a[0] == '-' && a[1] != '\0')
            {
                std::fprintf(stderr, "Unknown option: %s\n", a);
                return false;
            }
            else
            {
                if (args.input_path != nullptr)
                {
                    std::fprintf(stderr, "Multiple input files given\n");
                    return false;
                }
                args.input_path = a;
            }
        }
        return true;
    }

    // -----------------------------------------------------------------------------------------------------------------
    // Input parsing
    // -----------------------------------------------------------------------------------------------------------------

    /// @brief Read the input file (or stdin) and run the @ref wz::Parser over it.
    ///
    /// @param input_path Path to read from, or `nullptr` for stdin.
    /// @return The parsed points on success, or a @ref wz::Parser::Error on
    ///         either a file-open failure (synthesized) or a parse failure
    ///         (forwarded verbatim from the parser).
    [[nodiscard]] wz::Parser::Result LoadPoints(const char* input_path)
    {
        if (input_path == nullptr)
        {
            return wz::Parser::Parse(std::cin);
        }
        std::ifstream file(input_path);
        if (!file)
        {
            return std::unexpected(wz::Parser::Error{
                wz::Parser::Error::Kind::ParseFailure,
                /*line=*/0,
                /*message=*/std::string{"cannot open "} + input_path,
                /*raw=*/{}
            });
        }
        return wz::Parser::Parse(file);
    }

    // -----------------------------------------------------------------------------------------------------------------
    // Solve + report
    // -----------------------------------------------------------------------------------------------------------------

    /// @brief Run the solver @p reps times and return the (last) result alongside the mean wall time.
    struct TimedSolve
    {
        wz::Result result; ///< Result of the final repetition; identical to earlier reps for deterministic inputs.
        double ms_mean;    ///< Mean wall-clock time across all reps, in milliseconds.
    };

    /// @brief Run the solver @p reps times and return @ref TimedSolve.
    ///
    /// All reps use the same input and configuration; this function exists so
    /// that benchmarking the solver does not pollute @c main with timing code.
    TimedSolve RunTimed(const wz::Solver& solver, const bool force_scalar, const int reps)
    {
        wz::Result r{};
        const auto t0 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < reps; ++i)
        {
            r = force_scalar ? solver.RunWith<wz::ScalarLane>() : solver.Run();
        }
        const auto t1 = std::chrono::high_resolution_clock::now();
        const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count() / reps;
        return {r, ms};
    }

    /// @brief Print one space-separated line of machine-readable results.
    void PrintQuiet(const wz::Result& r)
    {
        std::printf("%.15g %.15g %.15g %d %d\n",
            r.location.x, r.location.y, r.cost, r.iterations, r.converged ? 1 : 0);
    }

    /// @brief Print a multi-line, human-readable result summary.
    void PrintHuman(const wz::Result& r, const std::size_t n, const bool scalar, const double ms_mean, const int reps)
    {
        std::printf("n          = %zu\n", n);
        std::printf("lane       = %s\n", scalar ? "ScalarLane" : "ActiveLane");
        std::printf("S          = (%.10f, %.10f)\n", r.location.x, r.location.y);
        std::printf("K(S)       = %.10f\n", r.cost);
        std::printf("iterations = %d\n", r.iterations);
        std::printf("converged  = %s\n", r.converged ? "yes" : "no");
        std::printf("time       = %.3f ms (mean of %d run%s)\n", ms_mean, reps, reps == 1 ? "" : "s");
    }
} // namespace

/// @brief Command-line entry point.
int main(int argc, char** argv)
{
    Args args;
    if (!ParseArgs(argc, argv, args))
    {
        std::fputs("Run with --help for usage.\n", stderr);
        return kExitBadInput;
    }

    auto parsed = LoadPoints(args.input_path);
    if (!parsed)
    {
        std::fprintf(stderr, "%s\n", parsed.error().Format().c_str());
        return kExitBadInput;
    }
    const wz::Parser::Points& points = *parsed;
    if (points.Empty())
    {
        std::fputs("No points read from input.\n", stderr);
        return kExitBadInput;
    }

    const wz::Solver solver(points.xs, points.ys, points.bs, args.config);
    const TimedSolve ts = RunTimed(solver, args.scalar, args.reps);

    if (args.quiet)
    {
        PrintQuiet(ts.result);
    }
    else
    {
        PrintHuman(ts.result, points.Size(), args.scalar, ts.ms_mean, args.reps);
    }
    return ts.result.converged ? kExitConverged : kExitMaxIter;
}
