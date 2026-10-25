#pragma once

#if defined(_WIN32) || defined(__CYGWIN__)
    #if defined(SYSINFO_BUILDING_LIBRARY)
        #define SYSINFO_API __declspec(dllexport)
    #else
        #define SYSINFO_API __declspec(dllimport)
    #endif
#elif defined(__GNUC__) || defined(__clang__)
    #define SYSINFO_API __attribute__((visibility("default")))
#else
    #define SYSINFO_API
#endif
