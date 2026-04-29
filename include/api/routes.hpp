#pragma once

#include "api/app.hpp"
#include "repository/app_repository.hpp"
#include "repository/judge_repository.hpp"
#include "service/training_plan_service.hpp"

namespace atp {

void registerRoutes(
    ApiApp& app,
    AppRepository& repository,
    JudgeRepository& judge_repository,
    TrainingPlanService& training_plan_service
);

} // namespace atp
