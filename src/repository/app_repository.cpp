#include "repository/app_repository.hpp"

#include "db/connection_pool.hpp"

#include <pqxx/pqxx>

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace atp {
namespace {

std::optional<std::string> optional_string(const pqxx::field& field) {
    if (field.is_null()) {
        return std::nullopt;
    }
    return field.as<std::string>();
}

std::optional<long long> optional_long(const pqxx::field& field) {
    if (field.is_null()) {
        return std::nullopt;
    }
    return field.as<long long>();
}

std::optional<bool> optional_bool(const pqxx::field& field) {
    if (field.is_null()) {
        return std::nullopt;
    }
    return field.as<bool>();
}

std::string string_or_empty(const pqxx::field& field) {
    if (field.is_null()) {
        return {};
    }
    return field.as<std::string>();
}

void replace_all(std::string& text, const std::string& from, const std::string& to) {
    std::size_t position = 0;
    while ((position = text.find(from, position)) != std::string::npos) {
        text.replace(position, from.size(), to);
        position += to.size();
    }
}

std::string localized_selected_reason(std::string reason) {
    replace_all(reason, "inside stable difficulty window", "位于稳定难度范围");
    replace_all(reason, "covers target tags: ", "覆盖目标标签：");
    replace_all(reason, "historical wrong problem", "历史错题优先复习");
    replace_all(reason, "covers weak tags", "覆盖薄弱标签");
    replace_all(reason, "fits time budget", "预计耗时符合时间预算");
    replace_all(reason, ", ", "、");
    replace_all(reason, "; ", "；");
    return reason;
}

Tag tag_from_row(const pqxx::row& row) {
    Tag tag;
    tag.id = row["id"].as<long long>();
    tag.name = row["name"].as<std::string>();
    tag.description = string_or_empty(row["description"]);
    tag.mastery_score = row["mastery_score"].as<int>();
    tag.wrong_count = row["wrong_count"].as<int>();
    tag.last_trained_at = optional_string(row["last_trained_at"]);
    return tag;
}

Problem problem_from_row(const pqxx::row& row) {
    Problem problem;
    problem.id = row["id"].as<long long>();
    problem.problem_code = row["problem_code"].as<std::string>();
    problem.title = row["title"].as<std::string>();
    problem.source_platform = row["source_platform"].as<std::string>();
    problem.source_url = string_or_empty(row["source_url"]);
    problem.difficulty = row["difficulty"].as<int>();
    problem.estimated_minutes = row["estimated_minutes"].as<int>();
    problem.summary = string_or_empty(row["summary"]);
    problem.is_completed = row["is_completed"].as<bool>();
    problem.is_wrong_problem = row["is_wrong_problem"].as<bool>();
    problem.wrong_count = row["wrong_count"].as<int>();
    problem.last_practiced_at = optional_string(row["last_practiced_at"]);
    problem.created_at = string_or_empty(row["created_at"]);
    problem.updated_at = string_or_empty(row["updated_at"]);
    return problem;
}

TrainingGoal goal_from_row(const pqxx::row& row) {
    TrainingGoal goal;
    goal.id = row["id"].as<long long>();
    if (!row["user_id"].is_null()) {
        goal.user_id = row["user_id"].as<long long>();
    }
    goal.name = row["name"].as<std::string>();
    goal.description = string_or_empty(row["description"]);
    goal.target_count = row["target_count"].as<int>();
    goal.time_budget_minutes = row["time_budget_minutes"].as<int>();
    if (!row["difficulty_min"].is_null()) {
        goal.difficulty_min = row["difficulty_min"].as<int>();
    }
    if (!row["difficulty_max"].is_null()) {
        goal.difficulty_max = row["difficulty_max"].as<int>();
    }
    goal.prefer_wrong_problems = row["prefer_wrong_problems"].as<bool>();
    goal.prefer_weak_tags = row["prefer_weak_tags"].as<bool>();
    goal.difficulty_weight = row["difficulty_weight"].as<int>();
    goal.tag_coverage_weight = row["tag_coverage_weight"].as<int>();
    goal.wrong_problem_weight = row["wrong_problem_weight"].as<int>();
    goal.weak_tag_weight = row["weak_tag_weight"].as<int>();
    goal.estimated_time_weight = row["estimated_time_weight"].as<int>();
    goal.created_at = string_or_empty(row["created_at"]);
    return goal;
}

TrainingPlanSummary plan_from_row(const pqxx::row& row) {
    TrainingPlanSummary plan;
    plan.id = row["id"].as<long long>();
    if (!row["goal_id"].is_null()) {
        plan.goal_id = row["goal_id"].as<long long>();
    }
    plan.name = row["name"].as<std::string>();
    plan.candidate_count = row["candidate_count"].as<int>();
    plan.selected_count = row["selected_count"].as<int>();
    plan.total_estimated_time = row["total_estimated_time"].as<int>();
    plan.total_score = row["total_score"].as<int>();
    plan.difficulty_span = row["difficulty_span"].as<int>();
    plan.covered_tag_mask = row["covered_tag_mask"].as<int>();
    plan.status = row["status"].as<std::string>();
    plan.algorithm_summary = string_or_empty(row["algorithm_summary"]);
    plan.created_at = string_or_empty(row["created_at"]);
    return plan;
}

TrainingRecord record_from_row(const pqxx::row& row) {
    TrainingRecord record;
    record.id = row["id"].as<long long>();
    if (!row["plan_id"].is_null()) {
        record.plan_id = row["plan_id"].as<long long>();
    }
    if (!row["problem_id"].is_null()) {
        record.problem_id = row["problem_id"].as<long long>();
    }
    record.is_finished = row["is_finished"].as<bool>();
    record.is_first_try_ac = row["is_first_try_ac"].as<bool>();
    if (!row["actual_minutes"].is_null()) {
        record.actual_minutes = row["actual_minutes"].as<int>();
    }
    record.error_type = string_or_empty(row["error_type"]);
    record.review_note = string_or_empty(row["review_note"]);
    record.code_link = string_or_empty(row["code_link"]);
    record.practiced_at = string_or_empty(row["practiced_at"]);
    record.started_at = optional_string(row["started_at"]);
    record.ended_at = optional_string(row["ended_at"]);
    record.duration_source = row["duration_source"].as<std::string>();
    return record;
}

TrainingSession session_from_row(const pqxx::row& row) {
    TrainingSession session;
    session.id = row["id"].as<long long>();
    session.plan_id = optional_long(row["plan_id"]);
    session.plan_item_id = optional_long(row["plan_item_id"]);
    session.problem_id = row["problem_id"].as<long long>();
    session.status = row["status"].as<std::string>();
    session.started_at = string_or_empty(row["started_at"]);
    session.last_resumed_at = string_or_empty(row["last_resumed_at"]);
    session.accumulated_seconds = row["accumulated_seconds"].as<int>();
    session.elapsed_seconds = row["elapsed_seconds"].as<int>();
    session.finished_at = optional_string(row["finished_at"]);
    session.created_record_id = optional_long(row["created_record_id"]);
    return session;
}

std::vector<std::string> tag_names_from_problem(const Problem& problem) {
    std::vector<std::string> names;
    names.reserve(problem.tags.size());
    for (const auto& tag : problem.tags) {
        names.push_back(tag.name);
    }
    return names;
}

std::vector<std::string> tag_names_from_goal(const TrainingGoal& goal) {
    if (!goal.target_tag_names.empty()) {
        return goal.target_tag_names;
    }
    std::vector<std::string> names;
    names.reserve(goal.target_tags.size());
    for (const auto& tag : goal.target_tags) {
        names.push_back(tag.name);
    }
    return names;
}

constexpr const char* kTrainingRecordColumns = R"SQL(
    id, plan_id, problem_id, is_finished, is_first_try_ac, actual_minutes,
    error_type, review_note, code_link, practiced_at, started_at, ended_at, duration_source
)SQL";

constexpr const char* kTrainingSessionColumns = R"SQL(
    id, plan_id, plan_item_id, problem_id, status, started_at, last_resumed_at,
    accumulated_seconds, finished_at, created_record_id,
    accumulated_seconds + CASE
        WHEN status = 'running' THEN GREATEST(0, FLOOR(EXTRACT(EPOCH FROM (CURRENT_TIMESTAMP - last_resumed_at)))::integer)
        ELSE 0
    END AS elapsed_seconds
)SQL";

