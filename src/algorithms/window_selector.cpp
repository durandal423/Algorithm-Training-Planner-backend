#include "algorithms/window_selector.hpp"

#include <algorithm>
#include <chrono>
#include <unordered_map>
#include <unordered_set>

namespace atp {
namespace {

using Clock = std::chrono::steady_clock;

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

std::vector<Problem> filtered_sorted(const std::vector<Problem>& problems, const TrainingGoal& goal) {
    std::vector<Problem> filtered;
    for (const auto& problem : problems) {
        if (goal.difficulty_min && problem.difficulty < *goal.difficulty_min) {
            continue;
        }
        if (goal.difficulty_max && problem.difficulty > *goal.difficulty_max) {
            continue;
        }
        filtered.push_back(problem);
    }

    std::stable_sort(filtered.begin(), filtered.end(), [](const Problem& lhs, const Problem& rhs) {
        if (lhs.difficulty != rhs.difficulty) {
            return lhs.difficulty < rhs.difficulty;
        }
        return lhs.id < rhs.id;
    });
    return filtered;
}

bool is_weak_tag(const Tag& tag) {
    return tag.mastery_score < 60 || tag.wrong_count > 0;
}

std::unordered_set<std::string> available_goal_tags(
    const std::vector<Problem>& problems,
    const std::vector<std::string>& requested
) {
    std::unordered_set<std::string> requested_set{requested.begin(), requested.end()};
    std::unordered_set<std::string> available;
    for (const auto& problem : problems) {
        for (const auto& tag : problem.tags) {
            if (requested_set.contains(tag.name)) {
                available.insert(tag.name);
            }
        }
    }
    return available;
}

struct WindowStats {
    int wrong_count{};
    int weak_hits{};
    std::unordered_map<std::string, int> tag_counts;
};

void add_problem(WindowStats& stats, const Problem& problem) {
    if (problem.is_wrong_problem || problem.wrong_count > 0) {
        ++stats.wrong_count;
    }
    for (const auto& tag : problem.tags) {
        ++stats.tag_counts[tag.name];
        if (is_weak_tag(tag)) {
            ++stats.weak_hits;
        }
    }
}

void remove_problem(WindowStats& stats, const Problem& problem) {
    if (problem.is_wrong_problem || problem.wrong_count > 0) {
        --stats.wrong_count;
    }
    for (const auto& tag : problem.tags) {
        if (auto iter = stats.tag_counts.find(tag.name); iter != stats.tag_counts.end()) {
            --iter->second;
            if (iter->second <= 0) {
                stats.tag_counts.erase(iter);
            }
        }
        if (is_weak_tag(tag)) {
            --stats.weak_hits;
        }
    }
}

int coverage_count(const WindowStats& stats, const std::unordered_set<std::string>& available) {
    int count = 0;
    for (const auto& name : available) {
        if (stats.tag_counts.contains(name)) {
            ++count;
        }
    }
    return count;
}

std::vector<std::string> covered_names(
    const WindowStats& stats,
    const std::vector<std::string>& requested
) {
    std::vector<std::string> result;
    for (const auto& name : requested) {
        if (stats.tag_counts.contains(name)) {
            result.push_back(name);
        }
    }
    return result;
}

std::vector<std::string> uncovered_names(
    const std::vector<std::string>& requested,
    const std::vector<std::string>& covered
) {
    std::unordered_set<std::string> covered_set{covered.begin(), covered.end()};
    std::vector<std::string> result;
    for (const auto& name : requested) {
        if (!covered_set.contains(name)) {
            result.push_back(name);
        }
    }
    return result;
}

int desired_candidate_count(const std::vector<Problem>& filtered, const TrainingGoal& goal) {
    const int target = std::max(goal.target_count, 0);
    if (target == 0) {
        return 0;
    }
    const int desired = std::min(80, std::max(target, target * 3));
    return std::min<int>(static_cast<int>(filtered.size()), desired);
}

bool better_window(
    const CandidateWindow& candidate,
    const CandidateWindow& best,
    bool prefer_wrong,
    bool prefer_weak
) {
    if (best.left_index < 0) {
        return true;
    }
    if (candidate.difficulty_span != best.difficulty_span) {
        return candidate.difficulty_span < best.difficulty_span;
    }
    if (candidate.covered_tags.size() != best.covered_tags.size()) {
        return candidate.covered_tags.size() > best.covered_tags.size();
    }
    if (prefer_wrong && candidate.wrong_problem_count != best.wrong_problem_count) {
        return candidate.wrong_problem_count > best.wrong_problem_count;
    }
    if (prefer_weak && candidate.weak_tag_hit_count != best.weak_tag_hit_count) {
        return candidate.weak_tag_hit_count > best.weak_tag_hit_count;
    }
    return candidate.candidates.size() < best.candidates.size();
}

CandidateWindow make_window(
    const std::vector<Problem>& filtered,
    int left,
    int right,
    const WindowStats& stats,
    const std::vector<std::string>& requested,
    int available_count
) {
    CandidateWindow window;
    window.left_index = left;
    window.right_index = right;
    window.left_difficulty = filtered[left].difficulty;
    window.right_difficulty = filtered[right].difficulty;
    window.difficulty_span = window.right_difficulty - window.left_difficulty;
    window.wrong_problem_count = stats.wrong_count;
    window.weak_tag_hit_count = stats.weak_hits;
    window.covered_tags = covered_names(stats, requested);
    window.uncovered_tags = uncovered_names(requested, window.covered_tags);
    window.coverage_ratio = available_count == 0
        ? 1.0
        : static_cast<double>(window.covered_tags.size()) / static_cast<double>(available_count);
    window.candidates.assign(filtered.begin() + left, filtered.begin() + right + 1);
    return window;
}

CandidateWindow make_fallback(
    const std::vector<Problem>& filtered,
    const std::vector<std::string>& requested,
    const Clock::time_point& started
) {
    CandidateWindow window;
    if (!filtered.empty()) {
        WindowStats stats;
        for (const auto& problem : filtered) {
            add_problem(stats, problem);
        }
        window = make_window(filtered, 0, static_cast<int>(filtered.size()) - 1, stats, requested,
            static_cast<int>(available_goal_tags(filtered, requested).size()));
    }
    window.elapsed_microseconds = std::chrono::duration_cast<std::chrono::microseconds>(
        Clock::now() - started
    ).count();
    return window;
}

bool valid_window(
    const WindowStats& stats,
    int size,
    int min_count,
    const std::unordered_set<std::string>& available
) {
    return size >= min_count && coverage_count(stats, available) >= static_cast<int>(available.size());
}

} // namespace

CandidateWindow selectStableDifficultyWindow(
    const std::vector<Problem>& problems,
    const TrainingGoal& goal
) {
    const auto started = Clock::now();
    const auto requested = goal_tags(goal);
    const auto filtered = filtered_sorted(problems, goal);
    if (filtered.empty() || goal.target_count <= 0) {
        return make_fallback(filtered, requested, started);
    }

    const int min_count = desired_candidate_count(filtered, goal);
    const auto available = available_goal_tags(filtered, requested);
    CandidateWindow best;
    WindowStats stats;
    int left = 0;

    for (int right = 0; right < static_cast<int>(filtered.size()); ++right) {
        add_problem(stats, filtered[right]);

        while (left <= right && valid_window(stats, right - left + 1, min_count, available)) {
            auto candidate = make_window(
                filtered,
                left,
                right,
                stats,
                requested,
                static_cast<int>(available.size())
            );

            if (better_window(candidate, best, goal.prefer_wrong_problems, goal.prefer_weak_tags)) {
                best = std::move(candidate);
            }
            remove_problem(stats, filtered[left]);
            ++left;
        }
    }

    if (best.left_index < 0) {
        best = make_fallback(filtered, requested, started);
    }

    best.elapsed_microseconds = std::chrono::duration_cast<std::chrono::microseconds>(
        Clock::now() - started
    ).count();
    return best;
}

CandidateWindow selectNaiveStableDifficultyWindow(
    const std::vector<Problem>& problems,
    const TrainingGoal& goal
) {
    const auto started = Clock::now();
    const auto requested = goal_tags(goal);
    const auto filtered = filtered_sorted(problems, goal);
    if (filtered.empty() || goal.target_count <= 0) {
        return make_fallback(filtered, requested, started);
    }

    const int min_count = desired_candidate_count(filtered, goal);
    const auto available = available_goal_tags(filtered, requested);
    CandidateWindow best;

    for (int left = 0; left < static_cast<int>(filtered.size()); ++left) {
        WindowStats stats;
        for (int right = left; right < static_cast<int>(filtered.size()); ++right) {
            add_problem(stats, filtered[right]);
            if (!valid_window(stats, right - left + 1, min_count, available)) {
                continue;
            }
            auto candidate = make_window(
                filtered,
                left,
                right,
                stats,
                requested,
                static_cast<int>(available.size())
            );
            if (better_window(candidate, best, goal.prefer_wrong_problems, goal.prefer_weak_tags)) {
                best = std::move(candidate);
            }
        }
    }

    if (best.left_index < 0) {
        best = make_fallback(filtered, requested, started);
    }

    best.elapsed_microseconds = std::chrono::duration_cast<std::chrono::microseconds>(
        Clock::now() - started
    ).count();
    return best;
}

} // namespace atp
