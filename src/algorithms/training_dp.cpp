#include "algorithms/training_dp.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdlib>
#include <unordered_map>
#include <unordered_set>

namespace atp {
namespace {

using Clock = std::chrono::steady_clock;
constexpr int kMaxDpCandidates = 80;
constexpr int kMaxMaskTags = 8;
constexpr int kNegativeInfinity = -1'000'000'000;

std::vector<std::string> goal_tags(const TrainingGoal& goal) {
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

bool is_weak_tag(const Tag& tag) {
    return tag.mastery_score < 60 || tag.wrong_count > 0;
}

std::vector<std::string> core_tags(const std::vector<Problem>& candidates, const TrainingGoal& goal) {
    std::vector<std::string> names = goal_tags(goal);
    std::unordered_set<std::string> seen;
    std::vector<std::string> result;
    result.reserve(kMaxMaskTags);

    for (const auto& name : names) {
        if (!seen.contains(name)) {
            result.push_back(name);
            seen.insert(name);
            if (result.size() == kMaxMaskTags) {
                return result;
            }
        }
    }

    std::vector<std::pair<int, std::string>> weak_tags;
    for (const auto& problem : candidates) {
        for (const auto& tag : problem.tags) {
            if (seen.contains(tag.name)) {
                continue;
            }
            int priority = (100 - tag.mastery_score) + tag.wrong_count * 10;
            if (is_weak_tag(tag)) {
                priority += 100;
            }
            weak_tags.emplace_back(priority, tag.name);
        }
    }

    std::stable_sort(weak_tags.begin(), weak_tags.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.first > rhs.first;
    });

    for (const auto& [priority, name] : weak_tags) {
        (void)priority;
        if (seen.contains(name)) {
            continue;
        }
        result.push_back(name);
        seen.insert(name);
        if (result.size() == kMaxMaskTags) {
            break;
        }
    }
    return result;
}

std::unordered_map<std::string, int> tag_index_map(const std::vector<std::string>& tags) {
    std::unordered_map<std::string, int> index;
    for (int i = 0; i < static_cast<int>(tags.size()); ++i) {
        index[tags[i]] = i;
    }
    return index;
}

int problem_mask(const Problem& problem, const std::unordered_map<std::string, int>& tag_index) {
    int mask = 0;
    for (const auto& tag : problem.tags) {
        if (auto iter = tag_index.find(tag.name); iter != tag_index.end()) {
            mask |= (1 << iter->second);
        }
    }
    return mask;
}

int weak_tag_count(const Problem& problem) {
    int count = 0;
    for (const auto& tag : problem.tags) {
        if (is_weak_tag(tag)) {
            ++count;
        }
    }
    return count;
}

std::vector<std::string> tag_names_for_mask(int mask, const std::vector<std::string>& tags) {
    std::vector<std::string> result;
    for (int i = 0; i < static_cast<int>(tags.size()); ++i) {
        if ((mask & (1 << i)) != 0) {
            result.push_back(tags[i]);
        }
    }
    return result;
}

std::vector<std::string> uncovered_for_mask(int mask, const std::vector<std::string>& requested) {
    std::vector<std::string> result;
    const int limit = std::min<int>(static_cast<int>(requested.size()), kMaxMaskTags);
    for (int i = 0; i < limit; ++i) {
        if ((mask & (1 << i)) == 0) {
            result.push_back(requested[i]);
        }
    }
    for (int i = limit; i < static_cast<int>(requested.size()); ++i) {
        result.push_back(requested[i]);
    }
    return result;
}

std::string reason_for_problem(
    const Problem& problem,
    const TrainingGoal& goal,
    const std::unordered_set<std::string>& covered_target
) {
    std::vector<std::string> parts;
    parts.push_back("位于稳定难度范围");
    if (!covered_target.empty()) {
        std::vector<std::string> names{covered_target.begin(), covered_target.end()};
        std::sort(names.begin(), names.end());
        std::string joined;
        bool first = true;
        for (const auto& name : names) {
            if (!first) {
                joined += "、";
            }
            joined += name;
            first = false;
        }
        parts.push_back("覆盖目标标签：" + joined);
    }
    if (goal.prefer_wrong_problems && (problem.is_wrong_problem || problem.wrong_count > 0)) {
        parts.push_back("历史错题优先复习");
    }
    if (goal.prefer_weak_tags && weak_tag_count(problem) > 0) {
        parts.push_back("覆盖薄弱标签");
    }
    parts.push_back("预计耗时符合时间预算");

    std::string result;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) {
            result += "；";
        }
        result += parts[i];
    }
    return result;
}

struct IndexedProblem {
    Problem problem;
    int original_index{};
    int score{};
};

