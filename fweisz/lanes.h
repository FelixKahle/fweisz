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

/// @file lanes.h
/// @brief Thin, zero-cost wrappers around fixed-width SIMD vectors of `double`.
///
/// The wrappers implement just enough surface area to express the inner loop of
/// Weiszfeld's iteration in a SIMD-agnostic way (see @ref fweisz::SimdLane).
/// Every method is `FWEISZ_FORCE_INLINE` and operates on a single underlying
/// intrinsic vector member, so the abstraction compiles down to exactly the
/// handwritten intrinsic sequence — no spills, no extra moves.
///
/// Three lane types are provided:
///   - @ref fweisz::ScalarLane — width 1, always available.
///   - @ref fweisz::Avx2Lane   — width 4 doubles on x86_64 with AVX2 + FMA.
///   - @ref fweisz::NeonLane   — width 2 doubles on AArch64 NEON.
///
/// The type alias @ref fweisz::ActiveLane resolves to the widest lane available
/// on the build target.

#ifndef FWEISZ_LANES_H_
#define FWEISZ_LANES_H_

#include "fweisz/base/platform.h"
#include "fweisz/base/simd.h"

#include <algorithm>
#include <cmath>
#include <concepts>
#include <cstddef>

namespace fweisz
{
    // -----------------------------------------------------------------------------------------------------------------
    // SimdLane concept
    // -----------------------------------------------------------------------------------------------------------------

    /// @brief A fixed-width SIMD value type with arithmetic operators and a few named primitives.
    ///
    /// Exposes only the operations needed by Weiszfeld's iteration. Other
    /// algorithms may need a richer surface (blend, reciprocal-sqrt,
    /// compare-mask, ...) and should extend this concept rather than reuse it
    /// as-is.
    ///
    /// `Fma` uses the accumulator convention: `acc.Fma(b, c) == acc + b * c`.
    template <typename L>
    concept SimdLane = std::default_initializable<L> && std::copyable<L> && requires(L v, double s, const double* p)
    {
        { L::kLanes }          -> std::convertible_to<std::size_t>;
        { L::Broadcast(s) }    -> std::same_as<L>;
        { L::Zero() }          -> std::same_as<L>;
        { L::Load(p) }         -> std::same_as<L>;
        { v + v }              -> std::same_as<L>;
        { v - v }              -> std::same_as<L>;
        { v * v }              -> std::same_as<L>;
        { v / v }              -> std::same_as<L>;
        { v.Max(v) }           -> std::same_as<L>;
        { v.Sqrt() }           -> std::same_as<L>;
        { v.Fma(v, v) }        -> std::same_as<L>;
        { v.HorizontalSum() }  -> std::same_as<double>;
    };

    // -----------------------------------------------------------------------------------------------------------------
    // ScalarLane (width 1)
    // -----------------------------------------------------------------------------------------------------------------

    /// @brief Width-1 SIMD lane (i.e. plain `double`).
    ///
    /// Always available; used by the scalar tail of vectorized loops and as the
    /// fallback when no handwritten SIMD lane matches the target ISA.
    ///
    /// `Fma` is implemented as `a + b * c` rather than `std::fma` so the
    /// compiler emits a hardware FMA when available and plain mul+add
    /// otherwise. `std::fma` would force a slow correctly-rounded library call
    /// on hosts without hardware FMA.
    class ScalarLane
    {
    public:
        /// @brief Underlying scalar type.
        using Underlying = double;

        /// @brief Number of doubles processed per lane operation.
        static constexpr std::size_t kLanes = 1;

        /// @brief Default-construct an uninitialized lane.
        ScalarLane() = default;

        /// @brief Construct a lane holding the given scalar value.
        explicit constexpr ScalarLane(const double v) noexcept : v_(v)
        {
        }

        /// @brief Lane filled with @p x.
        static FWEISZ_FORCE_INLINE ScalarLane Broadcast(const double x) noexcept
        {
            return ScalarLane{x};
        }

        /// @brief Lane filled with zero.
        static FWEISZ_FORCE_INLINE ScalarLane Zero() noexcept
        {
            return ScalarLane{0.0};
        }

        /// @brief Load one double from @p p.
        static FWEISZ_FORCE_INLINE ScalarLane Load(const double* p) noexcept
        {
            return ScalarLane{*p};
        }

        /// @brief Lane-wise addition.
        FWEISZ_FORCE_INLINE ScalarLane operator+(const ScalarLane r) const noexcept
        {
            return ScalarLane{v_ + r.v_};
        }

        /// @brief Lane-wise subtraction.
        FWEISZ_FORCE_INLINE ScalarLane operator-(const ScalarLane r) const noexcept
        {
            return ScalarLane{v_ - r.v_};
        }

