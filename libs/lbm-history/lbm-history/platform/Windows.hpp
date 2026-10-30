#pragma once

// Windows: CreateProcessW with pipes (the program is looked up in PATH, .exe implied), outputs
// read by threads; the DNS host name. Text crosses the boundary as UTF-8.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <lattice-tools/Exception.hpp>

#include "../Process.hpp"

namespace Lattice::BmHistory::Platform {

namespace Windows {

inline std::wstring widen(std::string_view text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
    std::wstring result(size_t(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), result.data(), size);
    return result;
}

inline std::string narrow(std::wstring_view text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
    std::string result(size_t(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), result.data(), size, nullptr, nullptr);
    return result;
}

/// One argument as CommandLineToArgvW reads it back: quotes and the backslashes
/// before them escaped.
inline void appendArgument(std::wstring& line, std::wstring_view argument) {
    if (!line.empty()) line += L' ';
    if (!argument.empty() && argument.find_first_of(L" \t\n\v\"") == std::wstring_view::npos) {
        line += argument;
        return;
    }
    line += L'"';
    size_t backslashes = 0;
    for (const wchar_t c : argument) {
        if (c == L'\\') {
            ++backslashes;
            continue;
        }
        line.append(c == L'"' ? backslashes * 2 + 1 : backslashes, L'\\');
        line += c;
        backslashes = 0;
    }
    line.append(backslashes * 2, L'\\');
    line += L'"';
}

class Handle {
public:
    Handle() = default;
    explicit Handle(HANDLE handle) : handle_(handle) {}
    ~Handle() { reset(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;

    HANDLE get() const { return handle_; }
    HANDLE* out() { return &handle_; }
    void reset() {
        if (handle_ && handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
        handle_ = nullptr;
    }

private:
    HANDLE handle_ = nullptr;
};

inline void readAll(HANDLE pipe, std::string& text) {
    char buffer[65536];
    DWORD count = 0;
    while (ReadFile(pipe, buffer, sizeof buffer, &count, nullptr) && count > 0) text.append(buffer, count);
}

}

/// Exit status 127 with no output: the program could not be started.
inline ProcessOutput runProgram(std::string_view program, const std::vector<std::string>& arguments,
                                const std::filesystem::path& directory, std::string_view input) {
    using namespace Windows;
    SECURITY_ATTRIBUTES inheritable{.nLength = sizeof(SECURITY_ATTRIBUTES), .bInheritHandle = TRUE};
    Handle inRead, inWrite, outRead, outWrite, errRead, errWrite;
    if (!CreatePipe(inRead.out(), inWrite.out(), &inheritable, 0) ||
        !CreatePipe(outRead.out(), outWrite.out(), &inheritable, 0) ||
        !CreatePipe(errRead.out(), errWrite.out(), &inheritable, 0))
        throw Exception<ProcessOutput>("{}: CreatePipe failed: error {}", program, GetLastError());
    // The child inherits only its own ends.
    for (HANDLE own : {inWrite.get(), outRead.get(), errRead.get()}) SetHandleInformation(own, HANDLE_FLAG_INHERIT, 0);

    std::wstring line;
    appendArgument(line, widen(program));
    for (const auto& argument : arguments) appendArgument(line, widen(argument));

    STARTUPINFOW startup{.cb = sizeof(STARTUPINFOW), .dwFlags = STARTF_USESTDHANDLES};
    startup.hStdInput = inRead.get();
    startup.hStdOutput = outWrite.get();
    startup.hStdError = errWrite.get();
    PROCESS_INFORMATION process{};
    const std::wstring workdir = directory.empty() ? std::wstring{} : directory.wstring();
    if (!CreateProcessW(nullptr, line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                        workdir.empty() ? nullptr : workdir.c_str(), &startup, &process))
        return {.status = 127};
    Handle processHandle(process.hProcess), threadHandle(process.hThread);
    inRead.reset();
    outWrite.reset();
    errWrite.reset();

    // Write stdin and read both outputs together: a large output must not block
    // git.
    ProcessOutput result;
    std::thread writer([&] {
        DWORD written = 0;
        for (size_t offset = 0; offset < input.size(); offset += written)
            if (!WriteFile(inWrite.get(), input.data() + offset, DWORD(input.size() - offset), &written, nullptr))
                break;
        inWrite.reset();
    });
    std::thread errors([&] { readAll(errRead.get(), result.err); });
    readAll(outRead.get(), result.out);
    errors.join();
    writer.join();

    WaitForSingleObject(processHandle.get(), INFINITE);
    DWORD code = 0;
    result.status = GetExitCodeProcess(processHandle.get(), &code) ? int(code) : -1;
    return result;
}

/// The exit status; nullopt when the program could not be started.
inline std::optional<int> runAttached(const std::filesystem::path& program, const std::vector<std::string>& arguments) {
    std::wstring line;
    Windows::appendArgument(line, program.wstring());
    for (const auto& argument : arguments) Windows::appendArgument(line, Windows::widen(argument));
    STARTUPINFOW startup{.cb = sizeof(STARTUPINFOW)};
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(program.c_str(), line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process))
        return std::nullopt;
    Windows::Handle processHandle(process.hProcess), threadHandle(process.hThread);
    WaitForSingleObject(processHandle.get(), INFINITE);
    DWORD code = 0;
    return GetExitCodeProcess(processHandle.get(), &code) ? int(code) : -1;
}

inline bool interactiveTerminal() {
    DWORD mode = 0;
    return GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &mode) &&
           GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &mode);
}

/// The host name as the OS gives it; empty when unknown.
inline std::string systemHostName() {
    wchar_t buffer[256] = {};
    DWORD size = 256;
    if (!GetComputerNameExW(ComputerNameDnsHostname, buffer, &size)) return {};
    return Windows::narrow(std::wstring_view(buffer, size));
}

}
