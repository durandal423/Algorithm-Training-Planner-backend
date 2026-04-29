#include "service/training_plan_service.hpp"

#include "algorithms/training_dp.hpp"
#include "algorithms/window_selector.hpp"
#include "api/json_utils.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace atp {
namespace {

using Clock = std::chrono::steady_clock;

long long elapsed_ms(Clock::time_point started) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count();
}

std::vector<Problem> comparison_sample(std::vector<Problem> problems) {
    std::stable_sort(problems.begin(), problems.end(), [](const Problem& lhs, const Problem& rhs) {
        if (lhs.difficulty != rhs.difficulty) {
            return lhs.difficulty < rhs.difficulty;
        }
        return lhs.id < rhs.id;
    });
    if (problems.size() > 500) {
        problems.resize(500);
    }
    return problems;
}

nlohmann::json plan_response(
    long long plan_id,
    const CandidateWindow& window,
    const TrainingPlanResult& result,
    const std::vector<SelectedProblem>& stored_items
) {
    nlohmann::json items = nlohmann::json::array();
    for (const auto& item : stored_items) {
        items.push_back(toJson(item));
    }

    return {
        {"plan_id", plan_id},
        {"candidate_window", toJson(window)},
        {"dp_result", {
            {"selected_count", result.items.size()},
            {"total_estimated_time", result.total_estimated_time},
            {"total_score", result.total_score},
            {"covered_tag_mask", result.covered_tag_mask},
            {"covered_tags", result.covered_tags},
            {"uncovered_tags", result.uncovered_tags},
            {"dp_elapsed_microseconds", result.dp_elapsed_microseconds}
        }},
        {"items", items}
    };
}

nlohmann::json metric_json(const CandidateWindow& window) {
    return {
        {"elapsed_microseconds", window.elapsed_microseconds},
        {"difficulty_span", window.difficulty_span},
        {"candidate_count", window.candidates.size()},
        {"covered_tags", window.covered_tags},
        {"coverage_ratio", window.coverage_ratio},
        {"wrong_problem_count", window.wrong_problem_count}
    };
}

nlohmann::json metric_json(const TrainingPlanResult& result) {
    int wrong_count = 0;
    for (const auto& item : result.items) {
        if (item.problem.is_wrong_problem || item.problem.wrong_count > 0) {
            ++wrong_count;
        }
    }
    return {
        {"elapsed_microseconds", result.dp_elapsed_microseconds},
        {"selected_count", result.items.size()},
        {"total_score", result.total_score},
        {"covered_tags", result.covered_tags},
        {"covered_tag_count", result.covered_tags.size()},
        {"wrong_problem_count", wrong_count},
        {"total_estimated_time", result.total_estimated_time}
    };
}

void validate_goal_for_planning(const TrainingGoal& goal) {
    if (goal.target_count <= 0) {
        throw std::invalid_argument("target_count must be positive");
    }
    if (goal.time_budget_minutes <= 0) {
        throw std::invalid_argument("time_budget_minutes must be positive");
    }
    if (goal.difficulty_min && goal.difficulty_max && *goal.difficulty_min > *goal.difficulty_max) {
        throw std::invalid_argument("difficulty_min must be less than or equal to difficulty_max");
    }
    if (goal.difficulty_weight < 0 || goal.tag_coverage_weight < 0 ||
        goal.wrong_problem_weight < 0 || goal.weak_tag_weight < 0 ||
        goal.estimated_time_weight < 0) {
        throw std::invalid_argument("weights must be non-negative");
    }
}

} // namespace

TrainingPlanService::TrainingPlanService(AppRepository& repository) : repository_(repository) {}

TrainingGoal TrainingPlanService::goalFromGenerateRequest(
    const nlohmann::json& request,
    std::optional<long long>& goal_id
) const {
    TrainingGoal goal;
    if (request.contains("goal_id") && !request.at("goal_id").is_null()) {
        goal_id = request.at("goal_id").get<long long>();
        const auto stored = repository_.getTrainingGoal(*goal_id);
        if (!stored) {
            throw std::runtime_error("training goal not found");
        }
        goal = *stored;
    }
    applyGoalOverrides(goal, request);
    return goal;
}

