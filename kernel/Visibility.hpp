#pragma once

#if defined(_WIN32) || defined(__CYGWIN__)
    #if defined(LATTICE_BUILDING_LIBRARY)
        #define LT_API __declspec(dllexport)
    #else
        #define LT_API __declspec(dllimport)
    #endif
#elif defined(__GNUC__) || defined(__clang__)
    #define LT_API __attribute__((visibility("default")))
#else
    #define LT_API
#endif

// Entry points a package library exports (a Windows DLL exports nothing unmarked).
#if defined(_WIN32) || defined(__CYGWIN__)
    #define LT_PACKAGE __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
    #define LT_PACKAGE __attribute__((visibility("default")))
#else
    #define LT_PACKAGE
#endif
