#include "api/admin_auth.hpp"
#include "algorithms/training_dp.hpp"
#include "algorithms/window_selector.hpp"
#include "config.hpp"
#include "db/connection_pool.hpp"
#include "db/database.hpp"
#include "db/schema_initializer.hpp"
#include "judge/output_comparator.hpp"
#include "judge/sandbox_runner.hpp"
#include "repository/app_repository.hpp"
#include "repository/judge_repository.hpp"
#include "service/judge_service.hpp"

#include <catch2/catch_test_macros.hpp>
#include <pqxx/pqxx>

#include <chrono>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

using namespace atp;

namespace {

Tag make_tag(std::string name, int mastery = 70, int wrong_count = 0) {
    Tag tag;
    tag.name = std::move(name);
    tag.mastery_score = mastery;
    tag.wrong_count = wrong_count;
    return tag;
}

Problem make_problem(
    long long id,
    int difficulty,
    int minutes,
    std::vector<Tag> tags,
    bool wrong = false,
    int wrong_count = 0
) {
    Problem problem;
    problem.id = id;
    problem.problem_code = "T" + std::to_string(id);
    problem.title = "Problem " + std::to_string(id);
    problem.source_platform = "Test";
    problem.difficulty = difficulty;
    problem.estimated_minutes = minutes;
    problem.tags = std::move(tags);
    problem.is_wrong_problem = wrong;
    problem.wrong_count = wrong_count;
    return problem;
}

TrainingGoal make_goal() {
    TrainingGoal goal;
    goal.target_count = 2;
    goal.time_budget_minutes = 60;
    goal.difficulty_min = 900;
    goal.difficulty_max = 1800;
    goal.target_tag_names = {"动态规划", "双指针"};
    goal.prefer_wrong_problems = true;
    goal.prefer_weak_tags = true;
    return goal;
}

bool db_tests_enabled() {
#ifdef _WIN32
    char* value = nullptr;
    std::size_t size = 0;
    if (::_dupenv_s(&value, &size, "RUN_DB_TESTS") != 0 || value == nullptr) {
        return false;
    }
    const std::string text{value};
    std::free(value);
    return !text.empty();
#else
    return std::getenv("RUN_DB_TESTS") != nullptr;
#endif
}

void set_env_string(const char* key, const char* value) {
#ifdef _WIN32
    _putenv_s(key, value);
#else
    if (value == nullptr || std::string{value}.empty()) {
        unsetenv(key);
    } else {
        setenv(key, value, 1);
    }
#endif
}

std::string conninfo_for_test_database(const std::string& database_name) {
    return "host=" + std::string(config::DATABASE_HOST) +
           " port=" + std::string(config::DATABASE_PORT) +
           " dbname=" + database_name +
           " user=" + std::string(config::DATABASE_USER) +
           " password=" + std::string(config::DATABASE_PASSWORD) +
           " options='-c search_path=public'";
}

} // namespace

TEST_CASE("admin auth uses configured token and local fallback") {
    set_env_string("ATP_ADMIN_TOKEN", "");
    crow::request local_admin;
    local_admin.add_header("X-Admin-Token", "dev-admin");
    REQUIRE(isAdminRequest(local_admin));

    crow::request missing;
    REQUIRE_FALSE(isAdminRequest(missing));
    REQUIRE_THROWS_AS(requireAdmin(missing), ForbiddenAccess);

    set_env_string("ATP_ADMIN_TOKEN", "secret-token");
    crow::request configured_admin;
    configured_admin.add_header("X-Admin-Token", "secret-token");
    REQUIRE(isAdminRequest(configured_admin));
    REQUIRE_FALSE(isAdminRequest(local_admin));
    set_env_string("ATP_ADMIN_TOKEN", "");
}

TEST_CASE("sliding window returns empty result for empty problem set") {
    const auto window = selectStableDifficultyWindow({}, make_goal());
    REQUIRE(window.candidates.empty());
    REQUIRE(window.left_index == -1);
}

TEST_CASE("sliding window selects a stable difficulty candidate pool") {
    auto goal = make_goal();
    goal.target_count = 1;
    std::vector<Problem> problems = {
        make_problem(1, 900, 15, {make_tag("排序")}),
        make_problem(2, 1000, 20, {make_tag("动态规划")}),
        make_problem(3, 1050, 20, {make_tag("双指针")}),
        make_problem(4, 1100, 20, {make_tag("贪心")}),
        make_problem(5, 1600, 30, {make_tag("图论")})
    };

    const auto window = selectStableDifficultyWindow(problems, goal);

    REQUIRE(window.candidates.size() >= 3);
    REQUIRE(window.difficulty_span == 100);
    REQUIRE(window.covered_tags.size() == 2);
}

