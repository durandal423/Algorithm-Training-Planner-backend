#pragma once

#include <string>
#include <vector>

namespace atp {

struct SchemaInitializationReport {
    std::vector<std::string> missing_tables_before;
    std::vector<std::string> missing_tables_after;
    bool schema_sql_executed{};
    bool seed_sql_executed{};
};

class SchemaInitializer {
public:
    static SchemaInitializationReport initialize(const std::string& conninfo);
};

} // namespace atp
