#include "db/database.hpp"

#include "config.hpp"
#include "db/schema_initializer.hpp"

namespace atp {
namespace {

std::string conninfo_for_database(const std::string& database_name) {
    return "host=" + std::string(config::DATABASE_HOST) +
           " port=" + std::string(config::DATABASE_PORT) +
           " dbname=" + database_name +
           " user=" + std::string(config::DATABASE_USER) +
           " password=" + std::string(config::DATABASE_PASSWORD) +
           " options='-c search_path=public'";
}

} // namespace

std::string Database::adminConninfo() {
    return conninfo_for_database("postgres");
}

std::string Database::appConninfo() {
    return conninfo_for_database(config::DATABASE_NAME);
}

pqxx::connection Database::connect() {
    return pqxx::connection{appConninfo()};
}

void Database::ensureDatabase() {
    pqxx::connection admin{adminConninfo()};
    {
        pqxx::work tx{admin};
        const bool exists = tx.exec(
            "SELECT EXISTS(SELECT 1 FROM pg_database WHERE datname = $1)",
            pqxx::params{config::DATABASE_NAME}
        ).one_field().as<bool>();
        tx.commit();

        if (exists) {
            return;
        }
    }

    pqxx::nontransaction ntx{admin};
    ntx.exec("CREATE DATABASE " + ntx.quote_name(config::DATABASE_NAME)).no_rows();
}

void Database::initializeSchemaAndSeed() {
    (void)SchemaInitializer::initialize(appConninfo());
}

} // namespace atp
