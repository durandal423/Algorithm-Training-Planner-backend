#include "api/routes.hpp"
#include "config.hpp"
#include "db/connection_pool.hpp"
#include "db/database.hpp"
#include "repository/app_repository.hpp"
#include "repository/judge_repository.hpp"
#include "service/training_plan_service.hpp"

#include <crow.h>
#include <spdlog/spdlog.h>

#include <exception>

int main() {
    try {
        spdlog::info("Preparing database '{}'", config::DATABASE_NAME);
        atp::Database::ensureDatabase();
        atp::Database::initializeSchemaAndSeed();

        atp::ConnectionPool pool{atp::Database::appConninfo(), config::CONNECTION_POOL_MAX_SIZE};
        atp::AppRepository repository{pool};
        atp::JudgeRepository judge_repository{pool};
        atp::TrainingPlanService training_plan_service{repository};

        atp::ApiApp app;
        app.get_middleware<crow::CORSHandler>()
            .global()
            .origin("*")
            .methods(
                crow::HTTPMethod::Get,
                crow::HTTPMethod::Post,
                crow::HTTPMethod::Put,
                crow::HTTPMethod::Delete,
                crow::HTTPMethod::Options
            )
            .headers("Content-Type", "X-Admin-Token")
            .max_age(600);
        atp::registerRoutes(app, repository, judge_repository, training_plan_service);

        spdlog::info("Algorithm Training Planner API listening on 0.0.0.0:{}", config::SERVER_PORT);
        app.port(config::SERVER_PORT).multithreaded().run();
        return 0;
    } catch (const std::exception& error) {
        spdlog::error("startup failed: {}", error.what());
        return 1;
    }
}
