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

#include "fweisz/cmd_parser.h"

#include <cstdlib>
#include <cstring>
#include <expected>
#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace fweisz
{
    namespace
    {
        constexpr std::string_view kUsage =
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

        /// @brief Parse a `double` from a null-terminated string, requiring full consumption.
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

        /// @brief Parse a `long` from a null-terminated string, requiring full consumption
        ///        and a value that fits in `int`.
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

        CmdParser::Error MakeUnknownOption(std::string_view flag)
        {
            return {CmdParser::Error::Kind::UnknownOption, std::format("unknown option: {}", flag)};
        }

        CmdParser::Error MakeMissingValue(std::string_view flag)
        {
            return {CmdParser::Error::Kind::MissingValue, std::format("{} requires a value", flag)};
        }

        CmdParser::Error MakeBadValue(std::string_view flag, std::string_view raw)
        {
            return {CmdParser::Error::Kind::BadValue, std::format("bad value for {}: \"{}\"", flag, raw)};
        }

        CmdParser::Error MakeMultipleInputs(std::string_view first, std::string_view second)
        {
            return {CmdParser::Error::Kind::MultipleInputs,
                std::format("multiple input files given: \"{}\" and \"{}\"", first, second)};
        }
    } // namespace

    std::string_view CmdParser::Usage() noexcept
    {
        return kUsage;
    }

    CmdParser::Result CmdParser::Parse(int argc, const char* const* argv)
    {
        Args args;
        for (int i = 1; i < argc; ++i)
        {
            const char* a = argv[i];

            // --help / -h: short-circuit, ignore everything else.
            if (std::strcmp(a, "--help") == 0 || std::strcmp(a, "-h") == 0)
            {
                args.help_requested = true;
                return args;
            }

            // Bare boolean flags.
            if (std::strcmp(a, "--scalar") == 0)
            {
                args.scalar = true;
                continue;
            }
            if (std::strcmp(a, "--quiet") == 0)
            {
                args.quiet = true;
                continue;
            }

            // Flags that take a value.
            const auto needs_value = [&](const char* flag) -> std::expected<const char*, Error>
            {
                if (++i >= argc)
                {
                    return std::unexpected(MakeMissingValue(flag));
                }
                return argv[i];
            };

            if (std::strcmp(a, "--tol") == 0)
            {
                auto v = needs_value("--tol");
                if (!v)
                {
                    return std::unexpected(std::move(v.error()));
                }
                if (!ParseDouble(*v, args.config.tolerance))
                {
                    return std::unexpected(MakeBadValue("--tol", *v));
                }
                continue;
            }
            if (std::strcmp(a, "--max-iter") == 0)
            {
                auto v = needs_value("--max-iter");
                if (!v)
                {
                    return std::unexpected(std::move(v.error()));
                }
                int parsed = 0;
                if (!ParseInt(*v, parsed) || parsed < 1)
                {
                    return std::unexpected(MakeBadValue("--max-iter", *v));
                }
                args.config.max_iterations = parsed;
                continue;
            }
            if (std::strcmp(a, "--eps2") == 0)
            {
                auto v = needs_value("--eps2");
                if (!v)
                {
                    return std::unexpected(std::move(v.error()));
                }
                if (!ParseDouble(*v, args.config.singular_eps2))
                {
                    return std::unexpected(MakeBadValue("--eps2", *v));
                }
                continue;
            }
            if (std::strcmp(a, "--reps") == 0)
            {
                auto v = needs_value("--reps");
                if (!v)
                {
                    return std::unexpected(std::move(v.error()));
                }
                int parsed = 0;
                if (!ParseInt(*v, parsed) || parsed < 1)
                {
                    return std::unexpected(MakeBadValue("--reps", *v));
                }
                args.reps = parsed;
                continue;
            }

            // Any other `-flag` form is unrecognised. A bare `-` (length 1) is
            // treated as a positional argument, matching the original behaviour.
            if (a[0] == '-' && a[1] != '\0')
            {
                return std::unexpected(MakeUnknownOption(a));
            }

            // Positional argument: input file path.
            if (args.input_path.has_value())
            {
                return std::unexpected(MakeMultipleInputs(*args.input_path, a));
            }
            args.input_path = std::string(a);
        }
        return args;
    }
} // namespace fweisz
