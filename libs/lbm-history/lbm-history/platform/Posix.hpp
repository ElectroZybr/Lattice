#pragma once

// Linux and macOS: fork/execvp with pipes, poll() over stdin and both outputs; posix_spawn for an
// attached child; gethostname().

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <lattice-tools/Exception.hpp>

#include "../Process.hpp"

extern char** environ;

namespace Lattice::BmHistory::Platform {

/// Exit status 127 with no output: the program could not be started.
inline ProcessOutput runProgram(std::string_view program, const std::vector<std::string>& arguments,
                                const std::filesystem::path& directory, std::string_view input) {
    int in[2], out[2], err[2];
    if (pipe(in) || pipe(out) || pipe(err))
        throw Exception<ProcessOutput>("{}: pipe failed: {}", program, std::strerror(errno));

    const pid_t pid = fork();
    if (pid < 0) throw Exception<ProcessOutput>("{}: fork failed: {}", program, std::strerror(errno));
    if (pid == 0) {
        dup2(in[0], 0);
        dup2(out[1], 1);
        dup2(err[1], 2);
        for (int fd : {in[0], in[1], out[0], out[1], err[0], err[1]}) close(fd);
        if (!directory.empty() && chdir(directory.c_str()) != 0) _exit(127);
        std::vector<char*> argv;
        const std::string name(program);
        argv.push_back(const_cast<char*>(name.c_str()));
        for (const auto& argument : arguments) argv.push_back(const_cast<char*>(argument.c_str()));
        argv.push_back(nullptr);
        execvp(name.c_str(), argv.data());
        _exit(127);
    }
    close(in[0]);
    close(out[1]);
    close(err[1]);

    // Write stdin and read both outputs together: a large output must not block
    // git.
    ProcessOutput result;
    size_t written = 0;
    if (input.empty()) {
        close(in[1]);
        in[1] = -1;
    } else {
        fcntl(in[1], F_SETFL, O_NONBLOCK);
    }
    bool outOpen = true, errOpen = true;
    char buffer[65536];
    while (outOpen || errOpen) {
        pollfd fds[3];
        int count = 0;
        if (outOpen) fds[count++] = {out[0], POLLIN, 0};
        if (errOpen) fds[count++] = {err[0], POLLIN, 0};
        if (in[1] >= 0) fds[count++] = {in[1], POLLOUT, 0};
        if (poll(fds, count, -1) < 0) {
            if (errno == EINTR) continue;
            break;
        }
        for (int i = 0; i < count; ++i) {
            if (!fds[i].revents) continue;
            if (fds[i].fd == in[1]) {
                const ssize_t n = write(in[1], input.data() + written, input.size() - written);
                if (n > 0) written += size_t(n);
                if (n < 0 || written == input.size()) {
                    close(in[1]);
                    in[1] = -1;
                }
                continue;
            }
            const ssize_t n = read(fds[i].fd, buffer, sizeof buffer);
            if (n <= 0) {
                (fds[i].fd == out[0] ? outOpen : errOpen) = false;
                continue;
            }
            (fds[i].fd == out[0] ? result.out : result.err).append(buffer, size_t(n));
        }
    }
    if (in[1] >= 0) close(in[1]);
    close(out[0]);
    close(err[0]);

    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    result.status = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return result;
}

/// The exit status, -1 when killed; nullopt when the program could not be
/// started.
inline std::optional<int> runAttached(const std::filesystem::path& program, const std::vector<std::string>& arguments) {
    std::vector<std::string> storage{program.string()};
    storage.insert(storage.end(), arguments.begin(), arguments.end());
    std::vector<char*> argv;
    for (auto& argument : storage) argv.push_back(argument.data());
    argv.push_back(nullptr);
    pid_t pid = 0;
    if (posix_spawn(&pid, program.c_str(), nullptr, nullptr, argv.data(), environ) != 0) return std::nullopt;
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

inline bool interactiveTerminal() { return isatty(STDIN_FILENO) && isatty(STDOUT_FILENO); }

/// The host name as the OS gives it; empty when unknown.
inline std::string systemHostName() {
    char buffer[256] = {};
    if (gethostname(buffer, sizeof buffer - 1) != 0) return {};
    return buffer;
}

}
