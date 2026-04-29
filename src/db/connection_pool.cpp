#include "db/connection_pool.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace atp {

ConnectionLease::ConnectionLease(ConnectionPool& pool, std::unique_ptr<pqxx::connection> connection)
    : pool_(&pool), connection_(std::move(connection)) {}

ConnectionLease::ConnectionLease(ConnectionLease&& other) noexcept
    : pool_(other.pool_), connection_(std::move(other.connection_)) {
    other.pool_ = nullptr;
}

ConnectionLease& ConnectionLease::operator=(ConnectionLease&& other) noexcept {
    if (this != &other) {
        reset();
        pool_ = other.pool_;
        connection_ = std::move(other.connection_);
        other.pool_ = nullptr;
    }
    return *this;
}

ConnectionLease::~ConnectionLease() {
    reset();
}

pqxx::connection& ConnectionLease::connection() const {
    if (connection_ == nullptr) {
        throw std::logic_error("database connection lease is empty");
    }
    return *connection_;
}

void ConnectionLease::reset() noexcept {
    if (pool_ != nullptr && connection_ != nullptr) {
        pool_->release(std::move(connection_));
    }
    pool_ = nullptr;
}

ConnectionPool::ConnectionPool(std::string conninfo, std::size_t max_size)
    : conninfo_(std::move(conninfo)), max_size_(std::max<std::size_t>(1, max_size)) {}

ConnectionLease ConnectionPool::acquire() {
    while (true) {
        std::unique_ptr<pqxx::connection> connection;
        {
            std::unique_lock lock{mutex_};
            available_cv_.wait(lock, [&] {
                return !available_.empty() || total_connections_ < max_size_;
            });

            if (!available_.empty()) {
                connection = std::move(available_.back());
                available_.pop_back();
            } else {
                ++total_connections_;
            }
        }

        if (connection != nullptr) {
            bool connection_is_open = false;
            try {
                connection_is_open = connection->is_open();
            } catch (...) {
                connection_is_open = false;
            }

            if (connection_is_open) {
                return ConnectionLease{*this, std::move(connection)};
            }

            {
                std::lock_guard lock{mutex_};
                --total_connections_;
            }
            available_cv_.notify_one();
            continue;
        }

        try {
            return ConnectionLease{*this, createConnection()};
        } catch (...) {
            {
                std::lock_guard lock{mutex_};
                --total_connections_;
            }
            available_cv_.notify_one();
            throw;
        }
    }
}

std::unique_ptr<pqxx::connection> ConnectionPool::createConnection() const {
    return std::make_unique<pqxx::connection>(conninfo_);
}

void ConnectionPool::release(std::unique_ptr<pqxx::connection> connection) noexcept {
    bool keep_connection = false;
    try {
        keep_connection = connection != nullptr && connection->is_open();
    } catch (...) {
        keep_connection = false;
    }

    {
        std::lock_guard lock{mutex_};
        if (keep_connection) {
            available_.push_back(std::move(connection));
        } else {
            --total_connections_;
        }
    }
    available_cv_.notify_one();
}

} // namespace atp
