# 工程设计模式导读

这份文档把项目里的实现手法抽象成可复用的工程模式。建议先读 [project-architecture.md](project-architecture.md)，再回来看这里。

## Repository Pattern

代表代码：

- `include/repository/app_repository.hpp`
- `include/repository/judge_repository.hpp`
- `src/repository/app_repository.cpp`
- `src/repository/judge_repository.cpp`

API 和 Service 不直接写 SQL，而是通过 Repository 表达业务意图，例如 `saveTrainingPlan()`、`createTrainingRecord()`、`fetchNextQueuedSubmission()`。这样 SQL、事务、行到领域对象的映射都集中在持久化层。

本项目的 Repository 不只是薄 CRUD，它还封装状态联动：训练记录变更会触发题目、标签、计划 item 状态重算；Judge 结果会触发题目错题状态和计划状态更新。

## Service Layer / Use Case Orchestration

代表代码：

- `TrainingPlanService::generateTrainingPlan()`
- `JudgeService::processNextSubmission()`

Service 层承担“用例编排”：

- 训练计划服务负责读取目标、校验、加载题库、调用算法、保存计划、组装响应。
- Judge 服务负责出队、检查配置、编译、运行测试点、比较输出、保存结果。

它们都不关心 HTTP 细节，也尽量不直接处理 SQL。这让同一用例可以被测试或 worker 复用。

## Functional Core for Algorithms

代表代码：

- `selectStableDifficultyWindow()`
- `selectNaiveStableDifficultyWindow()`
- `optimizeTrainingPlanByDP()`
- `buildGreedyTrainingPlan()`
- `scoreProblemForGoal()`

算法函数只接收 `Problem`、`TrainingGoal` 等领域对象，返回 `CandidateWindow` 或 `TrainingPlanResult`，没有数据库和网络副作用。这种“纯核心、外层编排”的结构让算法非常容易测试，也让对比算法接口可以复用同一套核心逻辑。

## DTO / JSON Adapter

代表代码：

- `src/api/json_utils.cpp`
- `src/api/judge_json.cpp`

领域模型不直接绑定 Crow response。API 层通过 JSON adapter 做转换，把 C++ struct 和 REST JSON 分开。好处是：

- API 字段名可以保持前端友好。
- 内部模型可以继续使用 `std::optional`、`std::vector<Tag>` 等 C++ 类型。
- 输出逻辑集中维护，减少各个路由手写 JSON 的重复。

## RAII Connection Lease

代表代码：

- `include/db/connection_pool.hpp`
- `src/db/connection_pool.cpp`

`ConnectionPool::acquire()` 返回 `ConnectionLease`。Lease 析构时自动把连接还回池中，移动构造/移动赋值用于转移所有权，拷贝被禁用。

这是 C++ 后端很典型的 RAII 资源管理：连接生命周期跟对象生命周期绑定，正常返回和异常路径都能释放连接。

## Worker Queue Pattern

代表代码：

- `POST /api/submissions`
- `JudgeRepository::fetchNextQueuedSubmission()`
- `judge_worker.exe`

API server 只负责入队，worker 轮询 queued submission 并执行耗时任务。这个模式把用户请求延迟和评测耗时解耦，也让评测逻辑可以独立部署或用 `--once` 做单任务执行。

## Strategy-like Configuration

代表代码：

- `compareOutputs(expected, actual, mode, epsilon)`
- `SandboxRunner::fromEnvironment()`

输出比较通过 `compare_mode` 在运行时选择策略；沙箱执行通过环境变量选择 Docker 或 Local。项目没有为这些策略建立复杂继承层级，而是用简单参数和封装函数实现可配置行为，这对当前规模更轻。

## Idempotent Schema Initialization

代表代码：

- `Database::initializeSchemaAndSeed()`
- `SchemaInitializer::initialize()`
- `sql/001_schema.sql`
- `sql/003_judge.sql`

启动时检查表、执行 `CREATE TABLE IF NOT EXISTS`、补充 `ALTER TABLE ... ADD COLUMN IF NOT EXISTS` 和约束检查。这让开发环境启动成本低，也让 API server 和 worker 都可以独立确保数据库就绪。

## Testing Strategy

代表代码：`tests/test_main.cpp`

测试分成两层：

- 无数据库测试：滑动窗口、DP、输出比较、admin token 等快速单元测试。
- 可选 DB 集成测试：通过 `RUN_DB_TESTS=1` 开启，验证 schema 初始化、CRUD、训练记录生命周期、Judge AC/WA/CE/TLE 回写。

这种设计让日常测试保持轻量，同时为关键持久化副作用保留集成测试入口。

