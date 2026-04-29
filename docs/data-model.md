# 数据模型导读

配套图：

- [06-data-model.drawio](diagrams/06-data-model.drawio)

## 表分组

数据库 schema 分为业务训练表和 Judge 表：

| 分组 | 表 | 作用 |
| --- | --- | --- |
| 题库与标签 | `problems`、`tags`、`problem_tags` | 管理题目、标签、多对多关系和题目训练状态 |
| 目标与计划 | `training_goals`、`training_goal_tags`、`training_plans`、`training_plan_items` | 保存用户目标、生成计划摘要和计划题目 |
| 训练记录与计时 | `training_records`、`training_sessions` | 记录复盘结果、计时会话、计划 item 状态回写 |
| Judge 配置 | `judge_configs`、`judge_test_cases` | 每题的判题配置和测试数据 |
| Judge 提交 | `submissions`、`submission_results` | 用户提交、最终 verdict、每个测试点结果 |
| 用户 | `users` | 当前 schema 保留用户维度，很多 API 仍以本地单用户使用为主 |

业务表主要在 `sql/001_schema.sql`，Judge 表在 `sql/003_judge.sql`。

## 核心关系

- `problems` 和 `tags` 通过 `problem_tags` 多对多关联。
- `training_goals` 和 `tags` 通过 `training_goal_tags` 多对多关联。
- `training_plans.goal_id` 指向生成来源目标；删除目标时计划保留，`goal_id` 置空。
- `training_plan_items` 连接计划和题目，保存选题得分、原因和当前 item 状态。
- `training_records` 可关联计划和题目；删除计划时记录保留，`plan_id` 置空。
- `training_sessions` 表示当前计时状态；唯一部分索引保证同时最多一个 running/paused 会话。
- `judge_configs` 对每道题唯一；`judge_test_cases` 属于题目。
- `submissions` 属于题目；`submission_results` 属于提交，并可引用某个测试点。

## 生命周期副作用

Repository 层不仅做 CRUD，还负责维护衍生状态：

- 创建或更新 `training_records` 后，会重新计算题目完成状态、错题计数、标签掌握度和计划 item 状态。
- 删除 `training_records` 后，会把受影响的题目和计划 item 重新计算到一致状态。
- 完成 `training_sessions` 会创建一条 `duration_source = 'timer'` 的训练记录。
- Judge AC/非 AC 会通过 `recordAcceptedSubmission()` / `recordRejectedSubmission()` 推动题目和计划 item 状态。
- 删除 `training_plans` 不删除历史训练记录，而是把记录上的 `plan_id` 置空。

这些副作用集中在 `src/repository/app_repository.cpp` 和 `src/repository/judge_repository.cpp`，避免 API 层手动拼装状态更新。

## 索引与约束

值得关注的约束设计：

- `idx_training_sessions_single_active`：部分唯一索引，保证同时只有一个 active session。
- `chk_training_plan_item_status`、`chk_training_session_status`、`chk_submission_status`：把状态机允许值落到数据库。
- `idx_submissions_status`：worker 按 `status/submitted_at` 找 queued submission。
- `idx_problems_difficulty`、`idx_problems_difficulty_id`：支持题库筛选和算法侧按难度排序的常见访问。
- `ON DELETE SET NULL` 用在训练记录保留历史的场景，`ON DELETE CASCADE` 用在强从属数据场景。

## 领域模型映射

| C++ struct | 主要表 | 说明 |
| --- | --- | --- |
| `Problem` | `problems`、`problem_tags`、`tags` | 题目详情携带标签列表 |
| `TrainingGoal` | `training_goals`、`training_goal_tags` | 生成计划的目标和权重 |
| `CandidateWindow` | 不直接对应单表 | 算法运行中间结果，摘要进入 `algorithm_summary` |
| `TrainingPlanSummary` | `training_plans` | 计划列表和详情摘要 |
| `SelectedProblem` | `training_plan_items` + `problems` | 计划中的一道题及其进度 |
| `TrainingRecord` | `training_records` | 复盘/练习记录 |
| `TrainingSession` | `training_sessions` | 当前计时器状态 |
| `JudgeConfig` | `judge_configs` | 判题配置 |
| `JudgeTestCase` | `judge_test_cases` | 测试点 |
| `Submission` | `submissions`、`submission_results` | 提交及测试点结果 |

## 源码锚点

- `sql/001_schema.sql`：业务训练 schema。
- `sql/003_judge.sql`：Judge schema。
- `include/domain/models.hpp`、`include/domain/judge_models.hpp`：领域模型。
- `src/repository/app_repository.cpp`：业务表读写和训练状态重算。
- `src/repository/judge_repository.cpp`：Judge 表读写和提交状态回写。

