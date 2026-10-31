/**
 @file Charconv.hpp
 @brief std::from_chars, and for floating point also where the standard library lacks it.
*/
#pragma once

#include <cctype>
#include <charconv>
#include <cstddef>
#include <locale>
#include <sstream>
#include <string>
#include <system_error>
#include <type_traits>

namespace Lattice {

/// std::from_chars; Apple libc++ has no floating-point overload, so there a "C"-locale stream reads it.
template<typename T>
std::from_chars_result fromChars(const char* first, const char* last, T& value) {
#if defined(__cpp_lib_to_chars)
    return std::from_chars(first, last, value);
#else
    if constexpr (!std::is_floating_point_v<T>) {
        return std::from_chars(first, last, value);
    } else {
        if (first == last || *first == '+' || std::isspace(static_cast<unsigned char>(*first)))
            return {first, std::errc::invalid_argument};
        std::istringstream in(std::string(first, last));
        in.imbue(std::locale::classic());
        T read{};
        if (!(in >> read)) return {first, std::errc::invalid_argument};
        value = read;
        return {in.eof() ? last : first + static_cast<std::ptrdiff_t>(in.tellg()), std::errc{}};
    }
#endif
}

} // namespace Lattice
