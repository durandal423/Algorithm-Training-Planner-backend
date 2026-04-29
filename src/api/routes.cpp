#include "api/routes.hpp"

#include "api/admin_auth.hpp"
#include "api/judge_routes.hpp"
#include "api/json_utils.hpp"

#include <algorithm>
#include <array>
#include <cctype>
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

crow::response options_response() {
    crow::response response{204};
    response.set_header("Access-Control-Allow-Origin", "*");
    response.set_header("Access-Control-Allow-Headers", "Content-Type, X-Admin-Token");
    response.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    return response;
}

crow::response ok(const nlohmann::json& body) {
    return json_response(200, body);
}

crow::response created(const nlohmann::json& body) {
    return json_response(201, body);
}

crow::response not_found(const std::string& message) {
    return json_response(404, {{"error", message}});
}

crow::response bad_request(const std::string& message) {
    return json_response(400, {{"error", message}});
}

crow::response conflict(const std::string& message) {
    return json_response(409, {{"error", message}});
}

crow::response forbidden(const std::string& message) {
    return json_response(403, {{"error", message}});
}

crow::response server_error(const std::string& message) {
    return json_response(500, {{"error", message}});
}

bool is_blank(const std::string& value) {
    return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    });
}

void require_not_blank(const std::string& value, const char* field) {
    if (is_blank(value)) {
        throw std::invalid_argument(std::string{field} + " is required");
    }
}

void validate_problem_payload(const Problem& problem) {
    require_not_blank(problem.problem_code, "problem_code");
    require_not_blank(problem.title, "title");
    require_not_blank(problem.source_platform, "source_platform");
    if (problem.difficulty <= 0) {
        throw std::invalid_argument("difficulty must be positive");
    }
    if (problem.estimated_minutes <= 0) {
        throw std::invalid_argument("estimated_minutes must be positive");
    }
    if (problem.wrong_count < 0) {
        throw std::invalid_argument("wrong_count must be non-negative");
    }
}

void validate_tag_payload(const Tag& tag) {
    require_not_blank(tag.name, "name");
    if (tag.mastery_score < 0 || tag.mastery_score > 100) {
        throw std::invalid_argument("mastery_score must be between 0 and 100");
    }
    if (tag.wrong_count < 0) {
        throw std::invalid_argument("wrong_count must be non-negative");
    }
}

void validate_goal_payload(const TrainingGoal& goal) {
    require_not_blank(goal.name, "name");
    if (goal.target_count <= 0) {
        throw std::invalid_argument("target_count must be positive");
    }
    if (goal.time_budget_minutes <= 0) {
        throw std::invalid_argument("time_budget_minutes must be positive");
    }
    if (goal.difficulty_min && goal.difficulty_max && *goal.difficulty_min > *goal.difficulty_max) {
        throw std::invalid_argument("difficulty_min must be less than or equal to difficulty_max");
    }
    if (goal.difficulty_weight < 0 || goal.tag_coverage_weight < 0 ||
        goal.wrong_problem_weight < 0 || goal.weak_tag_weight < 0 ||
        goal.estimated_time_weight < 0) {
        throw std::invalid_argument("weights must be non-negative");
    }
}

void validate_record_payload(const TrainingRecord& record) {
    if (!record.problem_id || *record.problem_id <= 0) {
        throw std::invalid_argument("problem_id is required");
    }
    if (record.plan_id && *record.plan_id <= 0) {
        throw std::invalid_argument("plan_id must be positive");
    }
    if (record.actual_minutes && *record.actual_minutes < 0) {
        throw std::invalid_argument("actual_minutes must be non-negative");
    }
    if (record.is_first_try_ac && !record.is_finished) {
        throw std::invalid_argument("is_first_try_ac cannot be true when is_finished is false");
    }
    if (record.duration_source != "manual" && record.duration_source != "timer" && record.duration_source != "judge") {
        throw std::invalid_argument("duration_source must be one of: manual, timer, judge");
    }
}

void validate_record_review_payload(const TrainingRecord& record) {
    if (record.actual_minutes && *record.actual_minutes < 0) {
        throw std::invalid_argument("actual_minutes must be non-negative");
    }
    if (record.is_first_try_ac && !record.is_finished) {
        throw std::invalid_argument("is_first_try_ac cannot be true when is_finished is false");
    }
}

