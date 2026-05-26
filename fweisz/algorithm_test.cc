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

#include "fweisz/algorithm.h"
#include "fweisz/lanes.h"

#include <gtest/gtest.h>
#include <cmath>
#include <random>
#include <vector>

namespace fweisz
{
    namespace
    {
        // The 1-median of an equilateral triangle with unit weights is its
        // Fermat point — which for a triangle whose largest angle is < 120°
        // coincides with the isogonic center. For a triangle (0,0), (1,0),
        // (0.5, √3/2) the answer is (0.5, √3/6) and the cost is √3.
        TEST(SolverTest, FermatPointOfUnitEquilateralTriangle)
        {
            const std::vector<double> xs{0.0, 1.0, 0.5};
            const std::vector<double> ys{0.0, 0.0, std::sqrt(3.0) / 2.0};
            const std::vector<double> bs{1.0, 1.0, 1.0};

            const Solver solver(xs, ys, bs);
            const Result r = solver.Run();

            EXPECT_TRUE(r.converged);
            EXPECT_NEAR(r.location.x, 0.5, 1e-7);
            EXPECT_NEAR(r.location.y, std::sqrt(3.0) / 6.0, 1e-7);
            EXPECT_NEAR(r.cost, std::sqrt(3.0), 1e-7);
        }

        TEST(SolverTest, CollinearTriplePicksTheMiddleByWeight)
        {
            // Three collinear points; the optimum sits on the dominant weight.
            const std::vector<double> xs{0.0, 5.0, 10.0};
            const std::vector<double> ys{0.0, 0.0, 0.0};
            const std::vector<double> bs{1.0, 100.0, 1.0};

            const Solver solver(xs, ys, bs);
            const Result r = solver.Run();

            EXPECT_TRUE(r.converged);
            EXPECT_NEAR(r.location.x, 5.0, 1e-6);
            EXPECT_NEAR(r.location.y, 0.0, 1e-6);
        }

        TEST(SolverTest, ScalarLaneAndActiveLaneAgree)
        {
            std::mt19937_64 rng(42);
            std::uniform_real_distribution<double> coord(-100.0, 100.0);
            std::uniform_real_distribution<double> weight(0.1, 5.0);

            constexpr std::size_t kN = 257; // not a multiple of any kLanes
            std::vector<double> xs(kN), ys(kN), bs(kN);
            for (std::size_t i = 0; i < kN; ++i)
            {
                xs[i] = coord(rng);
                ys[i] = coord(rng);
                bs[i] = weight(rng);
            }

            const Solver solver(xs, ys, bs);
            const auto [location, cost, iterations, converged] = solver.RunWith<ScalarLane>();
            const Result b = solver.Run();

            EXPECT_EQ(iterations, b.iterations);
            EXPECT_EQ(converged, b.converged);
            EXPECT_NEAR(location.x, b.location.x, 1e-8);
            EXPECT_NEAR(location.y, b.location.y, 1e-8);
            EXPECT_NEAR(cost, b.cost, 1e-8);
        }

        TEST(SolverTest, CenterOfGravityIsWeightedMean)
        {
            const std::vector<double> xs{0.0, 10.0};
            const std::vector<double> ys{0.0, 0.0};
            const std::vector<double> bs{3.0, 1.0};

            const Solver solver(xs, ys, bs);
            const Point g = solver.CenterOfGravity();

            EXPECT_DOUBLE_EQ(g.x, 2.5);
            EXPECT_DOUBLE_EQ(g.y, 0.0);
        }

        TEST(SolverTest, CostAtPointEqualsManualSum)
        {
            const std::vector<double> xs{0.0, 3.0};
            const std::vector<double> ys{0.0, 4.0};
            const std::vector<double> bs{2.0, 5.0};

            const Solver solver(xs, ys, bs);
            // At (0, 0): 2*0 + 5*5 = 25 (the b*sqrt(eps2) floor adds a few-e-10 perturbation).
            EXPECT_NEAR(solver.Cost(Point{0.0, 0.0}), 25.0, 1e-8);
            // At (3, 4): 2*5 + 5*0 = 10 (same floor on the coincident point).
            EXPECT_NEAR(solver.Cost(Point{3.0, 4.0}), 10.0, 1e-8);
        }

        TEST(SolverTest, SinglePointConvergesImmediately)
        {
            const std::vector<double> xs{7.5};
            const std::vector<double> ys{-3.25};
            const std::vector<double> bs{2.0};

            const Solver solver(xs, ys, bs);
            const Result r = solver.Run();

            // The starting iterate (the weighted center of gravity) is
            // already the optimum, so the next step is a fixed point. The
            // reported cost is b * sqrt(singular_eps2) rather than exactly
            // zero because the d² floor prevents 0/0 at coincident points.
            EXPECT_TRUE(r.converged);
            EXPECT_DOUBLE_EQ(r.location.x, 7.5);
            EXPECT_DOUBLE_EQ(r.location.y, -3.25);
            EXPECT_NEAR(r.cost, 0.0, 1e-8);
        }

        TEST(SolverTest, MaxIterationsCapReported)
        {
            // Eiselt & Sandblom (2004): heavy point pulls the iterate slowly.
            const std::vector<double> xs{100.0, 0.0, 0.0, 1.0, 1.0};
            const std::vector<double> ys{100.0, 0.0, 1.0, 0.0, 1.0};
            const std::vector<double> bs{4.0, 1.0, 1.0, 1.0, 1.0};

            Config cfg;
            cfg.max_iterations = 3;

            const Solver solver(xs, ys, bs, cfg);
            const Result r = solver.Run();

            EXPECT_FALSE(r.converged);
            EXPECT_EQ(r.iterations, 3);
        }

        TEST(SolveFreeFunctionTest, MatchesSolverRun)
        {
            const std::vector<double> xs{0.0, 1.0, 0.5};
            const std::vector<double> ys{0.0, 0.0, std::sqrt(3.0) / 2.0};
            const std::vector<double> bs{1.0, 1.0, 1.0};

            const Result a = Solve(xs, ys, bs);
            const Result b = Solver(xs, ys, bs).Run();

            EXPECT_DOUBLE_EQ(a.location.x, b.location.x);
            EXPECT_DOUBLE_EQ(a.location.y, b.location.y);
            EXPECT_EQ(a.iterations, b.iterations);
            EXPECT_EQ(a.converged, b.converged);
        }
    } // namespace
} // namespace fweisz
