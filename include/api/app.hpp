#pragma once

#include <crow.h>
#include <crow/middlewares/cors.h>

namespace atp {

using ApiApp = crow::App<crow::CORSHandler>;

} // namespace atp