TEST_CASE("sliding window reports unavailable requested tags") {
    auto goal = make_goal();
    goal.target_count = 1;
    goal.target_tag_names = {"动态规划", "不存在标签"};
    std::vector<Problem> problems = {
        make_problem(1, 1000, 20, {make_tag("动态规划")}),
        make_problem(2, 1050, 20, {make_tag("排序")}),
        make_problem(3, 1100, 20, {make_tag("贪心")})
    };

    const auto window = selectStableDifficultyWindow(problems, goal);

    REQUIRE(window.covered_tags == std::vector<std::string>{"动态规划"});
    REQUIRE(window.uncovered_tags == std::vector<std::string>{"不存在标签"});
}

TEST_CASE("sliding window tie breaker prefers wrong problems") {
    auto goal = make_goal();
    goal.target_count = 1;
    goal.target_tag_names.clear();
    std::vector<Problem> problems = {
        make_problem(1, 1000, 20, {make_tag("排序")}),
        make_problem(2, 1100, 20, {make_tag("排序")}),
        make_problem(3, 1200, 20, {make_tag("排序")}),
        make_problem(4, 1300, 20, {make_tag("排序")}, true, 2)
    };

    const auto window = selectStableDifficultyWindow(problems, goal);

    REQUIRE(window.difficulty_span == 200);
    REQUIRE(window.wrong_problem_count == 1);
}

TEST_CASE("sliding window handles 10000 problems without combinational enumeration") {
    TrainingGoal goal;
    goal.target_count = 10;
    goal.time_budget_minutes = 180;
    goal.difficulty_min = 800;
    goal.difficulty_max = 2200;
    goal.target_tag_names = {"动态规划", "双指针", "图论"};

    std::vector<Problem> problems;
    problems.reserve(10000);
    for (int i = 0; i < 10000; ++i) {
        std::vector<Tag> tags;
        tags.push_back(make_tag(i % 3 == 0 ? "动态规划" : (i % 3 == 1 ? "双指针" : "图论")));
        problems.push_back(make_problem(i + 1, 800 + (i % 1400), 15 + (i % 30), tags));
    }

    const auto window = selectStableDifficultyWindow(problems, goal);

    REQUIRE(window.candidates.size() >= static_cast<std::size_t>(goal.target_count));
    REQUIRE(window.covered_tags.size() == 3);
}

TEST_CASE("DP returns no item when time budget is too small") {
    auto goal = make_goal();
    goal.time_budget_minutes = 5;
    const std::vector<Problem> problems = {
        make_problem(1, 1000, 20, {make_tag("动态规划")}),
        make_problem(2, 1100, 20, {make_tag("双指针")})
    };

    const auto result = optimizeTrainingPlanByDP(problems, goal);

    REQUIRE(result.items.empty());
    REQUIRE(result.total_estimated_time == 0);
}

TEST_CASE("DP respects count and time budget while covering target tags") {
    auto goal = make_goal();
    goal.target_count = 2;
    goal.time_budget_minutes = 45;
    const std::vector<Problem> problems = {
        make_problem(1, 1000, 25, {make_tag("动态规划")}),
        make_problem(2, 1100, 20, {make_tag("双指针")}),
        make_problem(3, 1200, 40, {make_tag("排序")})
    };

    const auto result = optimizeTrainingPlanByDP(problems, goal);

    REQUIRE(result.items.size() == 2);
    REQUIRE(result.total_estimated_time <= goal.time_budget_minutes);
    REQUIRE(result.covered_tags.size() == 2);
    REQUIRE(result.items[0].selected_reason.find("预计耗时符合时间预算") != std::string::npos);
}

TEST_CASE("DP scoring prefers wrong and weak-tag problems") {
    TrainingGoal goal;
    goal.target_count = 1;
    goal.time_budget_minutes = 30;
    goal.target_tag_names = {"动态规划"};
    goal.prefer_wrong_problems = true;
    goal.prefer_weak_tags = true;
    goal.wrong_problem_weight = 50;
    goal.weak_tag_weight = 50;

    const std::vector<Problem> problems = {
        make_problem(1, 1000, 20, {make_tag("动态规划", 90, 0)}),
        make_problem(2, 1100, 20, {make_tag("动态规划", 40, 3)}, true, 2)
    };

    const auto result = optimizeTrainingPlanByDP(problems, goal);

    REQUIRE(result.items.size() == 1);
    REQUIRE(result.items.front().problem.id == 2);
}

