#include "api/json_utils.hpp"

#include <algorithm>
#include <stdexcept>

namespace atp {
namespace {

template <typename T>
void set_if_present(const nlohmann::json& body, const char* key, T& value) {
    if (body.contains(key) && !body.at(key).is_null()) {
        value = body.at(key).get<T>();
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

void localize_selected_reasons(nlohmann::json& value) {
    if (value.is_array()) {
        for (auto& item : value) {
            localize_selected_reasons(item);
        }
        return;
    }
    if (!value.is_object()) {
        return;
    }
    for (auto& [key, item] : value.items()) {
        if (key == "selected_reason" && item.is_string()) {
            item = localized_selected_reason(item.get<std::string>());
        } else {
            localize_selected_reasons(item);
        }
    }
}

} // namespace

nlohmann::json toJson(const Tag& tag) {
    return {
        {"id", tag.id},
        {"name", tag.name},
        {"description", tag.description},
        {"mastery_score", tag.mastery_score},
        {"wrong_count", tag.wrong_count},
        {"last_trained_at", optional_string_json(tag.last_trained_at)}
    };
}

nlohmann::json toJson(const Problem& problem) {
    nlohmann::json tags = nlohmann::json::array();
    for (const auto& tag : problem.tags) {
        tags.push_back(toJson(tag));
    }

    nlohmann::json tag_names = nlohmann::json::array();
    for (const auto& tag : problem.tags) {
        tag_names.push_back(tag.name);
    }

    return {
        {"id", problem.id},
        {"problem_code", problem.problem_code},
        {"title", problem.title},
        {"source_platform", problem.source_platform},
        {"source_url", problem.source_url},
        {"difficulty", problem.difficulty},
        {"estimated_minutes", problem.estimated_minutes},
        {"summary", problem.summary},
        {"is_completed", problem.is_completed},
        {"is_wrong_problem", problem.is_wrong_problem},
        {"wrong_count", problem.wrong_count},
        {"last_practiced_at", optional_string_json(problem.last_practiced_at)},
        {"created_at", problem.created_at},
        {"updated_at", problem.updated_at},
        {"tags", tag_names},
        {"tag_details", tags}
    };
}

nlohmann::json toJson(const TrainingGoal& goal) {
    nlohmann::json tags = nlohmann::json::array();
    if (!goal.target_tag_names.empty()) {
        for (const auto& name : goal.target_tag_names) {
            tags.push_back(name);
        }
    } else {
        for (const auto& tag : goal.target_tags) {
            tags.push_back(tag.name);
        }
    }

    return {
        {"id", goal.id},
        {"user_id", optional_long_json(goal.user_id)},
        {"name", goal.name},
        {"description", goal.description},
        {"target_count", goal.target_count},
        {"time_budget_minutes", goal.time_budget_minutes},
        {"difficulty_min", optional_int_json(goal.difficulty_min)},
        {"difficulty_max", optional_int_json(goal.difficulty_max)},
        {"target_tags", tags},
        {"prefer_wrong_problems", goal.prefer_wrong_problems},
        {"prefer_weak_tags", goal.prefer_weak_tags},
        {"difficulty_weight", goal.difficulty_weight},
        {"tag_coverage_weight", goal.tag_coverage_weight},
        {"wrong_problem_weight", goal.wrong_problem_weight},
        {"weak_tag_weight", goal.weak_tag_weight},
        {"estimated_time_weight", goal.estimated_time_weight},
        {"created_at", goal.created_at}
    };
}

nlohmann::json toJson(const CandidateWindow& window) {
    return {
        {"left_index", window.left_index},
        {"right_index", window.right_index},
        {"left_difficulty", window.left_difficulty},
        {"right_difficulty", window.right_difficulty},
        {"difficulty_span", window.difficulty_span},
        {"candidate_count", window.candidates.size()},
        {"wrong_problem_count", window.wrong_problem_count},
        {"weak_tag_hit_count", window.weak_tag_hit_count},
        {"coverage_ratio", window.coverage_ratio},
        {"covered_tags", window.covered_tags},
        {"uncovered_tags", window.uncovered_tags},
        {"elapsed_microseconds", window.elapsed_microseconds}
    };
}

nlohmann::json toJson(const SelectedProblem& item) {
    auto json = toJson(item.problem);
    json["problem_id"] = item.problem.id;
    json["plan_item_id"] = item.plan_item_id == 0 ? nlohmann::json(nullptr) : nlohmann::json(item.plan_item_id);
    json["score"] = item.score;
    json["selected_reason"] = item.selected_reason;
    json["covered_target_tags"] = item.covered_target_tags;
    json["item_status"] = item.item_status;
    json["last_submission_id"] = optional_long_json(item.last_submission_id);
    json["last_training_record_id"] = optional_long_json(item.last_training_record_id);
    json["last_verdict"] = optional_string_json(item.last_verdict);
    json["last_updated_at"] = optional_string_json(item.last_updated_at);
    json["last_error_type"] = optional_string_json(item.last_error_type);
    json["last_is_first_try_ac"] = item.last_is_first_try_ac ? nlohmann::json(*item.last_is_first_try_ac) : nlohmann::json(nullptr);
    return json;
}

nlohmann::json toJson(const TrainingPlanResult& result) {
    nlohmann::json items = nlohmann::json::array();
    for (const auto& item : result.items) {
        items.push_back(toJson(item));
    }
    return {
        {"selected_count", result.items.size()},
        {"total_estimated_time", result.total_estimated_time},
        {"total_score", result.total_score},
        {"covered_tag_mask", result.covered_tag_mask},
        {"covered_tags", result.covered_tags},
        {"uncovered_tags", result.uncovered_tags},
        {"dp_elapsed_microseconds", result.dp_elapsed_microseconds},
        {"items", items}
    };
}

nlohmann::json toJson(const TrainingPlanSummary& plan) {
    nlohmann::json algorithm_summary = nullptr;
    if (!plan.algorithm_summary.empty()) {
        algorithm_summary = nlohmann::json::parse(plan.algorithm_summary, nullptr, false);
        if (algorithm_summary.is_discarded()) {
            algorithm_summary = plan.algorithm_summary;
        } else {
            localize_selected_reasons(algorithm_summary);
        }
    }
    return {
        {"id", plan.id},
        {"goal_id", optional_long_json(plan.goal_id)},
        {"name", plan.name},
        {"candidate_count", plan.candidate_count},
        {"selected_count", plan.selected_count},
        {"total_estimated_time", plan.total_estimated_time},
        {"total_score", plan.total_score},
        {"difficulty_span", plan.difficulty_span},
        {"covered_tag_mask", plan.covered_tag_mask},
        {"status", plan.status},
        {"algorithm_summary", algorithm_summary},
        {"created_at", plan.created_at}
    };
}

nlohmann::json toJson(const TrainingRecord& record) {
    return {
        {"id", record.id},
        {"plan_id", optional_long_json(record.plan_id)},
        {"problem_id", optional_long_json(record.problem_id)},
        {"is_finished", record.is_finished},
        {"is_first_try_ac", record.is_first_try_ac},
        {"actual_minutes", optional_int_json(record.actual_minutes)},
        {"error_type", record.error_type},
        {"review_note", record.review_note},
        {"code_link", record.code_link},
        {"practiced_at", record.practiced_at},
        {"started_at", optional_string_json(record.started_at)},
        {"ended_at", optional_string_json(record.ended_at)},
        {"duration_source", record.duration_source}
    };
}

nlohmann::json toJson(const TrainingSession& session) {
    return {
        {"id", session.id},
        {"plan_id", optional_long_json(session.plan_id)},
        {"plan_item_id", optional_long_json(session.plan_item_id)},
        {"problem_id", session.problem_id},
        {"status", session.status},
        {"started_at", session.started_at},
        {"last_resumed_at", session.last_resumed_at},
        {"accumulated_seconds", session.accumulated_seconds},
        {"elapsed_seconds", session.elapsed_seconds},
        {"finished_at", optional_string_json(session.finished_at)},
        {"created_record_id", optional_long_json(session.created_record_id)}
    };
}

std::vector<std::string> stringArrayFromJson(const nlohmann::json& body, const char* key) {
    std::vector<std::string> values;
    if (!body.contains(key) || body.at(key).is_null()) {
        return values;
    }
    if (!body.at(key).is_array()) {
        throw std::invalid_argument(std::string{key} + " must be an array");
    }
    for (const auto& item : body.at(key)) {
        values.push_back(item.get<std::string>());
    }
    return values;
}

Tag tagFromJson(const nlohmann::json& body) {
    Tag tag;
    set_if_present(body, "name", tag.name);
    set_if_present(body, "description", tag.description);
    set_if_present(body, "mastery_score", tag.mastery_score);
    set_if_present(body, "wrong_count", tag.wrong_count);
    return tag;
}

Problem problemFromJson(const nlohmann::json& body) {
    Problem problem;
    set_if_present(body, "problem_code", problem.problem_code);
    set_if_present(body, "title", problem.title);
    set_if_present(body, "source_platform", problem.source_platform);
    set_if_present(body, "source_url", problem.source_url);
    set_if_present(body, "difficulty", problem.difficulty);
    set_if_present(body, "estimated_minutes", problem.estimated_minutes);
    set_if_present(body, "summary", problem.summary);
    set_if_present(body, "is_completed", problem.is_completed);
    set_if_present(body, "is_wrong_problem", problem.is_wrong_problem);
    set_if_present(body, "wrong_count", problem.wrong_count);
    for (const auto& name : stringArrayFromJson(body, "tags")) {
        Tag tag;
        tag.name = name;
        problem.tags.push_back(tag);
    }
    return problem;
}

TrainingGoal goalFromJson(const nlohmann::json& body) {
    TrainingGoal goal;
    applyGoalOverrides(goal, body);
    return goal;
}

TrainingRecord recordFromJson(const nlohmann::json& body) {
    TrainingRecord record;
    set_optional_long(body, "plan_id", record.plan_id);
    set_optional_long(body, "problem_id", record.problem_id);
    set_if_present(body, "is_finished", record.is_finished);
    set_if_present(body, "is_first_try_ac", record.is_first_try_ac);
    if (body.contains("actual_minutes")) {
        if (body.at("actual_minutes").is_null()) {
            record.actual_minutes.reset();
        } else {
            record.actual_minutes = body.at("actual_minutes").get<int>();
        }
    }
    set_if_present(body, "error_type", record.error_type);
    set_if_present(body, "review_note", record.review_note);
    set_if_present(body, "code_link", record.code_link);
    set_if_present(body, "practiced_at", record.practiced_at);
    return record;
}

TrainingSession sessionFromJson(const nlohmann::json& body) {
    TrainingSession session;
    set_optional_long(body, "plan_id", session.plan_id);
    set_optional_long(body, "plan_item_id", session.plan_item_id);
    set_if_present(body, "problem_id", session.problem_id);
    return session;
}

void applyGoalOverrides(TrainingGoal& goal, const nlohmann::json& body) {
    set_optional_long(body, "user_id", goal.user_id);
    set_if_present(body, "name", goal.name);
    set_if_present(body, "description", goal.description);
    set_if_present(body, "target_count", goal.target_count);
    set_if_present(body, "time_budget_minutes", goal.time_budget_minutes);
    set_optional_int(body, "difficulty_min", goal.difficulty_min);
    set_optional_int(body, "difficulty_max", goal.difficulty_max);
    set_if_present(body, "prefer_wrong_problems", goal.prefer_wrong_problems);
    set_if_present(body, "prefer_weak_tags", goal.prefer_weak_tags);
    set_if_present(body, "difficulty_weight", goal.difficulty_weight);
    set_if_present(body, "tag_coverage_weight", goal.tag_coverage_weight);
    set_if_present(body, "wrong_problem_weight", goal.wrong_problem_weight);
    set_if_present(body, "weak_tag_weight", goal.weak_tag_weight);
    set_if_present(body, "estimated_time_weight", goal.estimated_time_weight);
    if (body.contains("target_tags")) {
        goal.target_tag_names = stringArrayFromJson(body, "target_tags");
        goal.target_tags.clear();
        for (const auto& name : goal.target_tag_names) {
            Tag tag;
            tag.name = name;
            goal.target_tags.push_back(tag);
        }
    }
}

nlohmann::json parseJsonObject(const std::string& body) {
    if (body.empty()) {
        return nlohmann::json::object();
    }
    auto parsed = nlohmann::json::parse(body);
    if (!parsed.is_object()) {
        throw std::invalid_argument("request body must be a JSON object");
    }
    return parsed;
}

} // namespace atp
