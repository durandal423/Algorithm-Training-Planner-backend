#include "repository/judge_repository.hpp"

#include "db/connection_pool.hpp"

#include <pqxx/pqxx>

#include <algorithm>

namespace atp {
namespace {

std::optional<std::string> optional_string(const pqxx::field& field) {
    if (field.is_null()) {
        return std::nullopt;
    }
    return field.as<std::string>();
}

std::optional<int> optional_int(const pqxx::field& field) {
    if (field.is_null()) {
        return std::nullopt;
    }
    return field.as<int>();
}

std::optional<long long> optional_long(const pqxx::field& field) {
    if (field.is_null()) {
        return std::nullopt;
    }
    return field.as<long long>();
}

std::string string_or_empty(const pqxx::field& field) {
    if (field.is_null()) {
        return {};
    }
    return field.as<std::string>();
}

JudgeConfig config_from_row(const pqxx::row& row) {
    JudgeConfig config;
    config.id = row["id"].as<long long>();
    config.problem_id = row["problem_id"].as<long long>();
    config.enabled = row["enabled"].as<bool>();
    config.language = row["language"].as<std::string>();
    config.compile_command_template = optional_string(row["compile_command_template"]);
    config.run_command_template = optional_string(row["run_command_template"]);
    config.time_limit_ms = row["time_limit_ms"].as<int>();
    config.memory_limit_mb = row["memory_limit_mb"].as<int>();
    config.output_limit_kb = row["output_limit_kb"].as<int>();
    config.compare_mode = row["compare_mode"].as<std::string>();
    config.float_epsilon = row["float_epsilon"].as<double>();
    config.official_solution_language = optional_string(row["official_solution_language"]);
    config.official_solution_code = optional_string(row["official_solution_code"]);
    config.checker_language = optional_string(row["checker_language"]);
    config.checker_code = optional_string(row["checker_code"]);
    config.created_at = string_or_empty(row["created_at"]);
    config.updated_at = string_or_empty(row["updated_at"]);
    return config;
}

JudgeTestCase test_case_from_row(const pqxx::row& row) {
    JudgeTestCase test_case;
    test_case.id = row["id"].as<long long>();
    test_case.problem_id = row["problem_id"].as<long long>();
    test_case.name = string_or_empty(row["name"]);
    test_case.input_data = row["input_data"].as<std::string>();
    test_case.expected_output = optional_string(row["expected_output"]);
    test_case.expected_output_hash = optional_string(row["expected_output_hash"]);
    test_case.visibility = row["visibility"].as<std::string>();
    test_case.points = row["points"].as<int>();
    test_case.order_index = row["order_index"].as<int>();
    test_case.time_limit_ms = optional_int(row["time_limit_ms"]);
    test_case.memory_limit_mb = optional_int(row["memory_limit_mb"]);
    test_case.is_sample = row["is_sample"].as<bool>();
    test_case.created_at = string_or_empty(row["created_at"]);
    test_case.updated_at = string_or_empty(row["updated_at"]);
    return test_case;
}

Submission submission_from_row(const pqxx::row& row) {
    Submission submission;
    submission.id = row["id"].as<long long>();
    submission.problem_id = row["problem_id"].as<long long>();
    submission.user_id = optional_long(row["user_id"]);
    submission.language = row["language"].as<std::string>();
    submission.source_code = string_or_empty(row["source_code"]);
    submission.status = row["status"].as<std::string>();
    submission.verdict = optional_string(row["verdict"]);
    submission.score = row["score"].as<int>();
    submission.compile_stdout = string_or_empty(row["compile_stdout"]);
    submission.compile_stderr = string_or_empty(row["compile_stderr"]);
    submission.compile_time_ms = optional_int(row["compile_time_ms"]);
    submission.max_time_ms = optional_int(row["max_time_ms"]);
    submission.max_memory_kb = optional_int(row["max_memory_kb"]);
    submission.submitted_at = string_or_empty(row["submitted_at"]);
    submission.started_at = optional_string(row["started_at"]);
    submission.finished_at = optional_string(row["finished_at"]);
    return submission;
}

SubmissionResult result_from_row(const pqxx::row& row) {
    SubmissionResult result;
    result.id = row["id"].as<long long>();
    result.submission_id = row["submission_id"].as<long long>();
    result.test_case_id = optional_long(row["test_case_id"]);
    result.order_index = row["order_index"].as<int>();
    result.status = row["status"].as<std::string>();
    result.verdict = optional_string(row["verdict"]);
    result.time_ms = optional_int(row["time_ms"]);
    result.memory_kb = optional_int(row["memory_kb"]);
    result.exit_code = optional_int(row["exit_code"]);
    result.stdout_sample = string_or_empty(row["stdout_sample"]);
    result.stderr_sample = string_or_empty(row["stderr_sample"]);
    result.message = string_or_empty(row["message"]);
    result.created_at = string_or_empty(row["created_at"]);
    return result;
}

std::vector<SubmissionResult> load_results(pqxx::transaction_base& tx, long long submission_id) {
    std::vector<SubmissionResult> results;
    const auto rows = tx.exec(
        R"SQL(
            SELECT id, submission_id, test_case_id, order_index, status, verdict, time_ms,
                   memory_kb, exit_code, stdout_sample, stderr_sample, message, created_at
            FROM submission_results
            WHERE submission_id = $1
            ORDER BY order_index, id
        )SQL",
        pqxx::params{submission_id}
    );
    for (const auto& row : rows) {
        results.push_back(result_from_row(row));
    }
    return results;
}

void refresh_plan_statuses_for_problem(pqxx::transaction_base& tx, long long problem_id) {
    tx.exec(
        R"SQL(
            WITH related_plans AS (
                SELECT DISTINCT plan_id
                FROM training_plan_items
                WHERE problem_id = $1
            ),
            item_stats AS (
                SELECT tpi.plan_id,
                       COUNT(*) AS total_items,
                       COUNT(*) FILTER (WHERE tpi.item_status = 'completed') AS completed_items,
                       COUNT(*) FILTER (WHERE tpi.item_status <> 'not_started') AS started_items
                FROM training_plan_items tpi
                JOIN related_plans rp ON rp.plan_id = tpi.plan_id
                GROUP BY tpi.plan_id
            )
            UPDATE training_plans tp
            SET status = CASE
                WHEN item_stats.total_items > 0
                  AND item_stats.completed_items = item_stats.total_items THEN 'completed'
                WHEN item_stats.started_items > 0 THEN 'in_progress'
                ELSE tp.status
            END
            FROM item_stats
            WHERE tp.id = item_stats.plan_id
              AND tp.status <> 'archived'
        )SQL",
        pqxx::params{problem_id}
    ).no_rows();
}