std::vector<IndexedProblem> limit_candidates(const std::vector<Problem>& candidates, const TrainingGoal& goal) {
    std::vector<IndexedProblem> indexed;
    indexed.reserve(candidates.size());
    for (int i = 0; i < static_cast<int>(candidates.size()); ++i) {
        indexed.push_back({candidates[i], i, scoreProblemForGoal(candidates[i], goal)});
    }

    std::stable_sort(indexed.begin(), indexed.end(), [](const IndexedProblem& lhs, const IndexedProblem& rhs) {
        if (lhs.score != rhs.score) {
            return lhs.score > rhs.score;
        }
        if (lhs.problem.difficulty != rhs.problem.difficulty) {
            return lhs.problem.difficulty < rhs.problem.difficulty;
        }
        return lhs.problem.id < rhs.problem.id;
    });

    if (indexed.size() > kMaxDpCandidates) {
        indexed.resize(kMaxDpCandidates);
    }

    std::stable_sort(indexed.begin(), indexed.end(), [](const IndexedProblem& lhs, const IndexedProblem& rhs) {
        return lhs.original_index < rhs.original_index;
    });
    return indexed;
}

struct State {
    int score{kNegativeInfinity};
    std::uint64_t selected_low{};
    std::uint64_t selected_high{};
};

int state_index(int time, int count, int mask, int max_count, int mask_count) {
    return ((time * (max_count + 1) + count) * mask_count + mask);
}

bool is_selected(const State& state, int index) {
    if (index < 64) {
        return ((state.selected_low >> index) & 1ULL) != 0;
    }
    return ((state.selected_high >> (index - 64)) & 1ULL) != 0;
}

State with_selected(State state, int index) {
    if (index < 64) {
        state.selected_low |= (1ULL << index);
    } else {
        state.selected_high |= (1ULL << (index - 64));
    }
    return state;
}

int bit_count(int mask) {
    return std::popcount(static_cast<unsigned>(mask));
}

bool better_final_state(int score, int count, int mask, const State& best, int best_count, int best_mask) {
    if (best.score == kNegativeInfinity) {
        return true;
    }
    if (score != best.score) {
        return score > best.score;
    }
    if (count != best_count) {
        return count > best_count;
    }
    return bit_count(mask) > bit_count(best_mask);
}

TrainingPlanResult result_from_state(
    const State& state,
    const std::vector<IndexedProblem>& indexed,
    const TrainingGoal& goal,
    const std::vector<std::string>& mask_tags,
    int mask,
    long long elapsed_us
) {
    TrainingPlanResult result;
    result.total_score = std::max(0, state.score);
    result.covered_tag_mask = mask;
    result.covered_tags = tag_names_for_mask(mask, mask_tags);
    result.uncovered_tags = uncovered_for_mask(mask, goal_tags(goal));
    result.dp_elapsed_microseconds = elapsed_us;

    const auto requested_names = goal_tags(goal);
    std::unordered_set<std::string> requested{requested_names.begin(), requested_names.end()};

    for (int i = 0; i < static_cast<int>(indexed.size()); ++i) {
        if (!is_selected(state, i)) {
            continue;
        }

        const auto& problem = indexed[i].problem;
        std::unordered_set<std::string> covered_target;
        for (const auto& tag : problem.tags) {
            if (requested.contains(tag.name)) {
                covered_target.insert(tag.name);
            }
        }

        SelectedProblem selected;
        selected.problem = problem;
        selected.score = indexed[i].score;
        selected.covered_target_tags.assign(covered_target.begin(), covered_target.end());
        std::sort(selected.covered_target_tags.begin(), selected.covered_target_tags.end());
        selected.selected_reason = reason_for_problem(problem, goal, covered_target);
        result.total_estimated_time += problem.estimated_minutes;
        result.items.push_back(std::move(selected));
    }

    std::stable_sort(result.items.begin(), result.items.end(), [](const SelectedProblem& lhs, const SelectedProblem& rhs) {
        if (lhs.problem.difficulty != rhs.problem.difficulty) {
            return lhs.problem.difficulty < rhs.problem.difficulty;
        }
        return lhs.problem.id < rhs.problem.id;
    });
    return result;
}

} // namespace

int scoreProblemForGoal(const Problem& problem, const TrainingGoal& goal) {
    const auto requested_names = goal_tags(goal);
    std::unordered_set<std::string> requested{requested_names.begin(), requested_names.end()};

    int target_hits = 0;
    for (const auto& tag : problem.tags) {
        if (requested.contains(tag.name)) {
            ++target_hits;
        }
    }

    int score = 20;
    score += target_hits * goal.tag_coverage_weight;
    if (goal.prefer_wrong_problems && (problem.is_wrong_problem || problem.wrong_count > 0)) {
        score += goal.wrong_problem_weight + problem.wrong_count * 3;
    }
    if (goal.prefer_weak_tags) {
        score += weak_tag_count(problem) * goal.weak_tag_weight;
    }

    if (goal.difficulty_min && goal.difficulty_max) {
        const int center = (*goal.difficulty_min + *goal.difficulty_max) / 2;
        const int distance = std::abs(problem.difficulty - center) / 100;
        score -= distance * std::max(1, goal.difficulty_weight / 5);
    }
    score -= (problem.estimated_minutes * std::max(1, goal.estimated_time_weight)) / 5;
    return std::max(1, score);
}

