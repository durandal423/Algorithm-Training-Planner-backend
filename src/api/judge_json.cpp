#include "api/judge_json.hpp"

#include <stdexcept>

namespace atp {
namespace {

template <typename T>
void set_if_present(const nlohmann::json& body, const char* key, T& value) {
    if (body.contains(key) && !body.at(key).is_null()) {
        value = body.at(key).get<T>();
    }
}

void set_optional_string(const nlohmann::json& body, const char* key, std::optional<std::string>& value) {
    if (!body.contains(key)) {
        return;
    }
    if (body.at(key).is_null()) {
        value.reset();
    } else {
        value = body.at(key).get<std::string>();
    }
}

void set_optional_int(const nlohmann::json& body, const char* key, std::optional<int>& value) {
    if (!body.contains(key)) {
        return;
    }
    if (body.at(key).is_null()) {
        value.reset();
    } else {
        value = body.at(key).get<int>();
    }
}

void set_optional_long(const nlohmann::json& body, const char* key, std::optional<long long>& value) {
    if (!body.contains(key)) {
        return;
    }
    if (body.at(key).is_null()) {
        value.reset();
    } else {
        value = body.at(key).get<long long>();
    }
}

nlohmann::json optional_string_json(const std::optional<std::string>& value) {
    return value ? nlohmann::json(*value) : nlohmann::json(nullptr);
}

nlohmann::json optional_long_json(const std::optional<long long>& value) {
    return value ? nlohmann::json(*value) : nlohmann::json(nullptr);
}

nlohmann::json optional_int_json(const std::optional<int>& value) {
    return value ? nlohmann::json(*value) : nlohmann::json(nullptr);
}

} // namespace

nlohmann::json toJson(const JudgeConfig& config, bool include_code) {
    nlohmann::json body = {
        {"id", config.id},
        {"problem_id", config.problem_id},
        {"enabled", config.enabled},
        {"language", config.language},
        {"compile_command_template", optional_string_json(config.compile_command_template)},
        {"run_command_template", optional_string_json(config.run_command_template)},
        {"time_limit_ms", config.time_limit_ms},
        {"memory_limit_mb", config.memory_limit_mb},
        {"output_limit_kb", config.output_limit_kb},
        {"compare_mode", config.compare_mode},
        {"float_epsilon", config.float_epsilon},
        {"official_solution_language", optional_string_json(config.official_solution_language)},
        {"has_official_solution", config.official_solution_code.has_value() && !config.official_solution_code->empty()},
        {"official_solution_size", config.official_solution_code ? config.official_solution_code->size() : 0},
        {"checker_language", optional_string_json(config.checker_language)},
        {"has_checker", config.checker_code.has_value() && !config.checker_code->empty()},
        {"checker_size", config.checker_code ? config.checker_code->size() : 0},
        {"created_at", config.created_at},
        {"updated_at", config.updated_at}
    };
    if (include_code) {
        body["official_solution_code"] = optional_string_json(config.official_solution_code);
        body["checker_code"] = optional_string_json(config.checker_code);
    }
    return body;
}

nlohmann::json toJson(const JudgeTestCase& test_case, bool include_payload) {
    nlohmann::json body = {
        {"id", test_case.id},
        {"problem_id", test_case.problem_id},
        {"name", test_case.name},
        {"visibility", test_case.visibility},
        {"points", test_case.points},
        {"order_index", test_case.order_index},
        {"time_limit_ms", optional_int_json(test_case.time_limit_ms)},
        {"memory_limit_mb", optional_int_json(test_case.memory_limit_mb)},
        {"is_sample", test_case.is_sample},
        {"expected_output_hash", optional_string_json(test_case.expected_output_hash)},
        {"has_expected_output", test_case.expected_output.has_value()},
        {"created_at", test_case.created_at},
        {"updated_at", test_case.updated_at}
    };
    if (include_payload) {
        body["input_data"] = test_case.input_data;
        body["expected_output"] = optional_string_json(test_case.expected_output);
    }
    return body;
}

nlohmann::json toJson(const SubmissionResult& result) {
    return {
        {"id", result.id},
        {"submission_id", result.submission_id},
        {"test_case_id", optional_long_json(result.test_case_id)},
        {"order_index", result.order_index},
        {"status", result.status},
        {"verdict", optional_string_json(result.verdict)},
        {"time_ms", optional_int_json(result.time_ms)},
        {"memory_kb", optional_int_json(result.memory_kb)},
        {"exit_code", optional_int_json(result.exit_code)},
        {"stdout_sample", result.stdout_sample},
        {"stderr_sample", result.stderr_sample},
        {"message", result.message},
        {"created_at", result.created_at}
    };
}

nlohmann::json toJson(const Submission& submission, bool include_source) {
    nlohmann::json results = nlohmann::json::array();
    for (const auto& result : submission.results) {
        results.push_back(toJson(result));
    }
    nlohmann::json body = {
        {"id", submission.id},
        {"problem_id", submission.problem_id},
        {"user_id", optional_long_json(submission.user_id)},
        {"language", submission.language},
        {"status", submission.status},
        {"verdict", optional_string_json(submission.verdict)},
        {"score", submission.score},
        {"compile_stdout", submission.compile_stdout},
        {"compile_stderr", submission.compile_stderr},
        {"compile_time_ms", optional_int_json(submission.compile_time_ms)},
        {"max_time_ms", optional_int_json(submission.max_time_ms)},
        {"max_memory_kb", optional_int_json(submission.max_memory_kb)},
        {"submitted_at", submission.submitted_at},
        {"started_at", optional_string_json(submission.started_at)},
        {"finished_at", optional_string_json(submission.finished_at)},
        {"results", results}
    };
    if (include_source) {
        body["source_code"] = submission.source_code;
    }
    return body;
}

JudgeConfig judgeConfigFromJson(long long problem_id, const nlohmann::json& body) {
    JudgeConfig config;
    config.problem_id = problem_id;
    set_if_present(body, "enabled", config.enabled);
    set_if_present(body, "language", config.language);
    set_optional_string(body, "compile_command_template", config.compile_command_template);
    set_optional_string(body, "run_command_template", config.run_command_template);
    set_if_present(body, "time_limit_ms", config.time_limit_ms);
    set_if_present(body, "memory_limit_mb", config.memory_limit_mb);
    set_if_present(body, "output_limit_kb", config.output_limit_kb);
    set_if_present(body, "compare_mode", config.compare_mode);
    set_if_present(body, "float_epsilon", config.float_epsilon);
    set_optional_string(body, "official_solution_language", config.official_solution_language);
    set_optional_string(body, "official_solution_code", config.official_solution_code);
    set_optional_string(body, "checker_language", config.checker_language);
    set_optional_string(body, "checker_code", config.checker_code);
    return config;
}

JudgeTestCase judgeTestCaseFromJson(long long problem_id, const nlohmann::json& body) {
    JudgeTestCase test_case;
    test_case.problem_id = problem_id;
    applyJudgeTestCaseOverrides(test_case, body);
    return test_case;
}

void applyJudgeTestCaseOverrides(JudgeTestCase& test_case, const nlohmann::json& body) {
    set_if_present(body, "name", test_case.name);
    set_if_present(body, "input_data", test_case.input_data);
    set_optional_string(body, "expected_output", test_case.expected_output);
    set_if_present(body, "visibility", test_case.visibility);
    set_if_present(body, "points", test_case.points);
    set_if_present(body, "order_index", test_case.order_index);
    set_optional_int(body, "time_limit_ms", test_case.time_limit_ms);
    set_optional_int(body, "memory_limit_mb", test_case.memory_limit_mb);
    set_if_present(body, "is_sample", test_case.is_sample);
}

Submission submissionFromJson(const nlohmann::json& body) {
    Submission submission;
    set_if_present(body, "problem_id", submission.problem_id);
    set_optional_long(body, "user_id", submission.user_id);
    set_if_present(body, "language", submission.language);
    set_if_present(body, "source_code", submission.source_code);
    return submission;
}

} // namespace atp