TEST_CASE("output comparator supports exact and trailing-trim modes") {
    REQUIRE(compareOutputs("1 2\n", "1 2\n", "exact", 1e-6).accepted);
    REQUIRE_FALSE(compareOutputs("1 2\n", "1 2", "exact", 1e-6).accepted);
    REQUIRE(compareOutputs("1 2  \r\n\n", "1 2\n", "trim_trailing", 1e-6).accepted);
    REQUIRE_FALSE(compareOutputs("1 2\n3\n", "1 2\n4\n", "trim_trailing", 1e-6).accepted);
}

TEST_CASE("output comparator supports token and floating point modes") {
    REQUIRE(compareOutputs("1 2 3\n", "1\n2\t3", "ignore_whitespace", 1e-6).accepted);
    REQUIRE_FALSE(compareOutputs("1 2 3", "1 2 4", "ignore_whitespace", 1e-6).accepted);
    REQUIRE(compareOutputs("3.1415926 answer", "3.1415927 answer", "float_epsilon", 1e-5).accepted);
    REQUIRE_FALSE(compareOutputs("3.1415926", "3.15", "float_epsilon", 1e-5).accepted);
}

TEST_CASE("optional DB schema initializer creates tables and seed data in an empty database") {
    if (!db_tests_enabled()) {
        SUCCEED("Set RUN_DB_TESTS=1 to run PostgreSQL schema initializer checks.");
        return;
    }

    const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto database_name = "atp_schema_init_" + suffix;

    {
        pqxx::connection admin{Database::adminConninfo()};
        pqxx::nontransaction ntx{admin};
        ntx.exec("CREATE DATABASE " + ntx.quote_name(database_name)).no_rows();
    }

    {
        pqxx::connection db{conninfo_for_test_database(database_name)};
        pqxx::work tx{db};
        tx.exec("DROP SCHEMA public CASCADE").no_rows();
        tx.commit();
    }

    try {
        const auto report = SchemaInitializer::initialize(conninfo_for_test_database(database_name));
        REQUIRE_FALSE(report.missing_tables_before.empty());
        REQUIRE(report.missing_tables_after.empty());
        REQUIRE(report.schema_sql_executed);
        REQUIRE(report.seed_sql_executed);

        pqxx::connection db{conninfo_for_test_database(database_name)};
        pqxx::work tx{db};
        REQUIRE(tx.exec("SELECT COUNT(*) FROM problems").one_field().as<long long>() >= 30);
        REQUIRE(tx.exec("SELECT COUNT(*) FROM tags").one_field().as<long long>() >= 10);
        REQUIRE(tx.exec("SELECT to_regclass('public.submissions') IS NOT NULL").one_field().as<bool>());
        tx.commit();
    } catch (...) {
        pqxx::connection admin{Database::adminConninfo()};
        pqxx::nontransaction ntx{admin};
        ntx.exec("DROP DATABASE IF EXISTS " + ntx.quote_name(database_name)).no_rows();
        throw;
    }

    pqxx::connection admin{Database::adminConninfo()};
    pqxx::nontransaction ntx{admin};
    ntx.exec("DROP DATABASE " + ntx.quote_name(database_name)).no_rows();
}

