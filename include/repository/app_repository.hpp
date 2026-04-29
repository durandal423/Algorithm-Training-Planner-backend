#pragma once

#include "domain/models.hpp"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>

namespace pqxx {
class transaction_base;
}

namespace atp {

class ConnectionPool;

class AppRepository {
public:
    explicit AppRepository(ConnectionPool& pool);

    ProblemPage listProblems(const ProblemFilter& filter) const;
    std::vector<Problem> listAllProblems() const;
    std::optional<Problem> getProblem(long long id) const;
    long long createProblem(const Problem& problem, const std::vector<std::string>& tag_names) const;
    bool updateProblem(long long id, const Problem& problem, const std::vector<std::string>& tag_names) const;
    bool deleteProblem(long long id) const;

    std::vector<Tag> listTags() const;
    std::optional<Tag> getTag(long long id) const;
    long long createTag(const Tag& tag) const;
    bool updateTag(long long id, const Tag& tag) const;

    std::vector<TrainingGoal> listTrainingGoals() const;
    std::optional<TrainingGoal> getTrainingGoal(long long id) const;
    long long createTrainingGoal(const TrainingGoal& goal) const;
    bool updateTrainingGoal(long long id, const TrainingGoal& goal) const;
    bool deleteTrainingGoal(long long id) const;

    long long saveTrainingPlan(
        std::optional<long long> goal_id,
        const std::string& name,
        const CandidateWindow& window,
        const TrainingPlanResult& result,
        const std::string& algorithm_summary
    ) const;
    std::vector<TrainingPlanSummary> listTrainingPlans() const;
    std::optional<TrainingPlanSummary> getTrainingPlan(long long id) const;
    std::vector<SelectedProblem> getTrainingPlanItems(long long plan_id) const;
    bool updateTrainingPlanStatus(long long id, const std::string& status) const;
    bool deleteTrainingPlan(long long id) const;

    long long createTrainingRecord(const TrainingRecord& record) const;
    std::optional<TrainingRecord> getTrainingRecord(long long id) const;
    std::optional<TrainingRecord> updateTrainingRecord(long long id, const TrainingRecord& record) const;
    bool deleteTrainingRecord(long long id) const;
    std::vector<TrainingRecord> listTrainingRecords(std::optional<long long> plan_id = std::nullopt) const;

    std::optional<TrainingSession> startTrainingSession(const TrainingSession& session) const;
    std::optional<TrainingSession> getActiveTrainingSession() const;
    std::optional<TrainingSession> pauseTrainingSession(long long id) const;
    std::optional<TrainingSession> resumeTrainingSession(long long id) const;
    std::optional<TrainingSession> finishTrainingSession(long long id, const TrainingRecord& review) const;
    std::optional<TrainingSession> cancelTrainingSession(long long id) const;

    nlohmann::json dashboardSummary() const;
    nlohmann::json dashboardTagStats() const;
    nlohmann::json dashboardRecentActivity() const;

private:
    ConnectionPool& pool_;

    long long upsertTag(pqxx::transaction_base& tx, const std::string& name) const;
    void replaceProblemTags(pqxx::transaction_base& tx, long long problem_id, const std::vector<std::string>& names) const;
    void replaceGoalTags(pqxx::transaction_base& tx, long long goal_id, const std::vector<std::string>& names) const;
    std::vector<Tag> loadProblemTags(pqxx::transaction_base& tx, long long problem_id) const;
    std::vector<Tag> loadGoalTags(pqxx::transaction_base& tx, long long goal_id) const;
    void updatePlanItemFromTrainingRecord(pqxx::transaction_base& tx, const TrainingRecord& record, long long record_id) const;
    long long insertTrainingRecord(pqxx::transaction_base& tx, const TrainingRecord& record) const;
    void recalculateProblemProgress(pqxx::transaction_base& tx, long long problem_id) const;
    void recalculateTagProgressForProblem(pqxx::transaction_base& tx, long long problem_id) const;
    void recalculatePlanItemProgress(pqxx::transaction_base& tx, long long plan_id, long long problem_id) const;
    void refreshPlanStatusFromItems(pqxx::transaction_base& tx, long long plan_id) const;
};

} // namespace atp
