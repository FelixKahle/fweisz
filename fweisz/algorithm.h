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

/// @file algorithm.h
/// @brief Weiszfeld 1-median solver in the plane.
///
/// Given a set of demand points @f$ B_j = (x_j, y_j) @f$ with positive weights
/// @f$ b_j @f$, this file finds a point @f$ S^* \in \mathbb{R}^2 @f$ minimizing
/// the total weighted Euclidean cost
/// @f[
///     K(S) \;=\; \sum_{j} b_j \, \lVert S - B_j \rVert_2.
/// @f]
///
/// The classical fixed-point iteration of Weiszfeld is used:
/// @f[
///     S^{(r+1)} \;=\;
///     \frac{\sum_j (b_j / d_j^{(r)})\, B_j}
///          {\sum_j  b_j / d_j^{(r)}},
///     \qquad d_j^{(r)} = \lVert S^{(r)} - B_j \rVert_2.
/// @f]
///
/// The inner accumulation pass is dispatched through a @ref fweisz::SimdLane
/// "SimdLane" so the same source code lights up the widest hardware lane
/// available — AVX2 on x86_64, NEON on AArch64, or the scalar fallback.

#ifndef FWEISZ_ALGORITHM_H_
#define FWEISZ_ALGORITHM_H_

#include "fweisz/base/platform.h"
#include "fweisz/lanes.h"

#include <algorithm>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <span>

namespace fweisz
{
    // -----------------------------------------------------------------------------------------------------------------
    // Public types
    // -----------------------------------------------------------------------------------------------------------------

    /// @brief A point in the Euclidean plane.
    struct Point
    {
        double x; ///< Cartesian x coordinate.
        double y; ///< Cartesian y coordinate.
    };

    /// @brief Outcome of a single Weiszfeld solve.
    struct Result
    {
        Point location; ///< Approximate 1-median @f$ S^* @f$.
        double cost;    ///< Objective @f$ K(S^*) = \sum_j b_j \lVert S^* - B_j \rVert_2 @f$ at @ref location.
        int iterations; ///< Number of Weiszfeld iterations actually performed.
        bool converged; ///< `true` iff the step norm fell below @ref Config::tolerance before @ref Config::max_iterations.
    };

    /// @brief Knobs controlling termination and numerical guards.
    ///
    /// The defaults are conservative: they prioritize correctness over speed and
    /// are appropriate for double-precision inputs in a typical engineering
    /// coordinate range.
    struct Config
    {
        int max_iterations = 500;     ///< Hard cap on iteration count.
        double tolerance = 1e-8;      ///< Convergence threshold on @f$ \lVert S^{(r+1)} - S^{(r)} \rVert_2 @f$.
        double singular_eps2 = 1e-20; ///< Floor for @f$ d_j^2 @f$ to avoid division by zero when @f$ S @f$ coincides with a demand point.
    };

    // -----------------------------------------------------------------------------------------------------------------
    // Solver
    // -----------------------------------------------------------------------------------------------------------------

    /// @brief Weiszfeld 1-median solver.
    ///
    /// Holds non-owning views of three parallel arrays — `xs`, `ys`, `bs` — and
    /// a @ref Config. The solver itself is stateless beyond those views: every
    /// call to @ref Run / @ref RunWith starts a fresh iteration from the
    /// weighted center of gravity.
    ///
    /// @ref Run picks the widest SIMD lane available on the build target;
    /// @ref RunWith accepts any type satisfying the @ref SimdLane concept and is
    /// useful for benchmarking lanes against one another or for differential
    /// testing of the SIMD lane implementations.
    class Solver
    {
    public:
        /// @brief Construct a solver over the supplied demand points and weights.
        ///
        /// @param xs    x coordinates of the demand points.
        /// @param ys    y coordinates of the demand points. Must have the same size as @p xs.
        /// @param bs    Positive weights of the demand points. Must have the same size as @p xs.
        /// @param config Iteration and numerical-guard parameters.
        ///
        /// @pre `xs.size() == ys.size() == bs.size()` and the arrays are non-empty.
        /// @pre Every entry of @p bs is strictly positive.
        Solver(const std::span<const double> xs, const std::span<const double> ys, const std::span<const double> bs,
            const Config& config = {}) noexcept
            : xs_(xs), ys_(ys), bs_(bs), config_(config)
        {
            assert(xs.size() == ys.size() && ys.size() == bs.size());
            assert(!xs.empty());
        }

        /// @brief Run the algorithm with the widest SIMD lane available on this build target.
        /// @return The result of the solve.
        [[nodiscard]] Result Run() const noexcept
        {
            return RunWith<ActiveLane>();
        }