void update_plan_items_for_submission(
    pqxx::transaction_base& tx,
    const Submission& submission,
    const std::string& item_status,
    const std::string& verdict
) {
    const auto link = "submission:" + std::to_string(submission.id);
    tx.exec(
        R"SQL(
            WITH latest_records AS (
                SELECT DISTINCT ON (plan_id) id, plan_id
                FROM training_records
                WHERE code_link = $1
                  AND problem_id = $2
                  AND plan_id IS NOT NULL
                ORDER BY plan_id, id DESC
            )
            UPDATE training_plan_items tpi
            SET item_status = $3,
                last_submission_id = $4,
                last_training_record_id = latest_records.id,
                last_verdict = $5,
                last_updated_at = CURRENT_TIMESTAMP
            FROM latest_records
            JOIN training_plans tp ON tp.id = latest_records.plan_id
            WHERE tpi.plan_id = latest_records.plan_id
              AND tpi.problem_id = $2
              AND tp.status <> 'archived'
        )SQL",
        pqxx::params{link, submission.problem_id, item_status, submission.id, verdict}
    ).no_rows();
    refresh_plan_statuses_for_problem(tx, submission.problem_id);
}

} // namespace

JudgeRepository::JudgeRepository(ConnectionPool& pool) : pool_(pool) {}

std::optional<JudgeConfig> JudgeRepository::getConfig(long long problem_id) const {
    auto lease = pool_.acquire();
    pqxx::work tx{lease.connection()};
    const auto row = tx.exec(
        R"SQL(
            SELECT id, problem_id, enabled, language, compile_command_template,
                   run_command_template, time_limit_ms, memory_limit_mb, output_limit_kb,
                   compare_mode, float_epsilon, official_solution_language,
                   official_solution_code, checker_language, checker_code, created_at, updated_at
            FROM judge_configs
            WHERE problem_id = $1
        )SQL",
        pqxx::params{problem_id}
    ).opt_row();
    tx.commit();
    if (!row) {
        return std::nullopt;
    }
    return config_from_row(*row);
}

