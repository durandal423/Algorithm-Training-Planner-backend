# Algorithm Training Planner Backend

C++23 REST API backend for the algorithm training plan generation system.

## Tech Stack

- CMake + vcpkg
- Crow HTTP framework
- PostgreSQL + libpqxx 7.10.5
- nlohmann/json
- spdlog
- Catch2 and k6

## Features

- Problem, tag, training goal, training plan, and training record APIs.
- Online Judge style C++ submission queue, judge config, test cases, and worker.
- Automatic database creation, schema initialization, and seed data loading.
- Two-pointer/sliding-window candidate pool selection.
- State-compressed dynamic programming training plan optimization.
- Dashboard summary/tag/activity APIs.
- Algorithm comparison APIs for presentation and thesis defense.

## Configuration

Copy `.env.example` to `.env` and set your PostgreSQL credentials:

```powershell
Copy-Item .env.example .env
```

The service reads `.env` during CMake configuration and generates `build/config.hpp`.

## Build

```powershell
cmake -S . -B build
cmake --build build
```

## Run

```powershell
.\build\Algorithm_Training_Planner.exe
```

The backend listens on `http://localhost:8080`.

On startup it:

1. Ensures the configured database exists.
2. Checks the required application and judge tables.
3. Runs `sql/001_schema.sql` and `sql/003_judge.sql` idempotently to create
   any missing tables.
4. Verifies that all required tables now exist.
5. Runs `sql/002_seed.sql` when core seed data such as users, tags, or
   problems is missing.

## Judge Worker

Start the API server in one terminal, then start the judge worker in another:

```powershell
.\build\Algorithm_Training_Planner.exe
.\build\judge_worker.exe
```

Run one queued submission and exit:

```powershell
.\build\judge_worker.exe --once
```

By default the worker uses Docker with `gcc:13` and runs submissions with
`--network none`, memory, CPU, and pid limits. Override the image with:

```powershell
$env:ATP_JUDGE_DOCKER_IMAGE='gcc:13'
```

For local development only, enable host execution explicitly:

```powershell
$env:ATP_JUDGE_ALLOW_LOCAL='1'
```

Admin judge routes require `X-Admin-Token`. Set `ATP_ADMIN_TOKEN` for a real
token; when it is not set, the development token is `dev-admin`.

## Tests

```powershell
ctest --test-dir build --output-on-failure
```

Run the API smoke test after starting the backend:

```powershell
k6 run tests/k6/smoke.js
```

Use a different target with:

```powershell
$env:BASE_URL='http://localhost:8080'
k6 run tests/k6/smoke.js
```

Optional PostgreSQL integration checks are disabled by default because they
create and remove temporary rows in the configured database:

```powershell
$env:RUN_DB_TESTS='1'
ctest --test-dir build --output-on-failure
Remove-Item Env:\RUN_DB_TESTS
```

## API Overview

Full request/response details are maintained in `docs/api.md`.

- `GET /health`
- `GET/POST /api/problems`
- `GET/PUT/DELETE /api/problems/{id}`
- `GET/PUT /api/problems/{id}/judge-config`
- `GET/POST /api/problems/{id}/test-cases`
- `POST /api/problems/{id}/test-cases/import`
- `POST /api/problems/{id}/test-cases/generate-expected`
- `PUT/DELETE /api/test-cases/{id}`
- `POST /api/submissions`
- `GET /api/submissions/{id}`
- `GET /api/problems/{id}/submissions`
- `POST /api/submissions/{id}/rejudge`
- `POST /api/problems/import`
- `GET/POST /api/tags`
- `PUT /api/tags/{id}`
- `GET/POST /api/training-goals`
- `GET/PUT/DELETE /api/training-goals/{id}`
- `POST /api/training-plans/generate`
- `GET /api/training-plans`
- `GET/DELETE /api/training-plans/{id}`
- `PUT /api/training-plans/{id}/status`
- `POST/GET /api/training-records`
- `GET /api/training-records/by-plan/{plan_id}`
- `GET /api/dashboard/summary`
- `GET /api/dashboard/tag-stats`
- `GET /api/dashboard/recent-activity`

`GET /api/problems` supports `keyword`, `platform`, `difficulty_min`,
`difficulty_max`, `tag`, `completed`, `wrong`, `last_practiced_from`,
`last_practiced_to`, `page`, and `page_size`.

Training plan status is restricted to `not_started`, `in_progress`,
`completed`, or `archived`.

## Algorithm Notes

The candidate selector sorts filtered problems by difficulty and advances a
single `[left, right]` window while maintaining tag counts, wrong-problem
counts, and weak-tag hits. This gives `O(n log n)` sorting plus `O(n)` scanning.

The optimizer uses compressed DP state `dp[t][k][mask]`, where `t` is time,
`k` is selected problem count, and `mask` records up to eight core tags. Each
selected item keeps a recovery bitset so the API can explain why it was chosen.

## Blue Bridge Cup Transfer

The backend is designed to show how contest skills become engineering modules:

- `P12134 画展布置` maps to the sliding-window candidate pool.
- `P12135 水质检测` maps to state-compressed dynamic programming.
- These algorithms run behind the training-plan generator rather than as
  standalone user-facing comparison features.
- Training records turn contest review habits into recommendation weights.