TEST_CASE("optional DB integration manages training records and timer sessions") {
    if (!db_tests_enabled()) {
        SUCCEED("Set RUN_DB_TESTS=1 to run PostgreSQL training record lifecycle checks.");
        return;
    }

    const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto database_name = "atp_record_lifecycle_" + suffix;

    {
        pqxx::connection admin{Database::adminConninfo()};
        pqxx::nontransaction ntx{admin};
        ntx.exec("CREATE DATABASE " + ntx.quote_name(database_name)).no_rows();
    }

    try {
        SchemaInitializer::initialize(conninfo_for_test_database(database_name));
        {
            ConnectionPool pool{conninfo_for_test_database(database_name), 2};
            AppRepository repository{pool};

            Problem problem;
            problem.problem_code = "REC-" + suffix;
            problem.title = "Training record lifecycle problem";
            problem.source_platform = "DBTest";
            problem.difficulty = 1200;
            problem.estimated_minutes = 20;
            const auto problem_id = repository.createProblem(problem, {"db-record-" + suffix});
            const auto stored_problem = repository.getProblem(problem_id);
            REQUIRE(stored_problem.has_value());

        CandidateWindow window;
        window.left_index = 0;
        window.right_index = 0;
        window.left_difficulty = stored_problem->difficulty;
        window.right_difficulty = stored_problem->difficulty;
        window.candidates = {*stored_problem};

        SelectedProblem selected;
        selected.problem = *stored_problem;
        selected.score = 80;
        selected.selected_reason = "training record lifecycle";

        TrainingPlanResult plan_result;
        plan_result.items = {selected};
        plan_result.total_estimated_time = stored_problem->estimated_minutes;
        plan_result.total_score = selected.score;

        const auto plan_id = repository.saveTrainingPlan(std::nullopt, "record lifecycle plan " + suffix, window, plan_result, "{}");
        const auto plan_item_id = repository.getTrainingPlanItems(plan_id).front().plan_item_id;

        TrainingRecord failed;
        failed.plan_id = plan_id;
        failed.problem_id = problem_id;
        failed.is_finished = false;
        failed.is_first_try_ac = false;
        failed.actual_minutes = 12;
        failed.error_type = "代码实现错误";
        const auto record_id = repository.createTrainingRecord(failed);
        REQUIRE(repository.getTrainingPlanItems(plan_id).front().item_status == "failed");
        REQUIRE(repository.getProblem(problem_id)->wrong_count == 1);

        TrainingRecord fixed = failed;
        fixed.is_finished = true;
        fixed.is_first_try_ac = true;
        fixed.actual_minutes = 18;
        fixed.error_type.clear();
        fixed.review_note = "fixed after review";
        const auto updated = repository.updateTrainingRecord(record_id, fixed);
        REQUIRE(updated.has_value());
        REQUIRE(updated->actual_minutes == 18);
        REQUIRE(updated->duration_source == "manual");
        REQUIRE(repository.getTrainingPlanItems(plan_id).front().item_status == "completed");
        REQUIRE(repository.getProblem(problem_id)->is_completed);
        REQUIRE(repository.getProblem(problem_id)->wrong_count == 0);

        REQUIRE(repository.deleteTrainingRecord(record_id));
        REQUIRE_FALSE(repository.getTrainingRecord(record_id).has_value());
        REQUIRE(repository.getTrainingPlanItems(plan_id).front().item_status == "not_started");
        REQUIRE_FALSE(repository.getProblem(problem_id)->is_completed);

        TrainingSession session;
        session.plan_id = plan_id;
        session.plan_item_id = plan_item_id;
        session.problem_id = problem_id;
        const auto started = repository.startTrainingSession(session);
        REQUIRE(started.has_value());
        REQUIRE_FALSE(repository.startTrainingSession(session).has_value());

        {
            pqxx::connection conn{conninfo_for_test_database(database_name)};
            pqxx::work tx{conn};
            tx.exec(
                "UPDATE training_sessions SET last_resumed_at = CURRENT_TIMESTAMP - INTERVAL '125 seconds' WHERE id = $1",
                pqxx::params{started->id}
            ).no_rows();
            tx.commit();
        }

        const auto paused = repository.pauseTrainingSession(started->id);
        REQUIRE(paused.has_value());
        REQUIRE(paused->status == "paused");
        REQUIRE(paused->elapsed_seconds >= 120);

        const auto resumed = repository.resumeTrainingSession(started->id);
        REQUIRE(resumed.has_value());
        REQUIRE(resumed->status == "running");

        {
            pqxx::connection conn{conninfo_for_test_database(database_name)};
            pqxx::work tx{conn};
            tx.exec(
                "UPDATE training_sessions SET last_resumed_at = CURRENT_TIMESTAMP - INTERVAL '65 seconds' WHERE id = $1",
                pqxx::params{started->id}
            ).no_rows();
            tx.commit();
        }

        TrainingRecord finish_review;
        finish_review.is_finished = true;
        finish_review.is_first_try_ac = false;
        finish_review.actual_minutes = 7;
        finish_review.error_type = "边界条件遗漏";
        finish_review.review_note = "timer review";
        const auto finished = repository.finishTrainingSession(started->id, finish_review);
        REQUIRE(finished.has_value());
        REQUIRE(finished->status == "completed");
        REQUIRE(finished->created_record_id.has_value());

        const auto timer_record = repository.getTrainingRecord(*finished->created_record_id);
        REQUIRE(timer_record.has_value());
        REQUIRE(timer_record->actual_minutes == 7);
        REQUIRE(timer_record->duration_source == "timer");
        REQUIRE(timer_record->started_at.has_value());
        REQUIRE(timer_record->ended_at.has_value());
            REQUIRE(repository.getTrainingPlanItems(plan_id).front().item_status == "completed");
        }

        pqxx::connection admin{Database::adminConninfo()};
        pqxx::nontransaction ntx{admin};
        ntx.exec("DROP DATABASE " + ntx.quote_name(database_name)).no_rows();
    } catch (...) {
        pqxx::connection admin{Database::adminConninfo()};
        pqxx::nontransaction ntx{admin};
        ntx.exec("DROP DATABASE IF EXISTS " + ntx.quote_name(database_name)).no_rows();
        throw;
    }
}