std::string add_param(pqxx::params& params, const auto& value) {
    params.append(value);
    return "$" + std::to_string(params.size());
}

std::string problem_filter_from_sql(const ProblemFilter& filter, pqxx::params& params) {
    std::vector<std::string> where;
    std::string sql = " FROM problems p";

    if (!filter.tag.empty()) {
        sql += " JOIN problem_tags pt_filter ON pt_filter.problem_id = p.id"
               " JOIN tags t_filter ON t_filter.id = pt_filter.tag_id";
        where.push_back("t_filter.name = " + add_param(params, filter.tag));
    }
    if (!filter.keyword.empty()) {
        where.push_back("(p.problem_code ILIKE " + add_param(params, "%" + filter.keyword + "%") +
            " OR p.title ILIKE " + add_param(params, "%" + filter.keyword + "%") + ")");
    }
    if (!filter.platform.empty()) {
        where.push_back("p.source_platform = " + add_param(params, filter.platform));
    }
    if (filter.difficulty_min) {
        where.push_back("p.difficulty >= " + add_param(params, *filter.difficulty_min));
    }
    if (filter.difficulty_max) {
        where.push_back("p.difficulty <= " + add_param(params, *filter.difficulty_max));
    }
    if (filter.completed) {
        where.push_back("p.is_completed = " + add_param(params, *filter.completed));
    }
    if (filter.wrong) {
        where.push_back("p.is_wrong_problem = " + add_param(params, *filter.wrong));
    }
    if (filter.last_practiced_from) {
        where.push_back("p.last_practiced_at >= " + add_param(params, *filter.last_practiced_from) + "::timestamp");
    }
    if (filter.last_practiced_to) {
        where.push_back("p.last_practiced_at <= " + add_param(params, *filter.last_practiced_to) + "::timestamp");
    }
    if (!where.empty()) {
        sql += " WHERE ";
        for (std::size_t i = 0; i < where.size(); ++i) {
            if (i > 0) {
                sql += " AND ";
            }
            sql += where[i];
        }
    }
    return sql;
}

void attach_problem_tags(pqxx::transaction_base& tx, std::vector<Problem>& problems) {
    if (problems.empty()) {
        return;
    }

    std::unordered_map<long long, std::size_t> index_by_id;
    pqxx::params params;
    std::string placeholders;
    for (const auto& problem : problems) {
        index_by_id[problem.id] = index_by_id.size();
        if (!placeholders.empty()) {
            placeholders += ", ";
        }
        placeholders += add_param(params, problem.id);
    }

    const auto rows = tx.exec(
        R"SQL(
            SELECT pt.problem_id, t.id, t.name, t.description, t.mastery_score,
                   t.wrong_count, t.last_trained_at
            FROM problem_tags pt
            JOIN tags t ON t.id = pt.tag_id
            WHERE pt.problem_id IN ()SQL" + placeholders + R"SQL()
            ORDER BY pt.problem_id, t.name
        )SQL",
        params
    );

    for (const auto& row : rows) {
        const auto problem_id = row["problem_id"].as<long long>();
        if (const auto iter = index_by_id.find(problem_id); iter != index_by_id.end()) {
            problems[iter->second].tags.push_back(tag_from_row(row));
        }
    }
}

} // namespace

AppRepository::AppRepository(ConnectionPool& pool) : pool_(pool) {}

std::vector<Tag> AppRepository::loadProblemTags(pqxx::transaction_base& tx, long long problem_id) const {
    std::vector<Tag> tags;
    const auto rows = tx.exec(
        R"SQL(
            SELECT t.id, t.name, t.description, t.mastery_score, t.wrong_count, t.last_trained_at
            FROM tags t
            JOIN problem_tags pt ON pt.tag_id = t.id
            WHERE pt.problem_id = $1
            ORDER BY t.name
        )SQL",
        pqxx::params{problem_id}
    );
    for (const auto& row : rows) {
        tags.push_back(tag_from_row(row));
    }
    return tags;
}

std::vector<Tag> AppRepository::loadGoalTags(pqxx::transaction_base& tx, long long goal_id) const {
    std::vector<Tag> tags;
    const auto rows = tx.exec(
        R"SQL(
            SELECT t.id, t.name, t.description, t.mastery_score, t.wrong_count, t.last_trained_at
            FROM tags t
            JOIN training_goal_tags tgt ON tgt.tag_id = t.id
            WHERE tgt.goal_id = $1
            ORDER BY t.name
        )SQL",
        pqxx::params{goal_id}
    );
    for (const auto& row : rows) {
        tags.push_back(tag_from_row(row));
    }
    return tags;
}

long long AppRepository::upsertTag(pqxx::transaction_base& tx, const std::string& name) const {
    return tx.exec(
        R"SQL(
            INSERT INTO tags(name)
            VALUES ($1)
            ON CONFLICT(name) DO UPDATE SET name = EXCLUDED.name
            RETURNING id
        )SQL",
        pqxx::params{name}
    ).one_field().as<long long>();
}

void AppRepository::replaceProblemTags(
    pqxx::transaction_base& tx,
    long long problem_id,
    const std::vector<std::string>& names
) const {
    tx.exec("DELETE FROM problem_tags WHERE problem_id = $1", pqxx::params{problem_id}).no_rows();
    std::unordered_set<std::string> seen;
    for (const auto& name : names) {
        if (name.empty() || seen.contains(name)) {
            continue;
        }
        seen.insert(name);
        const auto tag_id = upsertTag(tx, name);
        tx.exec(
            "INSERT INTO problem_tags(problem_id, tag_id) VALUES ($1, $2) ON CONFLICT DO NOTHING",
            pqxx::params{problem_id, tag_id}
        ).no_rows();
    }
}

void AppRepository::replaceGoalTags(
    pqxx::transaction_base& tx,
    long long goal_id,
    const std::vector<std::string>& names
) const {
    tx.exec("DELETE FROM training_goal_tags WHERE goal_id = $1", pqxx::params{goal_id}).no_rows();
    std::unordered_set<std::string> seen;
    for (const auto& name : names) {
        if (name.empty() || seen.contains(name)) {
            continue;
        }
        seen.insert(name);
        const auto tag_id = upsertTag(tx, name);
        tx.exec(
            "INSERT INTO training_goal_tags(goal_id, tag_id) VALUES ($1, $2) ON CONFLICT DO NOTHING",
            pqxx::params{goal_id, tag_id}
        ).no_rows();
    }
}