        /// @brief Run the algorithm using an explicit SIMD lane.
        ///
        /// Useful for benchmarking lanes against each other, for forcing the
        /// scalar fallback, or for differential testing.
        ///
        /// @tparam Lane A type satisfying the @ref SimdLane concept.
        /// @return The result of the solve.
        template <SimdLane Lane = ActiveLane>
        [[nodiscard]] Result RunWith() const noexcept
        {
            Point s = CenterOfGravity();
            const double tol2 = config_.tolerance * config_.tolerance;
            double cost = 0.0;
            int iter = 0;
            bool converged = false;

            while (iter < config_.max_iterations)
            {
                const auto acc = Step<Lane>(s.x, s.y);
                cost = acc.cost;
                const Point next = WeightedCentroid(acc);

                const double dx = next.x - s.x;
                const double dy = next.y - s.y;
                s = next;
                ++iter;

                if (dx * dx + dy * dy < tol2)
                {
                    converged = true;
                    break;
                }
            }
            return {s, cost, iter, converged};
        }

        /// @brief Compute the weighted center of gravity of the demand points.
        ///
        /// This is the starting iterate used by @ref Run / @ref RunWith and is
        /// itself the minimizer of the *squared* Euclidean cost, which makes it
        /// a good warm start for the (un-squared) Weiszfeld iteration.
        ///
        /// @return @f$ \bigl(\sum_j b_j x_j,\, \sum_j b_j y_j\bigr) / \sum_j b_j @f$.
        [[nodiscard]] Point CenterOfGravity() const noexcept
        {
            double sx = 0.0, sy = 0.0, sb = 0.0;
            for (std::size_t j = 0; j < xs_.size(); ++j)
            {
                sx += bs_[j] * xs_[j];
                sy += bs_[j] * ys_[j];
                sb += bs_[j];
            }
            const double inv_sb = 1.0 / sb;
            return {sx * inv_sb, sy * inv_sb};
        }

        /// @brief Evaluate the objective @f$ K(S) = \sum_j b_j \lVert S - B_j \rVert_2 @f$.
        ///
        /// Implemented by running a single @ref Step pass and discarding the
        /// numerator/denominator accumulators — this reuses the SIMD body of
        /// the iteration and so picks up the same vectorization. The same
        /// singularity guard @ref Config::singular_eps2 is applied as during
        /// iteration, so the value returned here matches @ref Result::cost
        /// reported for the same query point.
        ///
        /// @param s Query point.
        /// @return @f$ K(s) @f$.
        [[nodiscard]] double Cost(const Point s) const noexcept
        {
            return Step<ActiveLane>(s.x, s.y).cost;
        }

    private:
        /// @brief Per-iteration partial sums of the Weiszfeld update.
        ///
        /// Holds the unnormalized numerator (@ref num_x, @ref num_y), the
        /// denominator (@ref den), and the current objective value
        /// @f$ K(S) @f$ (@ref cost). The next iterate is
        /// @f$ (\text{num\_x}/\text{den},\, \text{num\_y}/\text{den}) @f$.
        struct Accumulators
        {
            double num_x; ///< @f$ \sum_j (b_j / d_j)\, x_j @f$.
            double num_y; ///< @f$ \sum_j (b_j / d_j)\, y_j @f$.
            double den;   ///< @f$ \sum_j b_j / d_j @f$.
            double cost;  ///< @f$ \sum_j b_j \, d_j = K(S) @f$.
        };

        /// @brief Convert raw Weiszfeld accumulators into the next iterate.
        ///
        /// @param acc Partial sums produced by @ref Step.
        /// @return The weighted centroid implied by @p acc.
        [[nodiscard]] FWEISZ_FORCE_INLINE static Point WeightedCentroid(const Accumulators& acc) noexcept
        {
            const double inv_den = 1.0 / acc.den;
            return {acc.num_x * inv_den, acc.num_y * inv_den};
        }

        /// @brief Vectorized body of a single Weiszfeld step.
        ///
        /// Processes whole `Lane::kLanes`-wide chunks of the demand arrays. The
        /// partial sums returned in @p acc_* are vector-valued and still need
        /// horizontal reduction by the caller; the integer @p j is advanced past
        /// the SIMD prefix.
        ///
        /// @tparam Lane SIMD lane satisfying @ref SimdLane.
        /// @param[in]     v_sx,v_sy  Broadcasted current iterate.
        /// @param[in]     v_eps2     Broadcasted singularity guard for @f$ d^2 @f$.
        /// @param[in]     v_one      Broadcasted constant 1.0 (passed in to keep the hot loop branch-free).
        /// @param[in,out] j          Index into the demand arrays; advanced by the number of lanes processed.
        /// @param[in,out] acc_nx,acc_ny,acc_de,acc_k  Lane-wise accumulators (added to).
        template <SimdLane Lane>
        FWEISZ_FORCE_INLINE void AccumulateSimd(Lane v_sx, Lane v_sy, Lane v_eps2, Lane v_one, std::size_t& j,
            Lane& acc_nx, Lane& acc_ny, Lane& acc_de, Lane& acc_k) const noexcept
        {
            const double* FWEISZ_RESTRICT xs = xs_.data();
            const double* FWEISZ_RESTRICT ys = ys_.data();
            const double* FWEISZ_RESTRICT bs = bs_.data();
            const std::size_t n_main = xs_.size() & ~(Lane::kLanes - 1);

            for (; j < n_main; j += Lane::kLanes)
            {
                const Lane x = Lane::Load(xs + j);
                const Lane y = Lane::Load(ys + j);
                const Lane b = Lane::Load(bs + j);

                const Lane dx = v_sx - x;
                const Lane dy = v_sy - y;
                const Lane d2 = (dx * dx).Fma(dy, dy).Max(v_eps2); // dx² + dy², clipped from below
                const Lane d = d2.Sqrt();
                const Lane w = b * (v_one / d); // w_j = b_j / d_j

                acc_nx = acc_nx.Fma(w, x); // += w·x
                acc_ny = acc_ny.Fma(w, y); // += w·y
                acc_de = acc_de + w;       // += w
                acc_k = acc_k.Fma(b, d);   // += b·d
            }
        }

