#pragma once

#include <optional>
#include <string>
#include <vector>

namespace atp {

struct JudgeConfig {
    long long id{};
    long long problem_id{};
    bool enabled{false};
    std::string language{"cpp"};
    std::optional<std::string> compile_command_template;
    std::optional<std::string> run_command_template;
    int time_limit_ms{1000};
    int memory_limit_mb{256};
    int output_limit_kb{1024};
    std::string compare_mode{"ignore_whitespace"};
    double float_epsilon{1e-6};
    std::optional<std::string> official_solution_language;
    std::optional<std::string> official_solution_code;
    std::optional<std::string> checker_language;
    std::optional<std::string> checker_code;
    std::string created_at;
    std::string updated_at;
};

struct JudgeTestCase {
    long long id{};
    long long problem_id{};
    std::string name;
    std::string input_data;
    std::optional<std::string> expected_output;
    std::optional<std::string> expected_output_hash;
    std::string visibility{"hidden"};
    int points{1};
    int order_index{};
    std::optional<int> time_limit_ms;
    std::optional<int> memory_limit_mb;
    bool is_sample{false};
    std::string created_at;
    std::string updated_at;
};

struct SubmissionResult {
    long long id{};
    long long submission_id{};
    std::optional<long long> test_case_id;
    int order_index{};
    std::string status{"completed"};
    std::optional<std::string> verdict;
    std::optional<int> time_ms;
    std::optional<int> memory_kb;
    std::optional<int> exit_code;
    std::string stdout_sample;
    std::string stderr_sample;
    std::string message;
    std::string created_at;
};

struct Submission {
    long long id{};
    long long problem_id{};
    std::optional<long long> user_id;
    std::string language{"cpp"};
    std::string source_code;
    std::string status{"queued"};
    std::optional<std::string> verdict;
    int score{};
    std::string compile_stdout;
    std::string compile_stderr;
    std::optional<int> compile_time_ms;
    std::optional<int> max_time_ms;
    std::optional<int> max_memory_kb;
    std::string submitted_at;
    std::optional<std::string> started_at;
    std::optional<std::string> finished_at;
    std::vector<SubmissionResult> results;
};

} // namespace atp