TEST_CASE("optional DB integration verifies seed, CRUD, and unbounded planning query") {
    if (!db_tests_enabled()) {
        SUCCEED("Set RUN_DB_TESTS=1 to run PostgreSQL integration checks.");
        return;
    }

    Database::ensureDatabase();
    Database::initializeSchemaAndSeed();
    ConnectionPool pool{Database::appConninfo(), 2};
    AppRepository repository{pool};

    REQUIRE(repository.listTags().size() >= 10);
    REQUIRE(repository.listAllProblems().size() >= 30);

    {
        pqxx::connection conn{Database::appConninfo()};
        pqxx::work tx{conn};
        tx.exec("DELETE FROM problems WHERE problem_code LIKE 'DBT-%'").no_rows();
        tx.exec("DELETE FROM tags WHERE name LIKE 'db-test-%'").no_rows();
        tx.commit();
    }

    const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    Tag tag;
    tag.name = "db-test-" + suffix;
    tag.description = "optional integration test tag";
    tag.mastery_score = 50;
    tag.wrong_count = 0;
    const auto tag_id = repository.createTag(tag);
    REQUIRE(tag_id > 0);
    REQUIRE(repository.getTag(tag_id).has_value());

    const auto prefix = "DBT-" + suffix + "-";
    {
        pqxx::connection conn{Database::appConninfo()};
        pqxx::work tx{conn};
        for (int i = 0; i < 205; ++i) {
            tx.exec(
                R"SQL(
                    INSERT INTO problems(problem_code, title, source_platform, difficulty, estimated_minutes)
                    VALUES ($1, $2, 'DBTest', $3, 10)
                    ON CONFLICT(problem_code) DO NOTHING
                )SQL",
                pqxx::params{
                    prefix + std::to_string(i),
                    "DB integration problem " + std::to_string(i),
                    800 + i
                }
            ).no_rows();
        }
        tx.commit();
    }

    REQUIRE(repository.listAllProblems().size() >= 205);

    {
        pqxx::connection conn{Database::appConninfo()};
        pqxx::work tx{conn};
        tx.exec(
            "DELETE FROM problems WHERE problem_code LIKE $1",
            pqxx::params{prefix + "%"}
        ).no_rows();
        tx.commit();
    }
}

