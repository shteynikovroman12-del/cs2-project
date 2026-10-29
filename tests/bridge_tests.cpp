#include "bridge_core.hpp"
#include "catalog.hpp"
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
}
int main() {
    using namespace cs2;
    using namespace cs2::bridge;
    try {
        Shared state{};
        check(!valid_header(state), "Uninitialized protocol accepted");
        state.signature = magic; state.protocol = version; state.bytes = sizeof(Shared);
        check(valid_header(state), "Valid protocol rejected");
        --state.protocol;
        check(!valid_header(state), "Old protocol accepted");
        Profile p;
        p.item.definition_id = 7; p.item.paint_kit = 282; p.item.wear = 0.2f; p.item.seed = 789;
        Request request{};
        std::string error;
        check(make_request(p, request, error), "Known AK finish rejected");
        check(request.definition == 7 && request.paint == 282 && request.seed == 789 && request.wear == 0.2f && request.teams == 3, "Profile mapping lost fields");
        p.item.stattrak = true;
        check(!make_request(p, request, error), "Unsupported StatTrak silently accepted");
        p.item.stattrak = false; p.item.custom_name = "test";
        check(!make_request(p, request, error), "Unsupported name silently accepted");
        p.item.custom_name.clear();
        auto invalid = request;
        invalid.wear = std::numeric_limits<float>::quiet_NaN();
        check(!validate_request(invalid, error), "NaN wear accepted");
        invalid = request; invalid.wear = 1.1f;
        check(!validate_request(invalid, error), "Out-of-range wear accepted");
        invalid = request; invalid.seed = 1001;
        check(!validate_request(invalid, error), "Out-of-range seed accepted");
        for (auto mask : {0u, 4u, 7u}) {
            invalid = request; invalid.teams = mask;
            check(!validate_request(invalid, error), "Invalid team mask accepted");
        }
        invalid = request; invalid.definition = 5033; invalid.paint = 10028;
        check(!validate_request(invalid, error), "Unsupported glove accepted");
        invalid = request; invalid.paint = 999999;
        check(!validate_request(invalid, error), "Unknown finish accepted");
        invalid.command = static_cast<Command>(999);
        check(!validate_request(invalid, error), "Unknown command accepted");
        check(validate_request(Request{Command::Restore}, error), "Restore rejected");
        check(team_enabled(1, 2) && !team_enabled(1, 3) && team_enabled(2, 3) && !team_enabled(2, 2), "Team selection reversed");
        check(!team_enabled(3, 0) && !team_enabled(3, 1), "Spectator accepted");
        Attribute old[5]{};
        old[0].definition = 80; old[0].value = 123; old[0].owner = 0x9876;
        old[1].definition = 6; old[1].value = 2;
        old[2].definition = 6; old[2].value = 3;
        old[3].definition = 7; old[3].value = 4;
        old[4].definition = 8; old[4].value = 5;
        Attribute original[5]; std::memcpy(original, old, sizeof(old));
        const auto result = paint_attributes(old, 5, request, 0x12345678);
        check(result.size() == 4, "Paint attribute replacement did not deduplicate");
        check(std::memcmp(&result[0], &old[0], sizeof(Attribute)) == 0, "Non-paint attribute changed");
        check(std::memcmp(original, old, sizeof(old)) == 0, "Original game attributes modified");
        for (unsigned i = 1; i <= 3; ++i) {
            check(result[i].definition == 5 + i && result[i].vtable == 0x12345678 && result[i].owner == 0, "Malformed attribute object");
            check(result[i].value == result[i].initial_value, "Initial attribute value missing");
        }
        check(result[1].value == 282 && result[2].value == 789 && result[3].value == 0.2f, "Native cosmetic attribute values incorrect");
        check(paint_attributes(nullptr, 0, request, 1).size() == 3, "Empty item rejected");
        bool rejected = false;
        try { paint_attributes(old, 129, request, 1); } catch (const std::invalid_argument&) { rejected = true; }
        check(rejected, "Oversized vector accepted");
        rejected = false;
        try { paint_attributes(nullptr, 1, request, 1); } catch (const std::invalid_argument&) { rejected = true; }
        check(rejected, "Null attribute vector accepted");
        std::cout << "Bridge protocol, request validation and native attribute conversion passed.\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
