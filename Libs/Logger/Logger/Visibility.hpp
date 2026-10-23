#pragma once

#if defined(_WIN32) || defined(__CYGWIN__)
    #if defined(LOGGER_BUILDING_LIBRARY)
        #define LOGGER_API __declspec(dllexport)
    #else
        #define LOGGER_API __declspec(dllimport)
    #endif
#elif defined(__GNUC__) || defined(__clang__)
    #define LOGGER_API __attribute__((visibility("default")))
#else
    #define LOGGER_API
#endif
