#pragma once

#include "domain/models.hpp"

#include <nlohmann/json.hpp>

namespace atp {

nlohmann::json toJson(const Tag& tag);
nlohmann::json toJson(const Problem& problem);
nlohmann::json toJson(const TrainingGoal& goal);
nlohmann::json toJson(const CandidateWindow& window);
nlohmann::json toJson(const SelectedProblem& item);
nlohmann::json toJson(const TrainingPlanResult& result);
nlohmann::json toJson(const TrainingPlanSummary& plan);
nlohmann::json toJson(const TrainingRecord& record);
nlohmann::json toJson(const TrainingSession& session);

Tag tagFromJson(const nlohmann::json& body);
Problem problemFromJson(const nlohmann::json& body);
TrainingGoal goalFromJson(const nlohmann::json& body);
TrainingRecord recordFromJson(const nlohmann::json& body);
TrainingSession sessionFromJson(const nlohmann::json& body);
std::vector<std::string> stringArrayFromJson(const nlohmann::json& body, const char* key);

void applyGoalOverrides(TrainingGoal& goal, const nlohmann::json& body);
nlohmann::json parseJsonObject(const std::string& body);

} // namespace atp
