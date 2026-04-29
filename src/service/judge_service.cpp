#include "service/judge_service.hpp"

#include "judge/output_comparator.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <utility>

namespace atp {
namespace {

using Clock = std::chrono::steady_clock;

long long elapsed_ms(Clock::time_point started) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count();
}

std::filesystem::path make_submission_work_dir(long long submission_id) {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = std::filesystem::temp_directory_path() /
        ("atp-judge-" + std::to_string(submission_id) + "-" + std::to_string(now));
    std::filesystem::create_directories(path);
    return path;
}

bool counts_as_wrong_submission(const std::string& verdict) {
    return verdict != "AC" && verdict != "JE";
}

SubmissionResult make_result(
    const Submission& submission,
    const JudgeTestCase& test_case,
    const std::string& verdict,
    const SandboxRunResult& run,
    std::string message
) {
    SubmissionResult result;
    result.submission_id = submission.id;
    result.test_case_id = test_case.id;
    result.order_index = test_case.order_index;
    result.status = "completed";
    result.verdict = verdict;
    result.time_ms = run.time_ms;
    result.memory_kb = run.memory_kb;
    result.exit_code = run.exit_code;
    result.stdout_sample = run.stdout_sample;
    result.stderr_sample = run.stderr_sample;
    result.message = std::move(message);
    return result;
}

} // namespace

JudgeService::JudgeService(JudgeRepository& repository)
    : JudgeService(repository, SandboxRunner::fromEnvironment()) {}

JudgeService::JudgeService(JudgeRepository& repository, SandboxRunner runner)
    : repository_(repository), runner_(std::move(runner)) {}

bool JudgeService::processNextSubmission() {
    auto submission = repository_.fetchNextQueuedSubmission();
    if (!submission) {
        return false;
    }

    const auto submission_started = Clock::now();
    spdlog::info("judge submission picked up (submission_id={}, problem_id={})",
        submission->id,
        submission->problem_id);

    auto work_dir = make_submission_work_dir(submission->id);
    const auto log_finished = [&](const std::string& verdict, int score) {
        spdlog::info("judge submission finished in {} ms (submission_id={}, verdict={}, score={})",
            elapsed_ms(submission_started),
            submission->id,
            verdict,
            score);
    };
    try {
        const auto config = repository_.getConfig(submission->problem_id);
        if (!config || !config->enabled) {
            repository_.failSubmission(submission->id, "JE", "judge config is missing or disabled");
            log_finished("JE", 0);
            std::filesystem::remove_all(work_dir);
            return true;
        }
        if (submission->language != "cpp" || config->language != "cpp") {
            repository_.failSubmission(submission->id, "JE", "only cpp submissions are supported");
            log_finished("JE", 0);
            std::filesystem::remove_all(work_dir);
            return true;
        }

        const auto test_cases = repository_.listJudgingTestCases(submission->problem_id);
        if (test_cases.empty()) {
            repository_.failSubmission(submission->id, "JE", "no judge test cases configured");
            log_finished("JE", 0);
            std::filesystem::remove_all(work_dir);
            return true;
        }

        const auto compile_started = Clock::now();
        const auto compile = runner_.compile(submission->source_code, *config, work_dir);
        spdlog::info("judge compile completed in {} ms (submission_id={}, reported_time_ms={}, success={})",
            elapsed_ms(compile_started),
            submission->id,
            compile.time_ms,
            compile.success);
        if (!compile.success) {
            const auto verdict = compile.system_error ? "JE" : "CE";
            if (compile.system_error) {
                repository_.failSubmission(
                    submission->id,
                    verdict,
                    compile.message.empty() ? "judge runner failed during compilation" : compile.message,
                    compile.stdout_sample,
                    compile.stderr_sample
                );
            } else {
                repository_.completeSubmission(
                    submission->id,
                    verdict,
                    0,
                    compile.stdout_sample,
                    compile.stderr_sample,
                    compile.time_ms,
                    std::nullopt,
                    std::nullopt
                );
                if (counts_as_wrong_submission(verdict)) {
                    repository_.recordRejectedSubmission(*submission, verdict);
                }
            }
            log_finished(verdict, 0);
            std::filesystem::remove_all(work_dir);
            return true;
        }

        std::string final_verdict = "AC";
        int score = 0;
        int max_time_ms = 0;
        int max_memory_kb = 0;
        int run_index = 1;

        for (const auto& test_case : test_cases) {
            if (!test_case.expected_output) {
                SandboxRunResult empty_run;
                SubmissionResult result;
                result.submission_id = submission->id;
                result.test_case_id = test_case.id;
                result.order_index = test_case.order_index;
                result.status = "failed";
                result.verdict = "JE";
                result.message = "test case has no expected_output";
                repository_.insertSubmissionResult(result);
                final_verdict = "JE";
                break;
            }

            const auto current_run_index = run_index++;
            const auto run_started = Clock::now();
            const auto run = runner_.run(*config, test_case, work_dir, current_run_index);
            spdlog::debug("judge test run completed in {} ms (submission_id={}, test_case_id={}, run_index={}, reported_time_ms={}, exit_code={})",
                elapsed_ms(run_started),
                submission->id,
                test_case.id,
                current_run_index,
                run.time_ms,
                run.exit_code);
            max_time_ms = std::max(max_time_ms, run.time_ms);
            max_memory_kb = std::max(max_memory_kb, run.memory_kb);

            std::string verdict = "AC";
            std::string message = "accepted";
            if (run.system_error) {
                verdict = "JE";
                message = run.message.empty() ? "judge runner failed during execution" : run.message;
            } else if (run.timed_out) {
                verdict = "TLE";
                message = "time limit exceeded";
            } else if (run.output_limit_exceeded) {
                verdict = "OLE";
                message = "output limit exceeded";
            } else if (run.exit_code != 0) {
                verdict = "RE";
                message = "runtime error";
            } else {
                const auto comparison = compareOutputs(
                    *test_case.expected_output,
                    run.stdout_sample,
                    config->compare_mode,
                    config->float_epsilon
                );
                if (!comparison.accepted) {
                    verdict = "WA";
                    message = comparison.message;
                }
            }

            repository_.insertSubmissionResult(make_result(*submission, test_case, verdict, run, message));
            if (verdict == "AC") {
                score += test_case.points;
            } else {
                final_verdict = verdict;
                break;
            }
        }

        repository_.completeSubmission(
            submission->id,
            final_verdict,
            score,
            compile.stdout_sample,
            compile.stderr_sample,
            compile.time_ms,
            max_time_ms,
            max_memory_kb
        );
        if (final_verdict == "AC") {
            repository_.recordAcceptedSubmission(*submission);
        } else if (counts_as_wrong_submission(final_verdict)) {
            repository_.recordRejectedSubmission(*submission, final_verdict);
        }
        log_finished(final_verdict, score);
        std::filesystem::remove_all(work_dir);
        return true;
    } catch (const std::exception& error) {
        repository_.failSubmission(submission->id, "JE", error.what());
        log_finished("JE", 0);
        std::filesystem::remove_all(work_dir);
        return true;
    }
}

} // namespace atp