ProblemPage AppRepository::listProblems(const ProblemFilter& filter) const {
    auto lease = pool_.acquire();
    pqxx::work tx{lease.connection()};

    pqxx::params count_params;
    const auto count_sql = "SELECT COUNT(DISTINCT p.id)" + problem_filter_from_sql(filter, count_params);
    const auto total_count = tx.exec(count_sql, count_params).one_field().as<long long>();

    pqxx::params params;
    std::string sql = R"SQL(
        SELECT DISTINCT p.id, p.problem_code, p.title, p.source_platform, p.source_url,
               p.difficulty, p.estimated_minutes, p.summary, p.is_completed,
               p.is_wrong_problem, p.wrong_count, p.last_practiced_at, p.created_at, p.updated_at
    )SQL" + problem_filter_from_sql(filter, params);

    const int page_size = std::clamp(filter.page_size, 1, 200);
    const int page = std::max(1, filter.page);
    sql += " ORDER BY p.difficulty, p.id LIMIT " + add_param(params, page_size) +
           " OFFSET " + add_param(params, (page - 1) * page_size);

    std::vector<Problem> problems;
    for (const auto& row : tx.exec(sql, params)) {
        auto problem = problem_from_row(row);
        problems.push_back(std::move(problem));
    }
    attach_problem_tags(tx, problems);
    tx.commit();
    return ProblemPage{std::move(problems), total_count, page, page_size};
}

std::vector<Problem> AppRepository::listAllProblems() const {
    auto lease = pool_.acquire();
    pqxx::work tx{lease.connection()};
    std::vector<Problem> problems;
    std::unordered_map<long long, std::size_t> index_by_id;

    const auto rows = tx.exec(
        R"SQL(
            SELECT id, problem_code, title, source_platform, source_url, difficulty,
                   estimated_minutes, summary, is_completed, is_wrong_problem, wrong_count,
                   last_practiced_at, created_at, updated_at
            FROM problems
            ORDER BY difficulty, id
        )SQL"
    );

    problems.reserve(rows.size());
    for (const auto& row : rows) {
        auto problem = problem_from_row(row);
        index_by_id[problem.id] = problems.size();
        problems.push_back(std::move(problem));
    }

    const auto tag_rows = tx.exec(
        R"SQL(
            SELECT pt.problem_id, t.id, t.name, t.description, t.mastery_score,
                   t.wrong_count, t.last_trained_at
            FROM problem_tags pt
            JOIN tags t ON t.id = pt.tag_id
            ORDER BY pt.problem_id, t.name
        )SQL"
    );
    for (const auto& row : tag_rows) {
        const auto problem_id = row["problem_id"].as<long long>();
        if (const auto iter = index_by_id.find(problem_id); iter != index_by_id.end()) {
            problems[iter->second].tags.push_back(tag_from_row(row));
        }
    }

    tx.commit();
    return problems;
}

std::optional<Problem> AppRepository::getProblem(long long id) const {
    auto lease = pool_.acquire();
    pqxx::work tx{lease.connection()};
    const auto row = tx.exec(
        R"SQL(
            SELECT id, problem_code, title, source_platform, source_url, difficulty,
                   estimated_minutes, summary, is_completed, is_wrong_problem, wrong_count,
                   last_practiced_at, created_at, updated_at
            FROM problems
            WHERE id = $1
        )SQL",
        pqxx::params{id}
    ).opt_row();

    if (!row) {
        tx.commit();
        return std::nullopt;
    }

    auto problem = problem_from_row(*row);
    problem.tags = loadProblemTags(tx, problem.id);
    tx.commit();
    return problem;
}

long long AppRepository::createProblem(const Problem& problem, const std::vector<std::string>& tag_names) const {
    auto lease = pool_.acquire();
    pqxx::work tx{lease.connection()};
    const auto id = tx.exec(
        R"SQL(
            INSERT INTO problems(problem_code, title, source_platform, source_url, difficulty,
                                 estimated_minutes, summary, is_completed, is_wrong_problem, wrong_count)
            VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10)
            RETURNING id
        )SQL",
        pqxx::params{
            problem.problem_code,
            problem.title,
            problem.source_platform,
            problem.source_url,
            problem.difficulty,
            problem.estimated_minutes,
            problem.summary,
            problem.is_completed,
            problem.is_wrong_problem,
            problem.wrong_count
        }
    ).one_field().as<long long>();

    replaceProblemTags(tx, id, tag_names.empty() ? tag_names_from_problem(problem) : tag_names);
    tx.commit();
    return id;
}

bool AppRepository::updateProblem(
    long long id,
    const Problem& problem,
    const std::vector<std::string>& tag_names
) const {
    auto lease = pool_.acquire();
    pqxx::work tx{lease.connection()};
    const auto result = tx.exec(
        R"SQL(
            UPDATE problems
            SET problem_code = $1, title = $2, source_platform = $3, source_url = $4,
                difficulty = $5, estimated_minutes = $6, summary = $7,
                is_completed = $8, is_wrong_problem = $9, wrong_count = $10,
                updated_at = CURRENT_TIMESTAMP
            WHERE id = $11
        )SQL",
        pqxx::params{
            problem.problem_code,
            problem.title,
            problem.source_platform,
            problem.source_url,
            problem.difficulty,
            problem.estimated_minutes,
            problem.summary,
            problem.is_completed,
            problem.is_wrong_problem,
            problem.wrong_count,
            id
        }
    );
    if (result.affected_rows() == 0) {
        tx.commit();
        return false;
    }
    replaceProblemTags(tx, id, tag_names.empty() ? tag_names_from_problem(problem) : tag_names);
    tx.commit();
    return true;
}

bool AppRepository::deleteProblem(long long id) const {
    auto lease = pool_.acquire();
    pqxx::work tx{lease.connection()};
    const auto result = tx.exec("DELETE FROM problems WHERE id = $1", pqxx::params{id});
    tx.commit();
    return result.affected_rows() > 0;
}

std::vector<Tag> AppRepository::listTags() const {
    auto lease = pool_.acquire();
    pqxx::work tx{lease.connection()};
    std::vector<Tag> tags;
    const auto rows = tx.exec(
        "SELECT id, name, description, mastery_score, wrong_count, last_trained_at FROM tags ORDER BY name"
    );
    for (const auto& row : rows) {
        tags.push_back(tag_from_row(row));
    }
    tx.commit();
    return tags;
}

std::optional<Tag> AppRepository::getTag(long long id) const {
    auto lease = pool_.acquire();
    pqxx::work tx{lease.connection()};
    const auto row = tx.exec(
        "SELECT id, name, description, mastery_score, wrong_count, last_trained_at FROM tags WHERE id = $1",
        pqxx::params{id}
    ).opt_row();
    if (!row) {
        tx.commit();
        return std::nullopt;
    }
    auto tag = tag_from_row(*row);
    tx.commit();
    return tag;
}

