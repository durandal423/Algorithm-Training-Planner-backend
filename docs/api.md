# Algorithm Training Planner Backend API Contract

This document is the source of truth for building the frontend against the
current C++ backend. The backend is a JSON REST API served by Crow.

Default base URL:

```text
http://localhost:8080
```

All request and response bodies are JSON unless stated otherwise. Error
responses use:

```json
{
  "error": "message"
}
```

Admin write routes require `X-Admin-Token`. In local development, when
`ATP_ADMIN_TOKEN` is not configured, the fallback token is `dev-admin`.

The frontend should use `VITE_API_BASE_URL` and default it to
`http://localhost:8080`.

## Shared Conventions

- Pagination: `GET /api/problems` uses `page` starting at `1`; `page_size` is
  clamped to `1..200`.
- Boolean query values accepted by the backend include `true`, `false`, `1`,
  `0`, and `yes`.
- Timestamps are returned as backend-formatted timestamp strings.
- Optional fields may be `null`.
- IDs are numeric.
- CORS currently allows `Content-Type` and `X-Admin-Token`.

## Frontend Domain Types

These TypeScript-like shapes summarize fields returned by the API.

```ts
type Tag = {
  id: number;
  name: string;
  description: string;
  mastery_score: number;
  wrong_count: number;
  last_trained_at: string | null;
};

type Problem = {
  id: number;
  problem_code: string;
  title: string;
  source_platform: string;
  source_url: string;
  difficulty: number;
  estimated_minutes: number;
  summary: string;
  is_completed: boolean;
  is_wrong_problem: boolean;
  wrong_count: number;
  last_practiced_at: string | null;
  created_at: string;
  updated_at: string;
  tags: string[];
  tag_details: Tag[];
};

type TrainingGoal = {
  id: number;
  user_id: number | null;
  name: string;
  description: string;
  target_count: number;
  time_budget_minutes: number;
  difficulty_min: number | null;
  difficulty_max: number | null;
  target_tags: string[];
  prefer_wrong_problems: boolean;
  prefer_weak_tags: boolean;
  difficulty_weight: number;
  tag_coverage_weight: number;
  wrong_problem_weight: number;
  weak_tag_weight: number;
  estimated_time_weight: number;
  created_at: string;
};

type CandidateWindow = {
  left_index: number;
  right_index: number;
  left_difficulty: number;
  right_difficulty: number;
  difficulty_span: number;
  candidate_count: number;
  wrong_problem_count: number;
  weak_tag_hit_count: number;
  coverage_ratio: number;
  covered_tags: string[];
  uncovered_tags: string[];
  elapsed_microseconds: number;
};

type SelectedProblem = Problem & {
  problem_id: number;
  plan_item_id: number | null;
  score: number;
  selected_reason: string;
  covered_target_tags: string[];
  item_status: "not_started" | "failed" | "completed";
  last_submission_id: number | null;
  last_training_record_id: number | null;
  last_verdict: "AC" | "WA" | "TLE" | "MLE" | "RE" | "CE" | "OLE" | "JE" | null;
  last_updated_at: string | null;
  last_error_type: string | null;
  last_is_first_try_ac: boolean | null;
};

type TrainingPlanSummary = {
  id: number;
  goal_id: number | null;
  name: string;
  candidate_count: number;
  selected_count: number;
  total_estimated_time: number;
  total_score: number;
  difficulty_span: number;
  covered_tag_mask: number;
  status: "not_started" | "in_progress" | "completed" | "archived";
  algorithm_summary: unknown;
  created_at: string;
};

type TrainingRecord = {
  id: number;
  plan_id: number | null;
  problem_id: number | null;
  is_finished: boolean;
  is_first_try_ac: boolean;
  actual_minutes: number | null;
  error_type: string;
  review_note: string;
  code_link: string;
  practiced_at: string;
  started_at: string | null;
  ended_at: string | null;
  duration_source: "manual" | "timer" | "judge";
};

type TrainingSession = {
  id: number;
  plan_id: number | null;
  plan_item_id: number | null;
  problem_id: number;
  status: "running" | "paused" | "completed" | "canceled";
  started_at: string;
  last_resumed_at: string;
  accumulated_seconds: number;
  elapsed_seconds: number;
  finished_at: string | null;
  created_record_id: number | null;
};
```

Judge-related shapes:

