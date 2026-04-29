#pragma once

#include "api/app.hpp"
#include "repository/judge_repository.hpp"

namespace atp {

void registerJudgeRoutes(ApiApp& app, JudgeRepository& repository);

} // namespace atp
