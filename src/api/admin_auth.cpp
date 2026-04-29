#include "api/admin_auth.hpp"

#include <cstdlib>
#include <string>

namespace atp {
namespace {

std::string env_string(const char* key) {
#ifdef _WIN32
    char* value = nullptr;
    std::size_t size = 0;
    if (::_dupenv_s(&value, &size, key) != 0 || value == nullptr) {
        return {};
    }
    std::string text{value};
    std::free(value);
    return text;
#else
    const char* value = std::getenv(key);
    return value == nullptr ? std::string{} : std::string{value};
#endif
}

} // namespace

bool isAdminRequest(const crow::request& req) {
    const auto configured_token = env_string("ATP_ADMIN_TOKEN");
    const auto provided_token = req.get_header_value("X-Admin-Token");
    if (!configured_token.empty()) {
        return provided_token == configured_token;
    }
    return provided_token == "dev-admin";
}

void requireAdmin(const crow::request& req) {
    if (!isAdminRequest(req)) {
        throw ForbiddenAccess{"admin access requires X-Admin-Token"};
    }
}

} // namespace atp
