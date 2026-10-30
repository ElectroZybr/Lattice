#pragma once

#if defined(_WIN32) || defined(__CYGWIN__)
    #if defined(LATTICE_TEST_BUILDING_LIBRARY)
        #define LT_TEST __declspec(dllexport)
    #else
        #define LT_TEST __declspec(dllimport)
    #endif
#elif defined(__GNUC__) || defined(__clang__)
    #define LT_TEST __attribute__((visibility("default")))
#else
    #define LT_TEST
#endif
