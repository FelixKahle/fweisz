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

#include <gtest/gtest.h>
#include <array>
#include <cstddef>

namespace fweisz
{
    namespace
    {
        // Small helper: build argv from a brace-initializer list of literals.
        template <std::size_t N>
        CmdParser::Result ParseLiterals(const char* const (&argv)[N])
        {
            return CmdParser::Parse(static_cast<int>(N), argv);
        }

        TEST(CmdParserTest, DefaultsWhenNoArgs)
        {
            const char* const argv[] = {"fweisz"};
            const auto r = ParseLiterals(argv);
            ASSERT_TRUE(r.has_value());
            EXPECT_FALSE(r->scalar);
            EXPECT_FALSE(r->quiet);
            EXPECT_FALSE(r->help_requested);
            EXPECT_EQ(r->reps, 1);
            EXPECT_FALSE(r->input_path.has_value());
            EXPECT_EQ(r->config.max_iterations, Config{}.max_iterations);
            EXPECT_DOUBLE_EQ(r->config.tolerance, Config{}.tolerance);
            EXPECT_DOUBLE_EQ(r->config.singular_eps2, Config{}.singular_eps2);
        }

        TEST(CmdParserTest, TreatsPositionalArgAsInputPath)
        {
            const char* const argv[] = {"fweisz", "examples/eiselt.txt"};
            const auto r = ParseLiterals(argv);
            ASSERT_TRUE(r.has_value());
            ASSERT_TRUE(r->input_path.has_value());
            EXPECT_EQ(*r->input_path, "examples/eiselt.txt");
        }

        TEST(CmdParserTest, ShortHelpFlagSetsHelpRequested)
        {
            const char* const argv[] = {"fweisz", "-h"};
            const auto r = ParseLiterals(argv);
            ASSERT_TRUE(r.has_value());
            EXPECT_TRUE(r->help_requested);
        }

        TEST(CmdParserTest, LongHelpFlagSetsHelpRequested)
        {
            const char* const argv[] = {"fweisz", "--help"};
            const auto r = ParseLiterals(argv);
            ASSERT_TRUE(r.has_value());
            EXPECT_TRUE(r->help_requested);
        }

        TEST(CmdParserTest, HelpShortCircuitsLaterArgs)
        {
            // A bad value after --help must not produce an error: help wins.
            const char* const argv[] = {"fweisz", "--help", "--max-iter", "not-a-number"};
            const auto r = ParseLiterals(argv);
            ASSERT_TRUE(r.has_value());
            EXPECT_TRUE(r->help_requested);
        }

        TEST(CmdParserTest, ScalarFlagSetsScalar)
        {
            const char* const argv[] = {"fweisz", "--scalar"};
            const auto r = ParseLiterals(argv);
            ASSERT_TRUE(r.has_value());
            EXPECT_TRUE(r->scalar);
        }

        TEST(CmdParserTest, QuietFlagSetsQuiet)
        {
            const char* const argv[] = {"fweisz", "--quiet"};
            const auto r = ParseLiterals(argv);
            ASSERT_TRUE(r.has_value());
            EXPECT_TRUE(r->quiet);
        }

        TEST(CmdParserTest, ParsesTolerance)
        {
            const char* const argv[] = {"fweisz", "--tol", "1.5e-6"};
            const auto r = ParseLiterals(argv);
            ASSERT_TRUE(r.has_value());
            EXPECT_DOUBLE_EQ(r->config.tolerance, 1.5e-6);
        }

        TEST(CmdParserTest, ParsesMaxIter)
        {
            const char* const argv[] = {"fweisz", "--max-iter", "2500"};
            const auto r = ParseLiterals(argv);
            ASSERT_TRUE(r.has_value());
            EXPECT_EQ(r->config.max_iterations, 2500);
        }

        TEST(CmdParserTest, ParsesEps2)
        {
            const char* const argv[] = {"fweisz", "--eps2", "1e-30"};
            const auto r = ParseLiterals(argv);
            ASSERT_TRUE(r.has_value());
            EXPECT_DOUBLE_EQ(r->config.singular_eps2, 1e-30);
        }

        TEST(CmdParserTest, ParsesReps)
        {
            const char* const argv[] = {"fweisz", "--reps", "7"};
            const auto r = ParseLiterals(argv);
            ASSERT_TRUE(r.has_value());
            EXPECT_EQ(r->reps, 7);
        }

