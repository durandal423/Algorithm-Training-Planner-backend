#include "judge/sandbox_runner.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace atp {
namespace {

struct ProcessResult {
    bool timed_out{};
    bool output_limit_exceeded{};
    bool system_error{};
    int exit_code{};
    int time_ms{};
    std::string message;
};

std::string env_string(const char* key) {
#ifdef _WIN32
    char* value = nullptr;
    std::size_t size = 0;
    if (::_dupenv_s(&value, &size, key) != 0 || value == nullptr) {
        return {};
    }
    std::string text{value};
    std::free(value);
    return text;
#else
    const char* value = std::getenv(key);
    return value == nullptr ? std::string{} : std::string{value};
#endif
}

std::string quote_host_arg(const std::string& value) {
    std::string quoted = "\"";
    for (const char ch : value) {
        if (ch == '"') {
            quoted += "\\\"";
        } else {
            quoted.push_back(ch);
        }
    }
    quoted.push_back('"');
    return quoted;
}

std::string replace_all(std::string text, const std::string& from, const std::string& to) {
    std::size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
    return text;
}

std::filesystem::path source_path(const std::filesystem::path& work_dir) {
    return work_dir / "main.cpp";
}

std::filesystem::path executable_path(const std::filesystem::path& work_dir, SandboxRunner::Mode mode) {
#ifdef _WIN32
    if (mode == SandboxRunner::Mode::Local) {
        return work_dir / "main.exe";
    }
#endif
    return work_dir / "main";
}

std::string default_compile_template() {
    return "g++ -std=c++17 -O2 -pipe {source} -o {executable}";
}

std::string default_run_template() {
    return "{executable}";
}

std::string expand_template(
    std::string command_template,
    const std::string& source,
    const std::string& executable,
    const std::string& input,
    const std::string& stdout_file,
    const std::string& stderr_file
) {
    command_template = replace_all(std::move(command_template), "{source}", source);
    command_template = replace_all(std::move(command_template), "{executable}", executable);
    command_template = replace_all(std::move(command_template), "{input}", input);
    command_template = replace_all(std::move(command_template), "{stdout}", stdout_file);
    command_template = replace_all(std::move(command_template), "{stderr}", stderr_file);
    return command_template;
}

std::string docker_base_command(const std::filesystem::path& work_dir, const JudgeConfig& config, const std::string& image) {
    std::ostringstream command;
    command << "docker run --rm -i --network none"
            << " --memory " << config.memory_limit_mb << "m"
            << " --cpus 1"
            << " --pids-limit 128"
            << " -v " << quote_host_arg(work_dir.string() + ":/workspace")
            << " -w /workspace "
            << image << " ";
    return command.str();
}

struct SampleRead {
    std::string text;
    bool truncated{};
};

SampleRead read_limited(const std::filesystem::path& path, std::size_t limit) {
    SampleRead result;
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (!error && size > limit) {
        result.truncated = true;
    }

    std::ifstream input{path, std::ios::binary};
    if (!input) {
        return result;
    }
    result.text.resize(limit);
    input.read(result.text.data(), static_cast<std::streamsize>(limit));
    result.text.resize(static_cast<std::size_t>(input.gcount()));
    return result;
}

bool exceeds_output_limit(
    const std::filesystem::path& stdout_path,
    const std::filesystem::path& stderr_path,
    std::size_t limit
) {
    if (limit == 0) {
        return false;
    }
    std::error_code stdout_error;
    std::error_code stderr_error;
    const auto stdout_size = std::filesystem::file_size(stdout_path, stdout_error);
    const auto stderr_size = std::filesystem::file_size(stderr_path, stderr_error);
    const auto total = (stdout_error ? 0 : stdout_size) + (stderr_error ? 0 : stderr_size);
    return total > limit;
}

#ifdef _WIN32

std::string last_error_message() {
    const DWORD error = GetLastError();
    LPSTR buffer = nullptr;
    const DWORD size = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&buffer),
        0,
        nullptr
    );
    std::string message = size == 0 ? "unknown Windows error" : std::string{buffer, size};
    if (buffer != nullptr) {
        LocalFree(buffer);
    }
    return message;
}

