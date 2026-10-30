#pragma once

// Windows: CreateProcessW in a job object (a timeout kills the whole tree), stdin from NUL, the
// output appended to the log; the real directory of the program first in its PATH, so a linked
// suite finds its DLLs as $ORIGIN does on Linux; an executable is an .exe file. Text crosses the
// boundary as UTF-8.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <optional>
#include <string_view>

#include <lattice-tools/Exception.hpp>

namespace Lattice::Testing::Platform {

namespace Windows {

inline std::wstring widen(std::string_view text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
    std::wstring result(size_t(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), result.data(), size);
    return result;
}

/// One argument as CommandLineToArgvW reads it back: quotes and the backslashes before them escaped.
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

/// This environment with `directory` first in PATH, as CreateProcessW takes it.
inline std::wstring environmentWith(const std::filesystem::path& directory) {
    std::wstring block;
    bool found = false;
    if (wchar_t* strings = GetEnvironmentStringsW()) {
        for (const wchar_t* entry = strings; *entry; entry += wcslen(entry) + 1) {
            std::wstring_view variable(entry);
            if (!found && variable.size() > 5 && _wcsnicmp(entry, L"PATH=", 5) == 0) {
                block += L"PATH=" + directory.wstring() + L";" + std::wstring(variable.substr(5));
                found = true;
            } else {
                block += variable;
            }
            block += L'\0';
        }
        FreeEnvironmentStringsW(strings);
    }
    if (!found) block += L"PATH=" + directory.wstring() + L'\0';
    return block + L'\0';
}

struct Handle {
    HANDLE value = nullptr;
    ~Handle() {
        if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value);
    }
};

}

/// Run `binary` (looked up in PATH) in `directory` with an empty stdin, stdout and stderr appended
/// to `log`; `forward` runs while it works and once at the end. A timeout or a throwing `forward`
/// kills the child with everything it started.
inline Child run(const std::string& binary, const std::vector<std::string>& arguments,
                 const std::filesystem::path& directory, const std::filesystem::path& log,
                 std::chrono::milliseconds timeout, const std::function<void()>& forward) {
    using Windows::Handle;
    SECURITY_ATTRIBUTES inheritable{.nLength = sizeof(SECURITY_ATTRIBUTES), .bInheritHandle = TRUE};
    constexpr DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
    Handle output{
        CreateFileW(log.c_str(), FILE_APPEND_DATA, share, &inheritable, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)};
    Handle input{CreateFileW(L"NUL", GENERIC_READ, share, &inheritable, OPEN_EXISTING, 0, nullptr)};
    if (output.value == INVALID_HANDLE_VALUE || input.value == INVALID_HANDLE_VALUE)
        throw Exception<Suite>("cannot open the log '{}': error {}", log.string(), GetLastError());

    Handle job{CreateJobObjectW(nullptr, nullptr)};
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job.value || !SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limits, sizeof limits))
        throw Exception<Suite>("cannot create a job object: error {}", GetLastError());

    // Only these two handles reach the child: suites run in parallel must not inherit each other's logs.
    HANDLE inherited[] = {input.value, output.value};
    SIZE_T size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
    std::vector<char> storage(size);
    auto* attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if (!InitializeProcThreadAttributeList(attributes, 1, 0, &size) ||
        !UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof inherited,
                                   nullptr, nullptr))
        throw Exception<Suite>("spawn setup: error {}", GetLastError());
    struct Attributes {
        LPPROC_THREAD_ATTRIBUTE_LIST list;
        ~Attributes() { DeleteProcThreadAttributeList(list); }
    } cleanup{attributes};

    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof startup;
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = input.value;
    startup.StartupInfo.hStdOutput = output.value;
    startup.StartupInfo.hStdError = output.value;
    startup.lpAttributeList = attributes;

    std::wstring line;
    Windows::appendArgument(line, Windows::widen(binary));
    for (const auto& argument : arguments) Windows::appendArgument(line, Windows::widen(argument));
    const std::wstring workdir = directory.wstring();
    std::error_code error;
    const auto program = std::filesystem::canonical(std::filesystem::path(Windows::widen(binary)), error);
    std::wstring environment = error ? std::wstring{} : Windows::environmentWith(program.parent_path());
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, line.data(), nullptr, nullptr, TRUE,
                        CREATE_SUSPENDED | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT,
                        environment.empty() ? nullptr : environment.data(), workdir.empty() ? nullptr : workdir.c_str(),
                        &startup.StartupInfo, &process))
        throw Exception<Suite>("cannot start '{}': error {}", binary, GetLastError());
    Handle child{process.hProcess}, thread{process.hThread};
    AssignProcessToJobObject(job.value, child.value);
    ResumeThread(thread.value);

    const auto stop = [&] {
        TerminateJobObject(job.value, 124);
        WaitForSingleObject(child.value, INFINITE);
    };
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true) {
        // Output callbacks must not orphan the child process when they throw.
        try {
            forward();
        } catch (...) {
            stop();
            throw;
        }
        if (WaitForSingleObject(child.value, 5) == WAIT_OBJECT_0) {
            forward();
            DWORD code = 0;
            GetExitCodeProcess(child.value, &code);
            return {int(code), false};
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            stop();
            forward();
            return {124, true};
        }
    }
}

/// The name of an executable file without its .exe, or nullopt for another file.
inline std::optional<std::string> programName(const std::filesystem::path& file) {
    if (file.extension() != ".exe") return std::nullopt;
    return file.stem().string();
}

}
