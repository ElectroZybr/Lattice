#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Lattice::BmHistory {

/// The system `git` in one working directory: every call is a process with its
/// arguments as they are (no shell). Errors carry the command and git's own message.
class Git {
public:
    explicit Git(std::filesystem::path directory) : directory_(std::move(directory)) {}

    struct Output {
        int status = 0;
        std::string out;
        std::string err;
    };

    /// Run and return stdout; a non-zero exit is an exception.
    std::string run(const std::vector<std::string>& arguments, std::string_view input = {}) const;
    /// Run and return everything, whatever the exit status.
    Output call(const std::vector<std::string>& arguments, std::string_view input = {}) const;

    /// Contents of many `rev:path` objects in one process (`cat-file --batch`);
    /// nullopt for a missing object.
    std::vector<std::optional<std::string>> show(const std::vector<std::string>& objects) const;

    const std::filesystem::path& directory() const noexcept { return directory_; }

private:
    std::filesystem::path directory_;
};

/// The repository holding some code: what a run measured.
struct CodeState {
    std::string name;   // folder name of the repository root
    std::string commit; // short sha
    bool dirty = false; // uncommitted changes of tracked files
};

/// State of the git repository containing `path`; nullopt outside a repository.
std::optional<CodeState> codeState(const std::filesystem::path& path);

}
