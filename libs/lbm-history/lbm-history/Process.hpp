#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Lattice::BmHistory {

struct ProcessOutput {
    int status = 0;
    std::string out;
    std::string err;
};

/// Run `program` (looked up in PATH) with its arguments as they are — no shell — in
/// `directory`, feeding `input` and collecting both outputs. A program that cannot be
/// started is an exception; its exit status is the caller's business.
ProcessOutput runProcess(std::string_view program, const std::vector<std::string>& arguments,
                         const std::filesystem::path& directory = {}, std::string_view input = {});

}