TEST_CASE("optional DB integration deletes goals and plans while preserving history") {
    if (!db_tests_enabled()) {
        SUCCEED("Set RUN_DB_TESTS=1 to run PostgreSQL delete integration checks.");
        return;
    }

    Database::ensureDatabase();
    Database::initializeSchemaAndSeed();
    ConnectionPool pool{Database::appConninfo(), 2};
    AppRepository repository{pool};

    const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    Problem problem;
    problem.problem_code = "DEL-" + suffix;
    problem.title = "Delete integration problem";
    problem.source_platform = "DBTest";
    problem.difficulty = 1200;
    problem.estimated_minutes = 20;
    const auto problem_id = repository.createProblem(problem, {"db-delete-" + suffix});
    auto stored_problem = repository.getProblem(problem_id);
    REQUIRE(stored_problem.has_value());

    TrainingGoal goal;
    goal.name = "delete integration goal " + suffix;
    goal.target_count = 1;
    goal.time_budget_minutes = 30;
    goal.difficulty_min = 1000;
    goal.difficulty_max = 1400;
    goal.target_tag_names = {"db-delete-" + suffix};
    const auto goal_id = repository.createTrainingGoal(goal);

    CandidateWindow window;
    window.left_index = 0;
    window.right_index = 0;
    window.left_difficulty = stored_problem->difficulty;
    window.right_difficulty = stored_problem->difficulty;
    window.difficulty_span = 0;
    window.candidates = {*stored_problem};
    window.covered_tags = goal.target_tag_names;
    window.coverage_ratio = 1.0;

    SelectedProblem selected;
    selected.problem = *stored_problem;
    selected.score = 100;
    selected.selected_reason = "删除集成测试";
    selected.covered_target_tags = goal.target_tag_names;

    TrainingPlanResult result;
    result.items = {selected};
    result.total_estimated_time = stored_problem->estimated_minutes;
    result.total_score = selected.score;
    result.covered_tags = goal.target_tag_names;

    const auto plan_id = repository.saveTrainingPlan(goal_id, "delete integration plan " + suffix, window, result, "{}");
    auto initial_items = repository.getTrainingPlanItems(plan_id);
    REQUIRE(initial_items.size() == 1);
    REQUIRE(initial_items.front().item_status == "not_started");

    REQUIRE(repository.deleteTrainingGoal(goal_id));
    REQUIRE_FALSE(repository.getTrainingGoal(goal_id).has_value());

    auto plan_after_goal_delete = repository.getTrainingPlan(plan_id);
    REQUIRE(plan_after_goal_delete.has_value());
    REQUIRE_FALSE(plan_after_goal_delete->goal_id.has_value());

    TrainingRecord failed_record;
    failed_record.plan_id = plan_id;
    failed_record.problem_id = problem_id;
    failed_record.is_finished = false;
    failed_record.is_first_try_ac = false;
    failed_record.error_type = "代码实现错误";
    failed_record.code_link = "delete-test-failed:" + suffix;
    const auto failed_record_id = repository.createTrainingRecord(failed_record);
    auto failed_items = repository.getTrainingPlanItems(plan_id);
    REQUIRE(failed_items.size() == 1);
    REQUIRE(failed_items.front().item_status == "failed");

    TrainingRecord record;
    record.plan_id = plan_id;
    record.problem_id = problem_id;
    record.is_finished = true;
    record.is_first_try_ac = false;
    record.error_type = "边界条件遗漏";
    record.code_link = "delete-test:" + suffix;
    const auto record_id = repository.createTrainingRecord(record);
    auto completed_items = repository.getTrainingPlanItems(plan_id);
    REQUIRE(completed_items.size() == 1);
    REQUIRE(completed_items.front().item_status == "completed");
    REQUIRE(completed_items.front().last_training_record_id == record_id);
    REQUIRE(completed_items.front().last_error_type == "边界条件遗漏");
    REQUIRE(completed_items.front().last_is_first_try_ac == false);

    REQUIRE(repository.deleteTrainingPlan(plan_id));
    REQUIRE_FALSE(repository.getTrainingPlan(plan_id).has_value());
    REQUIRE_FALSE(repository.deleteTrainingPlan(plan_id));

    {
        pqxx::connection conn{Database::appConninfo()};
        pqxx::work tx{conn};
        const auto row = tx.exec(
            "SELECT plan_id FROM training_records WHERE id = $1",
            pqxx::params{record_id}
        ).one_row();
        REQUIRE(row["plan_id"].is_null());
        const auto failed_row = tx.exec(
            "SELECT plan_id FROM training_records WHERE id = $1",
            pqxx::params{failed_record_id}
        ).one_row();
        REQUIRE(failed_row["plan_id"].is_null());
        tx.exec("DELETE FROM training_records WHERE id = $1", pqxx::params{record_id}).no_rows();
        tx.exec("DELETE FROM training_records WHERE id = $1", pqxx::params{failed_record_id}).no_rows();
        tx.exec("DELETE FROM problems WHERE id = $1", pqxx::params{problem_id}).no_rows();
        tx.exec("DELETE FROM tags WHERE name = $1", pqxx::params{"db-delete-" + suffix}).no_rows();
        tx.commit();
    }
}

