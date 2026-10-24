#include "Git.hpp"

#include "Process.hpp"

#include <Lattice/Tools/Exception.hpp>

namespace Lattice::BmHistory {

using Error = Exception<Git>;

namespace {

std::string command(const std::vector<std::string>& arguments) {
    std::string text = "git";
    for (const auto& argument : arguments)
        text += " " + argument;
    return text;
}

}

Git::Output Git::call(const std::vector<std::string>& arguments, std::string_view input) const {
    auto result = runProcess("git", arguments, directory_, input);
    return {result.status, std::move(result.out), std::move(result.err)};
}

std::string Git::run(const std::vector<std::string>& arguments, std::string_view input) const {
    Output result = call(arguments, input);
    if (result.status != 0) {
        while (!result.err.empty() && (result.err.back() == '\n' || result.err.back() == ' '))
            result.err.pop_back();
        throw Error("{} (in {}): {}", command(arguments), directory_.string(), result.err);
    }
    return std::move(result.out);
}

std::vector<std::optional<std::string>> Git::show(const std::vector<std::string>& objects) const {
    std::vector<std::optional<std::string>> result;
    if (objects.empty())
        return result;
    std::string input;
    for (const auto& object : objects)
        input += object + "\n";
    const std::string out = run({"cat-file", "--batch"}, input);

    // "<sha> <type> <size>\n<content>\n" or "<object> missing\n".
    size_t at = 0;
    for (size_t i = 0; i < objects.size(); ++i) {
        const size_t end = out.find('\n', at);
        if (end == std::string::npos)
            throw Error("cat-file: truncated output");
        const std::string_view header(out.data() + at, end - at);
        at = end + 1;
        if (header.ends_with(" missing") || header.ends_with(" ambiguous")) {
            result.emplace_back();
            continue;
        }
        const size_t space = header.rfind(' ');
        const size_t size = std::stoull(std::string(header.substr(space + 1)));
        result.emplace_back(out.substr(at, size));
        at += size + 1;
    }
    return result;
}

std::optional<CodeState> codeState(const std::filesystem::path& path) {
    std::error_code ec;
    auto directory = std::filesystem::is_directory(path, ec) ? path : path.parent_path();
    if (directory.empty())
        directory = ".";
    const Git git(directory);
    const auto root = git.call({"rev-parse", "--show-toplevel"});
    if (root.status != 0)
        return std::nullopt;
    CodeState state;
    std::string top = root.out;
    while (!top.empty() && (top.back() == '\n' || top.back() == '\r'))
        top.pop_back();
    state.name = std::filesystem::path(top).filename().string();
    const auto head = git.call({"rev-parse", "--short", "HEAD"});
    state.commit = head.status == 0 ? head.out.substr(0, head.out.find('\n')) : "unborn";
    state.dirty = !git.run({"status", "--porcelain", "--untracked-files=no"}).empty();
    return state;
}

}