```ts
type JudgeConfig = {
  id: number;
  problem_id: number;
  enabled: boolean;
  language: "cpp";
  compile_command_template: string | null;
  run_command_template: string | null;
  time_limit_ms: number;
  memory_limit_mb: number;
  output_limit_kb: number;
  compare_mode: "exact" | "trim_trailing" | "ignore_whitespace" | "float_epsilon";
  float_epsilon: number;
  official_solution_language: "cpp" | null;
  has_official_solution: boolean;
  official_solution_size: number;
  official_solution_code?: string | null;
  checker_language: string | null;
  has_checker: boolean;
  checker_size: number;
  checker_code?: string | null;
  created_at: string;
  updated_at: string;
};

type JudgeTestCase = {
  id: number;
  problem_id: number;
  name: string;
  visibility: "sample" | "hidden" | "admin_only";
  points: number;
  order_index: number;
  time_limit_ms: number | null;
  memory_limit_mb: number | null;
  is_sample: boolean;
  expected_output_hash: string | null;
  has_expected_output: boolean;
  input_data?: string;
  expected_output?: string | null;
  created_at: string;
  updated_at: string;
};

type Submission = {
  id: number;
  problem_id: number;
  user_id: number | null;
  language: "cpp";
  status: "queued" | "judging" | "completed" | "failed" | "canceled";
  verdict: "AC" | "WA" | "TLE" | "MLE" | "RE" | "CE" | "OLE" | "JE" | null;
  score: number;
  compile_stdout: string;
  compile_stderr: string;
  compile_time_ms: number | null;
  max_time_ms: number | null;
  max_memory_kb: number | null;
  submitted_at: string;
  started_at: string | null;
  finished_at: string | null;
  source_code?: string;
  results: SubmissionResult[];
};

type SubmissionResult = {
  id: number;
  submission_id: number;
  test_case_id: number | null;
  order_index: number;
  status: "queued" | "judging" | "completed" | "failed";
  verdict: Submission["verdict"];
  time_ms: number | null;
  memory_kb: number | null;
  exit_code: number | null;
  stdout_sample: string;
  stderr_sample: string;
  message: string;
  created_at: string;
};
```

## Health

### `GET /health`

Returns:

```json
{
  "status": "ok"
}
```

Use this for a simple backend connectivity indicator.

## Problems

### `GET /api/problems`

Lists problems with filters.

Query parameters:

| Name | Type | Notes |
| --- | --- | --- |
| `keyword` | string | Matches `problem_code` or `title`. |
| `platform` | string | Exact source platform. |
| `difficulty_min` | number | Inclusive. |
| `difficulty_max` | number | Inclusive. |
| `tag` | string | Exact tag name. |
| `completed` | boolean | `true`, `false`, `1`, `0`, `yes`. |
| `wrong` | boolean | `true`, `false`, `1`, `0`, `yes`. |
| `last_practiced_from` | string | Timestamp/date string, inclusive. |
| `last_practiced_to` | string | Timestamp/date string, inclusive. |
| `page` | number | Starts at `1`. |
| `page_size` | number | Clamped to `1..200`. |

Response:

```json
{
  "items": [
    {
      "id": 1,
      "problem_code": "P12134",
      "title": "画展布置",
      "source_platform": "蓝桥杯",
      "source_url": "https://example.com/problems/P12134",
      "difficulty": 1300,
      "estimated_minutes": 25,
      "summary": "从画作区间中选择稳定难度窗口。",
      "is_completed": false,
      "is_wrong_problem": true,
      "wrong_count": 2,
      "last_practiced_at": null,
      "created_at": "2026-04-27 14:49:32.749037",
      "updated_at": "2026-04-27 14:49:32.749037",
      "tags": ["双指针", "排序"],
      "tag_details": []
    }
  ],
  "count": 1,
  "total_count": 34,
  "pagination": {
    "page": 1,
    "page_size": 50,
    "total_count": 34,
    "total_pages": 1
  }
}
```

### `GET /api/problems/{id}`

Returns one `Problem`.

### `POST /api/problems`

Admin only.

Creates a problem. Required fields: `problem_code`, `title`,
`source_platform`, `difficulty`, `estimated_minutes`.

Request:

```json
{
  "problem_code": "T1001",
  "title": "区间训练",
  "source_platform": "洛谷",
  "source_url": "https://example.com",
  "difficulty": 1200,
  "estimated_minutes": 25,
  "summary": "练习前缀和与双指针",
  "is_completed": false,
  "is_wrong_problem": false,
  "wrong_count": 0,
  "tags": ["前缀和", "双指针"]
}
```

Response:

```json
{
  "id": 101
}
```

### `PUT /api/problems/{id}`

Admin only.

Updates a problem. Use the same body shape as create. Returns the updated
`Problem`.

