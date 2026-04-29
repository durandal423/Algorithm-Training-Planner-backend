#pragma once

#include "domain/models.hpp"

#include <vector>

namespace atp {

TrainingPlanResult optimizeTrainingPlanByDP(
    const std::vector<Problem>& candidates,
    const TrainingGoal& goal
);

TrainingPlanResult buildGreedyTrainingPlan(
    const std::vector<Problem>& candidates,
    const TrainingGoal& goal
);

int scoreProblemForGoal(const Problem& problem, const TrainingGoal& goal);

} // namespace atp
