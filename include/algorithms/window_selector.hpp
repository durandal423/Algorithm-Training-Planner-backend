#pragma once

#include "domain/models.hpp"

#include <vector>

namespace atp {

// Selects a candidate pool using the two-pointer/sliding-window idea:
// after sorting by difficulty, the [left, right] window is advanced once
// across the list while maintaining tag coverage and tie-break metrics.
CandidateWindow selectStableDifficultyWindow(
    const std::vector<Problem>& problems,
    const TrainingGoal& goal
);

CandidateWindow selectNaiveStableDifficultyWindow(
    const std::vector<Problem>& problems,
    const TrainingGoal& goal
);

} // namespace atp
