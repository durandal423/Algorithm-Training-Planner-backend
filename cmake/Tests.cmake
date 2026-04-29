# 启用 CTest
include(CTest)

# 找 Catch2
find_package(Catch2 CONFIG REQUIRED)

# 包含 Catch2 的 CMake 工具（关键）
include(Catch)

# 收集所有测试源文件（可扩展）
file(GLOB TEST_SOURCES
    ${CMAKE_SOURCE_DIR}/tests/*.cpp
)

# 定义测试可执行文件
add_executable(tests ${TEST_SOURCES})

# 链接 Catch2（带 main）
target_link_libraries(tests PRIVATE
    Catch2::Catch2WithMain
    backend_core
)

target_compile_features(tests PRIVATE cxx_std_23)

# 自动发现 TEST_CASE（核心）
catch_discover_tests(tests)