long long AppRepository::createTag(const Tag& tag) const {
    auto lease = pool_.acquire();
    pqxx::work tx{lease.connection()};
    const auto id = tx.exec(
        R"SQL(
            INSERT INTO tags(name, description, mastery_score, wrong_count)
            VALUES ($1, $2, $3, $4)
            ON CONFLICT(name) DO UPDATE
            SET description = EXCLUDED.description,
                mastery_score = EXCLUDED.mastery_score,
                wrong_count = EXCLUDED.wrong_count
            RETURNING id
        )SQL",
        pqxx::params{tag.name, tag.description, tag.mastery_score, tag.wrong_count}
    ).one_field().as<long long>();
    tx.commit();
    return id;
}

bool AppRepository::updateTag(long long id, const Tag& tag) const {
    auto lease = pool_.acquire();
    pqxx::work tx{lease.connection()};
    const auto result = tx.exec(
        R"SQL(
            UPDATE tags
            SET name = $1, description = $2, mastery_score = $3, wrong_count = $4
            WHERE id = $5
        )SQL",
        pqxx::params{tag.name, tag.description, tag.mastery_score, tag.wrong_count, id}
    );
    tx.commit();
    return result.affected_rows() > 0;
}

std::vector<TrainingGoal> AppRepository::listTrainingGoals() const {
    auto lease = pool_.acquire();
    pqxx::work tx{lease.connection()};
    std::vector<TrainingGoal> goals;
    const auto rows = tx.exec(
        R"SQL(
            SELECT id, user_id, name, description, target_count, time_budget_minutes,
                   difficulty_min, difficulty_max, prefer_wrong_problems, prefer_weak_tags,
                   difficulty_weight, tag_coverage_weight, wrong_problem_weight,
                   weak_tag_weight, estimated_time_weight, created_at
            FROM training_goals
            ORDER BY id DESC
        )SQL"
    );
    for (const auto& row : rows) {
        auto goal = goal_from_row(row);
        goal.target_tags = loadGoalTags(tx, goal.id);
        goal.target_tag_names = tag_names_from_goal(goal);
        goals.push_back(std::move(goal));
    }
    tx.commit();
    return goals;
}

std::optional<TrainingGoal> AppRepository::getTrainingGoal(long long id) const {
    auto lease = pool_.acquire();
    pqxx::work tx{lease.connection()};
    const auto row = tx.exec(
        R"SQL(
            SELECT id, user_id, name, description, target_count, time_budget_minutes,
                   difficulty_min, difficulty_max, prefer_wrong_problems, prefer_weak_tags,
                   difficulty_weight, tag_coverage_weight, wrong_problem_weight,
                   weak_tag_weight, estimated_time_weight, created_at
            FROM training_goals
            WHERE id = $1
        )SQL",
        pqxx::params{id}
    ).opt_row();
    if (!row) {
        tx.commit();
        return std::nullopt;
    }
    auto goal = goal_from_row(*row);
    goal.target_tags = loadGoalTags(tx, goal.id);
    goal.target_tag_names = tag_names_from_goal(goal);
    tx.commit();
    return goal;
}

long long AppRepository::createTrainingGoal(const TrainingGoal& goal) const {
    auto lease = pool_.acquire();
    pqxx::work tx{lease.connection()};
    pqxx::params params;
    params.append(goal.user_id);
    params.append(goal.name);
    params.append(goal.description);
    params.append(goal.target_count);
    params.append(goal.time_budget_minutes);
    params.append(goal.difficulty_min);
    params.append(goal.difficulty_max);
    params.append(goal.prefer_wrong_problems);
    params.append(goal.prefer_weak_tags);
    params.append(goal.difficulty_weight);
    params.append(goal.tag_coverage_weight);
    params.append(goal.wrong_problem_weight);
    params.append(goal.weak_tag_weight);
    params.append(goal.estimated_time_weight);

    const auto id = tx.exec(
        R"SQL(
            INSERT INTO training_goals(
                user_id, name, description, target_count, time_budget_minutes,
                difficulty_min, difficulty_max, prefer_wrong_problems, prefer_weak_tags,
                difficulty_weight, tag_coverage_weight, wrong_problem_weight,
                weak_tag_weight, estimated_time_weight
            )
            VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14)
            RETURNING id
        )SQL",
        params
    ).one_field().as<long long>();

    replaceGoalTags(tx, id, tag_names_from_goal(goal));
    tx.commit();
    return id;
}

bool AppRepository::updateTrainingGoal(long long id, const TrainingGoal& goal) const {
    auto lease = pool_.acquire();
    pqxx::work tx{lease.connection()};
    pqxx::params params;
    params.append(goal.user_id);
    params.append(goal.name);
    params.append(goal.description);
    params.append(goal.target_count);
    params.append(goal.time_budget_minutes);
    params.append(goal.difficulty_min);
    params.append(goal.difficulty_max);
    params.append(goal.prefer_wrong_problems);
    params.append(goal.prefer_weak_tags);
    params.append(goal.difficulty_weight);
    params.append(goal.tag_coverage_weight);
    params.append(goal.wrong_problem_weight);
    params.append(goal.weak_tag_weight);
    params.append(goal.estimated_time_weight);
    params.append(id);

    const auto result = tx.exec(
        R"SQL(
            UPDATE training_goals
            SET user_id = $1, name = $2, description = $3, target_count = $4,
                time_budget_minutes = $5, difficulty_min = $6, difficulty_max = $7,
                prefer_wrong_problems = $8, prefer_weak_tags = $9,
                difficulty_weight = $10, tag_coverage_weight = $11,
                wrong_problem_weight = $12, weak_tag_weight = $13,
                estimated_time_weight = $14
            WHERE id = $15
        )SQL",
        params
    );
    if (result.affected_rows() == 0) {
        tx.commit();
        return false;
    }
    replaceGoalTags(tx, id, tag_names_from_goal(goal));
    tx.commit();
    return true;
}

bool AppRepository::deleteTrainingGoal(long long id) const {
    auto lease = pool_.acquire();
    pqxx::work tx{lease.connection()};
    tx.exec("UPDATE training_plans SET goal_id = NULL WHERE goal_id = $1", pqxx::params{id}).no_rows();
    const auto result = tx.exec("DELETE FROM training_goals WHERE id = $1", pqxx::params{id});
    tx.commit();
    return result.affected_rows() > 0;
}

long long AppRepository::saveTrainingPlan(
    std::optional<long long> goal_id,
    const std::string& name,
    const CandidateWindow& window,
    const TrainingPlanResult& result,
    const std::string& algorithm_summary
) const {
    auto lease = pool_.acquire();
    pqxx::work tx{lease.connection()};
    pqxx::params params;
    params.append(goal_id);
    params.append(name);
    params.append(static_cast<int>(window.candidates.size()));
    params.append(static_cast<int>(result.items.size()));
    params.append(result.total_estimated_time);
    params.append(result.total_score);
    params.append(window.difficulty_span);
    params.append(result.covered_tag_mask);
    params.append(algorithm_summary);

    const auto plan_id = tx.exec(
        R"SQL(
            INSERT INTO training_plans(
                goal_id, name, candidate_count, selected_count, total_estimated_time,
                total_score, difficulty_span, covered_tag_mask, algorithm_summary
            )
            VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9)
            RETURNING id
        )SQL",
        params
    ).one_field().as<long long>();

    int order = 1;
    for (const auto& item : result.items) {
        tx.exec(
            R"SQL(
                INSERT INTO training_plan_items(
                    plan_id, problem_id, order_index, estimated_minutes, score, selected_reason
                )
                VALUES ($1, $2, $3, $4, $5, $6)
            )SQL",
            pqxx::params{
                plan_id,
                item.problem.id,
                order++,
                item.problem.estimated_minutes,
                item.score,
                item.selected_reason
            }
        ).no_rows();
    }

    tx.commit();
    return plan_id;
}

