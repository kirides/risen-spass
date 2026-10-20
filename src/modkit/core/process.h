// Running an external program (ffmpeg) portably: argument vector in, exit code and the combined stdout/stderr text out, with a cancel flag that
// kills the child. Windows uses CreateProcessW with the MSVC argument quoting, Linux / macOS posix_spawnp; nothing goes through a shell.
#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace mk {

struct ProcessResult {
    bool started = false;    // false: the program could not be run (`error` says why)
    bool cancelled = false;  // the cancel flag was set and the child was killed
    int exit_code = -1;
    std::string output;      // stdout and stderr, as the program wrote them
    std::string error;
};
// argv[0] is the program (a path or a name found on PATH); `cancel` is polled while it runs
using ProcessRunner = std::function<ProcessResult(const std::vector<std::string>& argv, const std::atomic<bool>* cancel)>;
ProcessResult run_process(const std::vector<std::string>& argv, const std::atomic<bool>* cancel = nullptr);

// one argument quoted for a Windows command line (CommandLineToArgvW / the MSVC runtime read it back unchanged)
std::string quote_windows_arg(const std::string& arg);
// the program `name` in the directories of a PATH value (';' on Windows, ':' elsewhere); on Windows ".exe" is tried as well. "" when not found.
std::string find_in_path(const std::string& name, const std::string& path_value, const std::function<bool(const std::string&)>& is_file, bool windows);

}  // namespace mk