nlohmann::json TrainingPlanService::generateTrainingPlan(const nlohmann::json& request) const {
    const auto total_started = Clock::now();
    std::optional<long long> goal_id;
    auto goal = goalFromGenerateRequest(request, goal_id);
    validate_goal_for_planning(goal);

    const auto load_started = Clock::now();
    const auto problems = repository_.listAllProblems();
    spdlog::debug("training plan listAllProblems completed in {} ms (problem_count={})",
        elapsed_ms(load_started),
        problems.size());

    const auto window_started = Clock::now();
    auto window = selectStableDifficultyWindow(problems, goal);
    spdlog::debug("training plan window selection completed in {} ms (candidate_count={})",
        elapsed_ms(window_started),
        window.candidates.size());

    const auto dp_started = Clock::now();
    auto result = optimizeTrainingPlanByDP(window.candidates, goal);
    spdlog::debug("training plan DP completed in {} ms (selected_count={})",
        elapsed_ms(dp_started),
        result.items.size());

    nlohmann::json algorithm_summary = {
        {"goal", toJson(goal)},
        {"candidate_window", toJson(window)},
        {"dp_result", toJson(result)}
    };

    const auto save_started = Clock::now();
    const auto plan_id = repository_.saveTrainingPlan(
        goal_id,
        goal.name.empty() ? "Generated Training Plan" : goal.name,
        window,
        result,
        algorithm_summary.dump()
    );
    spdlog::debug("training plan saveTrainingPlan completed in {} ms (plan_id={})",
        elapsed_ms(save_started),
        plan_id);

    spdlog::info("training plan generated in {} ms (plan_id={}, problem_count={}, candidate_count={}, selected_count={})",
        elapsed_ms(total_started),
        plan_id,
        problems.size(),
        window.candidates.size(),
        result.items.size());

    return plan_response(plan_id, window, result, repository_.getTrainingPlanItems(plan_id));
}

nlohmann::json TrainingPlanService::compareWindowAlgorithms(const nlohmann::json& request) const {
    const auto total_started = Clock::now();
    TrainingGoal goal = goalFromJson(request);
    validate_goal_for_planning(goal);

    const auto load_started = Clock::now();
    auto problems = comparison_sample(repository_.listAllProblems());
    spdlog::debug("window compare listAllProblems completed in {} ms (sample_count={})",
        elapsed_ms(load_started),
        problems.size());

    const auto sliding_started = Clock::now();
    auto sliding = selectStableDifficultyWindow(problems, goal);
    spdlog::debug("window compare sliding selector completed in {} ms (candidate_count={})",
        elapsed_ms(sliding_started),
        sliding.candidates.size());

    const auto naive_started = Clock::now();
    auto naive = selectNaiveStableDifficultyWindow(problems, goal);
    spdlog::debug("window compare naive selector completed in {} ms (candidate_count={})",
        elapsed_ms(naive_started),
        naive.candidates.size());
    spdlog::info("window compare completed in {} ms (sample_count={})",
        elapsed_ms(total_started),
        problems.size());

    return {
        {"problem_count", problems.size()},
        {"sliding_window", metric_json(sliding)},
        {"naive_enumeration", metric_json(naive)},
        {"note", "Naive comparison samples at most 500 problems to keep the demo endpoint responsive."}
    };
}

nlohmann::json TrainingPlanService::compareDpAlgorithms(const nlohmann::json& request) const {
    const auto total_started = Clock::now();
    TrainingGoal goal = goalFromJson(request);
    validate_goal_for_planning(goal);

    const auto load_started = Clock::now();
    const auto problems = repository_.listAllProblems();
    spdlog::debug("DP compare listAllProblems completed in {} ms (problem_count={})",
        elapsed_ms(load_started),
        problems.size());

    const auto window_started = Clock::now();
    const auto window = selectStableDifficultyWindow(problems, goal);
    spdlog::debug("DP compare window selection completed in {} ms (candidate_count={})",
        elapsed_ms(window_started),
        window.candidates.size());

    const auto dp_started = Clock::now();
    const auto dp = optimizeTrainingPlanByDP(window.candidates, goal);
    spdlog::debug("DP compare dynamic programming completed in {} ms (selected_count={})",
        elapsed_ms(dp_started),
        dp.items.size());

    const auto greedy_started = Clock::now();
    const auto greedy = buildGreedyTrainingPlan(window.candidates, goal);
    spdlog::debug("DP compare greedy planner completed in {} ms (selected_count={})",
        elapsed_ms(greedy_started),
        greedy.items.size());
    spdlog::info("DP compare completed in {} ms (problem_count={}, candidate_count={})",
        elapsed_ms(total_started),
        problems.size(),
        window.candidates.size());

    return {
        {"candidate_count", window.candidates.size()},
        {"candidate_window", toJson(window)},
        {"dynamic_programming", metric_json(dp)},
        {"greedy", metric_json(greedy)},
        {"within_time_budget", {
            {"dynamic_programming", dp.total_estimated_time <= goal.time_budget_minutes},
            {"greedy", greedy.total_estimated_time <= goal.time_budget_minutes}
        }}
    };
}

} // namespace atp