std::vector<TrainingPlanSummary> AppRepository::listTrainingPlans() const {
    auto lease = pool_.acquire();
    pqxx::work tx{lease.connection()};
    std::vector<TrainingPlanSummary> plans;
    const auto rows = tx.exec(
        R"SQL(
            SELECT id, goal_id, name, candidate_count, selected_count, total_estimated_time,
                   total_score, difficulty_span, covered_tag_mask, status,
                   algorithm_summary, created_at
            FROM training_plans
            ORDER BY id DESC
        )SQL"
    );
    for (const auto& row : rows) {
        plans.push_back(plan_from_row(row));
    }
    tx.commit();
    return plans;
}

std::optional<TrainingPlanSummary> AppRepository::getTrainingPlan(long long id) const {
    auto lease = pool_.acquire();
    pqxx::work tx{lease.connection()};
    const auto row = tx.exec(
        R"SQL(
            SELECT id, goal_id, name, candidate_count, selected_count, total_estimated_time,
                   total_score, difficulty_span, covered_tag_mask, status,
                   algorithm_summary, created_at
            FROM training_plans
            WHERE id = $1
        )SQL",
        pqxx::params{id}
    ).opt_row();
    if (!row) {
        tx.commit();
        return std::nullopt;
    }
    auto plan = plan_from_row(*row);
    tx.commit();
    return plan;
}

std::vector<SelectedProblem> AppRepository::getTrainingPlanItems(long long plan_id) const {
    auto lease = pool_.acquire();
    pqxx::work tx{lease.connection()};
    std::vector<SelectedProblem> items;
    const auto rows = tx.exec(
        R"SQL(
            SELECT p.id, p.problem_code, p.title, p.source_platform, p.source_url,
                   p.difficulty, p.estimated_minutes, p.summary, p.is_completed,
                   p.is_wrong_problem, p.wrong_count, p.last_practiced_at,
                   p.created_at, p.updated_at,
                   tpi.id AS plan_item_id, tpi.score, tpi.selected_reason,
                   tpi.item_status, tpi.last_submission_id, tpi.last_training_record_id,
                   tpi.last_verdict, tpi.last_updated_at,
                   tr.error_type AS last_error_type,
                   tr.is_first_try_ac AS last_is_first_try_ac
            FROM training_plan_items tpi
            JOIN problems p ON p.id = tpi.problem_id
            LEFT JOIN training_records tr ON tr.id = tpi.last_training_record_id
            WHERE tpi.plan_id = $1
            ORDER BY tpi.order_index
        )SQL",
        pqxx::params{plan_id}
    );
    for (const auto& row : rows) {
        SelectedProblem item;
        item.problem = problem_from_row(row);
        item.problem.tags = loadProblemTags(tx, item.problem.id);
        item.plan_item_id = row["plan_item_id"].as<long long>();
        item.score = row["score"].as<int>();
        item.selected_reason = localized_selected_reason(string_or_empty(row["selected_reason"]));
        item.item_status = row["item_status"].as<std::string>();
        item.last_submission_id = optional_long(row["last_submission_id"]);
        item.last_training_record_id = optional_long(row["last_training_record_id"]);
        item.last_verdict = optional_string(row["last_verdict"]);
        item.last_updated_at = optional_string(row["last_updated_at"]);
        item.last_error_type = optional_string(row["last_error_type"]);
        item.last_is_first_try_ac = optional_bool(row["last_is_first_try_ac"]);
        items.push_back(std::move(item));
    }
    tx.commit();
    return items;
}

bool AppRepository::updateTrainingPlanStatus(long long id, const std::string& status) const {
    auto lease = pool_.acquire();
    pqxx::work tx{lease.connection()};
    const auto result = tx.exec(
        "UPDATE training_plans SET status = $1 WHERE id = $2",
        pqxx::params{status, id}
    );
    tx.commit();
    return result.affected_rows() > 0;
}

bool AppRepository::deleteTrainingPlan(long long id) const {
    auto lease = pool_.acquire();
    pqxx::work tx{lease.connection()};
    tx.exec("UPDATE training_records SET plan_id = NULL WHERE plan_id = $1", pqxx::params{id}).no_rows();
    const auto result = tx.exec("DELETE FROM training_plans WHERE id = $1", pqxx::params{id});
    tx.commit();
    return result.affected_rows() > 0;
}

void AppRepository::refreshPlanStatusFromItems(pqxx::transaction_base& tx, long long plan_id) const {
    tx.exec(
        R"SQL(
            WITH item_stats AS (
                SELECT COUNT(*) AS total_items,
                       COUNT(*) FILTER (WHERE item_status = 'completed') AS completed_items,
                       COUNT(*) FILTER (WHERE item_status <> 'not_started') AS started_items
                FROM training_plan_items
                WHERE plan_id = $1
            )
            UPDATE training_plans tp
            SET status = CASE
                WHEN item_stats.total_items > 0
                  AND item_stats.completed_items = item_stats.total_items THEN 'completed'
                WHEN item_stats.started_items > 0 THEN 'in_progress'
                ELSE 'not_started'
            END
            FROM item_stats
            WHERE tp.id = $1
              AND tp.status <> 'archived'
        )SQL",
        pqxx::params{plan_id}
    ).no_rows();
}

void AppRepository::updatePlanItemFromTrainingRecord(
    pqxx::transaction_base& tx,
    const TrainingRecord& record,
    long long record_id
) const {
    (void)record_id;
    if (!record.plan_id || !record.problem_id) {
        return;
    }

    recalculatePlanItemProgress(tx, *record.plan_id, *record.problem_id);
}

long long AppRepository::insertTrainingRecord(pqxx::transaction_base& tx, const TrainingRecord& record) const {
    pqxx::params params;
    params.append(record.plan_id);
    params.append(record.problem_id);
    params.append(record.is_finished);
    params.append(record.is_first_try_ac);
    params.append(record.actual_minutes);
    params.append(record.error_type);
    params.append(record.review_note);
    params.append(record.code_link);
    params.append(record.practiced_at);
    params.append(record.started_at);
    params.append(record.ended_at);
    params.append(record.duration_source);

    return tx.exec(
        R"SQL(
            INSERT INTO training_records(
                plan_id, problem_id, is_finished, is_first_try_ac,
                actual_minutes, error_type, review_note, code_link,
                practiced_at, started_at, ended_at, duration_source
            )
            VALUES (
                $1, $2, $3, $4, $5, $6, $7, $8,
                COALESCE(NULLIF($9, '')::timestamp, CURRENT_TIMESTAMP),
                $10, $11, $12
            )
            RETURNING id
        )SQL",
        params
    ).one_field().as<long long>();
}

