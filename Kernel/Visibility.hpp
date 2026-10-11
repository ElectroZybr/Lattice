#pragma once

#if defined(_WIN32) || defined(__CYGWIN__)
    #if defined(LATTICE_BUILDING_LIBRARY)
        #define LATTICE_API __declspec(dllexport)
    #else
        #define LATTICE_API __declspec(dllimport)
    #endif
#elif defined(__GNUC__) || defined(__clang__)
    #define LATTICE_API __attribute__((visibility("default")))
#else
    #define LATTICE_API
#endif