JudgeConfig JudgeRepository::upsertConfig(const JudgeConfig& config) const {
    auto lease = pool_.acquire();
    pqxx::work tx{lease.connection()};
    pqxx::params params;
    params.append(config.problem_id);
    params.append(config.enabled);
    params.append(config.language);
    params.append(config.compile_command_template);
    params.append(config.run_command_template);
    params.append(config.time_limit_ms);
    params.append(config.memory_limit_mb);
    params.append(config.output_limit_kb);
    params.append(config.compare_mode);
    params.append(config.float_epsilon);
    params.append(config.official_solution_language);
    params.append(config.official_solution_code);
    params.append(config.checker_language);
    params.append(config.checker_code);

    const auto row = tx.exec(
        R"SQL(
            INSERT INTO judge_configs(
                problem_id, enabled, language, compile_command_template, run_command_template,
                time_limit_ms, memory_limit_mb, output_limit_kb, compare_mode, float_epsilon,
                official_solution_language, official_solution_code, checker_language, checker_code
            )
            VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14)
            ON CONFLICT(problem_id) DO UPDATE SET
                enabled = EXCLUDED.enabled,
                language = EXCLUDED.language,
                compile_command_template = EXCLUDED.compile_command_template,
                run_command_template = EXCLUDED.run_command_template,
                time_limit_ms = EXCLUDED.time_limit_ms,
                memory_limit_mb = EXCLUDED.memory_limit_mb,
                output_limit_kb = EXCLUDED.output_limit_kb,
                compare_mode = EXCLUDED.compare_mode,
                float_epsilon = EXCLUDED.float_epsilon,
                official_solution_language = EXCLUDED.official_solution_language,
                official_solution_code = EXCLUDED.official_solution_code,
                checker_language = EXCLUDED.checker_language,
                checker_code = EXCLUDED.checker_code
            RETURNING id, problem_id, enabled, language, compile_command_template,
                      run_command_template, time_limit_ms, memory_limit_mb, output_limit_kb,
                      compare_mode, float_epsilon, official_solution_language,
                      official_solution_code, checker_language, checker_code, created_at, updated_at
        )SQL",
        params
    ).one_row();
    tx.commit();
    return config_from_row(row);
}

std::vector<JudgeTestCase> JudgeRepository::listTestCases(long long problem_id, bool include_hidden) const {
    auto lease = pool_.acquire();
    pqxx::work tx{lease.connection()};
    std::vector<JudgeTestCase> test_cases;
    const auto rows = tx.exec(
        R"SQL(
            SELECT id, problem_id, name, input_data, expected_output, expected_output_hash,
                   visibility, points, order_index, time_limit_ms, memory_limit_mb,
                   is_sample, created_at, updated_at
            FROM judge_test_cases
            WHERE problem_id = $1
              AND ($2 OR visibility = 'sample' OR is_sample)
            ORDER BY order_index, id
        )SQL",
        pqxx::params{problem_id, include_hidden}
    );
    for (const auto& row : rows) {
        test_cases.push_back(test_case_from_row(row));
    }
    tx.commit();
    return test_cases;
}

std::vector<JudgeTestCase> JudgeRepository::listJudgingTestCases(long long problem_id) const {
    auto lease = pool_.acquire();
    pqxx::work tx{lease.connection()};
    std::vector<JudgeTestCase> test_cases;
    const auto rows = tx.exec(
        R"SQL(
            SELECT id, problem_id, name, input_data, expected_output, expected_output_hash,
                   visibility, points, order_index, time_limit_ms, memory_limit_mb,
                   is_sample, created_at, updated_at
            FROM judge_test_cases
            WHERE problem_id = $1
            ORDER BY order_index, id
        )SQL",
        pqxx::params{problem_id}
    );
    for (const auto& row : rows) {
        test_cases.push_back(test_case_from_row(row));
    }
    tx.commit();
    return test_cases;
}

