#pragma once
#include "bridge_protocol.hpp"
#include "profile.hpp"
#include <array>
#include <vector>
#include <string>

namespace cs2::bridge {
// Storage layout independently checked against this client's schema and its
// attribute iterator. These objects are borrowed only for the synchronous call.
struct Attribute {
    std::uint64_t vtable{}, owner{};
    std::array<std::uint8_t, 32> reserved{};
    std::uint16_t definition{}, pad{};
    float value{}, initial_value{};
    std::int32_t refundable{};
    std::uint8_t set_bonus{};
    std::array<std::uint8_t, 7> tail{};
};
static_assert(sizeof(Attribute) == 0x48);
static_assert(offsetof(Attribute, definition) == 0x30 && offsetof(Attribute, value) == 0x34);
struct AttributeVector { std::uint64_t size{}, data{}; };
static_assert(sizeof(AttributeVector) == 16);
bool make_request(const Profile& profile, Request& out, std::string& error);
bool validate_request(const Request& request, std::string& error);
std::vector<Attribute> paint_attributes(const Attribute* old, std::size_t count,
                                        const Request& request, std::uint64_t vtable);
bool team_enabled(std::uint32_t mask, unsigned team);
} // namespace cs2::bridge
