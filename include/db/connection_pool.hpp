#pragma once

#include "config.hpp"

#include <pqxx/pqxx>

#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace atp {

class ConnectionPool;

class ConnectionLease final {
public:
    ConnectionLease() = default;
    ConnectionLease(ConnectionLease&& other) noexcept;
    ConnectionLease& operator=(ConnectionLease&& other) noexcept;
    ConnectionLease(const ConnectionLease&) = delete;
    ConnectionLease& operator=(const ConnectionLease&) = delete;
    ~ConnectionLease();

    pqxx::connection& connection() const;

private:
    friend class ConnectionPool;

    ConnectionLease(ConnectionPool& pool, std::unique_ptr<pqxx::connection> connection);
    void reset() noexcept;

    ConnectionPool* pool_{nullptr};
    std::unique_ptr<pqxx::connection> connection_;
};

class ConnectionPool final {
public:
    explicit ConnectionPool(std::string conninfo, std::size_t max_size = config::CONNECTION_POOL_MAX_SIZE);
    ConnectionPool(const ConnectionPool&) = delete;
    ConnectionPool& operator=(const ConnectionPool&) = delete;

    ConnectionLease acquire();

private:
    friend class ConnectionLease;

    std::unique_ptr<pqxx::connection> createConnection() const;
    void release(std::unique_ptr<pqxx::connection> connection) noexcept;

    std::string conninfo_;
    std::size_t max_size_;
    std::mutex mutex_;
    std::condition_variable available_cv_;
    std::vector<std::unique_ptr<pqxx::connection>> available_;
    std::size_t total_connections_{0};
};

} // namespace atp