        /// @brief Lane-wise multiplication.
        FWEISZ_FORCE_INLINE ScalarLane operator*(const ScalarLane r) const noexcept
        {
            return ScalarLane{v_ * r.v_};
        }

        /// @brief Lane-wise division.
        FWEISZ_FORCE_INLINE ScalarLane operator/(const ScalarLane r) const noexcept
        {
            return ScalarLane{v_ / r.v_};
        }

        /// @brief Lane-wise maximum.
        [[nodiscard]] FWEISZ_FORCE_INLINE ScalarLane Max(const ScalarLane r) const noexcept
        {
            return ScalarLane{std::max(v_, r.v_)};
        }

        /// @brief Lane-wise square root.
        [[nodiscard]] FWEISZ_FORCE_INLINE ScalarLane Sqrt() const noexcept
        {
            return ScalarLane{std::sqrt(v_)};
        }

        /// @brief Fused multiply-add in accumulator form: `*this + a * b`.
        [[nodiscard]] FWEISZ_FORCE_INLINE ScalarLane Fma(const ScalarLane a, const ScalarLane b) const noexcept
        {
            return ScalarLane{v_ + a.v_ * b.v_};
        }

        /// @brief Reduce the lane to a single scalar (the value itself).
        [[nodiscard]] FWEISZ_FORCE_INLINE double HorizontalSum() const noexcept
        {
            return v_;
        }

    private:
        double v_{};
    };

    // -----------------------------------------------------------------------------------------------------------------
    // Avx2Lane (width 4 doubles, x86_64)
    // -----------------------------------------------------------------------------------------------------------------

#if FWEISZ_HAS_AVX2
    /// @brief Width-4 SIMD lane backed by AVX2 + FMA intrinsics (`__m256d`).
    class Avx2Lane
    {
    public:
        /// @brief Underlying intrinsic vector type.
        using Underlying = __m256d;

        /// @brief Number of doubles processed per lane operation.
        static constexpr std::size_t kLanes = 4;

        /// @brief Default-construct an uninitialized lane.
        Avx2Lane() = default;

        /// @brief Construct a lane holding the given intrinsic vector.
        explicit Avx2Lane(__m256d v) noexcept : v_(v)
        {
        }

        /// @brief Lane with all four elements set to @p x.
        static FWEISZ_FORCE_INLINE Avx2Lane Broadcast(double x) noexcept
        {
            return Avx2Lane{_mm256_set1_pd(x)};
        }

        /// @brief Lane with all four elements set to zero.
        static FWEISZ_FORCE_INLINE Avx2Lane Zero() noexcept
        {
            return Avx2Lane{_mm256_setzero_pd()};
        }

        /// @brief Unaligned load of four doubles from @p p.
        static FWEISZ_FORCE_INLINE Avx2Lane Load(const double* p) noexcept
        {
            return Avx2Lane{_mm256_loadu_pd(p)};
        }

        /// @brief Lane-wise addition.
        FWEISZ_FORCE_INLINE Avx2Lane operator+(Avx2Lane r) const noexcept
        {
            return Avx2Lane{_mm256_add_pd(v_, r.v_)};
        }

        /// @brief Lane-wise subtraction.
        FWEISZ_FORCE_INLINE Avx2Lane operator-(Avx2Lane r) const noexcept
        {
            return Avx2Lane{_mm256_sub_pd(v_, r.v_)};
        }

        /// @brief Lane-wise multiplication.
        FWEISZ_FORCE_INLINE Avx2Lane operator*(Avx2Lane r) const noexcept
        {
            return Avx2Lane{_mm256_mul_pd(v_, r.v_)};
        }

        /// @brief Lane-wise division.
        FWEISZ_FORCE_INLINE Avx2Lane operator/(Avx2Lane r) const noexcept
        {
            return Avx2Lane{_mm256_div_pd(v_, r.v_)};
        }

        /// @brief Lane-wise maximum.
        [[nodiscard]] FWEISZ_FORCE_INLINE Avx2Lane Max(Avx2Lane r) const noexcept
        {
            return Avx2Lane{_mm256_max_pd(v_, r.v_)};
        }

        /// @brief Lane-wise square root.
        [[nodiscard]] FWEISZ_FORCE_INLINE Avx2Lane Sqrt() const noexcept
        {
            return Avx2Lane{_mm256_sqrt_pd(v_)};
        }

        /// @brief Fused multiply-add in accumulator form: `*this + a * b`.
        [[nodiscard]] FWEISZ_FORCE_INLINE Avx2Lane Fma(Avx2Lane a, Avx2Lane b) const noexcept
        {
            return Avx2Lane{_mm256_fmadd_pd(a.v_, b.v_, v_)};
        }