HANDLE open_inheritable_file(const std::filesystem::path& path, DWORD access, DWORD creation) {
    SECURITY_ATTRIBUTES security;
    security.nLength = sizeof(SECURITY_ATTRIBUTES);
    security.lpSecurityDescriptor = nullptr;
    security.bInheritHandle = TRUE;
    return CreateFileA(
        path.string().c_str(),
        access,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        &security,
        creation,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
}

ProcessResult run_process(
    const std::string& command,
    int timeout_ms,
    const std::filesystem::path& stdin_path,
    const std::filesystem::path& stdout_path,
    const std::filesystem::path& stderr_path,
    std::size_t output_limit = 0
) {
    const auto started = std::chrono::steady_clock::now();
    HANDLE stdin_handle = open_inheritable_file(stdin_path, GENERIC_READ, OPEN_EXISTING);
    HANDLE stdout_handle = open_inheritable_file(stdout_path, GENERIC_WRITE, CREATE_ALWAYS);
    HANDLE stderr_handle = open_inheritable_file(stderr_path, GENERIC_WRITE, CREATE_ALWAYS);
    if (stdin_handle == INVALID_HANDLE_VALUE || stdout_handle == INVALID_HANDLE_VALUE || stderr_handle == INVALID_HANDLE_VALUE) {
        if (stdin_handle != INVALID_HANDLE_VALUE) {
            CloseHandle(stdin_handle);
        }
        if (stdout_handle != INVALID_HANDLE_VALUE) {
            CloseHandle(stdout_handle);
        }
        if (stderr_handle != INVALID_HANDLE_VALUE) {
            CloseHandle(stderr_handle);
        }
        return {false, false, true, -1, 0, "failed to open process stdio files: " + last_error_message()};
    }

    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = stdin_handle;
    startup.hStdOutput = stdout_handle;
    startup.hStdError = stderr_handle;

    PROCESS_INFORMATION process{};
    auto command_line = command;
    const BOOL created = CreateProcessA(
        nullptr,
        command_line.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW | CREATE_SUSPENDED,
        nullptr,
        nullptr,
        &startup,
        &process
    );

    CloseHandle(stdin_handle);
    CloseHandle(stdout_handle);
    CloseHandle(stderr_handle);

    if (!created) {
        return {false, false, true, -1, 0, "failed to start process: " + last_error_message()};
    }

    HANDLE job = CreateJobObjectA(nullptr, nullptr);
    if (job != nullptr) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
        AssignProcessToJobObject(job, process.hProcess);
    }

    ResumeThread(process.hThread);
    bool timed_out = false;
    bool output_limit_exceeded = false;
    while (true) {
        const DWORD wait_result = WaitForSingleObject(process.hProcess, 25);
        if (wait_result == WAIT_OBJECT_0) {
            break;
        }

        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started
        ).count();
        if (elapsed > timeout_ms) {
            timed_out = true;
        } else if (exceeds_output_limit(stdout_path, stderr_path, output_limit)) {
            output_limit_exceeded = true;
        } else {
            continue;
        }

        if (job != nullptr) {
            TerminateJobObject(job, timed_out ? 124 : 125);
        } else {
            TerminateProcess(process.hProcess, timed_out ? 124 : 125);
        }
        WaitForSingleObject(process.hProcess, 1000);
        break;
    }

    DWORD exit_code = 0;
    GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (job != nullptr) {
        CloseHandle(job);
    }

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started
    ).count();
    return {timed_out, output_limit_exceeded, false, static_cast<int>(exit_code), static_cast<int>(elapsed), {}};
}

#else