void validate_session_payload(const TrainingSession& session) {
    if (session.problem_id <= 0) {
        throw std::invalid_argument("problem_id is required");
    }
    if (session.plan_id && *session.plan_id <= 0) {
        throw std::invalid_argument("plan_id must be positive");
    }
    if (session.plan_item_id && *session.plan_item_id <= 0) {
        throw std::invalid_argument("plan_item_id must be positive");
    }
}

void validate_plan_status(const std::string& status) {
    static constexpr std::array allowed{
        "not_started",
        "in_progress",
        "completed",
        "archived"
    };
    if (std::find(allowed.begin(), allowed.end(), status) == allowed.end()) {
        throw std::invalid_argument("status must be one of: not_started, in_progress, completed, archived");
    }
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

std::optional<int> int_query(const crow::request& req, const char* key) {
    if (const auto* value = req.url_params.get(key)) {
        return std::stoi(value);
    }
    return std::nullopt;
}

std::string string_query(const crow::request& req, const char* key) {
    if (const auto* value = req.url_params.get(key)) {
        return value;
    }
    return {};
}

std::optional<bool> bool_query(const crow::request& req, const char* key) {
    if (const auto* value = req.url_params.get(key)) {
        std::string text = value;
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return text == "true" || text == "1" || text == "yes";
    }
    return std::nullopt;
}

ProblemFilter problem_filter_from_request(const crow::request& req) {
    ProblemFilter filter;
    filter.keyword = string_query(req, "keyword");
    filter.platform = string_query(req, "platform");
    filter.tag = string_query(req, "tag");
    filter.difficulty_min = int_query(req, "difficulty_min");
    filter.difficulty_max = int_query(req, "difficulty_max");
    filter.completed = bool_query(req, "completed");
    filter.wrong = bool_query(req, "wrong");
    if (const auto value = string_query(req, "last_practiced_from"); !value.empty()) {
        filter.last_practiced_from = value;
    }
    if (const auto value = string_query(req, "last_practiced_to"); !value.empty()) {
        filter.last_practiced_to = value;
    }
    if (const auto page = int_query(req, "page")) {
        filter.page = *page;
    }
    if (const auto page_size = int_query(req, "page_size")) {
        filter.page_size = *page_size;
    }
    return filter;
}

nlohmann::json problem_array(const std::vector<Problem>& problems) {
    nlohmann::json items = nlohmann::json::array();
    for (const auto& problem : problems) {
        items.push_back(toJson(problem));
    }
    return items;
}

nlohmann::json tag_array(const std::vector<Tag>& tags) {
    nlohmann::json items = nlohmann::json::array();
    for (const auto& tag : tags) {
        items.push_back(toJson(tag));
    }
    return items;
}

nlohmann::json goal_array(const std::vector<TrainingGoal>& goals) {
    nlohmann::json items = nlohmann::json::array();
    for (const auto& goal : goals) {
        items.push_back(toJson(goal));
    }
    return items;
}

nlohmann::json plan_array(const std::vector<TrainingPlanSummary>& plans) {
    nlohmann::json items = nlohmann::json::array();
    for (const auto& plan : plans) {
        items.push_back(toJson(plan));
    }
    return items;
}

nlohmann::json record_array(const std::vector<TrainingRecord>& records) {
    nlohmann::json items = nlohmann::json::array();
    for (const auto& record : records) {
        items.push_back(toJson(record));
    }
    return items;
}

} // namespace