void AppRepository::recalculateProblemProgress(pqxx::transaction_base& tx, long long problem_id) const {
    tx.exec(
        R"SQL(
            WITH stats AS (
                SELECT
                    COALESCE(BOOL_OR(is_finished), FALSE) AS is_completed,
                    COALESCE(COUNT(*) FILTER (
                        WHERE NOT is_finished
                           OR NOT is_first_try_ac
                           OR COALESCE(error_type, '') <> ''
                    ), 0) AS wrong_count,
                    MAX(practiced_at) AS last_practiced_at
                FROM training_records
                WHERE problem_id = $1
            )
            UPDATE problems
            SET is_completed = stats.is_completed,
                is_wrong_problem = stats.wrong_count > 0,
                wrong_count = stats.wrong_count,
                last_practiced_at = stats.last_practiced_at
            FROM stats
            WHERE problems.id = $1
        )SQL",
        pqxx::params{problem_id}
    ).no_rows();
}

void AppRepository::recalculateTagProgressForProblem(pqxx::transaction_base& tx, long long problem_id) const {
    tx.exec(
        R"SQL(
            WITH affected_tags AS (
                SELECT tag_id
                FROM problem_tags
                WHERE problem_id = $1
            ),
            stats AS (
                SELECT
                    at.tag_id,
                    COALESCE(COUNT(tr.id) FILTER (
                        WHERE NOT tr.is_finished
                           OR NOT tr.is_first_try_ac
                           OR COALESCE(tr.error_type, '') <> ''
                    ), 0) AS wrong_count,
                    COALESCE(SUM(
                        CASE
                            WHEN tr.id IS NULL THEN 0
                            WHEN tr.is_finished AND tr.is_first_try_ac THEN 5
                            WHEN tr.is_finished THEN 2
                            ELSE -6
                        END
                    ), 0) AS mastery_delta,
                    MAX(tr.practiced_at) AS last_trained_at
                FROM affected_tags at
                LEFT JOIN problem_tags pt ON pt.tag_id = at.tag_id
                LEFT JOIN training_records tr ON tr.problem_id = pt.problem_id
                GROUP BY at.tag_id
            )
            UPDATE tags
            SET wrong_count = stats.wrong_count,
                mastery_score = LEAST(100, GREATEST(0, 50 + stats.mastery_delta)),
                last_trained_at = stats.last_trained_at
            FROM stats
            WHERE tags.id = stats.tag_id
        )SQL",
        pqxx::params{problem_id}
    ).no_rows();
}

void AppRepository::recalculatePlanItemProgress(
    pqxx::transaction_base& tx,
    long long plan_id,
    long long problem_id
) const {
    tx.exec(
        R"SQL(
            WITH latest AS (
                SELECT tr.id, tr.is_finished, tr.code_link, s.id AS submission_id, s.verdict
                FROM training_records tr
                LEFT JOIN submissions s ON tr.code_link = 'submission:' || s.id::text
                WHERE tr.plan_id = $1
                  AND tr.problem_id = $2
                ORDER BY tr.practiced_at DESC, tr.id DESC
                LIMIT 1
            ),
            chosen AS (
                SELECT id, is_finished, submission_id, verdict FROM latest
                UNION ALL
                SELECT NULL::bigint, NULL::boolean, NULL::bigint, NULL::varchar
                WHERE NOT EXISTS (SELECT 1 FROM latest)
            )
            UPDATE training_plan_items tpi
            SET item_status = CASE
                    WHEN chosen.id IS NULL THEN 'not_started'
                    WHEN chosen.is_finished THEN 'completed'
                    ELSE 'failed'
                END,
                last_submission_id = chosen.submission_id,
                last_training_record_id = chosen.id,
                last_verdict = chosen.verdict,
                last_updated_at = CASE WHEN chosen.id IS NULL THEN NULL ELSE CURRENT_TIMESTAMP END
            FROM chosen
            WHERE tpi.plan_id = $1
              AND tpi.problem_id = $2
        )SQL",
        pqxx::params{plan_id, problem_id}
    ).no_rows();

    refreshPlanStatusFromItems(tx, plan_id);
}

long long AppRepository::createTrainingRecord(const TrainingRecord& record) const {
    auto lease = pool_.acquire();
    pqxx::work tx{lease.connection()};
    auto normalized = record;
    if (normalized.duration_source.empty()) {
        normalized.duration_source = "manual";
    }
    const auto id = insertTrainingRecord(tx, normalized);

    if (normalized.problem_id) {
        recalculateProblemProgress(tx, *normalized.problem_id);
        recalculateTagProgressForProblem(tx, *normalized.problem_id);
    }

    updatePlanItemFromTrainingRecord(tx, normalized, id);

    tx.commit();
    return id;
}

std::optional<TrainingRecord> AppRepository::getTrainingRecord(long long id) const {
    auto lease = pool_.acquire();
    pqxx::work tx{lease.connection()};
    const auto row = tx.exec(
        "SELECT " + std::string{kTrainingRecordColumns} + " FROM training_records WHERE id = $1",
        pqxx::params{id}
    ).opt_row();
    tx.commit();
    if (!row) {
        return std::nullopt;
    }
    return record_from_row(*row);
}

std::optional<TrainingRecord> AppRepository::updateTrainingRecord(long long id, const TrainingRecord& record) const {
    auto lease = pool_.acquire();
    pqxx::work tx{lease.connection()};
    const auto old_row = tx.exec(
        "SELECT " + std::string{kTrainingRecordColumns} + " FROM training_records WHERE id = $1 FOR UPDATE",
        pqxx::params{id}
    ).opt_row();
    if (!old_row) {
        tx.commit();
        return std::nullopt;
    }

    const auto old_record = record_from_row(*old_row);
    auto normalized = record;
    normalized.started_at = old_record.started_at;
    normalized.ended_at = old_record.ended_at;
    normalized.duration_source = old_record.duration_source;
    if (normalized.practiced_at.empty()) {
        normalized.practiced_at = old_record.practiced_at;
    }

    pqxx::params params;
    params.append(normalized.plan_id);
    params.append(normalized.problem_id);
    params.append(normalized.is_finished);
    params.append(normalized.is_first_try_ac);
    params.append(normalized.actual_minutes);
    params.append(normalized.error_type);
    params.append(normalized.review_note);
    params.append(normalized.code_link);
    params.append(normalized.practiced_at);
    params.append(id);

    const auto updated_row = tx.exec(
        R"SQL(
            UPDATE training_records
            SET plan_id = $1,
                problem_id = $2,
                is_finished = $3,
                is_first_try_ac = $4,
                actual_minutes = $5,
                error_type = $6,
                review_note = $7,
                code_link = $8,
                practiced_at = COALESCE(NULLIF($9, '')::timestamp, practiced_at)
            WHERE id = $10
            RETURNING )SQL" + std::string{kTrainingRecordColumns},
        params
    ).one_row();

    if (old_record.problem_id) {
        recalculateProblemProgress(tx, *old_record.problem_id);
        recalculateTagProgressForProblem(tx, *old_record.problem_id);
    }
    if (normalized.problem_id && normalized.problem_id != old_record.problem_id) {
        recalculateProblemProgress(tx, *normalized.problem_id);
        recalculateTagProgressForProblem(tx, *normalized.problem_id);
    }
    if (old_record.plan_id && old_record.problem_id) {
        recalculatePlanItemProgress(tx, *old_record.plan_id, *old_record.problem_id);
    }
    if (normalized.plan_id && normalized.problem_id &&
        (normalized.plan_id != old_record.plan_id || normalized.problem_id != old_record.problem_id)) {
        recalculatePlanItemProgress(tx, *normalized.plan_id, *normalized.problem_id);
    }

    auto updated = record_from_row(updated_row);
    tx.commit();
    return updated;
}