### `DELETE /api/problems/{id}`

Admin only.

Response:

```json
{
  "deleted": true
}
```

### `POST /api/problems/import`

Admin only.

Bulk import. Accepts either an array of problem objects or:

```json
{
  "problems": []
}
```

Response:

```json
{
  "imported_count": 2,
  "ids": [101, 102]
}
```

## Tags

### `GET /api/tags`

Returns:

```json
{
  "items": [],
  "count": 0
}
```

### `POST /api/tags`

Admin only.

Request:

```json
{
  "name": "动态规划",
  "description": "状态设计与转移",
  "mastery_score": 50,
  "wrong_count": 0
}
```

Response:

```json
{
  "id": 12
}
```

### `PUT /api/tags/{id}`

Admin only.

Updates a tag. Same body as create. Returns the updated `Tag`.

## Training Goals

### `GET /api/training-goals`

Returns:

```json
{
  "items": [],
  "count": 0
}
```

### `GET /api/training-goals/{id}`

Returns one `TrainingGoal`.

### `POST /api/training-goals`

Required fields: `name`, `target_count`, `time_budget_minutes`.

Request:

```json
{
  "name": "蓝桥杯复盘计划",
  "description": "重点覆盖 DP 与双指针",
  "target_count": 8,
  "time_budget_minutes": 180,
  "difficulty_min": 1000,
  "difficulty_max": 1800,
  "target_tags": ["动态规划", "双指针", "图论"],
  "prefer_wrong_problems": true,
  "prefer_weak_tags": true,
  "difficulty_weight": 10,
  "tag_coverage_weight": 20,
  "wrong_problem_weight": 15,
  "weak_tag_weight": 15,
  "estimated_time_weight": 1
}
```

Response:

```json
{
  "id": 3
}
```

### `PUT /api/training-goals/{id}`

Updates a goal. Same body as create. Returns the updated `TrainingGoal`.

### `DELETE /api/training-goals/{id}`

Deletes a goal and keeps already generated plans by clearing their `goal_id`.

Response:

```json
{
  "deleted": true
}
```

## Training Plans

### `POST /api/training-plans/generate`

Generates and saves a training plan. This endpoint actually runs the
sliding-window candidate selector and the dynamic-programming optimizer.

Request can use a stored goal:

```json
{
  "goal_id": 1
}
```

Or direct goal fields:

```json
{
  "name": "本周专题训练",
  "target_count": 10,
  "time_budget_minutes": 180,
  "difficulty_min": 1000,
  "difficulty_max": 1800,
  "target_tags": ["动态规划", "双指针", "图论"],
  "prefer_wrong_problems": true,
  "prefer_weak_tags": true
}
```

If `goal_id` and direct fields are both provided, direct fields override the
stored goal for this generation only.

Response:

```json
{
  "plan_id": 5,
  "candidate_window": {
    "difficulty_span": 400,
    "candidate_count": 12,
    "covered_tags": ["动态规划", "双指针"],
    "uncovered_tags": ["图论"],
    "elapsed_microseconds": 1200
  },
  "dp_result": {
    "selected_count": 8,
    "total_estimated_time": 170,
    "total_score": 280,
    "covered_tag_mask": 7,
    "covered_tags": ["动态规划", "双指针"],
    "uncovered_tags": ["图论"],
    "dp_elapsed_microseconds": 3200
  },
  "items": []
}
```

The `items` array contains persisted `SelectedProblem` records with plan item
status fields. Newly generated items start with `item_status: "not_started"`.

### `GET /api/training-plans`

Returns:

```json
{
  "items": [],
  "count": 0
}
```

Each item is a `TrainingPlanSummary`.

### `GET /api/training-plans/{id}`

Returns a `TrainingPlanSummary` plus:

```json
{
  "items": []
}
```

Each item is a selected problem with score and reason.

### `PUT /api/training-plans/{id}/status`

Allowed statuses: `not_started`, `in_progress`, `completed`, `archived`.

Request:

```json
{
  "status": "completed"
}
```

Response:

```json
{
  "id": 5,
  "status": "completed"
}
```

### `DELETE /api/training-plans/{id}`

Deletes a plan and its selected-plan items. Existing training records are kept
with `plan_id` set to `null`.

Response:

```json
{
  "deleted": true
}
```

## Training Sessions

Training sessions persist one active timer. Only one `running` or `paused`
session can exist at a time.

### `POST /api/training-sessions`

Starts a timer.

Request:

```json
{
  "problem_id": 1,
  "plan_id": 5,
  "plan_item_id": 12
}
```

