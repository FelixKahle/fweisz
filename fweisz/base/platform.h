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

/// @file platform.h
/// @brief Compiler / toolchain feature shims used across fweisz.
///
/// Pure-preprocessor header. Defines:
///   - The fweisz version string.
///   - Build-mode flags (debug vs. release).
///   - Cross-compiler attribute macros for force-inlining, restrict pointers,
///     and symbol visibility (DLL export / import / hidden).
///
/// Every macro is overridable: define it before including this header and the
/// header will respect the existing definition.

#ifndef FWEISZ_BASE_PLATFORM_H_
#define FWEISZ_BASE_PLATFORM_H_

// ---------------------------------------------------------------------------------------------------------------------
// fweisz version
// ---------------------------------------------------------------------------------------------------------------------

/// @brief Human-readable version of the fweisz library.
#define FWEISZ_VERSION_STRING "0.0.1"

// ---------------------------------------------------------------------------------------------------------------------
// C++ version
// ---------------------------------------------------------------------------------------------------------------------

#ifndef __cplusplus
#error "This library requires C++. Please compile with a C++ compiler."
#endif

#ifndef FWEISZ_CPLUSPLUS_LANG
#if defined(_MSVC_LANG)
/// @brief The active C++ language standard.
///
/// MSVC reports the standard via `_MSVC_LANG` rather than `__cplusplus` unless
/// `/Zc:__cplusplus` is set, so we prefer the former when present.
#define FWEISZ_CPLUSPLUS_LANG _MSVC_LANG
#elif defined(__cplusplus)
/// @copydoc FWEISZ_CPLUSPLUS_LANG
#define FWEISZ_CPLUSPLUS_LANG __cplusplus
#endif
#endif // FWEISZ_CPLUSPLUS_LANG

// ---------------------------------------------------------------------------------------------------------------------
// Debug + Release
// ---------------------------------------------------------------------------------------------------------------------

#ifdef NDEBUG
/// @brief `1` when building with `NDEBUG` (i.e. assertions disabled), `0` otherwise.
#define FWEISZ_BUILD_DEBUG 0
/// @brief Complement of @ref FWEISZ_BUILD_DEBUG.
#define FWEISZ_BUILD_RELEASE 1
#else
#define FWEISZ_BUILD_DEBUG 1
#define FWEISZ_BUILD_RELEASE 0
#endif

#ifndef FWEISZ_RUN_DEBUG_ONLY
#if FWEISZ_BUILD_DEBUG
/// @brief Expand @p x in debug builds and to nothing in release builds.
///
/// Use to guard expensive verification code that should never run in shipped
/// binaries, e.g. `FWEISZ_RUN_DEBUG_ONLY(VerifyInvariants());`.
#define FWEISZ_RUN_DEBUG_ONLY(x) x
#else
/// @copydoc FWEISZ_RUN_DEBUG_ONLY
#define FWEISZ_RUN_DEBUG_ONLY(x)
#endif
#endif

// ---------------------------------------------------------------------------------------------------------------------
// Force inline
// ---------------------------------------------------------------------------------------------------------------------

#ifndef FWEISZ_ALLOW_FORCE_INLINE
/// @brief When `1`, @ref FWEISZ_FORCE_INLINE expands to a compiler-specific force-inline attribute.
///        When `0`, it falls back to plain `inline` (useful for debugging codegen).
#define FWEISZ_ALLOW_FORCE_INLINE 1
#endif // FWEISZ_ALLOW_FORCE_INLINE

#ifndef FWEISZ_FORCE_INLINE
#if FWEISZ_ALLOW_FORCE_INLINE
#if defined(_MSC_VER) || defined(__INTEL_COMPILER) || (defined(__INTEL_LLVM_COMPILER) && defined(_WIN32))
// MSVC and Intel on Windows.
/// @brief Strong hint to inline a function regardless of the compiler's heuristics.
#define FWEISZ_FORCE_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__) || defined(__CYGWIN__) || defined(__IBMCPP__) || \
    defined(__SUNPRO_CC) || defined(__NVCOMPILER) || defined(__ARMCC_VERSION)
