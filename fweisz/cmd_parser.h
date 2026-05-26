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

/// @file cmd_parser.h
/// @brief Command-line argument parser for the `fweisz` CLI driver.

#ifndef FWEISZ_CMD_PARSER_H_
#define FWEISZ_CMD_PARSER_H_

#include "fweisz/algorithm.h"

#include <expected>
#include <optional>
#include <string>
#include <string_view>

namespace fweisz
{
    /// @brief Parser for the `fweisz` CLI options.
    ///
    /// Each call to @ref Parse returns either an @ref Args value describing
    /// the parsed command line or an @ref Error describing the first bad
    /// option encountered.
    ///
    /// Typical usage from `main`:
    /// @code
    /// int main(int argc, char** argv) {
    ///     const auto args = fweisz::CmdParser::Parse(argc, argv);
    ///     if (!args) {
    ///         std::fprintf(stderr, "%s\n", args.error().message.c_str());
    ///         return 2;
    ///     }
    ///     if (args->help_requested) {
    ///         std::cout << fweisz::CmdParser::Usage();
    ///         return 0;
    ///     }
    ///     // ... use *args ...
    /// }
    /// @endcode
    class CmdParser
    {
    public:
        /// @brief Parsed command-line options.
        struct Args
        {
            Config config{};                       ///< Iteration / numerical-guard parameters.
            bool scalar = false;                   ///< When true, force the scalar SIMD lane.
            int reps = 1;                          ///< Number of repeated solves used to compute the reported mean time.
            bool quiet = false;                    ///< When true, emit machine-readable output.
            bool help_requested = false;           ///< When true, the caller should print @ref Usage and exit 0.
            std::optional<std::string> input_path; ///< Path to the input file; `std::nullopt` means read from stdin.
        };

        /// @brief Structured description of an argument-parsing failure.
        struct Error
        {
            /// @brief Coarse category of failure.
            enum class Kind
            {
                UnknownOption,  ///< An option name that the parser does not recognise.
                MissingValue,   ///< A flag was given without its required value (e.g. `--tol` at end of argv).
                BadValue,       ///< A flag's value failed to parse, or was out of range.
                MultipleInputs, ///< More than one positional input path was given.
            };

            Kind kind;           ///< Category of the failure.
            std::string message; ///< Human-readable description.
        };

        /// @brief Result alias: either parsed @ref Args or a structured @ref Error.
        using Result = std::expected<Args, Error>;

        /// @brief Parse `argc`/`argv` as supplied to `main`.
        ///
        /// `argv[0]` is treated as the program name and ignored. If `--help`
        /// or `-h` is encountered, parsing stops immediately and the returned
        /// @ref Args has @ref Args::help_requested set to `true` — remaining
        /// arguments are not inspected.
        ///
        /// @param argc Argument count (as passed to `main`).
        /// @param argv Argument vector; entries are not modified.
        /// @return Parsed @ref Args on success, or an @ref Error describing
        ///         the first malformed option.
        [[nodiscard]] static Result Parse(int argc, const char* const* argv);

        /// @brief Usage / help text printed by `--help` and `-h`.
        [[nodiscard]] static std::string_view Usage() noexcept;
    };
} // namespace fweisz

#endif // FWEISZ_CMD_PARSER_H_
