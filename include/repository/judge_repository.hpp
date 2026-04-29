#pragma once

#include "domain/judge_models.hpp"

#include <optional>
#include <string>
#include <vector>

namespace atp {

class ConnectionPool;

class JudgeRepository {
public:
    explicit JudgeRepository(ConnectionPool& pool);

    std::optional<JudgeConfig> getConfig(long long problem_id) const;
    JudgeConfig upsertConfig(const JudgeConfig& config) const;

    std::vector<JudgeTestCase> listTestCases(long long problem_id, bool include_hidden) const;
    std::vector<JudgeTestCase> listJudgingTestCases(long long problem_id) const;
    std::optional<JudgeTestCase> getTestCase(long long id) const;
    long long createTestCase(const JudgeTestCase& test_case) const;
    bool updateTestCase(long long id, const JudgeTestCase& test_case) const;
    bool updateExpectedOutput(long long id, const std::string& expected_output) const;
    bool deleteTestCase(long long id) const;

    long long createSubmission(const Submission& submission) const;
    std::optional<Submission> getSubmission(long long id) const;
    std::vector<Submission> listSubmissionsForProblem(long long problem_id, int limit) const;
    std::optional<Submission> fetchNextQueuedSubmission() const;
    bool requeueSubmission(long long id) const;

    void insertSubmissionResult(const SubmissionResult& result) const;
    void completeSubmission(
        long long id,
        const std::string& verdict,
        int score,
        const std::string& compile_stdout,
        const std::string& compile_stderr,
        std::optional<int> compile_time_ms,
        std::optional<int> max_time_ms,
        std::optional<int> max_memory_kb
    ) const;
    void failSubmission(
        long long id,
        const std::string& verdict,
        const std::string& message,
        const std::string& compile_stdout = {},
        const std::string& compile_stderr = {}
    ) const;
    void recordAcceptedSubmission(const Submission& submission) const;
    void recordRejectedSubmission(const Submission& submission, const std::string& verdict) const;

private:
    ConnectionPool& pool_;
};

} // namespace atp
