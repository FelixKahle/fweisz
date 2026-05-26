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
#include "fweisz/cmd_parser.h"
#include "fweisz/lanes.h"
#include "fweisz/parser.h"

#include <chrono>
#include <cstdio>
#include <expected>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>

namespace wz = fweisz;

namespace
{
    /// @brief Process exited because the solver converged.
    constexpr int kExitConverged = 0;
    /// @brief Process exited because the solver hit the iteration cap.
    constexpr int kExitMaxIter = 1;
    /// @brief Process exited due to bad input or arguments.
    constexpr int kExitBadInput = 2;

    /// @brief Read the input file (or stdin) and run the @ref wz::Parser over it.
    ///
    /// @param input_path Path to read from, or `std::nullopt` for stdin.
    /// @return The parsed points on success, or a @ref wz::Parser::Error.
    [[nodiscard]] wz::Parser::Result LoadPoints(const std::optional<std::string>& input_path)
    {
        if (!input_path.has_value())
        {
            return wz::Parser::Parse(std::cin);
        }
        std::ifstream file(*input_path);
        if (!file)
        {
            return std::unexpected(wz::Parser::Error{
                wz::Parser::Error::Kind::ParseFailure,
                /*line=*/0,
                /*message=*/"cannot open " + *input_path,
                /*raw=*/{}
            });
        }
        return wz::Parser::Parse(file);
    }

    /// @brief Result of running the solver `reps` times: the (last) result and the mean wall time.
    struct TimedSolve
    {
        wz::Result result; ///< Result of the final repetition; identical to earlier reps for deterministic inputs.
        double ms_mean;    ///< Mean wall-clock time across all reps, in milliseconds.
    };

    /// @brief Run the solver @p reps times and report the (last) result plus the mean wall time.
    ///
    /// All repetitions use the same input and configuration; the loop exists
    /// so that small inputs can be timed reliably by amortising start-up
    /// noise across many runs.
    ///
    /// @param solver       Pre-constructed solver (input arrays + config).
    /// @param force_scalar When true, every repetition uses @ref wz::ScalarLane.
    /// @param reps         Number of solves to perform; must be ≥ 1.
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
    void PrintHuman(const wz::Result& r, const std::size_t n, const char* lane_name,
        const std::size_t lane_width, const double ms_mean, const int reps)
    {
        std::printf("n          = %zu\n", n);
        std::printf("lane       = %s (width %zu)\n", lane_name, lane_width);
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
    const auto parsed_args = wz::CmdParser::Parse(argc, argv);
    if (!parsed_args)
    {
        std::fprintf(stderr, "%s\nRun with --help for usage.\n", parsed_args.error().message.c_str());
        return kExitBadInput;
    }
    const wz::CmdParser::Args& args = *parsed_args;

    if (args.help_requested)
    {
        std::cout << wz::CmdParser::Usage();
        return kExitConverged;
    }

    auto loaded = LoadPoints(args.input_path);
    if (!loaded)
    {
        std::fprintf(stderr, "%s\n", loaded.error().Format().c_str());
        return kExitBadInput;
    }
    const wz::Parser::Points& points = *loaded;
    if (points.Empty())
    {
        std::fputs("No points read from input.\n", stderr);
        return kExitBadInput;
    }

    const wz::Solver solver(points.xs, points.ys, points.bs, args.config);
    const auto [result, ms_mean] = RunTimed(solver, args.scalar, args.reps);

    if (args.quiet)
    {
        PrintQuiet(result);
    }
    else
    {
        const char* lane_name = args.scalar ? wz::ScalarLane::kName : wz::ActiveLane::kName;
        const std::size_t lane_width = args.scalar ? wz::ScalarLane::kLanes : wz::ActiveLane::kLanes;
        PrintHuman(result, points.Size(), lane_name, lane_width, ms_mean, args.reps);
    }
    return result.converged ? kExitConverged : kExitMaxIter;
}
