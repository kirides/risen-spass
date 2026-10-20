#include "core/process.h"

#include <chrono>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace mk {

std::string quote_windows_arg(const std::string& arg) {
    if (!arg.empty() && arg.find_first_of(" \t\n\v\"") == std::string::npos) return arg;
    std::string out = "\"";
    size_t backslashes = 0;
    for (char c : arg) {
        if (c == '\\') {
            ++backslashes;
            continue;
        }
        if (c == '"') out.append(backslashes * 2 + 1, '\\');
        else out.append(backslashes, '\\');
        backslashes = 0;
        out += c;
    }
    out.append(backslashes * 2, '\\');
    return out + "\"";
}

std::string find_in_path(const std::string& name, const std::string& path_value, const std::function<bool(const std::string&)>& is_file, bool windows) {
    if (name.empty()) return {};
    const char sep = windows ? ';' : ':';
    const char slash = windows ? '\\' : '/';
    std::vector<std::string> names = {name};
    if (windows && (name.size() < 4 || (name.compare(name.size() - 4, 4, ".exe") != 0 && name.compare(name.size() - 4, 4, ".EXE") != 0)))
        names.insert(names.begin(), name + ".exe");
    size_t start = 0;
    while (start <= path_value.size()) {
        size_t end = path_value.find(sep, start);
        if (end == std::string::npos) end = path_value.size();
        std::string dir = path_value.substr(start, end - start);
        if (dir.size() >= 2 && dir.front() == '"' && dir.back() == '"') dir = dir.substr(1, dir.size() - 2);
        if (!dir.empty()) {
            if (dir.back() != '/' && dir.back() != '\\') dir += slash;
            for (auto& n : names)
                if (is_file(dir + n)) return dir + n;
        }
        start = end + 1;
    }
    return {};
}

#ifdef _WIN32

namespace {
std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}
}  // namespace

ProcessResult run_process(const std::vector<std::string>& argv, const std::atomic<bool>* cancel) {
    ProcessResult r;
    if (argv.empty()) {
        r.error = "no program";
        return r;
    }
    std::string cmd;
    for (size_t i = 0; i < argv.size(); ++i) cmd += (i ? " " : "") + quote_windows_arg(argv[i]);
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) {
        r.error = "cannot create a pipe";
        return r;
    }
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = nul;
    si.hStdOutput = wr;
    si.hStdError = wr;
    PROCESS_INFORMATION pi{};
    std::wstring wcmd = widen(cmd);
    const BOOL ok = CreateProcessW(nullptr, wcmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(wr);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (!ok) {
        CloseHandle(rd);
        r.error = "cannot start " + argv[0] + " (error " + std::to_string(GetLastError()) + ")";
        return r;
    }
    r.started = true;
    std::thread reader([&] {
        char buf[4096];
        DWORD n = 0;
        while (ReadFile(rd, buf, sizeof buf, &n, nullptr) && n > 0) r.output.append(buf, n);
    });
    for (;;) {
        if (WaitForSingleObject(pi.hProcess, 50) == WAIT_OBJECT_0) break;
        if (cancel && cancel->load()) {
            TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, 5000);
            r.cancelled = true;
            break;
        }
    }
    reader.join();
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    r.exit_code = static_cast<int>(code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(rd);
    return r;
}

#else

ProcessResult run_process(const std::vector<std::string>& argv, const std::atomic<bool>* cancel) {
    ProcessResult r;
    if (argv.empty()) {
        r.error = "no program";
        return r;
    }
    int fds[2];
    if (pipe(fds) != 0) {
        r.error = "cannot create a pipe";
        return r;
    }
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_adddup2(&fa, fds[1], 1);
    posix_spawn_file_actions_adddup2(&fa, fds[1], 2);
    posix_spawn_file_actions_addclose(&fa, fds[0]);
    std::vector<char*> args;
    for (auto& a : argv) args.push_back(const_cast<char*>(a.c_str()));
    args.push_back(nullptr);
    pid_t pid = 0;
    const int rc = posix_spawnp(&pid, argv[0].c_str(), &fa, nullptr, args.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    close(fds[1]);
    if (rc != 0) {
        close(fds[0]);
        r.error = "cannot start " + argv[0] + " (error " + std::to_string(rc) + ")";
        return r;
    }
    r.started = true;
    std::thread reader([&] {
        char buf[4096];
        ssize_t n = 0;
        while ((n = read(fds[0], buf, sizeof buf)) > 0) r.output.append(buf, static_cast<size_t>(n));
    });
    int status = 0;
    for (;;) {
        const pid_t w = waitpid(pid, &status, WNOHANG);
        if (w == pid) break;
        if (w < 0) break;
        if (cancel && cancel->load()) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            r.cancelled = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    reader.join();
    close(fds[0]);
    r.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return r;
}

#endif

}  // namespace mk
