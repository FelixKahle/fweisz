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

/// @file parser.h
/// @brief Parser for the fweisz problem input format.
///
/// The file format is one demand point per line as three whitespace-separated
/// numbers, `x y b`, where `b > 0` is the weight. Blank lines and lines whose
/// first non-whitespace character is `#` are treated as comments.
///
/// Each parse function returns either the parsed @ref Parser::Points or a
/// structured @ref Parser::Error describing what went wrong and where.

#ifndef FWEISZ_PARSER_H_
#define FWEISZ_PARSER_H_

#include <cstddef>
#include <expected>
#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

namespace fweisz
{
    /// @brief Parser for the fweisz `x y b` text format.
    ///
    /// Typical usage:
    /// @code
    /// const auto result = fweisz::Parser::Parse(std::cin);
    /// if (!result) {
    ///     std::fprintf(stderr, "%s\n", result.error().Format().c_str());
    ///     return 2;
    /// }
    /// const fweisz::Result r = fweisz::Solve(result->xs, result->ys, result->bs);
    /// @endcode
    class Parser
    {
    public:
        /// @brief Parallel coordinate / weight arrays produced by the parser.
        ///
        /// Stored as three parallel vectors so they can be passed directly to
        /// @c fweisz::Solver without an additional layout conversion.
        struct Points
        {
            std::vector<double> xs; ///< x coordinates, one entry per accepted line.
            std::vector<double> ys; ///< y coordinates, parallel to @ref xs.
            std::vector<double> bs; ///< Weights (all > 0), parallel to @ref xs.

            /// @brief Number of demand points.
            [[nodiscard]] std::size_t Size() const noexcept
            {
                return xs.size();
            }

            /// @brief `true` when no points were parsed.
            [[nodiscard]] bool Empty() const noexcept
            {
                return xs.empty();
            }
        };

        /// @brief Structured description of a parse failure.
        struct Error
        {
            /// @brief Coarse category of parse failure.
            ///
            /// Callers that want to react differently to a malformed numeric
            /// field vs. a domain-rule violation can switch on this enum
            /// instead of pattern-matching @ref message.
            enum class Kind
            {
                ParseFailure,      ///< A required field was missing or not a valid number.
                NonPositiveWeight, ///< The third field parsed but failed the `b > 0` precondition.
            };

            Kind kind;           ///< Category of the failure.
            std::size_t line;    ///< 1-based physical line number where the failure was detected.
            std::string message; ///< Human-readable description of the failure.
            std::string raw;     ///< Verbatim contents of the offending line (without the trailing newline).

            /// @brief Render a single-line, user-facing diagnostic.
            ///
            /// Composes the failure category, the 1-based @ref line number,
            /// the @ref message, and (when non-empty) the @ref raw line
            /// content.
            [[nodiscard]] std::string Format() const;
        };

        /// @brief Result alias: either parsed @ref Points or a structured @ref Error.
        using Result = std::expected<Points, Error>;

        /// @brief Parse all remaining lines from @p in.
        ///
        /// Reads until end-of-stream, accumulating one demand point per
        /// non-comment, non-blank line. On the first malformed line the
        /// remainder of the stream is left unread and an @ref Error is
        /// returned.
        ///
        /// @param in Input stream to consume.
        /// @return The parsed @ref Points, or an @ref Error describing the
        ///         first problem encountered.
        [[nodiscard]] static Result Parse(std::istream& in);

        /// @brief Parse an in-memory string buffer.
        ///
        /// Equivalent to wrapping @p input in a `std::istringstream` and
        /// calling @ref Parse. Useful for testing and for inputs already
        /// resident in memory.
        ///
        /// @param input Newline-separated text in the `x y b` format.
        /// @return The parsed @ref Points, or an @ref Error describing the
        ///         first problem encountered.
        [[nodiscard]] static Result ParseString(std::string_view input);
    };
} // namespace fweisz

#endif // FWEISZ_PARSER_H_