bool AppRepository::deleteTrainingRecord(long long id) const {
    auto lease = pool_.acquire();
    pqxx::work tx{lease.connection()};
    const auto old_row = tx.exec(
        "SELECT " + std::string{kTrainingRecordColumns} + " FROM training_records WHERE id = $1 FOR UPDATE",
        pqxx::params{id}
    ).opt_row();
    if (!old_row) {
        tx.commit();
        return false;
    }

    const auto old_record = record_from_row(*old_row);
    tx.exec("UPDATE training_sessions SET created_record_id = NULL WHERE created_record_id = $1", pqxx::params{id}).no_rows();
    tx.exec("DELETE FROM training_records WHERE id = $1", pqxx::params{id}).no_rows();

    if (old_record.problem_id) {
        recalculateProblemProgress(tx, *old_record.problem_id);
        recalculateTagProgressForProblem(tx, *old_record.problem_id);
    }
    if (old_record.plan_id && old_record.problem_id) {
        recalculatePlanItemProgress(tx, *old_record.plan_id, *old_record.problem_id);
    }

    tx.commit();
    return true;
}

std::vector<TrainingRecord> AppRepository::listTrainingRecords(std::optional<long long> plan_id) const {
    auto lease = pool_.acquire();
    pqxx::work tx{lease.connection()};
    std::vector<TrainingRecord> records;
    pqxx::params params;
    std::string sql = "SELECT " + std::string{kTrainingRecordColumns} + " FROM training_records";
    if (plan_id) {
        sql += " WHERE plan_id = $1";
        params.append(*plan_id);
    }
    sql += " ORDER BY practiced_at DESC, id DESC LIMIT 200";

    for (const auto& row : tx.exec(sql, params)) {
        records.push_back(record_from_row(row));
    }
    tx.commit();
    return records;
}

std::optional<TrainingSession> AppRepository::startTrainingSession(const TrainingSession& session) const {
    auto lease = pool_.acquire();
    pqxx::work tx{lease.connection()};
    const bool has_active = tx.exec(
        "SELECT EXISTS(SELECT 1 FROM training_sessions WHERE status IN ('running', 'paused'))"
    ).one_field().as<bool>();
    if (has_active) {
        tx.commit();
        return std::nullopt;
    }

    pqxx::params params;
    params.append(session.plan_id);
    params.append(session.plan_item_id);
    params.append(session.problem_id);

    const auto row = tx.exec(
        R"SQL(
            INSERT INTO training_sessions(plan_id, plan_item_id, problem_id, status)
            VALUES ($1, $2, $3, 'running')
            RETURNING )SQL" + std::string{kTrainingSessionColumns},
        params
    ).one_row();
    tx.commit();
    return session_from_row(row);
}

std::optional<TrainingSession> AppRepository::getActiveTrainingSession() const {
    auto lease = pool_.acquire();
    pqxx::work tx{lease.connection()};
    const auto row = tx.exec(
        R"SQL(
            SELECT )SQL" + std::string{kTrainingSessionColumns} + R"SQL(
            FROM training_sessions
            WHERE status IN ('running', 'paused')
            ORDER BY id DESC
            LIMIT 1
        )SQL"
    ).opt_row();
    tx.commit();
    if (!row) {
        return std::nullopt;
    }
    return session_from_row(*row);
}

std::optional<TrainingSession> AppRepository::pauseTrainingSession(long long id) const {
    auto lease = pool_.acquire();
    pqxx::work tx{lease.connection()};
    auto row = tx.exec(
        R"SQL(
            UPDATE training_sessions
            SET status = 'paused',
                accumulated_seconds = accumulated_seconds + GREATEST(0, FLOOR(EXTRACT(EPOCH FROM (CURRENT_TIMESTAMP - last_resumed_at)))::integer)
            WHERE id = $1
              AND status = 'running'
            RETURNING )SQL" + std::string{kTrainingSessionColumns},
        pqxx::params{id}
    ).opt_row();
    if (!row) {
        row = tx.exec(
            R"SQL(
                SELECT )SQL" + std::string{kTrainingSessionColumns} + R"SQL(
                FROM training_sessions
                WHERE id = $1
                  AND status = 'paused'
            )SQL",
            pqxx::params{id}
        ).opt_row();
    }
    tx.commit();
    if (!row) {
        return std::nullopt;
    }
    return session_from_row(*row);
}

std::optional<TrainingSession> AppRepository::resumeTrainingSession(long long id) const {
    auto lease = pool_.acquire();
    pqxx::work tx{lease.connection()};
    auto row = tx.exec(
        R"SQL(
            UPDATE training_sessions
            SET status = 'running',
                last_resumed_at = CURRENT_TIMESTAMP
            WHERE id = $1
              AND status = 'paused'
            RETURNING )SQL" + std::string{kTrainingSessionColumns},
        pqxx::params{id}
    ).opt_row();
    if (!row) {
        row = tx.exec(
            R"SQL(
                SELECT )SQL" + std::string{kTrainingSessionColumns} + R"SQL(
                FROM training_sessions
                WHERE id = $1
                  AND status = 'running'
            )SQL",
            pqxx::params{id}
        ).opt_row();
    }
    tx.commit();
    if (!row) {
        return std::nullopt;
    }
    return session_from_row(*row);
}

std::optional<TrainingSession> AppRepository::finishTrainingSession(long long id, const TrainingRecord& review) const {
    auto lease = pool_.acquire();
    pqxx::work tx{lease.connection()};
    const auto session_row = tx.exec(
        R"SQL(
            SELECT )SQL" + std::string{kTrainingSessionColumns} + R"SQL(
            FROM training_sessions
            WHERE id = $1
            FOR UPDATE
        )SQL",
        pqxx::params{id}
    ).opt_row();
    if (!session_row) {
        tx.commit();
        return std::nullopt;
    }

    auto session = session_from_row(*session_row);
    if (session.status != "running" && session.status != "paused") {
        tx.commit();
        return std::nullopt;
    }

    const int actual_minutes = review.actual_minutes.value_or((session.elapsed_seconds + 59) / 60);
    TrainingRecord record;
    record.plan_id = session.plan_id;
    record.problem_id = session.problem_id;
    record.is_finished = review.is_finished;
    record.is_first_try_ac = review.is_first_try_ac;
    record.actual_minutes = actual_minutes;
    record.error_type = review.error_type;
    record.review_note = review.review_note;
    record.code_link = review.code_link;
    record.started_at = session.started_at;
    record.duration_source = "timer";

    const auto record_id = tx.exec(
        R"SQL(
            INSERT INTO training_records(
                plan_id, problem_id, is_finished, is_first_try_ac,
                actual_minutes, error_type, review_note, code_link,
                practiced_at, started_at, ended_at, duration_source
            )
            VALUES (
                $1, $2, $3, $4, $5, $6, $7, $8,
                CURRENT_TIMESTAMP, $9, CURRENT_TIMESTAMP, 'timer'
            )
            RETURNING id
        )SQL",
        pqxx::params{
            record.plan_id,
            record.problem_id,
            record.is_finished,
            record.is_first_try_ac,
            record.actual_minutes,
            record.error_type,
            record.review_note,
            record.code_link,
            record.started_at
        }
    ).one_field().as<long long>();

    recalculateProblemProgress(tx, session.problem_id);
    recalculateTagProgressForProblem(tx, session.problem_id);
    if (session.plan_id) {
        recalculatePlanItemProgress(tx, *session.plan_id, session.problem_id);
    }

    const auto updated_row = tx.exec(
        R"SQL(
            UPDATE training_sessions
            SET status = 'completed',
                accumulated_seconds = $2,
                finished_at = CURRENT_TIMESTAMP,
                created_record_id = $3
            WHERE id = $1
            RETURNING )SQL" + std::string{kTrainingSessionColumns},
        pqxx::params{id, session.elapsed_seconds, record_id}
    ).one_row();
    tx.commit();
    return session_from_row(updated_row);
}

