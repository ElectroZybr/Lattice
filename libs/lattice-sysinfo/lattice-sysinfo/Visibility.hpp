#pragma once

#if defined(_WIN32) || defined(__CYGWIN__)
    #if defined(SYSINFO_BUILDING_LIBRARY)
        #define LT_SYSINFO __declspec(dllexport)
    #else
        #define LT_SYSINFO __declspec(dllimport)
    #endif
#elif defined(__GNUC__) || defined(__clang__)
    #define LT_SYSINFO __attribute__((visibility("default")))
#else
    #define LT_SYSINFO
#endif
