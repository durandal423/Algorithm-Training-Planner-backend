#include "api/judge_routes.hpp"

#include "api/admin_auth.hpp"
#include "api/judge_json.hpp"
#include "api/json_utils.hpp"
#include "judge/sandbox_runner.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace atp {
namespace {

crow::response json_response(int status, const nlohmann::json& body) {
    crow::response response{status};
    response.set_header("Content-Type", "application/json; charset=utf-8");
    response.set_header("Access-Control-Allow-Origin", "*");
    response.set_header("Access-Control-Allow-Headers", "Content-Type, X-Admin-Token");
    response.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    response.write(body.dump());
    return response;
}

crow::response ok(const nlohmann::json& body) {
    return json_response(200, body);
}

crow::response created(const nlohmann::json& body) {
    return json_response(201, body);
}

crow::response accepted(const nlohmann::json& body) {
    return json_response(202, body);
}

crow::response bad_request(const std::string& message) {
    return json_response(400, {{"error", message}});
}

crow::response forbidden(const std::string& message) {
    return json_response(403, {{"error", message}});
}

crow::response not_found(const std::string& message) {
    return json_response(404, {{"error", message}});
}

crow::response server_error(const std::string& message) {
    return json_response(500, {{"error", message}});
}

template <typename Fn>
crow::response guarded(Fn&& fn) {
    try {
        return fn();
    } catch (const nlohmann::json::exception& error) {
        return bad_request(error.what());
    } catch (const std::invalid_argument& error) {
        return bad_request(error.what());
    } catch (const ForbiddenAccess& error) {
        return forbidden(error.what());
    } catch (const std::exception& error) {
        return server_error(error.what());
    }
}

bool bool_query(const crow::request& req, const char* key) {
    const auto* value = req.url_params.get(key);
    if (value == nullptr) {
        return false;
    }
    std::string text = value;
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text == "true" || text == "1" || text == "yes";
}

int int_query_or(const crow::request& req, const char* key, int fallback) {
    const auto* value = req.url_params.get(key);
    if (value == nullptr) {
        return fallback;
    }
    return std::stoi(value);
}

void validate_config(const JudgeConfig& config) {
    static constexpr std::array compare_modes{
        "exact",
        "trim_trailing",
        "ignore_whitespace",
        "float_epsilon"
    };
    if (config.language != "cpp") {
        throw std::invalid_argument("language must be cpp");
    }
    if (config.time_limit_ms <= 0 || config.memory_limit_mb <= 0 || config.output_limit_kb <= 0) {
        throw std::invalid_argument("time_limit_ms, memory_limit_mb, and output_limit_kb must be positive");
    }
    if (std::find(compare_modes.begin(), compare_modes.end(), config.compare_mode) == compare_modes.end()) {
        throw std::invalid_argument("compare_mode must be exact, trim_trailing, ignore_whitespace, or float_epsilon");
    }
    if (config.float_epsilon <= 0.0) {
        throw std::invalid_argument("float_epsilon must be positive");
    }
    if (config.official_solution_language && *config.official_solution_language != "cpp") {
        throw std::invalid_argument("official_solution_language must be cpp");
    }
}

void validate_test_case(const JudgeTestCase& test_case) {
    static constexpr std::array visibilities{"sample", "hidden", "admin_only"};
    if (test_case.input_data.empty()) {
        throw std::invalid_argument("input_data is required");
    }
    if (std::find(visibilities.begin(), visibilities.end(), test_case.visibility) == visibilities.end()) {
        throw std::invalid_argument("visibility must be sample, hidden, or admin_only");
    }
    if (test_case.points < 0) {
        throw std::invalid_argument("points must be non-negative");
    }
    if (test_case.time_limit_ms && *test_case.time_limit_ms <= 0) {
        throw std::invalid_argument("time_limit_ms must be positive");
    }
    if (test_case.memory_limit_mb && *test_case.memory_limit_mb <= 0) {
        throw std::invalid_argument("memory_limit_mb must be positive");
    }
}

void validate_submission(const Submission& submission) {
    if (submission.problem_id <= 0) {
        throw std::invalid_argument("problem_id is required");
    }
    if (submission.language != "cpp") {
        throw std::invalid_argument("language must be cpp");
    }
    if (submission.source_code.empty()) {
        throw std::invalid_argument("source_code is required");
    }
    if (submission.source_code.size() > 256 * 1024) {
        throw std::invalid_argument("source_code exceeds 256KB limit");
    }
}

nlohmann::json test_case_array(const std::vector<JudgeTestCase>& test_cases, bool include_payload) {
    nlohmann::json items = nlohmann::json::array();
    for (const auto& test_case : test_cases) {
        items.push_back(toJson(test_case, include_payload));
    }
    return items;
}

nlohmann::json submission_array(const std::vector<Submission>& submissions) {
    nlohmann::json items = nlohmann::json::array();
    for (const auto& submission : submissions) {
        items.push_back(toJson(submission, false));
    }
    return items;
}

std::filesystem::path make_generation_work_dir(long long problem_id) {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = std::filesystem::temp_directory_path() /
        ("atp-judge-generate-" + std::to_string(problem_id) + "-" + std::to_string(now));
    std::filesystem::create_directories(path);
    return path;
}

} // namespace

