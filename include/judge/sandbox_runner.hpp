#pragma once

#include "domain/judge_models.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace atp {

struct CompileRunResult {
    bool success{};
    bool timed_out{};
    bool system_error{};
    int exit_code{};
    int time_ms{};
    std::string stdout_sample;
    std::string stderr_sample;
    std::string message;
};

struct SandboxRunResult {
    bool timed_out{};
    bool output_limit_exceeded{};
    bool system_error{};
    int exit_code{};
    int time_ms{};
    int memory_kb{};
    std::string stdout_sample;
    std::string stderr_sample;
    std::string message;
};

class SandboxRunner {
public:
    enum class Mode {
        Docker,
        Local
    };

    SandboxRunner(Mode mode, std::string docker_image);

    static SandboxRunner fromEnvironment();

    CompileRunResult compile(
        const std::string& source_code,
        const JudgeConfig& config,
        const std::filesystem::path& work_dir
    ) const;

    SandboxRunResult run(
        const JudgeConfig& config,
        const JudgeTestCase& test_case,
        const std::filesystem::path& work_dir,
        int run_index
    ) const;

    Mode mode() const;

private:
    Mode mode_;
    std::string docker_image_;
};

} // namespace atp
