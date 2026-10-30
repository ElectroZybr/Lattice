#pragma once

// Linux and macOS: posix_spawn into its own process group (a timeout kills the whole group), the
// output appended to the log; an executable is a file with the execute permission.

#include <cerrno>
#include <csignal>
#include <cstring>
#include <optional>
#include <thread>

#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <lattice-tools/Exception.hpp>

extern char** environ;

namespace Lattice::Testing::Platform {

/// Run `binary` (looked up in PATH) in `directory` with an empty stdin, stdout and stderr appended
/// to `log`; `forward` runs while it works and once at the end. A timeout or a throwing `forward`
/// kills the child with everything it started.
inline Child run(const std::string& binary, const std::vector<std::string>& arguments,
                 const std::filesystem::path& directory, const std::filesystem::path& log,
                 std::chrono::milliseconds timeout, const std::function<void()>& forward) {
    std::vector<char*> argv{const_cast<char*>(binary.c_str())};
    for (const auto& arg : arguments) argv.push_back(const_cast<char*>(arg.c_str()));
    argv.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attributes;
    posix_spawn_file_actions_init(&actions);
    posix_spawnattr_init(&attributes);
    struct Cleanup {
        posix_spawn_file_actions_t& actions;
        posix_spawnattr_t& attributes;
        ~Cleanup() {
            posix_spawn_file_actions_destroy(&actions);
            posix_spawnattr_destroy(&attributes);
        }
    } cleanup{actions, attributes};
    auto check = [&](int status) {
        if (status) throw Exception<Suite>("spawn setup: {}", std::strerror(status));
    };
    check(posix_spawn_file_actions_addchdir_np(&actions, directory.c_str()));
    check(posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0));
    check(posix_spawn_file_actions_addopen(&actions, 1, log.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0600));
    check(posix_spawn_file_actions_adddup2(&actions, 1, 2));
    check(posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP));
    check(posix_spawnattr_setpgroup(&attributes, 0));
    pid_t child;
    const int error = posix_spawnp(&child, binary.c_str(), &actions, &attributes, argv.data(), environ);
    if (error) throw Exception<Suite>("cannot start '{}': {}", binary, std::strerror(error));
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    int status;
    while (true) {
        // Output callbacks must not orphan the child process when they throw.
        try {
            forward();
        } catch (...) {
            kill(-child, SIGKILL);
            while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
            throw;
        }
        const auto done = waitpid(child, &status, WNOHANG);
        if (done == child) {
            forward();
            return {WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status), false};
        }
        if (done < 0 && errno != EINTR) throw Exception<Suite>("waitpid failed: {}", std::strerror(errno));
        if (std::chrono::steady_clock::now() >= deadline) {
            kill(-child, SIGKILL);
            while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
            forward();
            return {124, true};
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

/// The name of an executable file, or nullopt for another file.
inline std::optional<std::string> programName(const std::filesystem::path& file) {
    if (access(file.c_str(), X_OK) != 0) return std::nullopt;
    return file.filename().string();
}

}
