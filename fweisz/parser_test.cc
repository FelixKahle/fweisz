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
#include <gtest/gtest.h>

#include <sstream>
#include <string>
#include <string_view>

namespace fweisz
{
    namespace
    {
        TEST(ParserTest, EmptyInputProducesNoPoints)
        {
            const auto r = Parser::ParseString("");
            ASSERT_TRUE(r.has_value());
            EXPECT_TRUE(r->Empty());
            EXPECT_EQ(r->Size(), 0u);
        }

        TEST(ParserTest, ParsesSingleTriple)
        {
            const auto r = Parser::ParseString("1.5 -2.25 3\n");
            ASSERT_TRUE(r.has_value());
            ASSERT_EQ(r->Size(), 1u);
            EXPECT_DOUBLE_EQ(r->xs[0], 1.5);
            EXPECT_DOUBLE_EQ(r->ys[0], -2.25);
            EXPECT_DOUBLE_EQ(r->bs[0], 3.0);
        }

        TEST(ParserTest, AcceptsLineWithoutTrailingNewline)
        {
            const auto r = Parser::ParseString("1 2 3");
            ASSERT_TRUE(r.has_value());
            ASSERT_EQ(r->Size(), 1u);
            EXPECT_DOUBLE_EQ(r->bs[0], 3.0);
        }

        TEST(ParserTest, AccumulatesMultiplePoints)
        {
            const auto r = Parser::ParseString("0 0 1\n10 0 2\n5 8 4\n");
            ASSERT_TRUE(r.has_value());
            ASSERT_EQ(r->Size(), 3u);
            EXPECT_DOUBLE_EQ(r->xs[1], 10.0);
            EXPECT_DOUBLE_EQ(r->ys[2], 8.0);
            EXPECT_DOUBLE_EQ(r->bs[2], 4.0);
        }

        TEST(ParserTest, SkipsCommentLines)
        {
            const auto r = Parser::ParseString("# header\n1 2 3\n#trailer\n");
            ASSERT_TRUE(r.has_value());
            EXPECT_EQ(r->Size(), 1u);
        }

        TEST(ParserTest, SkipsBlankAndWhitespaceLines)
        {
            const auto r = Parser::ParseString("\n   \n\t\t\n1 2 3\n   \n");
            ASSERT_TRUE(r.has_value());
            EXPECT_EQ(r->Size(), 1u);
        }

        TEST(ParserTest, AcceptsLeadingWhitespaceOnDataLine)
        {
            const auto r = Parser::ParseString("   \t 1 2 3\n");
            ASSERT_TRUE(r.has_value());
            ASSERT_EQ(r->Size(), 1u);
            EXPECT_DOUBLE_EQ(r->xs[0], 1.0);
        }

        TEST(ParserTest, AcceptsCommentWithLeadingWhitespace)
        {
            const auto r = Parser::ParseString("   # indented comment\n1 2 3\n");
            ASSERT_TRUE(r.has_value());
            EXPECT_EQ(r->Size(), 1u);
        }

        TEST(ParserTest, AcceptsTabSeparatedValues)
        {
            const auto r = Parser::ParseString("1\t2\t3\n");
            ASSERT_TRUE(r.has_value());
            ASSERT_EQ(r->Size(), 1u);
            EXPECT_DOUBLE_EQ(r->ys[0], 2.0);
        }

        TEST(ParserTest, ReportsParseFailureOnNonNumericField)
        {
            const auto r = Parser::ParseString("1 abc 3\n");
            ASSERT_FALSE(r.has_value());
            EXPECT_EQ(r.error().kind, Parser::Error::Kind::ParseFailure);
            EXPECT_EQ(r.error().line, 1u);
            EXPECT_EQ(r.error().raw, "1 abc 3");
        }

        TEST(ParserTest, ReportsParseFailureOnMissingField)
        {
            const auto r = Parser::ParseString("1 2\n");
            ASSERT_FALSE(r.has_value());
            EXPECT_EQ(r.error().kind, Parser::Error::Kind::ParseFailure);
            EXPECT_EQ(r.error().line, 1u);
        }

        TEST(ParserTest, RejectsZeroWeight)
        {
            const auto r = Parser::ParseString("1 2 0\n");
            ASSERT_FALSE(r.has_value());
            EXPECT_EQ(r.error().kind, Parser::Error::Kind::NonPositiveWeight);
            EXPECT_EQ(r.error().line, 1u);
        }

        TEST(ParserTest, RejectsNegativeWeight)
        {
            const auto r = Parser::ParseString("1 2 -0.5\n");
            ASSERT_FALSE(r.has_value());
            EXPECT_EQ(r.error().kind, Parser::Error::Kind::NonPositiveWeight);
            EXPECT_EQ(r.error().line, 1u);
        }

        TEST(ParserTest, ErrorLineNumberCountsCommentsAndBlanks)
        {
            // The bad line is the 4th physical line.
            const auto r = Parser::ParseString("# c\n\n1 2 3\nbad bad bad\n");
            ASSERT_FALSE(r.has_value());
            EXPECT_EQ(r.error().line, 4u);
        }

        TEST(ParserTest, StopsAtFirstErrorWithoutConsumingFollowingLines)
        {
            const auto r = Parser::ParseString("1 2 3\nbad\n9 9 9\n");
            ASSERT_FALSE(r.has_value());
            EXPECT_EQ(r.error().line, 2u);
            EXPECT_EQ(r.error().raw, "bad");
        }

        TEST(ParserTest, ParseAcceptsArbitraryIstream)
        {
            // Same input as ParsesSingleTriple but via Parse(istream&).
            std::istringstream in("1.5 -2.25 3\n");
            const auto r = Parser::Parse(in);
            ASSERT_TRUE(r.has_value());
            EXPECT_DOUBLE_EQ(r->xs[0], 1.5);
        }

        TEST(ParserErrorTest, FormatIncludesRawWhenPresent)
        {
            const auto r = Parser::ParseString("1 abc 3\n");
            ASSERT_FALSE(r.has_value());
            const std::string s = r.error().Format();
            EXPECT_NE(s.find("line 1"), std::string::npos);
            EXPECT_NE(s.find("\"1 abc 3\""), std::string::npos);
        }

        TEST(ParserErrorTest, FormatOmitsRawWhenEmpty)
        {
            // Synthesize an error directly to exercise the raw-empty branch.
            const Parser::Error e{
                Parser::Error::Kind::ParseFailure,
                /*line=*/0,
                /*message=*/"cannot open foo.txt",
                /*raw=*/{}
            };
            const std::string s = e.Format();
            EXPECT_EQ(s.find('"'), std::string::npos);
            EXPECT_NE(s.find("cannot open foo.txt"), std::string::npos);
        }
    } // namespace
} // namespace fweisz
