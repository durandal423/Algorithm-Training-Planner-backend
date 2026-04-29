#pragma once

#include "api/app.hpp"

#include <stdexcept>

namespace atp {

struct ForbiddenAccess final : std::runtime_error {
    using std::runtime_error::runtime_error;
};

bool isAdminRequest(const crow::request& req);
void requireAdmin(const crow::request& req);

} // namespace atp
