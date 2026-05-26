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

/// @file simd.h
/// @brief SIMD ISA detection and intrinsic-header inclusion.
///
/// Defines two 0/1 macros mirroring the `FWEISZ_BUILD_*` style:
///
///   - @ref FWEISZ_HAS_AVX2 — AVX2 + FMA available (x86_64 with `-mavx2 -mfma`,
///     `/arch:AVX2`, or `-march=native` on a capable host).
///   - @ref FWEISZ_HAS_NEON — Advanced SIMD with `float64` available (AArch64).
///
/// The corresponding intrinsic header is included automatically when a feature
/// is detected, so a downstream translation unit only needs
/// `#include "fweisz/base/simd.h"` to get both the macros and the intrinsic
/// types.
///
/// No C++ types or namespaces are introduced here — this header is pure
/// preprocessor + the standard vendor intrinsic headers it gates. Lane
/// wrappers live with the algorithm that consumes them (see
/// `fweisz/lanes.h`).

#ifndef FWEISZ_BASE_SIMD_H_
#define FWEISZ_BASE_SIMD_H_

// ---------------------------------------------------------------------------------------------------------------------
// SIMD ISA detection
// ---------------------------------------------------------------------------------------------------------------------

#ifndef FWEISZ_HAS_AVX2
// MSVC's /arch:AVX2 enables AVX2+FMA together and defines only __AVX2__;
// GCC/Clang require -mavx2 -mfma (both implied by -march=native on capable hosts) and define
// __AVX2__ and __FMA__ independently.
#if defined(__AVX2__) && (defined(_MSC_VER) || defined(__FMA__))
/// @brief `1` when AVX2 + FMA intrinsics are available on this build target, `0` otherwise.
#define FWEISZ_HAS_AVX2 1
#else
/// @copydoc FWEISZ_HAS_AVX2
#define FWEISZ_HAS_AVX2 0
#endif
#endif // FWEISZ_HAS_AVX2

#ifndef FWEISZ_HAS_NEON
#if defined(__aarch64__) || defined(_M_ARM64)
/// @brief `1` when AArch64 NEON intrinsics (including `float64x2_t`) are available, `0` otherwise.
#define FWEISZ_HAS_NEON 1
#else
/// @copydoc FWEISZ_HAS_NEON
#define FWEISZ_HAS_NEON 0
#endif
#endif // FWEISZ_HAS_NEON

// ---------------------------------------------------------------------------------------------------------------------
// Intrinsic headers (gated by the detection macros above)
// ---------------------------------------------------------------------------------------------------------------------

#if FWEISZ_HAS_AVX2
#include <immintrin.h>
#endif

#if FWEISZ_HAS_NEON
#include <arm_neon.h>
#endif

#endif // FWEISZ_BASE_SIMD_H_
