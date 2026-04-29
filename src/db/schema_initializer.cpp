#include "db/schema_initializer.hpp"

#include "config.hpp"

#include <pqxx/pqxx>
#include <spdlog/spdlog.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace atp {
namespace {

constexpr std::array kRequiredTables{
    "users",
    "problems",
    "tags",
    "problem_tags",
    "training_goals",
    "training_goal_tags",
    "training_plans",
    "training_plan_items",
    "training_records",
    "training_sessions",
    "judge_configs",
    "judge_test_cases",
    "submissions",
    "submission_results"
};

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary};
    if (!input) {
        throw std::runtime_error("Cannot open SQL file: " + path.string());
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

std::filesystem::path sql_path(const std::string& filename) {
    return std::filesystem::path{config::BACKEND_SOURCE_DIR} / "sql" / filename;
}

std::vector<std::string> missing_required_tables(pqxx::transaction_base& tx) {
    std::vector<std::string> missing;
    for (const auto* table : kRequiredTables) {
        const auto qualified = "public." + std::string{table};
        const bool exists = tx.exec(
            "SELECT to_regclass($1) IS NOT NULL",
            pqxx::params{qualified}
        ).one_field().as<bool>();
        if (!exists) {
            missing.emplace_back(table);
        }
    }
    return missing;
}

void ensure_public_schema(pqxx::transaction_base& tx) {
    tx.exec("CREATE SCHEMA IF NOT EXISTS public").no_rows();
    tx.exec("GRANT USAGE, CREATE ON SCHEMA public TO CURRENT_USER").no_rows();
    tx.exec("SET search_path TO public").no_rows();
}

long long table_count(pqxx::transaction_base& tx, const std::string& table_name) {
    return tx.exec(
        "SELECT COUNT(*) FROM " + tx.quote_name("public") + "." + tx.quote_name(table_name)
    ).one_field().as<long long>();
}

bool seed_data_required(pqxx::transaction_base& tx) {
    return table_count(tx, "users") == 0 ||
           table_count(tx, "tags") == 0 ||
           table_count(tx, "problems") == 0;
}

std::string join_names(const std::vector<std::string>& names) {
    std::string result;
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (i > 0) {
            result += ", ";
        }
        result += names[i];
    }
    return result;
}

} // namespace

SchemaInitializationReport SchemaInitializer::initialize(const std::string& conninfo) {
    SchemaInitializationReport report;
    pqxx::connection db{conninfo};

    {
        pqxx::work tx{db};
        report.missing_tables_before = missing_required_tables(tx);
        tx.commit();
    }

    if (!report.missing_tables_before.empty()) {
        spdlog::warn(
            "database schema is incomplete; missing tables: {}",
            join_names(report.missing_tables_before)
        );
    }

    {
        pqxx::work tx{db};
        ensure_public_schema(tx);
        tx.exec(read_file(sql_path("001_schema.sql"))).no_rows();
        tx.exec(read_file(sql_path("003_judge.sql"))).no_rows();
        tx.commit();
        report.schema_sql_executed = true;
    }

    {
        pqxx::work tx{db};
        report.missing_tables_after = missing_required_tables(tx);
        tx.commit();
    }

    if (!report.missing_tables_after.empty()) {
        throw std::runtime_error(
            "Database schema initialization failed; missing tables after initialization: " +
            join_names(report.missing_tables_after)
        );
    }

    bool should_seed = false;
    {
        pqxx::work tx{db};
        tx.exec("SET search_path TO public").no_rows();
        should_seed = seed_data_required(tx);
        tx.commit();
    }

    if (should_seed) {
        pqxx::work tx{db};
        tx.exec("SET search_path TO public").no_rows();
        tx.exec(read_file(sql_path("002_seed.sql"))).no_rows();
        tx.commit();
        report.seed_sql_executed = true;
        spdlog::info("database seed data loaded because core data was missing");
    }

    spdlog::info(
        "database schema ready (schema_sql_executed={}, seed_sql_executed={})",
        report.schema_sql_executed,
        report.seed_sql_executed
    );
    return report;
}

} // namespace atp