        /// @brief Scalar remainder of a Weiszfeld step.
        ///
        /// Handles the at-most `Lane::kLanes − 1` tail elements that the
        /// vectorized body could not consume. The body is empty when
        /// `Lane::kLanes == 1`, in which case the scalar tail performs the whole
        /// pass.
        ///
        /// @param[in]     sx,sy      Current iterate.
        /// @param[in]     eps2       Singularity guard for @f$ d^2 @f$.
        /// @param[in,out] j          Index into the demand arrays; advanced to `xs.size()`.
        /// @param[in,out] nx,ny,de,k Scalar accumulators (added to).
        FWEISZ_FORCE_INLINE void AccumulateScalar(const double sx, const double sy, const double eps2, std::size_t& j,
            double& nx, double& ny, double& de, double& k) const noexcept
        {
            const double* FWEISZ_RESTRICT xs = xs_.data();
            const double* FWEISZ_RESTRICT ys = ys_.data();
            const double* FWEISZ_RESTRICT bs = bs_.data();
            const std::size_t n = xs_.size();

            for (; j < n; ++j)
            {
                const double dx = sx - xs[j];
                const double dy = sy - ys[j];
                const double d2 = std::max(dx * dx + dy * dy, eps2);
                const double d = std::sqrt(d2);
                const double w = bs[j] / d;
                nx += w * xs[j];
                ny += w * ys[j];
                de += w;
                k += bs[j] * d;
            }
        }

        /// @brief One Weiszfeld step expressed via the @ref SimdLane wrapper.
        ///
        /// Computes
        /// @f$ \bigl(\sum_j b_j x_j / d_j,\ \sum_j b_j y_j / d_j,\
        ///          \sum_j b_j / d_j,\ \sum_j b_j d_j\bigr) @f$
        /// in a single fused pass over the input, with the @f$ d^2 @f$
        /// singularity guard taken from @ref Config::singular_eps2. The
        /// implementation is split into a vectorized body (@ref AccumulateSimd)
        /// and a scalar tail (@ref AccumulateScalar); both are force-inlined
        /// so the codegen is identical to a single open-coded function.
        ///
        /// @tparam Lane SIMD lane satisfying @ref SimdLane.
        /// @param sx,sy Current iterate.
        /// @return The four partial sums (reduced to scalars).
        template <SimdLane Lane>
        [[nodiscard]] FWEISZ_FORCE_INLINE Accumulators Step(double sx, double sy) const noexcept
        {
            static_assert(std::has_single_bit(Lane::kLanes), "kLanes must be a power of 2");

            const Lane v_sx = Lane::Broadcast(sx);
            const Lane v_sy = Lane::Broadcast(sy);
            const Lane v_eps2 = Lane::Broadcast(config_.singular_eps2);
            const Lane v_one = Lane::Broadcast(1.0);

            Lane acc_nx = Lane::Zero();
            Lane acc_ny = Lane::Zero();
            Lane acc_de = Lane::Zero();
            Lane acc_k = Lane::Zero();

            std::size_t j = 0;
            AccumulateSimd<Lane>(v_sx, v_sy, v_eps2, v_one, j, acc_nx, acc_ny, acc_de, acc_k);

            double nx = acc_nx.HorizontalSum();
            double ny = acc_ny.HorizontalSum();
            double de = acc_de.HorizontalSum();
            double k = acc_k.HorizontalSum();

            AccumulateScalar(sx, sy, config_.singular_eps2, j, nx, ny, de, k);

            return {nx, ny, de, k};
        }

        std::span<const double> xs_; ///< Non-owning view of demand-point x coordinates.
        std::span<const double> ys_; ///< Non-owning view of demand-point y coordinates.
        std::span<const double> bs_; ///< Non-owning view of demand-point weights (all > 0).
        Config config_;              ///< Iteration / numerical-guard parameters.
    };

    // -----------------------------------------------------------------------------------------------------------------
    // Free function shortcut
    // -----------------------------------------------------------------------------------------------------------------

    /// @brief Construct a @ref Solver and run it with the active SIMD lane in one expression.
    ///
    /// @param xs,ys,bs Demand points and weights (see @ref Solver::Solver).
    /// @param config   Iteration / numerical-guard parameters.
    /// @return The result of the solve.
    [[nodiscard]] inline Result Solve(const std::span<const double> xs, const std::span<const double> ys, const std::span<const double> bs, const Config& config = {}) noexcept
    {
        return Solver(xs, ys, bs, config).Run();
    }
} // namespace fweisz

#endif // FWEISZ_ALGORITHM_H_
