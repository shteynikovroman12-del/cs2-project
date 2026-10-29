#pragma once
#include "bridge_protocol.hpp"
#include "profile.hpp"
#include <future>
#include <memory>
#include <string>

namespace cs2::bridge {
struct Connection;
struct ConnectResult { std::shared_ptr<Connection> connection; std::string error; };
class Client {
public:
    Client() = default;
    ~Client();
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;
    void connect();
    void poll();
    bool apply(const Profile& profile);
    bool restore();
    bool connected() const { return connection_ != nullptr; }
    bool connecting() const { return pending_.valid(); }
    bool usable() const { return connected() && snapshot_.state != State::Failed && snapshot_.state != State::Starting; }
    const Shared& snapshot() const { return snapshot_; }
    const std::string& message() const { return message_; }
private:
    bool send(const Request& request);
    std::shared_ptr<Connection> connection_;
    std::future<ConnectResult> pending_;
    Shared snapshot_{};
    std::string message_ = "Not connected. Experimental weapon paint integration.";
};
} // namespace cs2::bridge