TrainingPlanResult optimizeTrainingPlanByDP(
    const std::vector<Problem>& candidates,
    const TrainingGoal& goal
) {
    const auto started = Clock::now();
    if (candidates.empty() || goal.target_count <= 0 || goal.time_budget_minutes <= 0) {
        TrainingPlanResult empty;
        empty.uncovered_tags = goal_tags(goal);
        empty.dp_elapsed_microseconds = std::chrono::duration_cast<std::chrono::microseconds>(
            Clock::now() - started
        ).count();
        return empty;
    }

    const auto indexed = limit_candidates(candidates, goal);
    const int max_time = std::max(0, goal.time_budget_minutes);
    const int max_count = std::max(0, goal.target_count);
    const auto mask_tags = core_tags(candidates, goal);
    const auto tag_index = tag_index_map(mask_tags);
    const int mask_count = 1 << static_cast<int>(mask_tags.size());
    std::vector<State> dp(static_cast<std::size_t>(max_time + 1) * (max_count + 1) * mask_count);
    dp[state_index(0, 0, 0, max_count, mask_count)].score = 0;

    for (int i = 0; i < static_cast<int>(indexed.size()); ++i) {
        const auto& item = indexed[i];
        const int duration = std::max(1, item.problem.estimated_minutes);
        const int mask = problem_mask(item.problem, tag_index);
        for (int time = max_time; time >= duration; --time) {
            for (int count = max_count; count >= 1; --count) {
                for (int old_mask = 0; old_mask < mask_count; ++old_mask) {
                    const int previous_index = state_index(time - duration, count - 1, old_mask, max_count, mask_count);
                    const auto previous = dp[previous_index];
                    if (previous.score == kNegativeInfinity) {
                        continue;
                    }
                    const int new_mask = old_mask | mask;
                    const int current_index = state_index(time, count, new_mask, max_count, mask_count);
                    const int new_score = previous.score + item.score;
                    if (new_score > dp[current_index].score) {
                        auto next = with_selected(previous, i);
                        next.score = new_score;
                        dp[current_index] = next;
                    }
                }
            }
        }
    }

    State best;
    int best_count = 0;
    int best_mask = 0;
    for (int time = 0; time <= max_time; ++time) {
        for (int count = 0; count <= max_count; ++count) {
            for (int mask = 0; mask < mask_count; ++mask) {
                const auto& state = dp[state_index(time, count, mask, max_count, mask_count)];
                if (state.score == kNegativeInfinity) {
                    continue;
                }
                if (better_final_state(state.score, count, mask, best, best_count, best_mask)) {
                    best = state;
                    best_count = count;
                    best_mask = mask;
                }
            }
        }
    }

    const auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
        Clock::now() - started
    ).count();
    return result_from_state(best, indexed, goal, mask_tags, best_mask, elapsed_us);
}

TrainingPlanResult buildGreedyTrainingPlan(
    const std::vector<Problem>& candidates,
    const TrainingGoal& goal
) {
    const auto started = Clock::now();
    auto indexed = limit_candidates(candidates, goal);
    std::stable_sort(indexed.begin(), indexed.end(), [](const IndexedProblem& lhs, const IndexedProblem& rhs) {
        if (lhs.score != rhs.score) {
            return lhs.score > rhs.score;
        }
        return lhs.problem.estimated_minutes < rhs.problem.estimated_minutes;
    });

    const auto mask_tags = core_tags(candidates, goal);
    const auto tag_index = tag_index_map(mask_tags);
    int mask = 0;
    int total_time = 0;
    int selected_count = 0;
    State state;
    state.score = 0;

    for (int i = 0; i < static_cast<int>(indexed.size()); ++i) {
        if (selected_count >= goal.target_count) {
            break;
        }
        const int duration = std::max(1, indexed[i].problem.estimated_minutes);
        if (total_time + duration > goal.time_budget_minutes) {
            continue;
        }
        total_time += duration;
        ++selected_count;
        mask |= problem_mask(indexed[i].problem, tag_index);
        state.score += indexed[i].score;
        state = with_selected(state, i);
    }

    const auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
        Clock::now() - started
    ).count();
    return result_from_state(state, indexed, goal, mask_tags, mask, elapsed_us);
}

} // namespace atp
