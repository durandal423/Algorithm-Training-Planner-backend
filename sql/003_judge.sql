CREATE TABLE IF NOT EXISTS judge_configs (
    id BIGSERIAL PRIMARY KEY,
    problem_id BIGINT NOT NULL UNIQUE REFERENCES problems(id) ON DELETE CASCADE,
    enabled BOOLEAN NOT NULL DEFAULT FALSE,
    language VARCHAR(32) NOT NULL DEFAULT 'cpp',
    compile_command_template TEXT,
    run_command_template TEXT,
    time_limit_ms INTEGER NOT NULL DEFAULT 1000,
    memory_limit_mb INTEGER NOT NULL DEFAULT 256,
    output_limit_kb INTEGER NOT NULL DEFAULT 1024,
    compare_mode VARCHAR(32) NOT NULL DEFAULT 'ignore_whitespace',
    float_epsilon DOUBLE PRECISION NOT NULL DEFAULT 1e-6,
    official_solution_language VARCHAR(32),
    official_solution_code TEXT,
    checker_language VARCHAR(32),
    checker_code TEXT,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT chk_judge_config_language CHECK (language IN ('cpp')),
    CONSTRAINT chk_judge_config_compare_mode CHECK (
        compare_mode IN ('exact', 'trim_trailing', 'ignore_whitespace', 'float_epsilon')
    ),
    CONSTRAINT chk_judge_config_limits CHECK (
        time_limit_ms > 0 AND memory_limit_mb > 0 AND output_limit_kb > 0
    )
);

CREATE TABLE IF NOT EXISTS judge_test_cases (
    id BIGSERIAL PRIMARY KEY,
    problem_id BIGINT NOT NULL REFERENCES problems(id) ON DELETE CASCADE,
    name VARCHAR(128),
    input_data TEXT NOT NULL,
    expected_output TEXT,
    expected_output_hash VARCHAR(128),
    visibility VARCHAR(32) NOT NULL DEFAULT 'hidden',
    points INTEGER NOT NULL DEFAULT 1,
    order_index INTEGER NOT NULL DEFAULT 0,
    time_limit_ms INTEGER,
    memory_limit_mb INTEGER,
    is_sample BOOLEAN NOT NULL DEFAULT FALSE,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT chk_judge_test_case_visibility CHECK (visibility IN ('sample', 'hidden', 'admin_only')),
    CONSTRAINT chk_judge_test_case_points CHECK (points >= 0),
    CONSTRAINT chk_judge_test_case_limits CHECK (
        (time_limit_ms IS NULL OR time_limit_ms > 0) AND
        (memory_limit_mb IS NULL OR memory_limit_mb > 0)
    )
);

CREATE TABLE IF NOT EXISTS submissions (
    id BIGSERIAL PRIMARY KEY,
    problem_id BIGINT NOT NULL REFERENCES problems(id) ON DELETE CASCADE,
    user_id BIGINT REFERENCES users(id),
    language VARCHAR(32) NOT NULL,
    source_code TEXT NOT NULL,
    status VARCHAR(32) NOT NULL DEFAULT 'queued',
    verdict VARCHAR(32),
    score INTEGER NOT NULL DEFAULT 0,
    compile_stdout TEXT,
    compile_stderr TEXT,
    compile_time_ms INTEGER,
    max_time_ms INTEGER,
    max_memory_kb INTEGER,
    submitted_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    started_at TIMESTAMP,
    finished_at TIMESTAMP,
    CONSTRAINT chk_submission_language CHECK (language IN ('cpp')),
    CONSTRAINT chk_submission_status CHECK (status IN ('queued', 'judging', 'completed', 'failed', 'canceled')),
    CONSTRAINT chk_submission_verdict CHECK (
        verdict IS NULL OR verdict IN ('AC', 'WA', 'TLE', 'MLE', 'RE', 'CE', 'OLE', 'JE')
    )
);

CREATE TABLE IF NOT EXISTS submission_results (
    id BIGSERIAL PRIMARY KEY,
    submission_id BIGINT NOT NULL REFERENCES submissions(id) ON DELETE CASCADE,
    test_case_id BIGINT REFERENCES judge_test_cases(id) ON DELETE SET NULL,
    order_index INTEGER NOT NULL DEFAULT 0,
    status VARCHAR(32) NOT NULL,
    verdict VARCHAR(32),
    time_ms INTEGER,
    memory_kb INTEGER,
    exit_code INTEGER,
    stdout_sample TEXT,
    stderr_sample TEXT,
    message TEXT,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT chk_submission_result_status CHECK (status IN ('queued', 'judging', 'completed', 'failed')),
    CONSTRAINT chk_submission_result_verdict CHECK (
        verdict IS NULL OR verdict IN ('AC', 'WA', 'TLE', 'MLE', 'RE', 'CE', 'OLE', 'JE')
    )
);

CREATE INDEX IF NOT EXISTS idx_submissions_problem ON submissions(problem_id);
CREATE INDEX IF NOT EXISTS idx_submissions_problem_finished ON submissions(problem_id, finished_at DESC, id DESC);
CREATE INDEX IF NOT EXISTS idx_submissions_status ON submissions(status, submitted_at);
CREATE INDEX IF NOT EXISTS idx_submission_results_submission ON submission_results(submission_id);
CREATE INDEX IF NOT EXISTS idx_judge_test_cases_problem ON judge_test_cases(problem_id, order_index);

DROP TRIGGER IF EXISTS trg_judge_configs_updated_at ON judge_configs;
CREATE TRIGGER trg_judge_configs_updated_at
BEFORE UPDATE ON judge_configs
FOR EACH ROW
EXECUTE FUNCTION set_updated_at();

DROP TRIGGER IF EXISTS trg_judge_test_cases_updated_at ON judge_test_cases;
CREATE TRIGGER trg_judge_test_cases_updated_at
BEFORE UPDATE ON judge_test_cases
FOR EACH ROW
EXECUTE FUNCTION set_updated_at();