`problem_id` is required. `plan_id` and `plan_item_id` are optional. Returns
`409` when another session is already `running` or `paused`.

Response: `TrainingSession`.

### `GET /api/training-sessions/active`

Returns the current `running` or `paused` `TrainingSession`, or `null`.

### `POST /api/training-sessions/{id}/pause`

Moves a running session to `paused` and adds elapsed seconds to
`accumulated_seconds`. Returns `TrainingSession`.

### `POST /api/training-sessions/{id}/resume`

Moves a paused session to `running` and refreshes `last_resumed_at`. Returns
`TrainingSession`.

### `POST /api/training-sessions/{id}/finish`

Finishes a session and creates a `TrainingRecord` with
`duration_source: "timer"`. If `actual_minutes` is omitted, the backend uses
`ceil(elapsed_seconds / 60)`.

Request:

```json
{
  "is_finished": true,
  "is_first_try_ac": false,
  "actual_minutes": 28,
  "error_type": "边界条件遗漏",
  "review_note": "暂停后复盘",
  "code_link": "https://example.com/solution"
}
```

Response: completed `TrainingSession` with `created_record_id`.

### `POST /api/training-sessions/{id}/cancel`

Cancels a session without creating a training record. Returns
`TrainingSession`.

## Training Records

### `POST /api/training-records`

Creates a review/training record. Required field: `problem_id`.

Request:

```json
{
  "plan_id": 5,
  "problem_id": 1,
  "is_finished": true,
  "is_first_try_ac": false,
  "actual_minutes": 28,
  "error_type": "代码实现错误",
  "review_note": "边界条件漏判",
  "code_link": "https://example.com/solution"
}
```

Response:

```json
{
  "id": 88
}
```

Creating a record updates the problem completion/wrong status, tag mastery, and
the plan status when applicable.

### `GET /api/training-records`

Returns latest records, capped at 200:

```json
{
  "items": [],
  "count": 0
}
```

### `GET /api/training-records/by-plan/{plan_id}`

Returns latest records for one plan, capped at 200.

### `GET /api/training-records/{id}`

Returns one `TrainingRecord`.

### `PUT /api/training-records/{id}`

Updates an existing review/training record. Use the create body shape plus
optional `practiced_at`. `started_at`, `ended_at`, and `duration_source` are
read-only and preserved by the backend.

Response: updated `TrainingRecord`.

### `DELETE /api/training-records/{id}`

Deletes a record and recalculates the affected problem, tags, and plan item.
Deleting a judge-generated record does not delete the original submission.

Response:

```json
{
  "deleted": true
}
```

## Dashboard

### `GET /api/dashboard/summary`

Response:

```json
{
  "total_problems": 34,
  "completed_problems": 10,
  "wrong_problems": 8,
  "recent_7_days_minutes": 180,
  "average_actual_minutes": "24.50",
  "plan_completion_rate": 0.25,
  "current_plan": {
    "id": 5,
    "name": "本周专题训练",
    "status": "in_progress"
  }
}
```

`current_plan` can be `null`.

### `GET /api/dashboard/tag-stats`

Returns an array:

```json
[
  {
    "id": 1,
    "name": "动态规划",
    "mastery_score": 46,
    "wrong_count": 5,
    "completed_count": 3,
    "problem_count": 10,
    "last_trained_at": null
  }
]
```

### `GET /api/dashboard/recent-activity`

Returns latest 20 activity rows:

```json
[
  {
    "id": 1,
    "plan_id": 5,
    "problem_id": 1,
    "problem_code": "P12134",
    "title": "画展布置",
    "is_finished": true,
    "is_first_try_ac": false,
    "actual_minutes": 25,
    "error_type": "边界条件",
    "review_note": "复盘内容",
    "practiced_at": "2026-04-28 10:00:00"
  }
]
```

## Judge Configuration

Judge routes are useful for the problem detail page and an admin judge panel.
The API server only queues submissions; the separate `judge_worker` executable
must be running to compile and execute code.

### `GET /api/problems/{id}/judge-config`

Admin only. Returns config metadata without source code by default. Add
`?include_code=true` to include `official_solution_code` and `checker_code`.

Headers:

```text
X-Admin-Token: dev-admin
```

### `PUT /api/problems/{id}/judge-config`

Admin only. Creates or updates config.

Request:

