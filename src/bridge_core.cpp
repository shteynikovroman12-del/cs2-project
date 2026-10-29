#include "bridge_core.hpp"
#include "catalog.hpp"
#include <cmath>
#include <stdexcept>

namespace cs2::bridge {
bool validate_request(const Request& r, std::string& error) {
    error.clear();
    if (r.command == Command::Restore) return true;
    if (r.command != Command::ApplyPaint) { error = "Unknown bridge command."; return false; }
    const auto* skin = find_skin(r.definition, r.paint);
    if (!skin || skin->category != Category::Weapon) {
        error = "This experimental backend supports catalog weapon finishes only; model replacement is unavailable.";
        return false;
    }
    if (!std::isfinite(r.wear) || r.wear < skin->min_wear || r.wear > skin->max_wear || r.seed < 0 || r.seed > 1000) {
        error = "Wear or seed is outside the selected finish's range."; return false;
    }
    if (r.teams == 0 || (r.teams & ~3u)) { error = "Select T, CT, or both teams."; return false; }
    return true;
}
bool make_request(const Profile& p, Request& out, std::string& error) {
    if (p.item.stattrak || !p.item.custom_name.empty()) {
        error = "Paint integration does not yet apply StatTrak or custom names. Disable StatTrak and clear Custom name for this test.";
        return false;
    }
    Request r{Command::ApplyPaint, p.item.definition_id, p.item.paint_kit, p.item.seed,
              p.item.wear, (p.item.equip_t ? 1u : 0u) | (p.item.equip_ct ? 2u : 0u)};
    if (!validate_request(r, error)) return false;
    out = r;
    return true;
}
bool team_enabled(std::uint32_t mask, unsigned team) {
    return (team == 2 && (mask & 1)) || (team == 3 && (mask & 2));
}
std::vector<Attribute> paint_attributes(const Attribute* old, std::size_t count,
                                       const Request& r, std::uint64_t vtable) {
    if (count > 128 || (count && !old)) throw std::invalid_argument("Invalid existing attribute vector");
    std::string error;
    if (!validate_request(r, error) || r.command != Command::ApplyPaint) throw std::invalid_argument("Invalid paint request");
    std::vector<Attribute> result;
    result.reserve(count + 3);
    for (std::size_t i = 0; i < count; ++i) {
        if (old[i].definition < 6 || old[i].definition > 8) result.push_back(old[i]);
    }
    const float values[] = {static_cast<float>(r.paint), static_cast<float>(r.seed), r.wear};
    for (unsigned i = 0; i < 3; ++i) {
        Attribute a{};
        a.vtable = vtable;
        a.definition = static_cast<std::uint16_t>(6 + i);
        a.value = a.initial_value = values[i];
        result.push_back(a);
    }
    return result;
}
} // namespace cs2::bridge
