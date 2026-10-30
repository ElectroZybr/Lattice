#pragma once

#if defined(_WIN32) || defined(__CYGWIN__)
    #if defined(BMRUNNER_BUILDING_LIBRARY)
        #define LBM_API __declspec(dllexport)
    #else
        #define LBM_API __declspec(dllimport)
    #endif
#elif defined(__GNUC__) || defined(__clang__)
    #define LBM_API __attribute__((visibility("default")))
#else
    #define LBM_API
#endif
