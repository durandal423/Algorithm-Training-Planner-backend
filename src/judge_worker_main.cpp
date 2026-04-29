#include "db/connection_pool.hpp"
#include "db/database.hpp"
#include "repository/judge_repository.hpp"
#include "service/judge_service.hpp"

#include <spdlog/spdlog.h>

#include <chrono>
#include <exception>
#include <string>
#include <thread>

int main(int argc, char** argv) {
    try {
        bool once = false;
        for (int i = 1; i < argc; ++i) {
            if (std::string{argv[i]} == "--once") {
                once = true;
            }
        }

        atp::Database::ensureDatabase();
        atp::Database::initializeSchemaAndSeed();

        atp::ConnectionPool pool{atp::Database::appConninfo(), 2};
        atp::JudgeRepository repository{pool};
        atp::JudgeService service{repository};

        spdlog::info("Judge worker started{}", once ? " in --once mode" : "");
        while (true) {
            const bool processed = service.processNextSubmission();
            if (once) {
                return 0;
            }
            if (!processed) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    } catch (const std::exception& error) {
        spdlog::error("judge worker failed: {}", error.what());
        return 1;
    }
}
