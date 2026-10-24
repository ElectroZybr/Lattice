#include "Process.hpp"

#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

#include <Lattice/Tools/Exception.hpp>

namespace Lattice::BmHistory {

using Error = Exception<ProcessOutput>;

namespace {

std::string command(std::string_view program, const std::vector<std::string>& arguments) {
    std::string text(program);
    for (const auto& argument : arguments)
        text += " " + argument;
    return text;
}

}

ProcessOutput runProcess(std::string_view program, const std::vector<std::string>& arguments,
                         const std::filesystem::path& directory, std::string_view input) {
    int in[2], out[2], err[2];
    if (pipe(in) || pipe(out) || pipe(err))
        throw Error("{}: pipe failed: {}", command(program, arguments), std::strerror(errno));

    const pid_t pid = fork();
    if (pid < 0)
        throw Error("{}: fork failed: {}", command(program, arguments), std::strerror(errno));
    if (pid == 0) {
        dup2(in[0], 0);
        dup2(out[1], 1);
        dup2(err[1], 2);
        for (int fd : {in[0], in[1], out[0], out[1], err[0], err[1]})
            close(fd);
        if (!directory.empty() && chdir(directory.c_str()) != 0)
            _exit(127);
        std::vector<char*> argv;
        const std::string name(program);
        argv.push_back(const_cast<char*>(name.c_str()));
        for (const auto& argument : arguments)
            argv.push_back(const_cast<char*>(argument.c_str()));
        argv.push_back(nullptr);
        execvp(name.c_str(), argv.data());
        _exit(127);
    }
    close(in[0]);
    close(out[1]);
    close(err[1]);

    // Write stdin and read both outputs together: a large output must not block git.
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
            if (errno == EINTR)
                continue;
            break;
        }
        for (int i = 0; i < count; ++i) {
            if (!fds[i].revents)
                continue;
            if (fds[i].fd == in[1]) {
                const ssize_t n = write(in[1], input.data() + written, input.size() - written);
                if (n > 0)
                    written += size_t(n);
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
    if (in[1] >= 0)
        close(in[1]);
    close(out[0]);
    close(err[0]);

    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    result.status = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    if (result.status == 127 && result.out.empty() && result.err.empty())
        throw Error("cannot run {} in '{}' (is it installed?)", program, directory.string());
    return result;
}

}
