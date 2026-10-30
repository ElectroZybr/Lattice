#pragma once

// Compiler attributes and extensions: the only place with compiler macros.

#if defined(_MSC_VER)
#define RESTRICT __restrict
#elif defined(__GNUC__) || defined(__clang__)
#define RESTRICT __restrict__
#else
#define RESTRICT
#endif

// The MSVC ABI ignores the standard attribute, clang-cl included.
#if defined(_MSC_VER)
#define LATTICE_NO_UNIQUE_ADDRESS [[msvc::no_unique_address]]
#else
#define LATTICE_NO_UNIQUE_ADDRESS [[no_unique_address]]
#endif
