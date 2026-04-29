#pragma once

#include "domain/judge_models.hpp"

#include <nlohmann/json.hpp>

namespace atp {

nlohmann::json toJson(const JudgeConfig& config, bool include_code = false);
nlohmann::json toJson(const JudgeTestCase& test_case, bool include_payload = true);
nlohmann::json toJson(const SubmissionResult& result);
nlohmann::json toJson(const Submission& submission, bool include_source = false);

JudgeConfig judgeConfigFromJson(long long problem_id, const nlohmann::json& body);
JudgeTestCase judgeTestCaseFromJson(long long problem_id, const nlohmann::json& body);
void applyJudgeTestCaseOverrides(JudgeTestCase& test_case, const nlohmann::json& body);
Submission submissionFromJson(const nlohmann::json& body);

} // namespace atp
