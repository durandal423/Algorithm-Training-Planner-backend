#pragma once

#include "judge/sandbox_runner.hpp"
#include "repository/judge_repository.hpp"

namespace atp {

class JudgeService {
public:
    explicit JudgeService(JudgeRepository& repository);
    JudgeService(JudgeRepository& repository, SandboxRunner runner);

    bool processNextSubmission();

private:
    JudgeRepository& repository_;
    SandboxRunner runner_;
};

} // namespace atp
