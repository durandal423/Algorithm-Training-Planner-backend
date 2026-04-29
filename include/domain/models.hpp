#pragma once

#include <optional>
#include <string>
#include <vector>

namespace atp {

struct Tag {
    long long id{};
    std::string name;
    std::string description;
    int mastery_score{50};
    int wrong_count{};
    std::optional<std::string> last_trained_at;
};

struct Problem {
    long long id{};
    std::string problem_code;
    std::string title;
    std::string source_platform;
    std::string source_url;
    int difficulty{};
    int estimated_minutes{};
    std::string summary;
    bool is_completed{};
    bool is_wrong_problem{};
    int wrong_count{};
    std::optional<std::string> last_practiced_at;
    std::string created_at;
    std::string updated_at;
    std::vector<Tag> tags;
};

struct TrainingGoal {
    long long id{};
    std::optional<long long> user_id;
    std::string name{"Generated Training Plan"};
    std::string description;
    int target_count{10};
    int time_budget_minutes{180};
    std::optional<int> difficulty_min;
    std::optional<int> difficulty_max;
    bool prefer_wrong_problems{true};
    bool prefer_weak_tags{true};
    int difficulty_weight{10};
    int tag_coverage_weight{20};
    int wrong_problem_weight{15};
    int weak_tag_weight{15};
    int estimated_time_weight{1};
    std::vector<Tag> target_tags;
    std::vector<std::string> target_tag_names;
    std::string created_at;
};

struct CandidateWindow {
    std::vector<Problem> candidates;
    int difficulty_span{};
    int left_index{-1};
    int right_index{-1};
    int left_difficulty{};
    int right_difficulty{};
    int wrong_problem_count{};
    int weak_tag_hit_count{};
    double coverage_ratio{};
    std::vector<std::string> covered_tags;
    std::vector<std::string> uncovered_tags;
    long long elapsed_microseconds{};
};

struct SelectedProblem {
    Problem problem;
    long long plan_item_id{};
    int score{};
    std::string selected_reason;
    std::vector<std::string> covered_target_tags;
    std::string item_status{"not_started"};
    std::optional<long long> last_submission_id;
    std::optional<long long> last_training_record_id;
    std::optional<std::string> last_verdict;
    std::optional<std::string> last_updated_at;
    std::optional<std::string> last_error_type;
    std::optional<bool> last_is_first_try_ac;
};

struct TrainingPlanResult {
    std::vector<SelectedProblem> items;
    int total_estimated_time{};
    int total_score{};
    int covered_tag_mask{};
    std::vector<std::string> covered_tags;
    std::vector<std::string> uncovered_tags;
    long long dp_elapsed_microseconds{};
};

struct TrainingPlanSummary {
    long long id{};
    std::optional<long long> goal_id;
    std::string name;
    int candidate_count{};
    int selected_count{};
    int total_estimated_time{};
    int total_score{};
    int difficulty_span{};
    int covered_tag_mask{};
    std::string status{"not_started"};
    std::string algorithm_summary;
    std::string created_at;
};

struct TrainingRecord {
    long long id{};
    std::optional<long long> plan_id;
    std::optional<long long> problem_id;
    bool is_finished{};
    bool is_first_try_ac{};
    std::optional<int> actual_minutes;
    std::string error_type;
    std::string review_note;
    std::string code_link;
    std::string practiced_at;
    std::optional<std::string> started_at;
    std::optional<std::string> ended_at;
    std::string duration_source{"manual"};
};

struct TrainingSession {
    long long id{};
    std::optional<long long> plan_id;
    std::optional<long long> plan_item_id;
    long long problem_id{};
    std::string status{"running"};
    std::string started_at;
    std::string last_resumed_at;
    int accumulated_seconds{};
    int elapsed_seconds{};
    std::optional<std::string> finished_at;
    std::optional<long long> created_record_id;
};

struct ProblemFilter {
    std::string keyword;
    std::string platform;
    std::optional<int> difficulty_min;
    std::optional<int> difficulty_max;
    std::string tag;
    std::optional<bool> completed;
    std::optional<bool> wrong;
    std::optional<std::string> last_practiced_from;
    std::optional<std::string> last_practiced_to;
    int page{1};
    int page_size{50};
};

struct ProblemPage {
    std::vector<Problem> items;
    long long total_count{};
    int page{1};
    int page_size{50};
};

} // namespace atp