ProcessResult run_process(
    const std::string& command,
    int timeout_ms,
    const std::filesystem::path& stdin_path,
    const std::filesystem::path& stdout_path,
    const std::filesystem::path& stderr_path,
    std::size_t output_limit = 0
) {
    const auto started = std::chrono::steady_clock::now();
    const pid_t pid = fork();
    if (pid < 0) {
        return {false, false, true, -1, 0, "failed to fork process"};
    }
    if (pid == 0) {
        setpgid(0, 0);
        const int in_fd = open(stdin_path.c_str(), O_RDONLY);
        const int out_fd = open(stdout_path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0600);
        const int err_fd = open(stderr_path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0600);
        if (in_fd < 0 || out_fd < 0 || err_fd < 0) {
            _exit(127);
        }
        dup2(in_fd, STDIN_FILENO);
        dup2(out_fd, STDOUT_FILENO);
        dup2(err_fd, STDERR_FILENO);
        close(in_fd);
        close(out_fd);
        close(err_fd);
        execl("/bin/sh", "sh", "-c", command.c_str(), nullptr);
        _exit(127);
    }

    int status = 0;
    bool timed_out = false;
    bool output_limit_exceeded = false;
    while (true) {
        const auto wait_result = waitpid(pid, &status, WNOHANG);
        if (wait_result == pid) {
            break;
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started
        ).count();
        if (elapsed > timeout_ms) {
            timed_out = true;
        } else if (exceeds_output_limit(stdout_path, stderr_path, output_limit)) {
            output_limit_exceeded = true;
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        if (timed_out || output_limit_exceeded) {
            kill(-pid, SIGKILL);
            waitpid(pid, &status, 0);
            break;
        }
    }

    int exit_code = 0;
    if (WIFEXITED(status)) {
        exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        exit_code = 128 + WTERMSIG(status);
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started
    ).count();
    return {timed_out, output_limit_exceeded, false, exit_code, static_cast<int>(elapsed), {}};
}

#endif

std::filesystem::path null_input_file(const std::filesystem::path& work_dir) {
    const auto path = work_dir / "empty.in";
    if (!std::filesystem::exists(path)) {
        std::ofstream output{path, std::ios::binary};
    }
    return path;
}

std::string command_for_compile(
    const JudgeConfig& config,
    const std::filesystem::path& work_dir,
    SandboxRunner::Mode mode,
    const std::string& docker_image
) {
    const auto source = source_path(work_dir);
    const auto executable = executable_path(work_dir, mode);
    if (mode == SandboxRunner::Mode::Docker) {
        const auto inner = expand_template(
            config.compile_command_template.value_or(default_compile_template()),
            "/workspace/main.cpp",
            "/workspace/main",
            {},
            {},
            {}
        );
        return docker_base_command(work_dir, config, docker_image) + "sh -lc " + quote_host_arg(inner);
    }

    return expand_template(
        config.compile_command_template.value_or(default_compile_template()),
        quote_host_arg(source.string()),
        quote_host_arg(executable.string()),
        {},
        {},
        {}
    );
}

std::string command_for_run(
    const JudgeConfig& config,
    const JudgeTestCase& test_case,
    const std::filesystem::path& work_dir,
    SandboxRunner::Mode mode,
    const std::string& docker_image,
    const std::filesystem::path& stdin_path,
    const std::filesystem::path& stdout_path,
    const std::filesystem::path& stderr_path
) {
    const int time_limit_ms = test_case.time_limit_ms.value_or(config.time_limit_ms);
    if (mode == SandboxRunner::Mode::Docker) {
        const auto docker_input = "/workspace/" + stdin_path.filename().string();
        const auto docker_stdout = "/workspace/" + stdout_path.filename().string();
        const auto docker_stderr = "/workspace/" + stderr_path.filename().string();
        auto inner = expand_template(
            config.run_command_template.value_or(default_run_template()),
            "/workspace/main.cpp",
            "/workspace/main",
            docker_input,
            docker_stdout,
            docker_stderr
        );
        const int timeout_seconds = std::max(1, (time_limit_ms + 999) / 1000);
        inner = "timeout " + std::to_string(timeout_seconds) + "s " + inner;
        return docker_base_command(work_dir, config, docker_image) + "sh -lc " + quote_host_arg(inner);
    }

    return expand_template(
        config.run_command_template.value_or(default_run_template()),
        quote_host_arg(source_path(work_dir).string()),
        quote_host_arg(executable_path(work_dir, mode).string()),
        quote_host_arg(stdin_path.string()),
        quote_host_arg(stdout_path.string()),
        quote_host_arg(stderr_path.string())
    );
}

} // namespace