std::optional<TrainingSession> AppRepository::cancelTrainingSession(long long id) const {
    auto lease = pool_.acquire();
    pqxx::work tx{lease.connection()};
    const auto row = tx.exec(
        R"SQL(
            UPDATE training_sessions
            SET status = 'canceled',
                accumulated_seconds = accumulated_seconds + CASE
                    WHEN status = 'running' THEN GREATEST(0, FLOOR(EXTRACT(EPOCH FROM (CURRENT_TIMESTAMP - last_resumed_at)))::integer)
                    ELSE 0
                END,
                finished_at = CURRENT_TIMESTAMP
            WHERE id = $1
              AND status IN ('running', 'paused')
            RETURNING )SQL" + std::string{kTrainingSessionColumns},
        pqxx::params{id}
    ).opt_row();
    tx.commit();
    if (!row) {
        return std::nullopt;
    }
    return session_from_row(*row);
}

nlohmann::json AppRepository::dashboardSummary() const {
    auto lease = pool_.acquire();
    pqxx::work tx{lease.connection()};
    nlohmann::json summary;
    summary["total_problems"] = tx.exec("SELECT COUNT(*) FROM problems").one_field().as<long long>();
    summary["completed_problems"] = tx.exec("SELECT COUNT(*) FROM problems WHERE is_completed").one_field().as<long long>();
    summary["wrong_problems"] = tx.exec("SELECT COUNT(*) FROM problems WHERE is_wrong_problem").one_field().as<long long>();
    summary["recent_7_days_minutes"] = tx.exec(
        R"SQL(
            SELECT COALESCE(SUM(actual_minutes), 0)
            FROM training_records
            WHERE practiced_at >= CURRENT_TIMESTAMP - INTERVAL '7 days'
        )SQL"
    ).one_field().as<long long>();
    summary["average_actual_minutes"] = tx.exec(
        "SELECT COALESCE(ROUND(AVG(actual_minutes)::numeric, 2), 0) FROM training_records WHERE actual_minutes IS NOT NULL"
    ).one_field().as<std::string>();
    const auto plan_stats = tx.exec(
        R"SQL(
            SELECT COUNT(*) AS total,
                   COUNT(*) FILTER (WHERE status = 'completed') AS completed
            FROM training_plans
        )SQL"
    ).one_row();
    const auto total_plans = plan_stats["total"].as<double>();
    const auto completed_plans = plan_stats["completed"].as<double>();
    summary["plan_completion_rate"] = total_plans <= 0.0 ? 0.0 : completed_plans / total_plans;

    const auto current_plan = tx.exec(
        R"SQL(
            SELECT id, name, status
            FROM training_plans
            WHERE status IN ('not_started', 'in_progress')
            ORDER BY id DESC
            LIMIT 1
        )SQL"
    ).opt_row();
    if (current_plan) {
        summary["current_plan"] = {
            {"id", (*current_plan)["id"].as<long long>()},
            {"name", (*current_plan)["name"].as<std::string>()},
            {"status", (*current_plan)["status"].as<std::string>()}
        };
    } else {
        summary["current_plan"] = nullptr;
    }
    tx.commit();
    return summary;
}

nlohmann::json AppRepository::dashboardTagStats() const {
    auto lease = pool_.acquire();
    pqxx::work tx{lease.connection()};
    nlohmann::json stats = nlohmann::json::array();
    const auto rows = tx.exec(
        R"SQL(
            SELECT t.id, t.name, t.mastery_score, t.wrong_count,
                   COUNT(DISTINCT p.id) FILTER (WHERE p.is_completed) AS completed_count,
                   COUNT(DISTINCT p.id) AS problem_count,
                   t.last_trained_at
            FROM tags t
            LEFT JOIN problem_tags pt ON pt.tag_id = t.id
            LEFT JOIN problems p ON p.id = pt.problem_id
            GROUP BY t.id, t.name, t.mastery_score, t.wrong_count, t.last_trained_at
            ORDER BY t.mastery_score ASC, t.wrong_count DESC, t.name
        )SQL"
    );
    for (const auto& row : rows) {
        stats.push_back({
            {"id", row["id"].as<long long>()},
            {"name", row["name"].as<std::string>()},
            {"mastery_score", row["mastery_score"].as<int>()},
            {"wrong_count", row["wrong_count"].as<int>()},
            {"completed_count", row["completed_count"].as<long long>()},
            {"problem_count", row["problem_count"].as<long long>()},
            {"last_trained_at", row["last_trained_at"].is_null() ? nlohmann::json(nullptr) : nlohmann::json(row["last_trained_at"].as<std::string>())}
        });
    }
    tx.commit();
    return stats;
}

nlohmann::json AppRepository::dashboardRecentActivity() const {
    auto lease = pool_.acquire();
    pqxx::work tx{lease.connection()};
    nlohmann::json rows_json = nlohmann::json::array();
    const auto rows = tx.exec(
        R"SQL(
            SELECT tr.id, tr.plan_id, tr.problem_id, tr.is_finished, tr.is_first_try_ac,
                   tr.actual_minutes, tr.error_type, tr.review_note, tr.practiced_at,
                   p.problem_code, p.title
            FROM training_records tr
            LEFT JOIN problems p ON p.id = tr.problem_id
            ORDER BY tr.practiced_at DESC, tr.id DESC
            LIMIT 20
        )SQL"
    );
    for (const auto& row : rows) {
        rows_json.push_back({
            {"id", row["id"].as<long long>()},
            {"plan_id", row["plan_id"].is_null() ? nlohmann::json(nullptr) : nlohmann::json(row["plan_id"].as<long long>())},
            {"problem_id", row["problem_id"].is_null() ? nlohmann::json(nullptr) : nlohmann::json(row["problem_id"].as<long long>())},
            {"problem_code", row["problem_code"].is_null() ? "" : row["problem_code"].as<std::string>()},
            {"title", row["title"].is_null() ? "" : row["title"].as<std::string>()},
            {"is_finished", row["is_finished"].as<bool>()},
            {"is_first_try_ac", row["is_first_try_ac"].as<bool>()},
            {"actual_minutes", row["actual_minutes"].is_null() ? nlohmann::json(nullptr) : nlohmann::json(row["actual_minutes"].as<int>())},
            {"error_type", string_or_empty(row["error_type"])},
            {"review_note", string_or_empty(row["review_note"])},
            {"practiced_at", row["practiced_at"].as<std::string>()}
        });
    }
    tx.commit();
    return rows_json;
}

} // namespace atp
