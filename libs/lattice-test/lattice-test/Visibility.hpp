#pragma once

#if defined(_WIN32) || defined(__CYGWIN__)
    #if defined(LATTICE_TEST_BUILDING_LIBRARY)
        #define LATTICE_TEST_API __declspec(dllexport)
    #else
        #define LATTICE_TEST_API __declspec(dllimport)
    #endif
#elif defined(__GNUC__) || defined(__clang__)
    #define LATTICE_TEST_API __attribute__((visibility("default")))
#else
    #define LATTICE_TEST_API
#endif