// GCC, Clang, Cygwin, IBM, SunPro, NVIDIA, and ARM.
/// @copydoc FWEISZ_FORCE_INLINE
#define FWEISZ_FORCE_INLINE inline __attribute__((always_inline))
#else
// Fallback for everything else.
/// @copydoc FWEISZ_FORCE_INLINE
#define FWEISZ_FORCE_INLINE inline
#endif
#else
// Force inline disabled by user.
/// @copydoc FWEISZ_FORCE_INLINE
#define FWEISZ_FORCE_INLINE inline
#endif
#endif

// ---------------------------------------------------------------------------------------------------------------------
// Restrict
// ---------------------------------------------------------------------------------------------------------------------

#ifndef FWEISZ_RESTRICT
#if defined(_MSC_VER) || defined(__INTEL_COMPILER) || defined(__INTEL_LLVM_COMPILER)
// MSVC and Intel.
/// @brief Compiler-specific `restrict` qualifier promising that the pointed-to
///        memory is not aliased by any other pointer in scope.
///
/// Expands to nothing on compilers that lack a `restrict`-equivalent. Pointer
/// aliasing assumptions are checked at the call site, not here.
#define FWEISZ_RESTRICT __restrict
#elif defined(__GNUC__) || defined(__clang__) || defined(__CYGWIN__) || defined(__IBMCPP__) || \
    defined(__SUNPRO_CC) || defined(__NVCOMPILER) || defined(__ARMCC_VERSION)
// GCC, Clang and friends.
/// @copydoc FWEISZ_RESTRICT
#define FWEISZ_RESTRICT __restrict
#else
// Fallback.
/// @copydoc FWEISZ_RESTRICT
#define FWEISZ_RESTRICT
#endif
#endif

// ---------------------------------------------------------------------------------------------------------------------
// Symbol visibility
// ---------------------------------------------------------------------------------------------------------------------

#if !defined(FWEISZ_SYMBOL_EXPORT) && !defined(FWEISZ_SYMBOL_IMPORT) && !defined(FWEISZ_SYMBOL_LOCAL)
#if defined(_WIN32) || defined(__CYGWIN__)
/// @brief Mark a symbol as exported from a shared library.
#define FWEISZ_SYMBOL_EXPORT __declspec(dllexport)
/// @brief Mark a symbol as imported from a shared library.
#define FWEISZ_SYMBOL_IMPORT __declspec(dllimport)
/// @brief Mark a symbol as having local (translation-unit) visibility.
#define FWEISZ_SYMBOL_LOCAL
#elif defined(__GNUC__) || defined(__clang__) || defined(__INTEL_LLVM_COMPILER) || defined(__NVCOMPILER)
/// @copydoc FWEISZ_SYMBOL_EXPORT
#define FWEISZ_SYMBOL_EXPORT __attribute__((visibility("default")))
/// @copydoc FWEISZ_SYMBOL_IMPORT
#define FWEISZ_SYMBOL_IMPORT __attribute__((visibility("default")))
/// @copydoc FWEISZ_SYMBOL_LOCAL
#define FWEISZ_SYMBOL_LOCAL  __attribute__((visibility("hidden")))
#elif defined(__SUNPRO_CC)
/// @copydoc FWEISZ_SYMBOL_EXPORT
#define FWEISZ_SYMBOL_EXPORT __global
/// @copydoc FWEISZ_SYMBOL_IMPORT
#define FWEISZ_SYMBOL_IMPORT __global
/// @copydoc FWEISZ_SYMBOL_LOCAL
#define FWEISZ_SYMBOL_LOCAL  __hidden
#else
/// @copydoc FWEISZ_SYMBOL_EXPORT
#define FWEISZ_SYMBOL_EXPORT
/// @copydoc FWEISZ_SYMBOL_IMPORT
#define FWEISZ_SYMBOL_IMPORT
/// @copydoc FWEISZ_SYMBOL_LOCAL
#define FWEISZ_SYMBOL_LOCAL
#endif
#endif

#endif // FWEISZ_BASE_PLATFORM_H_
