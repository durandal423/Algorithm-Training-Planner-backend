cmake_minimum_required(VERSION 3.28)
project(Algorithm_Training_Planner CXX)

include(${CMAKE_SOURCE_DIR}/cmake/LoadDotEnv.cmake)

load_dotenv(${CMAKE_SOURCE_DIR}/.env)

configure_file(
    ${CMAKE_SOURCE_DIR}/cmake/config.hpp.in
    ${CMAKE_BINARY_DIR}/config.hpp
    @ONLY
)

find_package(Crow CONFIG REQUIRED)
find_package(libpqxx CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

file(GLOB_RECURSE BACKEND_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/src/*.cpp
)

list(REMOVE_ITEM BACKEND_SOURCES ${CMAKE_SOURCE_DIR}/src/main.cpp)
list(REMOVE_ITEM BACKEND_SOURCES ${CMAKE_SOURCE_DIR}/src/judge_worker_main.cpp)

add_library(backend_core STATIC
    ${BACKEND_SOURCES}
)

target_link_libraries(backend_core PUBLIC
    Crow::Crow
    libpqxx::pqxx
    nlohmann_json::nlohmann_json
    spdlog::spdlog
)

target_include_directories(backend_core PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}
)

target_compile_features(backend_core PUBLIC cxx_std_23)

add_executable(${PROJECT_NAME}
    src/main.cpp
)

target_link_libraries(${PROJECT_NAME} PRIVATE backend_core)

target_compile_features(${PROJECT_NAME} PRIVATE
    cxx_std_23
)

add_executable(judge_worker
    src/judge_worker_main.cpp
)

target_link_libraries(judge_worker PRIVATE backend_core)

target_compile_features(judge_worker PRIVATE
    cxx_std_23
)


include(${CMAKE_SOURCE_DIR}/cmake/Tests.cmake)