        TEST(CmdParserTest, CombinesMultipleFlagsAndPositional)
        {
            const char* const argv[] = {
                "fweisz", "--scalar", "--quiet", "--tol", "1e-10",
                "--max-iter", "1000", "--reps", "3", "input.txt"
            };
            const auto r = ParseLiterals(argv);
            ASSERT_TRUE(r.has_value());
            EXPECT_TRUE(r->scalar);
            EXPECT_TRUE(r->quiet);
            EXPECT_DOUBLE_EQ(r->config.tolerance, 1e-10);
            EXPECT_EQ(r->config.max_iterations, 1000);
            EXPECT_EQ(r->reps, 3);
            ASSERT_TRUE(r->input_path.has_value());
            EXPECT_EQ(*r->input_path, "input.txt");
        }

        TEST(CmdParserTest, RejectsUnknownLongOption)
        {
            const char* const argv[] = {"fweisz", "--bogus"};
            const auto r = ParseLiterals(argv);
            ASSERT_FALSE(r.has_value());
            EXPECT_EQ(r.error().kind, CmdParser::Error::Kind::UnknownOption);
        }

        TEST(CmdParserTest, RejectsUnknownShortOption)
        {
            const char* const argv[] = {"fweisz", "-x"};
            const auto r = ParseLiterals(argv);
            ASSERT_FALSE(r.has_value());
            EXPECT_EQ(r.error().kind, CmdParser::Error::Kind::UnknownOption);
        }

        TEST(CmdParserTest, RejectsMissingValueAtEndOfArgv)
        {
            const char* const argv[] = {"fweisz", "--tol"};
            const auto r = ParseLiterals(argv);
            ASSERT_FALSE(r.has_value());
            EXPECT_EQ(r.error().kind, CmdParser::Error::Kind::MissingValue);
        }

        TEST(CmdParserTest, RejectsNonNumericDoubleValue)
        {
            const char* const argv[] = {"fweisz", "--tol", "abc"};
            const auto r = ParseLiterals(argv);
            ASSERT_FALSE(r.has_value());
            EXPECT_EQ(r.error().kind, CmdParser::Error::Kind::BadValue);
        }

        TEST(CmdParserTest, RejectsNonNumericIntegerValue)
        {
            const char* const argv[] = {"fweisz", "--max-iter", "abc"};
            const auto r = ParseLiterals(argv);
            ASSERT_FALSE(r.has_value());
            EXPECT_EQ(r.error().kind, CmdParser::Error::Kind::BadValue);
        }

        TEST(CmdParserTest, RejectsZeroMaxIter)
        {
            const char* const argv[] = {"fweisz", "--max-iter", "0"};
            const auto r = ParseLiterals(argv);
            ASSERT_FALSE(r.has_value());
            EXPECT_EQ(r.error().kind, CmdParser::Error::Kind::BadValue);
        }

        TEST(CmdParserTest, RejectsNegativeMaxIter)
        {
            const char* const argv[] = {"fweisz", "--max-iter", "-3"};
            const auto r = ParseLiterals(argv);
            ASSERT_FALSE(r.has_value());
            EXPECT_EQ(r.error().kind, CmdParser::Error::Kind::BadValue);
        }

        TEST(CmdParserTest, RejectsZeroReps)
        {
            const char* const argv[] = {"fweisz", "--reps", "0"};
            const auto r = ParseLiterals(argv);
            ASSERT_FALSE(r.has_value());
            EXPECT_EQ(r.error().kind, CmdParser::Error::Kind::BadValue);
        }

        TEST(CmdParserTest, RejectsMultiplePositionalArgs)
        {
            const char* const argv[] = {"fweisz", "a.txt", "b.txt"};
            const auto r = ParseLiterals(argv);
            ASSERT_FALSE(r.has_value());
            EXPECT_EQ(r.error().kind, CmdParser::Error::Kind::MultipleInputs);
        }

        TEST(CmdParserTest, BareDashIsTreatedAsPositional)
        {
            // The original behaviour: a single `-` falls through to the positional
            // branch and is recorded as the input path.
            const char* const argv[] = {"fweisz", "-"};
            const auto r = ParseLiterals(argv);
            ASSERT_TRUE(r.has_value());
            ASSERT_TRUE(r->input_path.has_value());
            EXPECT_EQ(*r->input_path, "-");
        }

        TEST(CmdParserUsageTest, IsNonEmptyAndMentionsProgramName)
        {
            const auto usage = CmdParser::Usage();
            EXPECT_FALSE(usage.empty());
            EXPECT_NE(usage.find("fweisz"), std::string_view::npos);
        }
    } // namespace
} // namespace fweisz