```json
{
  "enabled": true,
  "language": "cpp",
  "compile_command_template": null,
  "run_command_template": null,
  "time_limit_ms": 1000,
  "memory_limit_mb": 256,
  "output_limit_kb": 1024,
  "compare_mode": "ignore_whitespace",
  "float_epsilon": 0.000001,
  "official_solution_language": "cpp",
  "official_solution_code": "#include <bits/stdc++.h>\nint main(){return 0;}",
  "checker_language": null,
  "checker_code": null
}
```

Only C++ is currently supported. `checker_code` is stored but custom checker
execution is not implemented yet.

## Judge Test Cases

### `GET /api/problems/{id}/test-cases`

Without admin token, returns sample-visible cases. With admin token, returns all
cases, including hidden and admin-only cases. Returned cases include `input_data`
and `expected_output` for the cases that are visible to the current request.

Response:

```json
{
  "items": [],
  "count": 0
}
```

### `POST /api/problems/{id}/test-cases`

Admin only. Creates one test case.

Request:

```json
{
  "name": "sample 1",
  "input_data": "4 2\n1 5 2 4\n",
  "expected_output": "3\n",
  "visibility": "sample",
  "points": 1,
  "order_index": 1,
  "time_limit_ms": null,
  "memory_limit_mb": null,
  "is_sample": true
}
```

Response:

```json
{
  "id": 6
}
```

### `POST /api/problems/{id}/test-cases/import`

Admin only. Accepts an array of test case objects or:

```json
{
  "test_cases": []
}
```

Response:

```json
{
  "imported_count": 2,
  "ids": [6, 7]
}
```

### `PUT /api/test-cases/{id}`

Admin only. Partial update supported. Returns the updated `JudgeTestCase` with
payload.

### `DELETE /api/test-cases/{id}`

Admin only.

Response:

```json
{
  "deleted": true
}
```

### `POST /api/problems/{id}/test-cases/generate-expected`

Admin only. Compiles the configured official solution, runs it against selected
test case inputs, and stores generated expected output.

Request:

```json
{
  "test_case_ids": [6, 7]
}
```

Omit `test_case_ids` to generate for all configured cases.

Response:

```json
{
  "generated_count": 2,
  "failed_count": 0,
  "failures": []
}
```

## Submissions

### `POST /api/submissions`

Queues a C++ submission.

Request:

```json
{
  "problem_id": 1,
  "user_id": null,
  "language": "cpp",
  "source_code": "#include <bits/stdc++.h>\nint main(){return 0;}"
}
```

Constraints:

- `language` must be `cpp`.
- `source_code` is required and must be at most 256 KB.
- A running `judge_worker` is required for the status to move beyond `queued`.

Response:

```json
{
  "id": 1001,
  "status": "queued"
}
```

### `GET /api/submissions/{id}`

Returns a `Submission`. Add `?include_source=true` to include `source_code`.
This local single-user tool does not require an admin token for historical
submission source viewing.

Frontend polling recommendation:

- Poll every 1.5-2 seconds while status is `queued` or `judging`.
- Stop polling when status is `completed`, `failed`, or `canceled`.
- Display `compile_stderr` when verdict is `CE`.
- Display per-test `message`, `stdout_sample`, and `stderr_sample` for failed
  results.

### `GET /api/problems/{id}/submissions`

Lists recent submissions for a problem.

Query:

| Name | Type | Notes |
| --- | --- | --- |
| `limit` | number | Clamped to `1..200`; default `50`. |

Response:

```json
{
  "items": [],
  "count": 0
}
```

### `POST /api/submissions/{id}/rejudge`

Admin only. Clears old per-test results and queues the submission again.

Response:

```json
{
  "id": 1001,
  "status": "queued"
}
```

## Suggested Frontend Page-to-API Map

| Page | Primary APIs |
| --- | --- |
| Dashboard | `/api/dashboard/summary`, `/api/dashboard/tag-stats`, `/api/dashboard/recent-activity` |
| Problem Bank | `/api/problems`, `/api/tags`, problem CRUD, `/api/problems/import` |
| Problem Detail | `/api/problems/{id}`, `/api/problems/{id}/test-cases`, `/api/problems/{id}/submissions`, `/api/submissions` |
| Judge Admin | judge config/test-case routes, generate expected output, rejudge |
| Tags | `/api/tags`, `/api/tags/{id}` |
| Training Goals | `/api/training-goals`, `/api/training-goals/{id}` |
| Plan Generation | `/api/training-plans/generate`, `/api/tags`, `/api/training-goals` |
| Plan Detail | `/api/training-plans/{id}`, `/api/training-records`, `/api/training-sessions`, plan status update/delete |
| Records | `/api/training-records`, `/api/training-records/{id}`, `/api/training-sessions/*` |