std::optional<JudgeTestCase> JudgeRepository::getTestCase(long long id) const {
    auto lease = pool_.acquire();
    pqxx::work tx{lease.connection()};
    const auto row = tx.exec(
        R"SQL(
            SELECT id, problem_id, name, input_data, expected_output, expected_output_hash,
                   visibility, points, order_index, time_limit_ms, memory_limit_mb,
                   is_sample, created_at, updated_at
            FROM judge_test_cases
            WHERE id = $1
        )SQL",
        pqxx::params{id}
    ).opt_row();
    tx.commit();
    if (!row) {
        return std::nullopt;
    }
    return test_case_from_row(*row);
}

long long JudgeRepository::createTestCase(const JudgeTestCase& test_case) const {
    auto lease = pool_.acquire();
    pqxx::work tx{lease.connection()};
    pqxx::params params;
    params.append(test_case.problem_id);
    params.append(test_case.name);
    params.append(test_case.input_data);
    params.append(test_case.expected_output);
    params.append(test_case.visibility);
    params.append(test_case.points);
    params.append(test_case.order_index);
    params.append(test_case.time_limit_ms);
    params.append(test_case.memory_limit_mb);
    params.append(test_case.is_sample);

    const auto id = tx.exec(
        R"SQL(
            INSERT INTO judge_test_cases(
                problem_id, name, input_data, expected_output, expected_output_hash,
                visibility, points, order_index, time_limit_ms, memory_limit_mb, is_sample
            )
            VALUES ($1, $2, $3, $4, CASE WHEN $4::text IS NULL THEN NULL ELSE md5($4::text) END,
                    $5, $6, $7, $8, $9, $10)
            RETURNING id
        )SQL",
        params
    ).one_field().as<long long>();
    tx.commit();
    return id;
}

bool JudgeRepository::updateTestCase(long long id, const JudgeTestCase& test_case) const {
    auto lease = pool_.acquire();
    pqxx::work tx{lease.connection()};
    pqxx::params params;
    params.append(test_case.name);
    params.append(test_case.input_data);
    params.append(test_case.expected_output);
    params.append(test_case.visibility);
    params.append(test_case.points);
    params.append(test_case.order_index);
    params.append(test_case.time_limit_ms);
    params.append(test_case.memory_limit_mb);
    params.append(test_case.is_sample);
    params.append(id);

    const auto result = tx.exec(
        R"SQL(
            UPDATE judge_test_cases
            SET name = $1,
                input_data = $2,
                expected_output = $3,
                expected_output_hash = CASE WHEN $3::text IS NULL THEN NULL ELSE md5($3::text) END,
                visibility = $4,
                points = $5,
                order_index = $6,
                time_limit_ms = $7,
                memory_limit_mb = $8,
                is_sample = $9
            WHERE id = $10
        )SQL",
        params
    );
    tx.commit();
    return result.affected_rows() > 0;
}

bool JudgeRepository::updateExpectedOutput(long long id, const std::string& expected_output) const {
    auto lease = pool_.acquire();
    pqxx::work tx{lease.connection()};
    const auto result = tx.exec(
        R"SQL(
            UPDATE judge_test_cases
            SET expected_output = $1,
                expected_output_hash = md5($1::text)
            WHERE id = $2
        )SQL",
        pqxx::params{expected_output, id}
    );
    tx.commit();
    return result.affected_rows() > 0;
}

bool JudgeRepository::deleteTestCase(long long id) const {
    auto lease = pool_.acquire();
    pqxx::work tx{lease.connection()};
    const auto result = tx.exec("DELETE FROM judge_test_cases WHERE id = $1", pqxx::params{id});
    tx.commit();
    return result.affected_rows() > 0;
}

long long JudgeRepository::createSubmission(const Submission& submission) const {
    auto lease = pool_.acquire();
    pqxx::work tx{lease.connection()};
    pqxx::params params;
    params.append(submission.problem_id);
    params.append(submission.user_id);
    params.append(submission.language);
    params.append(submission.source_code);

    const auto id = tx.exec(
        R"SQL(
            INSERT INTO submissions(problem_id, user_id, language, source_code, status)
            VALUES ($1, $2, $3, $4, 'queued')
            RETURNING id
        )SQL",
        params
    ).one_field().as<long long>();
    tx.commit();
    return id;
}