void registerJudgeRoutes(ApiApp& app, JudgeRepository& repository) {
    CROW_ROUTE(app, "/api/problems/<int>/judge-config").methods(crow::HTTPMethod::Get)(
        [&repository](const crow::request& req, int problem_id) {
            return guarded([&] {
                requireAdmin(req);
                const auto config = repository.getConfig(problem_id);
                if (!config) {
                    return not_found("judge config not found");
                }
                return ok(toJson(*config, bool_query(req, "include_code")));
            });
        }
    );

    CROW_ROUTE(app, "/api/problems/<int>/judge-config").methods(crow::HTTPMethod::Put)(
        [&repository](const crow::request& req, int problem_id) {
            return guarded([&] {
                requireAdmin(req);
                const auto config = judgeConfigFromJson(problem_id, parseJsonObject(req.body));
                validate_config(config);
                return ok(toJson(repository.upsertConfig(config), false));
            });
        }
    );

    CROW_ROUTE(app, "/api/problems/<int>/test-cases").methods(crow::HTTPMethod::Get)(
        [&repository](const crow::request& req, int problem_id) {
            return guarded([&] {
                const bool admin = isAdminRequest(req);
                const auto test_cases = repository.listTestCases(problem_id, admin);
                return ok({{"items", test_case_array(test_cases, true)}, {"count", test_cases.size()}});
            });
        }
    );

    CROW_ROUTE(app, "/api/problems/<int>/test-cases").methods(crow::HTTPMethod::Post)(
        [&repository](const crow::request& req, int problem_id) {
            return guarded([&] {
                requireAdmin(req);
                auto test_case = judgeTestCaseFromJson(problem_id, parseJsonObject(req.body));
                validate_test_case(test_case);
                const auto id = repository.createTestCase(test_case);
                return created({{"id", id}});
            });
        }
    );

    CROW_ROUTE(app, "/api/problems/<int>/test-cases/import").methods(crow::HTTPMethod::Post)(
        [&repository](const crow::request& req, int problem_id) {
            return guarded([&] {
                requireAdmin(req);
                const auto parsed = nlohmann::json::parse(req.body);
                const auto& list = parsed.is_array() ? parsed : parsed.at("test_cases");
                if (!list.is_array()) {
                    throw std::invalid_argument("test_cases must be an array");
                }
                nlohmann::json ids = nlohmann::json::array();
                for (const auto& entry : list) {
                    auto test_case = judgeTestCaseFromJson(problem_id, entry);
                    validate_test_case(test_case);
                    ids.push_back(repository.createTestCase(test_case));
                }
                return created({{"imported_count", ids.size()}, {"ids", ids}});
            });
        }
    );

    CROW_ROUTE(app, "/api/test-cases/<int>").methods(crow::HTTPMethod::Put)(
        [&repository](const crow::request& req, int id) {
            return guarded([&] {
                requireAdmin(req);
                auto existing = repository.getTestCase(id);
                if (!existing) {
                    return not_found("test case not found");
                }
                applyJudgeTestCaseOverrides(*existing, parseJsonObject(req.body));
                validate_test_case(*existing);
                if (!repository.updateTestCase(id, *existing)) {
                    return not_found("test case not found");
                }
                return ok(toJson(*repository.getTestCase(id), true));
            });
        }
    );

    CROW_ROUTE(app, "/api/test-cases/<int>").methods(crow::HTTPMethod::Delete)(
        [&repository](const crow::request& req, int id) {
            return guarded([&] {
                requireAdmin(req);
                if (!repository.deleteTestCase(id)) {
                    return not_found("test case not found");
                }
                return ok({{"deleted", true}});
            });
        }
    );

    CROW_ROUTE(app, "/api/problems/<int>/test-cases/generate-expected").methods(crow::HTTPMethod::Post)(
        [&repository](const crow::request& req, int problem_id) {
            return guarded([&] {
                requireAdmin(req);
                const auto config = repository.getConfig(problem_id);
                if (!config) {
                    return not_found("judge config not found");
                }
                if (!config->official_solution_code || config->official_solution_code->empty()) {
                    throw std::invalid_argument("official_solution_code is required before generating expected output");
                }
                if (config->official_solution_language && *config->official_solution_language != "cpp") {
                    throw std::invalid_argument("official_solution_language must be cpp");
                }

                std::vector<long long> selected_ids;
                const auto body = parseJsonObject(req.body);
                if (body.contains("test_case_ids") && !body.at("test_case_ids").is_null()) {
                    for (const auto& value : body.at("test_case_ids")) {
                        selected_ids.push_back(value.get<long long>());
                    }
                }

                auto test_cases = repository.listJudgingTestCases(problem_id);
                if (!selected_ids.empty()) {
                    test_cases.erase(
                        std::remove_if(test_cases.begin(), test_cases.end(), [&](const JudgeTestCase& test_case) {
                            return std::find(selected_ids.begin(), selected_ids.end(), test_case.id) == selected_ids.end();
                        }),
                        test_cases.end()
                    );
                }
                if (test_cases.empty()) {
                    throw std::invalid_argument("no test cases selected");
                }

                auto generation_config = *config;
                generation_config.enabled = true;
                generation_config.language = config->official_solution_language.value_or("cpp");
                auto runner = SandboxRunner::fromEnvironment();
                auto work_dir = make_generation_work_dir(problem_id);
                nlohmann::json failures = nlohmann::json::array();
                int generated_count = 0;
                try {
                    const auto compile = runner.compile(*config->official_solution_code, generation_config, work_dir);
                    if (!compile.success) {
                        std::filesystem::remove_all(work_dir);
                        return bad_request("official solution compilation failed: " + compile.stderr_sample);
                    }
                    for (const auto& test_case : test_cases) {
                        const auto run = runner.run(generation_config, test_case, work_dir, generated_count + 1);
                        if (run.timed_out || run.output_limit_exceeded || run.exit_code != 0 || run.system_error) {
                            failures.push_back({
                                {"test_case_id", test_case.id},
                                {"exit_code", run.exit_code},
                                {"message", run.message},
                                {"stderr_sample", run.stderr_sample}
                            });
                            continue;
                        }
                        repository.updateExpectedOutput(test_case.id, run.stdout_sample);
                        ++generated_count;
                    }
                    std::filesystem::remove_all(work_dir);
                } catch (...) {
                    std::filesystem::remove_all(work_dir);
                    throw;
                }
                return accepted({
                    {"generated_count", generated_count},
                    {"failed_count", failures.size()},
                    {"failures", failures}
                });
            });
        }
    );

    CROW_ROUTE(app, "/api/submissions").methods(crow::HTTPMethod::Post)(
        [&repository](const crow::request& req) {
            return guarded([&] {
                const auto submission = submissionFromJson(parseJsonObject(req.body));
                validate_submission(submission);
                const auto id = repository.createSubmission(submission);
                return created({{"id", id}, {"status", "queued"}});
            });
        }
    );

    CROW_ROUTE(app, "/api/submissions/<int>").methods(crow::HTTPMethod::Get)(
        [&repository](const crow::request& req, int id) {
            return guarded([&] {
                const auto submission = repository.getSubmission(id);
                if (!submission) {
                    return not_found("submission not found");
                }
                const bool include_source = bool_query(req, "include_source");
                return ok(toJson(*submission, include_source));
            });
        }
    );

    CROW_ROUTE(app, "/api/problems/<int>/submissions").methods(crow::HTTPMethod::Get)(
        [&repository](const crow::request& req, int problem_id) {
            return guarded([&] {
                const auto limit = int_query_or(req, "limit", 50);
                const auto submissions = repository.listSubmissionsForProblem(problem_id, limit);
                return ok({{"items", submission_array(submissions)}, {"count", submissions.size()}});
            });
        }
    );

    CROW_ROUTE(app, "/api/submissions/<int>/rejudge").methods(crow::HTTPMethod::Post)(
        [&repository](const crow::request& req, int id) {
            return guarded([&] {
                requireAdmin(req);
                if (!repository.requeueSubmission(id)) {
                    return not_found("submission not found");
                }
                return accepted({{"id", id}, {"status", "queued"}});
            });
        }
    );
}

} // namespace atp
