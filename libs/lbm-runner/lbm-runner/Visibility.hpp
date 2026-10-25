#pragma once

#if defined(_WIN32) || defined(__CYGWIN__)
    #if defined(BMRUNNER_BUILDING_LIBRARY)
        #define BMRUNNER_API __declspec(dllexport)
    #else
        #define BMRUNNER_API __declspec(dllimport)
    #endif
#elif defined(__GNUC__) || defined(__clang__)
    #define BMRUNNER_API __attribute__((visibility("default")))
#else
    #define BMRUNNER_API
#endif