std::optional<Submission> JudgeRepository::getSubmission(long long id) const {
    auto lease = pool_.acquire();
    pqxx::work tx{lease.connection()};
    const auto row = tx.exec(
        R"SQL(
            SELECT id, problem_id, user_id, language, source_code, status, verdict, score,
                   compile_stdout, compile_stderr, compile_time_ms, max_time_ms, max_memory_kb,
                   submitted_at, started_at, finished_at
            FROM submissions
            WHERE id = $1
        )SQL",
        pqxx::params{id}
    ).opt_row();
    if (!row) {
        tx.commit();
        return std::nullopt;
    }
    auto submission = submission_from_row(*row);
    submission.results = load_results(tx, submission.id);
    tx.commit();
    return submission;
}

std::vector<Submission> JudgeRepository::listSubmissionsForProblem(long long problem_id, int limit) const {
    auto lease = pool_.acquire();
    pqxx::work tx{lease.connection()};
    const int capped_limit = std::clamp(limit, 1, 200);
    std::vector<Submission> submissions;
    const auto rows = tx.exec(
        R"SQL(
            SELECT id, problem_id, user_id, language, source_code, status, verdict, score,
                   compile_stdout, compile_stderr, compile_time_ms, max_time_ms, max_memory_kb,
                   submitted_at, started_at, finished_at
            FROM submissions
            WHERE problem_id = $1
            ORDER BY submitted_at DESC, id DESC
            LIMIT $2
        )SQL",
        pqxx::params{problem_id, capped_limit}
    );
    for (const auto& row : rows) {
        submissions.push_back(submission_from_row(row));
    }
    tx.commit();
    return submissions;
}

std::optional<Submission> JudgeRepository::fetchNextQueuedSubmission() const {
    auto lease = pool_.acquire();
    pqxx::work tx{lease.connection()};
    const auto row = tx.exec(
        R"SQL(
            WITH picked AS (
                SELECT id
                FROM submissions
                WHERE status = 'queued'
                ORDER BY submitted_at, id
                FOR UPDATE SKIP LOCKED
                LIMIT 1
            )
            UPDATE submissions s
            SET status = 'judging',
                verdict = NULL,
                score = 0,
                compile_stdout = NULL,
                compile_stderr = NULL,
                compile_time_ms = NULL,
                max_time_ms = NULL,
                max_memory_kb = NULL,
                started_at = CURRENT_TIMESTAMP,
                finished_at = NULL
            FROM picked
            WHERE s.id = picked.id
            RETURNING s.id, s.problem_id, s.user_id, s.language, s.source_code, s.status,
                      s.verdict, s.score, s.compile_stdout, s.compile_stderr,
                      s.compile_time_ms, s.max_time_ms, s.max_memory_kb,
                      s.submitted_at, s.started_at, s.finished_at
        )SQL"
    ).opt_row();
    if (!row) {
        tx.commit();
        return std::nullopt;
    }
    auto submission = submission_from_row(*row);
    tx.exec("DELETE FROM submission_results WHERE submission_id = $1", pqxx::params{submission.id}).no_rows();
    tx.commit();
    return submission;
}

bool JudgeRepository::requeueSubmission(long long id) const {
    auto lease = pool_.acquire();
    pqxx::work tx{lease.connection()};
    tx.exec("DELETE FROM submission_results WHERE submission_id = $1", pqxx::params{id}).no_rows();
    const auto result = tx.exec(
        R"SQL(
            UPDATE submissions
            SET status = 'queued',
                verdict = NULL,
                score = 0,
                compile_stdout = NULL,
                compile_stderr = NULL,
                compile_time_ms = NULL,
                max_time_ms = NULL,
                max_memory_kb = NULL,
                started_at = NULL,
                finished_at = NULL
            WHERE id = $1
        )SQL",
        pqxx::params{id}
    );
    tx.commit();
    return result.affected_rows() > 0;
}