TEST_CASE("optional DB judge integration handles AC, WA, CE, and TLE") {
    if (!db_tests_enabled()) {
        SUCCEED("Set RUN_DB_TESTS=1 to run PostgreSQL judge integration checks.");
        return;
    }

    Database::ensureDatabase();
    Database::initializeSchemaAndSeed();
    ConnectionPool pool{Database::appConninfo(), 4};
    AppRepository app_repository{pool};
    JudgeRepository judge_repository{pool};
    JudgeService judge_service{
        judge_repository,
        SandboxRunner{SandboxRunner::Mode::Local, "gcc:13"}
    };

    {
        pqxx::connection conn{Database::appConninfo()};
        pqxx::work tx{conn};
        tx.exec(
            R"SQL(
                DELETE FROM training_records
                WHERE problem_id IN (
                    SELECT id FROM problems WHERE problem_code LIKE 'JUDGE-%'
                )
            )SQL"
        ).no_rows();
        tx.exec(
            R"SQL(
                DELETE FROM training_plans
                WHERE id IN (
                    SELECT tpi.plan_id
                    FROM training_plan_items tpi
                    JOIN problems p ON p.id = tpi.problem_id
                    WHERE p.problem_code LIKE 'JUDGE-%'
                )
            )SQL"
        ).no_rows();
        tx.exec("DELETE FROM problems WHERE problem_code LIKE 'JUDGE-%'").no_rows();
        tx.commit();
    }

    const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    Problem problem;
    problem.problem_code = "JUDGE-" + suffix;
    problem.title = "Judge integration problem";
    problem.source_platform = "DBTest";
    problem.difficulty = 1000;
    problem.estimated_minutes = 10;
    const auto problem_id = app_repository.createProblem(problem, {});

    JudgeConfig config;
    config.problem_id = problem_id;
    config.enabled = true;
    config.language = "cpp";
    config.time_limit_ms = 300;
    config.memory_limit_mb = 256;
    config.output_limit_kb = 64;
    config.compare_mode = "ignore_whitespace";
    judge_repository.upsertConfig(config);

    JudgeTestCase sample;
    sample.problem_id = problem_id;
    sample.name = "sample";
    sample.input_data = "3\n1 2 3\n";
    sample.expected_output = "6\n";
    sample.visibility = "sample";
    sample.points = 1;
    sample.order_index = 1;
    sample.is_sample = true;
    judge_repository.createTestCase(sample);

    JudgeTestCase hidden;
    hidden.problem_id = problem_id;
    hidden.name = "hidden";
    hidden.input_data = "2\n10 -4\n";
    hidden.expected_output = "6\n";
    hidden.visibility = "hidden";
    hidden.points = 1;
    hidden.order_index = 2;
    judge_repository.createTestCase(hidden);

    auto judge_problem_source = [&](long long target_problem_id, const std::string& source) {
        Submission submission;
        submission.problem_id = target_problem_id;
        submission.language = "cpp";
        submission.source_code = source;
        const auto id = judge_repository.createSubmission(submission);
        REQUIRE(judge_service.processNextSubmission());
        auto judged = judge_repository.getSubmission(id);
        REQUIRE(judged.has_value());
        return *judged;
    };
    auto judge_source = [&](const std::string& source) {
        return judge_problem_source(problem_id, source);
    };

    const auto stored_problem = app_repository.getProblem(problem_id);
    REQUIRE(stored_problem.has_value());
    CandidateWindow window;
    window.left_index = 0;
    window.right_index = 0;
    window.left_difficulty = stored_problem->difficulty;
    window.right_difficulty = stored_problem->difficulty;
    window.candidates = {*stored_problem};
    SelectedProblem selected;
    selected.problem = *stored_problem;
    selected.score = 100;
    selected.selected_reason = "judge status integration";
    TrainingPlanResult plan_result;
    plan_result.items = {selected};
    plan_result.total_estimated_time = stored_problem->estimated_minutes;
    plan_result.total_score = selected.score;
    const auto plan_id = app_repository.saveTrainingPlan(std::nullopt, "judge status plan " + suffix, window, plan_result, "{}");
    {
        const auto items = app_repository.getTrainingPlanItems(plan_id);
        REQUIRE(items.size() == 1);
        REQUIRE(items.front().item_status == "not_started");
    }

    const auto wrong = judge_source(
        "#include <bits/stdc++.h>\n"
        "int main(){ std::cout << 0 << '\\n'; }\n"
    );
    REQUIRE(wrong.status == "completed");
    REQUIRE(wrong.verdict == "WA");
    REQUIRE(wrong.results.size() == 1);
    {
        const auto after_wrong = app_repository.getProblem(problem_id);
        REQUIRE(after_wrong.has_value());
        REQUIRE(after_wrong->is_wrong_problem);
        REQUIRE(after_wrong->wrong_count == 1);
    }
    {
        const auto items = app_repository.getTrainingPlanItems(plan_id);
        REQUIRE(items.size() == 1);
        REQUIRE(items.front().item_status == "failed");
        REQUIRE(items.front().last_submission_id == wrong.id);
        REQUIRE(items.front().last_verdict == "WA");
    }

    const auto accepted = judge_source(
        "#include <bits/stdc++.h>\n"
        "using namespace std;\n"
        "int main(){int n; if(!(cin>>n)) return 0; long long s=0,x; while(n--&&cin>>x) s+=x; cout<<s<<'\\n';}\n"
    );
    REQUIRE(accepted.status == "completed");
    REQUIRE(accepted.verdict == "AC");
    REQUIRE(accepted.results.size() == 2);
    {
        const auto after_accept = app_repository.getProblem(problem_id);
        REQUIRE(after_accept.has_value());
        REQUIRE(after_accept->is_completed);
        REQUIRE(after_accept->is_wrong_problem);
        REQUIRE(after_accept->wrong_count == 1);
        const auto items = app_repository.getTrainingPlanItems(plan_id);
        REQUIRE(items.size() == 1);
        REQUIRE(items.front().item_status == "completed");
        REQUIRE(items.front().last_submission_id == accepted.id);
        REQUIRE(items.front().last_verdict == "AC");
        REQUIRE(app_repository.getTrainingPlan(plan_id)->status == "completed");
    }

    const auto compile_error = judge_source("int main(){\n");
    REQUIRE(compile_error.status == "completed");
    REQUIRE(compile_error.verdict == "CE");
    REQUIRE(app_repository.getProblem(problem_id)->wrong_count == 2);

    const auto time_limit = judge_source("int main(){ while(true){} }\n");
    REQUIRE(time_limit.status == "completed");
    REQUIRE(time_limit.verdict == "TLE");
    REQUIRE(time_limit.results.size() == 1);
    REQUIRE(app_repository.getProblem(problem_id)->wrong_count == 3);

    Problem other_problem;
    other_problem.problem_code = "JUDGE-OTHER-" + suffix;
    other_problem.title = "Other judge integration problem";
    other_problem.source_platform = "DBTest";
    other_problem.difficulty = 1000;
    other_problem.estimated_minutes = 10;
    const auto other_problem_id = app_repository.createProblem(other_problem, {});

    JudgeConfig other_config = config;
    other_config.problem_id = other_problem_id;
    judge_repository.upsertConfig(other_config);

    JudgeTestCase other_case;
    other_case.problem_id = other_problem_id;
    other_case.name = "other sample";
    other_case.input_data = "1\n";
    other_case.expected_output = "1\n";
    other_case.visibility = "sample";
    other_case.points = 1;
    other_case.order_index = 1;
    other_case.is_sample = true;
    judge_repository.createTestCase(other_case);

    const auto other_wrong = judge_problem_source(
        other_problem_id,
        "#include <bits/stdc++.h>\n"
        "int main(){ std::cout << 0 << '\\n'; }\n"
    );
    REQUIRE(other_wrong.status == "completed");
    REQUIRE(other_wrong.verdict == "WA");
    REQUIRE(app_repository.getProblem(problem_id)->wrong_count == 3);
    REQUIRE(app_repository.getProblem(other_problem_id)->wrong_count == 1);
    {
        const auto items = app_repository.getTrainingPlanItems(plan_id);
        REQUIRE(items.size() == 1);
        REQUIRE(items.front().item_status == "failed");
        REQUIRE(items.front().last_submission_id == time_limit.id);
    }

    REQUIRE(app_repository.deleteTrainingPlan(plan_id));

    {
        pqxx::connection conn{Database::appConninfo()};
        pqxx::work tx{conn};
        tx.exec("DELETE FROM training_records WHERE problem_id = $1", pqxx::params{problem_id}).no_rows();
        tx.exec("DELETE FROM training_records WHERE problem_id = $1", pqxx::params{other_problem_id}).no_rows();
        tx.exec("DELETE FROM problems WHERE id = $1", pqxx::params{problem_id}).no_rows();
        tx.exec("DELETE FROM problems WHERE id = $1", pqxx::params{other_problem_id}).no_rows();
        tx.commit();
    }
}
