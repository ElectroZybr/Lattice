#include "Process.hpp"

#include <optional>

#include <lattice-tools/Exception.hpp>

#include "platform/Platform.hpp"

namespace Lattice::BmHistory {

using Error = Exception<ProcessOutput>;

ProcessOutput runProcess(std::string_view program, const std::vector<std::string>& arguments,
                         const std::filesystem::path& directory, std::string_view input) {
    ProcessOutput result = Platform::runProgram(program, arguments, directory, input);
    if (result.status == 127 && result.out.empty() && result.err.empty())
        throw Error("cannot run {} in '{}' (is it installed?)", program, directory.string());
    return result;
}

int runAttached(const std::filesystem::path& program, const std::vector<std::string>& arguments) {
    const auto status = Platform::runAttached(program, arguments);
    if (!status)
        throw Error("cannot start '{}'", program.string());
    return *status;
}

bool interactiveTerminal() {
    return Platform::interactiveTerminal();
}

}