void JudgeRepository::insertSubmissionResult(const SubmissionResult& result) const {
    auto lease = pool_.acquire();
    pqxx::work tx{lease.connection()};
    pqxx::params params;
    params.append(result.submission_id);
    params.append(result.test_case_id);
    params.append(result.order_index);
    params.append(result.status);
    params.append(result.verdict);
    params.append(result.time_ms);
    params.append(result.memory_kb);
    params.append(result.exit_code);
    params.append(result.stdout_sample);
    params.append(result.stderr_sample);
    params.append(result.message);

    tx.exec(
        R"SQL(
            INSERT INTO submission_results(
                submission_id, test_case_id, order_index, status, verdict, time_ms,
                memory_kb, exit_code, stdout_sample, stderr_sample, message
            )
            VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11)
        )SQL",
        params
    ).no_rows();
    tx.commit();
}

void JudgeRepository::completeSubmission(
    long long id,
    const std::string& verdict,
    int score,
    const std::string& compile_stdout,
    const std::string& compile_stderr,
    std::optional<int> compile_time_ms,
    std::optional<int> max_time_ms,
    std::optional<int> max_memory_kb
) const {
    auto lease = pool_.acquire();
    pqxx::work tx{lease.connection()};
    pqxx::params params;
    params.append(verdict);
    params.append(score);
    params.append(compile_stdout);
    params.append(compile_stderr);
    params.append(compile_time_ms);
    params.append(max_time_ms);
    params.append(max_memory_kb);
    params.append(id);

    tx.exec(
        R"SQL(
            UPDATE submissions
            SET status = 'completed',
                verdict = $1,
                score = $2,
                compile_stdout = $3,
                compile_stderr = $4,
                compile_time_ms = $5,
                max_time_ms = $6,
                max_memory_kb = $7,
                finished_at = CURRENT_TIMESTAMP
            WHERE id = $8
        )SQL",
        params
    ).no_rows();
    tx.commit();
}

void JudgeRepository::failSubmission(
    long long id,
    const std::string& verdict,
    const std::string& message,
    const std::string& compile_stdout,
    const std::string& compile_stderr
) const {
    auto lease = pool_.acquire();
    pqxx::work tx{lease.connection()};
    tx.exec(
        R"SQL(
            UPDATE submissions
            SET status = 'failed',
                verdict = $1,
                compile_stdout = $2,
                compile_stderr = $3,
                finished_at = CURRENT_TIMESTAMP
            WHERE id = $4
        )SQL",
        pqxx::params{verdict, compile_stdout, compile_stderr.empty() ? message : compile_stderr, id}
    ).no_rows();
    tx.commit();
}

void JudgeRepository::recordAcceptedSubmission(const Submission& submission) const {
    auto lease = pool_.acquire();
    pqxx::work tx{lease.connection()};
    const auto link = "submission:" + std::to_string(submission.id);
    const bool exists = tx.exec(
        "SELECT EXISTS(SELECT 1 FROM training_records WHERE code_link = $1)",
        pqxx::params{link}
    ).one_field().as<bool>();

    const auto prior_count = tx.exec(
        R"SQL(
            SELECT COUNT(*)
            FROM submissions
            WHERE problem_id = $1
              AND id <> $2
              AND (($3::bigint IS NULL AND user_id IS NULL) OR user_id = $3::bigint)
        )SQL",
        pqxx::params{submission.problem_id, submission.id, submission.user_id}
    ).one_field().as<long long>();
    const bool is_first_try_ac = prior_count == 0;
    const auto review_note = "Accepted by judge submission #" + std::to_string(submission.id);

    if (exists) {
        tx.exec(
            R"SQL(
                UPDATE training_records
                SET is_finished = TRUE,
                    is_first_try_ac = $1,
                    error_type = '',
                    review_note = $2,
                    duration_source = 'judge'
                WHERE code_link = $3
            )SQL",
            pqxx::params{is_first_try_ac, review_note, link}
        ).no_rows();
    } else {
        tx.exec(
            R"SQL(
                WITH related_plans AS (
                    SELECT DISTINCT tpi.plan_id
                    FROM training_plan_items tpi
                    JOIN training_plans tp ON tp.id = tpi.plan_id
                    WHERE tpi.problem_id = $1
                      AND tp.status <> 'archived'
                ),
                inserted AS (
                    INSERT INTO training_records(
                        plan_id, problem_id, is_finished, is_first_try_ac,
                        error_type, review_note, code_link, duration_source
                    )
                    SELECT plan_id, $1, TRUE, $2, '', $3, $4, 'judge'
                    FROM related_plans
                    RETURNING id
                )
                INSERT INTO training_records(
                    problem_id, is_finished, is_first_try_ac, error_type, review_note, code_link, duration_source
                )
                SELECT $1, TRUE, $2, '', $3, $4, 'judge'
                WHERE NOT EXISTS (SELECT 1 FROM inserted)
            )SQL",
            pqxx::params{submission.problem_id, is_first_try_ac, review_note, link}
        ).no_rows();
    }

    tx.exec(
        R"SQL(
            UPDATE problems
            SET is_completed = TRUE,
                last_practiced_at = CURRENT_TIMESTAMP
            WHERE id = $1
        )SQL",
        pqxx::params{submission.problem_id}
    ).no_rows();

    if (!exists) {
        tx.exec(
            R"SQL(
                UPDATE tags
                SET mastery_score = LEAST(100, GREATEST(0, mastery_score + $1)),
                    last_trained_at = CURRENT_TIMESTAMP
                WHERE id IN (
                    SELECT tag_id FROM problem_tags WHERE problem_id = $2
                )
            )SQL",
            pqxx::params{is_first_try_ac ? 5 : 2, submission.problem_id}
        ).no_rows();
    }

    update_plan_items_for_submission(tx, submission, "completed", "AC");

    tx.commit();
}

