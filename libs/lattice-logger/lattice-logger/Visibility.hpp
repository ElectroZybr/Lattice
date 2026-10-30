#pragma once

#if defined(_WIN32) || defined(__CYGWIN__)
    #if defined(LOGGER_BUILDING_LIBRARY)
        #define LT_LOGGER __declspec(dllexport)
    #else
        #define LT_LOGGER __declspec(dllimport)
    #endif
#elif defined(__GNUC__) || defined(__clang__)
    #define LT_LOGGER __attribute__((visibility("default")))
#else
    #define LT_LOGGER
#endif
