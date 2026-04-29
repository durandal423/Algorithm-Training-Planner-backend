#pragma once

#include "repository/app_repository.hpp"

#include <nlohmann/json.hpp>

namespace atp {

class TrainingPlanService {
public:
    explicit TrainingPlanService(AppRepository& repository);

    nlohmann::json generateTrainingPlan(const nlohmann::json& request) const;
    nlohmann::json compareWindowAlgorithms(const nlohmann::json& request) const;
    nlohmann::json compareDpAlgorithms(const nlohmann::json& request) const;

private:
    AppRepository& repository_;

    TrainingGoal goalFromGenerateRequest(const nlohmann::json& request, std::optional<long long>& goal_id) const;
};

} // namespace atp