void registerRoutes(
    ApiApp& app,
    AppRepository& repository,
    JudgeRepository& judge_repository,
    TrainingPlanService& training_plan_service
) {
    CROW_ROUTE(app, "/<path>").methods(crow::HTTPMethod::Options)([](const std::string&) {
        return options_response();
    });

    CROW_ROUTE(app, "/")([] {
        return ok({{"name", "Algorithm Training Planner API"}, {"status", "running"}});
    });

    CROW_ROUTE(app, "/health").methods(crow::HTTPMethod::Get)([] {
        return ok({{"status", "ok"}});
    });

    CROW_ROUTE(app, "/api/problems").methods(crow::HTTPMethod::Get)([&repository](const crow::request& req) {
        return guarded([&] {
            const auto page = repository.listProblems(problem_filter_from_request(req));
            return ok({
                {"items", problem_array(page.items)},
                {"count", page.items.size()},
                {"total_count", page.total_count},
                {"pagination", {
                    {"page", page.page},
                    {"page_size", page.page_size},
                    {"total_count", page.total_count},
                    {"total_pages", page.page_size <= 0 ? 0 : (page.total_count + page.page_size - 1) / page.page_size}
                }}
            });
        });
    });

    CROW_ROUTE(app, "/api/problems/<int>").methods(crow::HTTPMethod::Get)([&repository](int id) {
        return guarded([&] {
            const auto problem = repository.getProblem(id);
            if (!problem) {
                return not_found("problem not found");
            }
            return ok(toJson(*problem));
        });
    });

    CROW_ROUTE(app, "/api/problems").methods(crow::HTTPMethod::Post)([&repository](const crow::request& req) {
        return guarded([&] {
            requireAdmin(req);
            const auto body = parseJsonObject(req.body);
            const auto problem = problemFromJson(body);
            validate_problem_payload(problem);
            const auto id = repository.createProblem(problem, stringArrayFromJson(body, "tags"));
            return created({{"id", id}});
        });
    });

    CROW_ROUTE(app, "/api/problems/<int>").methods(crow::HTTPMethod::Put)([&repository](const crow::request& req, int id) {
        return guarded([&] {
            requireAdmin(req);
            const auto body = parseJsonObject(req.body);
            const auto problem = problemFromJson(body);
            validate_problem_payload(problem);
            if (!repository.updateProblem(id, problem, stringArrayFromJson(body, "tags"))) {
                return not_found("problem not found");
            }
            const auto updated = repository.getProblem(id);
            return ok(toJson(*updated));
        });
    });

    CROW_ROUTE(app, "/api/problems/<int>").methods(crow::HTTPMethod::Delete)([&repository](const crow::request& req, int id) {
        return guarded([&] {
            requireAdmin(req);
            if (!repository.deleteProblem(id)) {
                return not_found("problem not found");
            }
            return ok({{"deleted", true}});
        });
    });

    CROW_ROUTE(app, "/api/problems/import").methods(crow::HTTPMethod::Post)([&repository](const crow::request& req) {
        return guarded([&] {
            requireAdmin(req);
            const auto parsed = nlohmann::json::parse(req.body);
            const auto& list = parsed.is_array() ? parsed : parsed.at("problems");
            nlohmann::json ids = nlohmann::json::array();
            for (const auto& entry : list) {
                const auto problem = problemFromJson(entry);
                validate_problem_payload(problem);
                ids.push_back(repository.createProblem(problem, stringArrayFromJson(entry, "tags")));
            }
            return created({{"imported_count", ids.size()}, {"ids", ids}});
        });
    });

    CROW_ROUTE(app, "/api/tags").methods(crow::HTTPMethod::Get)([&repository] {
        return guarded([&] {
            const auto tags = repository.listTags();
            return ok({{"items", tag_array(tags)}, {"count", tags.size()}});
        });
    });

    CROW_ROUTE(app, "/api/tags").methods(crow::HTTPMethod::Post)([&repository](const crow::request& req) {
        return guarded([&] {
            requireAdmin(req);
            const auto tag = tagFromJson(parseJsonObject(req.body));
            validate_tag_payload(tag);
            const auto id = repository.createTag(tag);
            return created({{"id", id}});
        });
    });

    CROW_ROUTE(app, "/api/tags/<int>").methods(crow::HTTPMethod::Put)([&repository](const crow::request& req, int id) {
        return guarded([&] {
            requireAdmin(req);
            const auto payload = tagFromJson(parseJsonObject(req.body));
            validate_tag_payload(payload);
            if (!repository.updateTag(id, payload)) {
                return not_found("tag not found");
            }
            const auto updated = repository.getTag(id);
            return ok(toJson(*updated));
        });
    });

    CROW_ROUTE(app, "/api/training-goals").methods(crow::HTTPMethod::Get)([&repository] {
        return guarded([&] {
            const auto goals = repository.listTrainingGoals();
            return ok({{"items", goal_array(goals)}, {"count", goals.size()}});
        });
    });

    CROW_ROUTE(app, "/api/training-goals/<int>").methods(crow::HTTPMethod::Get)([&repository](int id) {
        return guarded([&] {
            const auto goal = repository.getTrainingGoal(id);
            if (!goal) {
                return not_found("training goal not found");
            }
            return ok(toJson(*goal));
        });
    });

    CROW_ROUTE(app, "/api/training-goals").methods(crow::HTTPMethod::Post)([&repository](const crow::request& req) {
        return guarded([&] {
            const auto goal = goalFromJson(parseJsonObject(req.body));
            validate_goal_payload(goal);
            const auto id = repository.createTrainingGoal(goal);
            return created({{"id", id}});
        });
    });

    CROW_ROUTE(app, "/api/training-goals/<int>").methods(crow::HTTPMethod::Put)([&repository](const crow::request& req, int id) {
        return guarded([&] {
            const auto goal = goalFromJson(parseJsonObject(req.body));
            validate_goal_payload(goal);
            if (!repository.updateTrainingGoal(id, goal)) {
                return not_found("training goal not found");
            }
            const auto updated = repository.getTrainingGoal(id);
            return ok(toJson(*updated));
        });
    });

    CROW_ROUTE(app, "/api/training-goals/<int>").methods(crow::HTTPMethod::Delete)([&repository](int id) {
        return guarded([&] {
            if (!repository.deleteTrainingGoal(id)) {
                return not_found("training goal not found");
            }
            return ok({{"deleted", true}});
        });
    });

    CROW_ROUTE(app, "/api/training-plans/generate").methods(crow::HTTPMethod::Post)(
        [&training_plan_service](const crow::request& req) {
            return guarded([&] {
                return created(training_plan_service.generateTrainingPlan(parseJsonObject(req.body)));
            });
        }
    );

    CROW_ROUTE(app, "/api/training-plans").methods(crow::HTTPMethod::Get)([&repository] {
        return guarded([&] {
            const auto plans = repository.listTrainingPlans();
            return ok({{"items", plan_array(plans)}, {"count", plans.size()}});
        });
    });

    CROW_ROUTE(app, "/api/training-plans/<int>").methods(crow::HTTPMethod::Get)([&repository](int id) {
        return guarded([&] {
            const auto plan = repository.getTrainingPlan(id);
            if (!plan) {
                return not_found("training plan not found");
            }
            nlohmann::json body = toJson(*plan);
            nlohmann::json items = nlohmann::json::array();
            for (const auto& item : repository.getTrainingPlanItems(id)) {
                items.push_back(toJson(item));
            }
            body["items"] = items;
            return ok(body);
        });
    });

    CROW_ROUTE(app, "/api/training-plans/<int>/status").methods(crow::HTTPMethod::Put)([&repository](const crow::request& req, int id) {
        return guarded([&] {
            const auto body = parseJsonObject(req.body);
            const auto status = body.at("status").get<std::string>();
            validate_plan_status(status);
            if (!repository.updateTrainingPlanStatus(id, status)) {
                return not_found("training plan not found");
            }
            return ok({{"id", id}, {"status", status}});
        });
    });

    CROW_ROUTE(app, "/api/training-plans/<int>").methods(crow::HTTPMethod::Delete)([&repository](int id) {
        return guarded([&] {
            if (!repository.deleteTrainingPlan(id)) {
                return not_found("training plan not found");
            }
            return ok({{"deleted", true}});
        });
    });

    CROW_ROUTE(app, "/api/training-sessions").methods(crow::HTTPMethod::Post)([&repository](const crow::request& req) {
        return guarded([&] {
            const auto session = sessionFromJson(parseJsonObject(req.body));
            validate_session_payload(session);
            const auto created_session = repository.startTrainingSession(session);
            if (!created_session) {
                return conflict("active training session already exists");
            }
            return created(toJson(*created_session));
        });
    });

    CROW_ROUTE(app, "/api/training-sessions/active").methods(crow::HTTPMethod::Get)([&repository] {
        return guarded([&] {
            const auto session = repository.getActiveTrainingSession();
            return ok(session ? toJson(*session) : nlohmann::json(nullptr));
        });
    });

    CROW_ROUTE(app, "/api/training-sessions/<int>/pause").methods(crow::HTTPMethod::Post)([&repository](int id) {
        return guarded([&] {
            const auto session = repository.pauseTrainingSession(id);
            if (!session) {
                return not_found("active training session not found");
            }
            return ok(toJson(*session));
        });
    });

    CROW_ROUTE(app, "/api/training-sessions/<int>/resume").methods(crow::HTTPMethod::Post)([&repository](int id) {
        return guarded([&] {
            const auto session = repository.resumeTrainingSession(id);
            if (!session) {
                return not_found("paused training session not found");
            }
            return ok(toJson(*session));
        });
    });

    CROW_ROUTE(app, "/api/training-sessions/<int>/finish").methods(crow::HTTPMethod::Post)([&repository](const crow::request& req, int id) {
        return guarded([&] {
            const auto review = recordFromJson(parseJsonObject(req.body));
            validate_record_review_payload(review);
            const auto session = repository.finishTrainingSession(id, review);
            if (!session) {
                return not_found("active training session not found");
            }
            return ok(toJson(*session));
        });
    });

    CROW_ROUTE(app, "/api/training-sessions/<int>/cancel").methods(crow::HTTPMethod::Post)([&repository](int id) {
        return guarded([&] {
            const auto session = repository.cancelTrainingSession(id);
            if (!session) {
                return not_found("active training session not found");
            }
            return ok(toJson(*session));
        });
    });

    CROW_ROUTE(app, "/api/training-records").methods(crow::HTTPMethod::Post)([&repository](const crow::request& req) {
        return guarded([&] {
            const auto record = recordFromJson(parseJsonObject(req.body));
            validate_record_payload(record);
            const auto id = repository.createTrainingRecord(record);
            return created({{"id", id}});
        });
    });

    CROW_ROUTE(app, "/api/training-records").methods(crow::HTTPMethod::Get)([&repository] {
        return guarded([&] {
            const auto records = repository.listTrainingRecords();
            return ok({{"items", record_array(records)}, {"count", records.size()}});
        });
    });

    CROW_ROUTE(app, "/api/training-records/<int>").methods(crow::HTTPMethod::Get)([&repository](int id) {
        return guarded([&] {
            const auto record = repository.getTrainingRecord(id);
            if (!record) {
                return not_found("training record not found");
            }
            return ok(toJson(*record));
        });
    });

    CROW_ROUTE(app, "/api/training-records/<int>").methods(crow::HTTPMethod::Put)([&repository](const crow::request& req, int id) {
        return guarded([&] {
            const auto record = recordFromJson(parseJsonObject(req.body));
            validate_record_payload(record);
            const auto updated = repository.updateTrainingRecord(id, record);
            if (!updated) {
                return not_found("training record not found");
            }
            return ok(toJson(*updated));
        });
    });

    CROW_ROUTE(app, "/api/training-records/<int>").methods(crow::HTTPMethod::Delete)([&repository](int id) {
        return guarded([&] {
            if (!repository.deleteTrainingRecord(id)) {
                return not_found("training record not found");
            }
            return ok({{"deleted", true}});
        });
    });

    CROW_ROUTE(app, "/api/training-records/by-plan/<int>").methods(crow::HTTPMethod::Get)([&repository](int plan_id) {
        return guarded([&] {
            const auto records = repository.listTrainingRecords(plan_id);
            return ok({{"items", record_array(records)}, {"count", records.size()}});
        });
    });

    registerJudgeRoutes(app, judge_repository);

    CROW_ROUTE(app, "/api/dashboard/summary").methods(crow::HTTPMethod::Get)([&repository] {
        return guarded([&] { return ok(repository.dashboardSummary()); });
    });

    CROW_ROUTE(app, "/api/dashboard/tag-stats").methods(crow::HTTPMethod::Get)([&repository] {
        return guarded([&] { return ok(repository.dashboardTagStats()); });
    });

    CROW_ROUTE(app, "/api/dashboard/recent-activity").methods(crow::HTTPMethod::Get)([&repository] {
        return guarded([&] { return ok(repository.dashboardRecentActivity()); });
    });

}

} // namespace atp
