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

#include "fweisz/parser.h"

#include <expected>
#include <format>
#include <istream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace fweisz
{
    namespace
    {
        /// @brief Characters treated as horizontal/vertical whitespace by the parser.
        constexpr std::string_view kWhitespace = " \t\r\n";

        /// @brief Classification of a single physical line.
        enum class LineKind
        {
            Empty,   ///< Whitespace-only line; skipped.
            Comment, ///< Line whose first non-whitespace character is `#`; skipped.
            Data,    ///< Line that should be parsed as `x y b`.
        };

        /// @brief Classify @p line according to its first non-whitespace character.
        [[nodiscard]] LineKind Classify(const std::string_view line) noexcept
        {
            const std::size_t start = line.find_first_not_of(kWhitespace);
            if (start == std::string_view::npos)
            {
                return LineKind::Empty;
            }
            return line[start] == '#' ? LineKind::Comment : LineKind::Data;
        }

        /// @brief Construct an @ref Parser::Error of the given category.
        [[nodiscard]] Parser::Error MakeError(const Parser::Error::Kind kind, const std::size_t lineno,
            std::string message, const std::string_view raw)
        {
            return Parser::Error{kind, lineno, std::move(message), std::string(raw)};
        }

        /// @brief Parse a single physical line, appending one point on success.
        ///
        /// Empty / comment lines are accepted no-ops; bad data lines yield an
        /// @ref Parser::Error inside `std::unexpected`.
        [[nodiscard]] std::expected<void, Parser::Error>
        ParseOneLine(const std::string_view line, const std::size_t lineno, Parser::Points& out)
        {
            switch (Classify(line))
            {
                case LineKind::Empty:
                case LineKind::Comment:
                    return {};
                case LineKind::Data:
                    break;
            }

            std::istringstream ss{std::string(line)};
            double x = 0.0, y = 0.0, b = 0.0;
            if (!(ss >> x >> y >> b))
            {
                return std::unexpected(MakeError(Parser::Error::Kind::ParseFailure, lineno,
                    "expected three whitespace-separated numbers `x y b`", line));
            }
            if (b <= 0.0)
            {
                return std::unexpected(MakeError(Parser::Error::Kind::NonPositiveWeight, lineno,
                    "b must be strictly positive", line));
            }

            out.xs.push_back(x);
            out.ys.push_back(y);
            out.bs.push_back(b);
            return {};
        }
    } // namespace

    std::string Parser::Error::Format() const
    {
        const std::string_view category = (kind == Kind::NonPositiveWeight)
            ? "non-positive weight"
            : "parse error";
        if (raw.empty())
        {
            return std::format("{} on line {}: {}", category, line, message);
        }
        return std::format("{} on line {}: {} (\"{}\")", category, line, message, raw);
    }

    Parser::Result Parser::Parse(std::istream& in)
    {
        Points pts;
        std::string line;
        std::size_t lineno = 0;
        while (std::getline(in, line))
        {
            ++lineno;
            auto step = ParseOneLine(line, lineno, pts);
            if (!step)
            {
                return std::unexpected(std::move(step.error()));
            }
        }
        return pts;
    }

    Parser::Result Parser::ParseString(const std::string_view input)
    {
        std::istringstream in{std::string(input)};
        return Parse(in);
    }
} // namespace fweisz