void JudgeRepository::recordRejectedSubmission(const Submission& submission, const std::string& verdict) const {
    if (verdict == "AC" || verdict == "JE") {
        return;
    }

    auto lease = pool_.acquire();
    pqxx::work tx{lease.connection()};
    const auto link = "submission:" + std::to_string(submission.id);
    const bool exists = tx.exec(
        "SELECT EXISTS(SELECT 1 FROM training_records WHERE code_link = $1)",
        pqxx::params{link}
    ).one_field().as<bool>();

    const auto error_type = "判题未通过：" + verdict;
    const auto review_note = "Judge submission #" + std::to_string(submission.id) + " verdict " + verdict;

    if (exists) {
        tx.exec(
            R"SQL(
                UPDATE training_records
                SET is_finished = FALSE,
                    is_first_try_ac = FALSE,
                    error_type = $1,
                    review_note = $2,
                    duration_source = 'judge'
                WHERE code_link = $3
            )SQL",
            pqxx::params{error_type, review_note, link}
        ).no_rows();
    } else {
        tx.exec(
            R"SQL(
                WITH related_plans AS (
                    SELECT DISTINCT tpi.plan_id
                    FROM training_plan_items tpi
                    JOIN training_plans tp ON tp.id = tpi.plan_id
                    WHERE tpi.problem_id = $1
                      AND tp.status <> 'archived'
                ),
                inserted AS (
                    INSERT INTO training_records(
                        plan_id, problem_id, is_finished, is_first_try_ac,
                        error_type, review_note, code_link, duration_source
                    )
                    SELECT plan_id, $1, FALSE, FALSE, $2, $3, $4, 'judge'
                    FROM related_plans
                    RETURNING id
                )
                INSERT INTO training_records(
                    problem_id, is_finished, is_first_try_ac, error_type, review_note, code_link, duration_source
                )
                SELECT $1, FALSE, FALSE, $2, $3, $4, 'judge'
                WHERE NOT EXISTS (SELECT 1 FROM inserted)
            )SQL",
            pqxx::params{submission.problem_id, error_type, review_note, link}
        ).no_rows();

        tx.exec(
            R"SQL(
                UPDATE problems
                SET is_wrong_problem = TRUE,
                    wrong_count = wrong_count + 1,
                    last_practiced_at = CURRENT_TIMESTAMP
                WHERE id = $1
            )SQL",
            pqxx::params{submission.problem_id}
        ).no_rows();

        tx.exec(
            R"SQL(
                UPDATE tags
                SET mastery_score = LEAST(100, GREATEST(0, mastery_score - 6)),
                    wrong_count = wrong_count + 1,
                    last_trained_at = CURRENT_TIMESTAMP
                WHERE id IN (
                    SELECT tag_id FROM problem_tags WHERE problem_id = $1
                )
            )SQL",
            pqxx::params{submission.problem_id}
        ).no_rows();
    }

    update_plan_items_for_submission(tx, submission, "failed", verdict);

    tx.commit();
}

} // namespace atp