        /// @brief Reduce the lane to a single scalar by summing its four elements.
        [[nodiscard]] FWEISZ_FORCE_INLINE double HorizontalSum() const noexcept
        {
            const __m128d lo = _mm256_castpd256_pd128(v_);
            const __m128d hi = _mm256_extractf128_pd(v_, 1);
            const __m128d s = _mm_add_pd(lo, hi);
            return _mm_cvtsd_f64(_mm_add_sd(s, _mm_unpackhi_pd(s, s)));
        }

    private:
        __m256d v_{};
    };
#endif // FWEISZ_HAS_AVX2

    // -----------------------------------------------------------------------------------------------------------------
    // NeonLane (width 2 doubles, AArch64)
    // -----------------------------------------------------------------------------------------------------------------

#if FWEISZ_HAS_NEON
    /// @brief Width-2 SIMD lane backed by AArch64 NEON intrinsics (`float64x2_t`).
    class NeonLane
    {
    public:
        /// @brief Underlying intrinsic vector type.
        using Underlying = float64x2_t;

        /// @brief Number of doubles processed per lane operation.
        static constexpr std::size_t kLanes = 2;

        /// @brief Default-construct an uninitialized lane.
        NeonLane() = default;

        /// @brief Construct a lane holding the given intrinsic vector.
        explicit NeonLane(float64x2_t v) noexcept : v_(v)
        {
        }

        /// @brief Lane with both elements set to @p x.
        static FWEISZ_FORCE_INLINE NeonLane Broadcast(const double x) noexcept
        {
            return NeonLane{vdupq_n_f64(x)};
        }

        /// @brief Lane with both elements set to zero.
        static FWEISZ_FORCE_INLINE NeonLane Zero() noexcept
        {
            return NeonLane{vdupq_n_f64(0.0)};
        }

        /// @brief Unaligned load of two doubles from @p p.
        static FWEISZ_FORCE_INLINE NeonLane Load(const double* p) noexcept
        {
            return NeonLane{vld1q_f64(p)};
        }

        /// @brief Lane-wise addition.
        FWEISZ_FORCE_INLINE NeonLane operator+(const NeonLane r) const noexcept
        {
            return NeonLane{vaddq_f64(v_, r.v_)};
        }

        /// @brief Lane-wise subtraction.
        FWEISZ_FORCE_INLINE NeonLane operator-(const NeonLane r) const noexcept
        {
            return NeonLane{vsubq_f64(v_, r.v_)};
        }

        /// @brief Lane-wise multiplication.
        FWEISZ_FORCE_INLINE NeonLane operator*(const NeonLane r) const noexcept
        {
            return NeonLane{vmulq_f64(v_, r.v_)};
        }

        /// @brief Lane-wise division.
        FWEISZ_FORCE_INLINE NeonLane operator/(const NeonLane r) const noexcept
        {
            return NeonLane{vdivq_f64(v_, r.v_)};
        }

        /// @brief Lane-wise maximum.
        [[nodiscard]] FWEISZ_FORCE_INLINE NeonLane Max(const NeonLane r) const noexcept
        {
            return NeonLane{vmaxq_f64(v_, r.v_)};
        }

        /// @brief Lane-wise square root.
        [[nodiscard]] FWEISZ_FORCE_INLINE NeonLane Sqrt() const noexcept
        {
            return NeonLane{vsqrtq_f64(v_)};
        }

        /// @brief Fused multiply-add in accumulator form: `*this + a * b`.
        [[nodiscard]] FWEISZ_FORCE_INLINE NeonLane Fma(const NeonLane a, const NeonLane b) const noexcept
        {
            return NeonLane{vfmaq_f64(v_, a.v_, b.v_)};
        }

        /// @brief Reduce the lane to a single scalar by summing its two elements.
        [[nodiscard]] FWEISZ_FORCE_INLINE double HorizontalSum() const noexcept
        {
            return vaddvq_f64(v_);
        }

    private:
        float64x2_t v_{};
    };
#endif // FWEISZ_HAS_NEON

    // -----------------------------------------------------------------------------------------------------------------
    // ActiveLane — widest SIMD lane available on the build target
    // -----------------------------------------------------------------------------------------------------------------

    /// @brief Alias for the widest SIMD lane available on this build target.
    ///
    /// Resolution order: @ref Avx2Lane → @ref NeonLane → @ref ScalarLane.
#if FWEISZ_HAS_AVX2
    using ActiveLane = Avx2Lane;
#elif FWEISZ_HAS_NEON
    using ActiveLane = NeonLane;
#else
    using ActiveLane = ScalarLane;
#endif
} // namespace fweisz

#endif // FWEISZ_LANES_H_