SandboxRunner::SandboxRunner(Mode mode, std::string docker_image)
    : mode_(mode), docker_image_(std::move(docker_image)) {}

SandboxRunner SandboxRunner::fromEnvironment() {
    const auto allow_local = env_string("ATP_JUDGE_ALLOW_LOCAL");
    const auto image = env_string("ATP_JUDGE_DOCKER_IMAGE");
    if (allow_local == "1" || allow_local == "true" || allow_local == "TRUE") {
        return SandboxRunner{Mode::Local, image.empty() ? "gcc:13" : image};
    }
    return SandboxRunner{Mode::Docker, image.empty() ? "gcc:13" : image};
}

CompileRunResult SandboxRunner::compile(
    const std::string& source_code,
    const JudgeConfig& config,
    const std::filesystem::path& work_dir
) const {
    std::filesystem::create_directories(work_dir);
    {
        std::ofstream source{source_path(work_dir), std::ios::binary};
        if (!source) {
            throw std::runtime_error("failed to write source file");
        }
        source << source_code;
    }

    const auto stdout_path = work_dir / "compile.stdout.txt";
    const auto stderr_path = work_dir / "compile.stderr.txt";
    const auto process = run_process(
        command_for_compile(config, work_dir, mode_, docker_image_),
        15000,
        null_input_file(work_dir),
        stdout_path,
        stderr_path,
        128 * 1024
    );
    const auto stdout_sample = read_limited(stdout_path, 64 * 1024);
    const auto stderr_sample = read_limited(stderr_path, 64 * 1024);
    return {
        !process.timed_out && !process.output_limit_exceeded && !process.system_error && process.exit_code == 0,
        process.timed_out,
        process.system_error,
        process.exit_code,
        process.time_ms,
        stdout_sample.text,
        stderr_sample.text,
        process.message
    };
}

SandboxRunResult SandboxRunner::run(
    const JudgeConfig& config,
    const JudgeTestCase& test_case,
    const std::filesystem::path& work_dir,
    int run_index
) const {
    const auto input_path = work_dir / ("input-" + std::to_string(run_index) + ".txt");
    const auto stdout_path = work_dir / ("stdout-" + std::to_string(run_index) + ".txt");
    const auto stderr_path = work_dir / ("stderr-" + std::to_string(run_index) + ".txt");
    {
        std::ofstream input{input_path, std::ios::binary};
        if (!input) {
            throw std::runtime_error("failed to write test input");
        }
        input << test_case.input_data;
    }

    const int time_limit_ms = test_case.time_limit_ms.value_or(config.time_limit_ms);
    const int host_timeout_ms = mode_ == Mode::Docker ? time_limit_ms + 5000 : time_limit_ms;
    const auto output_limit = static_cast<std::size_t>(config.output_limit_kb) * 1024;
    const auto process = run_process(
        command_for_run(config, test_case, work_dir, mode_, docker_image_, input_path, stdout_path, stderr_path),
        host_timeout_ms,
        input_path,
        stdout_path,
        stderr_path,
        output_limit
    );
    const auto stdout_sample = read_limited(stdout_path, output_limit);
    const auto stderr_sample = read_limited(stderr_path, std::min<std::size_t>(output_limit, 64 * 1024));
    const bool docker_timeout = mode_ == Mode::Docker && process.exit_code == 124;

    return {
        process.timed_out || docker_timeout,
        process.output_limit_exceeded || stdout_sample.truncated,
        process.system_error,
        process.exit_code,
        process.time_ms,
        0,
        stdout_sample.text,
        stderr_sample.text,
        process.message
    };
}

SandboxRunner::Mode SandboxRunner::mode() const {
    return mode_;
}

} // namespace atp
