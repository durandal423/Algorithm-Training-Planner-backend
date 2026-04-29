# Algorithm Training Planner 源码导读

这组文档用于配合源码阅读，重点解释项目结构、核心流程、算法设计、评测系统、数据模型和工程模式。API 细节仍以 [api.md](api.md) 为准；这里更关注“代码为什么这样组织”和“请求如何穿过系统”。

## 推荐阅读顺序

1. [project-architecture.md](project-architecture.md)：先建立整体分层、两个可执行程序和请求链路的地图。
2. [training-plan-engine.md](training-plan-engine.md)：理解训练计划生成的业务编排、滑动窗口候选池和压缩 DP。
3. [judge-engine.md](judge-engine.md)：理解在线评测从提交入队到 worker 回写进度的完整流程。
4. [data-model.md](data-model.md)：对照 SQL 表关系，理解计划、记录、会话和评测数据如何联动。
5. [design-patterns.md](design-patterns.md)：提炼本项目里可以复用的工程设计方法。
6. [api.md](api.md)：最后回到接口合约，验证前端或调用方需要遵守的请求/响应结构。

## 图表索引

所有 draw.io 文件都是未压缩 XML，可直接用 diagrams.net 打开继续编辑。

| 图 | 说明 | 配套文档 |
| --- | --- | --- |
| [01-system-context.drawio](diagrams/01-system-context.drawio) | Frontend、API Server、Judge Worker、PostgreSQL、Docker Sandbox 的系统上下文 | [project-architecture.md](project-architecture.md) |
| [02-layered-architecture.drawio](diagrams/02-layered-architecture.drawio) | `api -> service -> algorithms/repository -> db/sql` 分层依赖 | [project-architecture.md](project-architecture.md) |
| [03-training-plan-generation.drawio](diagrams/03-training-plan-generation.drawio) | `POST /api/training-plans/generate` 从请求到持久化的主流程 | [training-plan-engine.md](training-plan-engine.md) |
| [04-algorithm-internals.drawio](diagrams/04-algorithm-internals.drawio) | 滑动窗口和压缩 DP 的内部决策 | [training-plan-engine.md](training-plan-engine.md) |
| [05-judge-pipeline.drawio](diagrams/05-judge-pipeline.drawio) | 提交、队列、worker、沙箱、结果写回流程 | [judge-engine.md](judge-engine.md) |
| [06-data-model.drawio](diagrams/06-data-model.drawio) | 核心业务表和评测表关系 | [data-model.md](data-model.md) |

## 源码模块速览

| 模块 | 入口文件 | 角色 |
| --- | --- | --- |
| 启动与装配 | `src/main.cpp`、`src/judge_worker_main.cpp` | 创建数据库、连接池、Repository、Service，并启动 API server 或 judge worker |
| API 层 | `src/api/routes.cpp`、`src/api/judge_routes.cpp` | Crow 路由、参数解析、权限校验、JSON 响应 |
| Service 层 | `src/service/training_plan_service.cpp`、`src/service/judge_service.cpp` | 业务用例编排，把 API 请求转成算法调用或评测执行 |
| Algorithm 层 | `src/algorithms/window_selector.cpp`、`src/algorithms/training_dp.cpp` | 滑动窗口候选池、状态压缩 DP、对比算法 |
| Repository 层 | `src/repository/app_repository.cpp`、`src/repository/judge_repository.cpp` | SQL 访问、事务边界、领域对象装配和状态回写 |
| DB 基础设施 | `src/db/database.cpp`、`src/db/connection_pool.cpp`、`src/db/schema_initializer.cpp` | 连接字符串、建库建表、连接池和 RAII 租约 |
| Judge 基础设施 | `src/judge/sandbox_runner.cpp`、`src/judge/output_comparator.cpp` | Docker/Local 编译运行、输出比较 |
| 领域模型 | `include/domain/models.hpp`、`include/domain/judge_models.hpp` | 跨层传递的业务数据结构 |

## 边看源码边看图的方法

- 先打开对应 draw.io 图，把主链路走一遍，再读 Markdown 的“源码锚点”。
- 读 API 层时重点看输入验证、权限和错误返回；读 Service 层时重点看流程编排；读 Repository 层时重点看事务、副作用和删除语义。
- 训练计划和 Judge 是项目的两个主轴。前者体现“算法如何产品化”，后者体现“异步 worker 如何把评测结果回写业务状态”。

