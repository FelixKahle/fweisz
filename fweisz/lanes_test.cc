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

#include "fweisz/lanes.h"
#include "fweisz/base/simd.h"

#include <gtest/gtest.h>
#include <array>
#include <cmath>

namespace fweisz
{
    namespace
    {
        // -------------------------------------------------------------------------------------------------------------
        // Concept satisfaction
        // -------------------------------------------------------------------------------------------------------------

        static_assert(SimdLane<ScalarLane>);
#if FWEISZ_HAS_AVX2
        static_assert(SimdLane<Avx2Lane>);
#endif
#if FWEISZ_HAS_NEON
        static_assert(SimdLane<NeonLane>);
#endif

        // -------------------------------------------------------------------------------------------------------------
        // kName is a non-null identifier matching each lane's class name.
        // -------------------------------------------------------------------------------------------------------------

        TEST(ScalarLaneTest, NameIsScalarLane)
        {
            EXPECT_STREQ(ScalarLane::kName, "ScalarLane");
        }

#if FWEISZ_HAS_AVX2
        TEST(Avx2LaneTest, NameIsAvx2Lane)
        {
            EXPECT_STREQ(Avx2Lane::kName, "Avx2Lane");
        }
#endif

#if FWEISZ_HAS_NEON
        TEST(NeonLaneTest, NameIsNeonLane)
        {
            EXPECT_STREQ(NeonLane::kName, "NeonLane");
        }
#endif

        // -------------------------------------------------------------------------------------------------------------
        // Per-lane behavior, tested generically. Uses an array of `kLanes`
        // doubles so the same body works at width 1, 2, or 4.
        // -------------------------------------------------------------------------------------------------------------

        template <typename Lane>
        void RunLaneSuite()
        {
            constexpr std::size_t N = Lane::kLanes;
            std::array<double, N> buf{};

            // Broadcast + HorizontalSum.
            {
                const Lane v = Lane::Broadcast(2.5);
                EXPECT_DOUBLE_EQ(v.HorizontalSum(), 2.5 * static_cast<double>(N));
            }

            // Zero.
            {
                const Lane z = Lane::Zero();
                EXPECT_DOUBLE_EQ(z.HorizontalSum(), 0.0);
            }

            // Load.
            {
                for (std::size_t i = 0; i < N; ++i)
                {
                    buf[i] = static_cast<double>(i + 1); // 1..N
                }
                const Lane v = Lane::Load(buf.data());
                // Sum of 1..N = N*(N+1)/2.
                const double expected = static_cast<double>(N * (N + 1)) / 2.0;
                EXPECT_DOUBLE_EQ(v.HorizontalSum(), expected);
            }

            // Arithmetic operators.
            {
                const Lane a = Lane::Broadcast(3.0);
                const Lane b = Lane::Broadcast(4.0);
                EXPECT_DOUBLE_EQ((a + b).HorizontalSum(), 7.0 * static_cast<double>(N));
                EXPECT_DOUBLE_EQ((a - b).HorizontalSum(), -1.0 * static_cast<double>(N));
                EXPECT_DOUBLE_EQ((a * b).HorizontalSum(), 12.0 * static_cast<double>(N));
                EXPECT_DOUBLE_EQ((b / a).HorizontalSum(), (4.0 / 3.0) * static_cast<double>(N));
            }

            // Max.
            {
                const Lane a = Lane::Broadcast(1.0);
                const Lane b = Lane::Broadcast(2.0);
                EXPECT_DOUBLE_EQ(a.Max(b).HorizontalSum(), 2.0 * static_cast<double>(N));
            }

            // Sqrt.
            {
                const Lane v = Lane::Broadcast(16.0);
                EXPECT_DOUBLE_EQ(v.Sqrt().HorizontalSum(), 4.0 * static_cast<double>(N));
            }

            // Fma: acc.Fma(a, b) == acc + a*b.
            {
                const Lane acc = Lane::Broadcast(1.0);
                const Lane a = Lane::Broadcast(2.0);
                const Lane b = Lane::Broadcast(3.0);
                EXPECT_DOUBLE_EQ(acc.Fma(a, b).HorizontalSum(), (1.0 + 2.0 * 3.0) * static_cast<double>(N));
            }
        }

        TEST(ScalarLaneTest, MatchesContract)
        {
            RunLaneSuite<ScalarLane>();
        }

#if FWEISZ_HAS_AVX2
        TEST(Avx2LaneTest, MatchesContract)
        {
            RunLaneSuite<Avx2Lane>();
        }
#endif

#if FWEISZ_HAS_NEON
        TEST(NeonLaneTest, MatchesContract)
        {
            RunLaneSuite<NeonLane>();
        }
#endif

        // -------------------------------------------------------------------------------------------------------------
        // Differential: the active lane and the scalar lane must agree element
        // by element on a non-trivial mixed pattern.
        // -------------------------------------------------------------------------------------------------------------

        template <typename Lane>
        // ReSharper disable once CppDFAConstantParameter
        double SumWithLane(const double* p, std::size_t n)
        {
            // Process whole lanes, then a scalar tail.
            std::size_t i = 0;
            Lane acc = Lane::Zero();
            for (const std::size_t main = n & ~(Lane::kLanes - 1); i < main; i += Lane::kLanes)
            {
                acc = acc + Lane::Load(p + i);
            }
            double s = acc.HorizontalSum();
            for (; i < n; ++i)
            {
                s += p[i];
            }
            return s;
        }

        TEST(ActiveLaneTest, SumAgreesWithScalarOnSyntheticInput)
        {
            constexpr std::size_t N = 137; // intentionally not a multiple of kLanes
            std::array<double, N> data{};
            for (std::size_t i = 0; i < N; ++i)
            {
                data[i] = std::sin(static_cast<double>(i)) * 1.25;
            }
            const double a = SumWithLane<ScalarLane>(data.data(), N);
            const double b = SumWithLane<ActiveLane>(data.data(), N);
            EXPECT_NEAR(a, b, 1e-12);
        }
    } // namespace
} // namespace fweisz
