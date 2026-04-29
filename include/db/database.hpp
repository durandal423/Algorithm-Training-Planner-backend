#pragma once

#include <pqxx/pqxx>

#include <string>

namespace atp {

class Database {
public:
    static std::string adminConninfo();
    static std::string appConninfo();
    static pqxx::connection connect();

    static void ensureDatabase();
    static void initializeSchemaAndSeed();
};

} // namespace atp
