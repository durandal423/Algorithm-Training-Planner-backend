CREATE TABLE IF NOT EXISTS users (
    id BIGSERIAL PRIMARY KEY,
    username VARCHAR(64) NOT NULL UNIQUE,
    display_name VARCHAR(64),
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS problems (
    id BIGSERIAL PRIMARY KEY,
    problem_code VARCHAR(64) NOT NULL UNIQUE,
    title VARCHAR(255) NOT NULL,
    source_platform VARCHAR(64) NOT NULL,
    source_url TEXT,
    difficulty INTEGER NOT NULL,
    estimated_minutes INTEGER NOT NULL,
    summary TEXT,
    is_completed BOOLEAN NOT NULL DEFAULT FALSE,
    is_wrong_problem BOOLEAN NOT NULL DEFAULT FALSE,
    wrong_count INTEGER NOT NULL DEFAULT 0,
    last_practiced_at TIMESTAMP,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS tags (
    id BIGSERIAL PRIMARY KEY,
    name VARCHAR(64) NOT NULL UNIQUE,
    description TEXT,
    mastery_score INTEGER NOT NULL DEFAULT 50,
    wrong_count INTEGER NOT NULL DEFAULT 0,
    last_trained_at TIMESTAMP
);

CREATE TABLE IF NOT EXISTS problem_tags (
    problem_id BIGINT NOT NULL REFERENCES problems(id) ON DELETE CASCADE,
    tag_id BIGINT NOT NULL REFERENCES tags(id) ON DELETE CASCADE,
    PRIMARY KEY(problem_id, tag_id)
);

CREATE TABLE IF NOT EXISTS training_goals (
    id BIGSERIAL PRIMARY KEY,
    user_id BIGINT REFERENCES users(id),
    name VARCHAR(128) NOT NULL,
    description TEXT,
    target_count INTEGER NOT NULL,
    time_budget_minutes INTEGER NOT NULL,
    difficulty_min INTEGER,
    difficulty_max INTEGER,
    prefer_wrong_problems BOOLEAN NOT NULL DEFAULT TRUE,
    prefer_weak_tags BOOLEAN NOT NULL DEFAULT TRUE,
    difficulty_weight INTEGER NOT NULL DEFAULT 10,
    tag_coverage_weight INTEGER NOT NULL DEFAULT 20,
    wrong_problem_weight INTEGER NOT NULL DEFAULT 15,
    weak_tag_weight INTEGER NOT NULL DEFAULT 15,
    estimated_time_weight INTEGER NOT NULL DEFAULT 1,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS training_goal_tags (
    goal_id BIGINT NOT NULL REFERENCES training_goals(id) ON DELETE CASCADE,
    tag_id BIGINT NOT NULL REFERENCES tags(id) ON DELETE CASCADE,
    PRIMARY KEY(goal_id, tag_id)
);

CREATE TABLE IF NOT EXISTS training_plans (
    id BIGSERIAL PRIMARY KEY,
    goal_id BIGINT REFERENCES training_goals(id),
    name VARCHAR(128) NOT NULL,
    candidate_count INTEGER NOT NULL,
    selected_count INTEGER NOT NULL,
    total_estimated_time INTEGER NOT NULL,
    total_score INTEGER NOT NULL,
    difficulty_span INTEGER NOT NULL,
    covered_tag_mask INTEGER NOT NULL DEFAULT 0,
    status VARCHAR(32) NOT NULL DEFAULT 'not_started',
    algorithm_summary TEXT,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS training_plan_items (
    id BIGSERIAL PRIMARY KEY,
    plan_id BIGINT NOT NULL REFERENCES training_plans(id) ON DELETE CASCADE,
    problem_id BIGINT NOT NULL REFERENCES problems(id),
    order_index INTEGER NOT NULL,
    estimated_minutes INTEGER NOT NULL,
    score INTEGER NOT NULL,
    selected_reason TEXT,
    item_status VARCHAR(32) NOT NULL DEFAULT 'not_started',
    last_submission_id BIGINT,
    last_training_record_id BIGINT,
    last_verdict VARCHAR(32),
    last_updated_at TIMESTAMP,
    CONSTRAINT chk_training_plan_item_status CHECK (
        item_status IN ('not_started', 'failed', 'completed')
    ),
    CONSTRAINT chk_training_plan_item_verdict CHECK (
        last_verdict IS NULL OR last_verdict IN ('AC', 'WA', 'TLE', 'MLE', 'RE', 'CE', 'OLE', 'JE')
    )
);

CREATE TABLE IF NOT EXISTS training_records (
    id BIGSERIAL PRIMARY KEY,
    plan_id BIGINT REFERENCES training_plans(id) ON DELETE SET NULL,
    problem_id BIGINT REFERENCES problems(id),
    is_finished BOOLEAN NOT NULL DEFAULT FALSE,
    is_first_try_ac BOOLEAN NOT NULL DEFAULT FALSE,
    actual_minutes INTEGER,
    error_type VARCHAR(64),
    review_note TEXT,
    code_link TEXT,
    practiced_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    started_at TIMESTAMP,
    ended_at TIMESTAMP,
    duration_source VARCHAR(32) NOT NULL DEFAULT 'manual',
    CONSTRAINT chk_training_record_duration_source CHECK (
        duration_source IN ('manual', 'timer', 'judge')
    )
);

ALTER TABLE training_records
    ADD COLUMN IF NOT EXISTS started_at TIMESTAMP;

ALTER TABLE training_records
    ADD COLUMN IF NOT EXISTS ended_at TIMESTAMP;

ALTER TABLE training_records
    ADD COLUMN IF NOT EXISTS duration_source VARCHAR(32) NOT NULL DEFAULT 'manual';

ALTER TABLE training_records
    DROP CONSTRAINT IF EXISTS training_records_plan_id_fkey;

ALTER TABLE training_records
    ADD CONSTRAINT training_records_plan_id_fkey
    FOREIGN KEY (plan_id) REFERENCES training_plans(id) ON DELETE SET NULL;

DO $$
BEGIN
    IF NOT EXISTS (
        SELECT 1
        FROM pg_constraint
        WHERE conname = 'chk_training_record_duration_source'
          AND conrelid = 'training_records'::regclass
    ) THEN
        ALTER TABLE training_records
            ADD CONSTRAINT chk_training_record_duration_source
            CHECK (duration_source IN ('manual', 'timer', 'judge'));
    END IF;
END;
$$;

CREATE TABLE IF NOT EXISTS training_sessions (
    id BIGSERIAL PRIMARY KEY,
    plan_id BIGINT REFERENCES training_plans(id) ON DELETE SET NULL,
    plan_item_id BIGINT REFERENCES training_plan_items(id) ON DELETE SET NULL,
    problem_id BIGINT NOT NULL REFERENCES problems(id) ON DELETE CASCADE,
    status VARCHAR(32) NOT NULL DEFAULT 'running',
    started_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    last_resumed_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    accumulated_seconds INTEGER NOT NULL DEFAULT 0,
    finished_at TIMESTAMP,
    created_record_id BIGINT REFERENCES training_records(id) ON DELETE SET NULL,
    CONSTRAINT chk_training_session_status CHECK (
        status IN ('running', 'paused', 'completed', 'canceled')
    ),
    CONSTRAINT chk_training_session_accumulated CHECK (accumulated_seconds >= 0)
);

CREATE INDEX IF NOT EXISTS idx_problems_difficulty ON problems(difficulty);
CREATE INDEX IF NOT EXISTS idx_problems_difficulty_id ON problems(difficulty, id);
CREATE INDEX IF NOT EXISTS idx_problems_platform ON problems(source_platform);
CREATE INDEX IF NOT EXISTS idx_problems_status ON problems(is_completed, is_wrong_problem);
CREATE INDEX IF NOT EXISTS idx_problems_completed ON problems(is_completed);
CREATE INDEX IF NOT EXISTS idx_problems_wrong ON problems(is_wrong_problem);
CREATE INDEX IF NOT EXISTS idx_problem_tags_tag_id ON problem_tags(tag_id);
CREATE INDEX IF NOT EXISTS idx_training_records_plan ON training_records(plan_id);
CREATE INDEX IF NOT EXISTS idx_training_records_problem ON training_records(problem_id);
CREATE INDEX IF NOT EXISTS idx_training_records_practiced_at ON training_records(practiced_at);
CREATE INDEX IF NOT EXISTS idx_training_sessions_problem ON training_sessions(problem_id);
CREATE UNIQUE INDEX IF NOT EXISTS idx_training_sessions_single_active
ON training_sessions((1))
WHERE status IN ('running', 'paused');
CREATE INDEX IF NOT EXISTS idx_training_plans_created_at ON training_plans(created_at DESC);
CREATE INDEX IF NOT EXISTS idx_training_plan_items_plan_status ON training_plan_items(plan_id, item_status);
CREATE INDEX IF NOT EXISTS idx_training_plan_items_problem_plan ON training_plan_items(problem_id, plan_id);

CREATE OR REPLACE FUNCTION set_updated_at()
RETURNS TRIGGER AS $$
BEGIN
    NEW.updated_at = CURRENT_TIMESTAMP;
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

DROP TRIGGER IF EXISTS trg_problems_updated_at ON problems;
CREATE TRIGGER trg_problems_updated_at
BEFORE UPDATE ON problems
FOR EACH ROW
EXECUTE FUNCTION set_updated_at();
