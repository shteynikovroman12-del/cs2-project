#include "catalog.hpp"
#include <algorithm>
#include <cctype>
#include <iterator>

namespace cs2 {
namespace {
const Skin skins[] = {
#include "generated/skins.inc"
};
std::string lower(std::string value) {
    for (char& ch : value) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return value;
}
} // namespace
const Skin* catalog_begin() { return std::begin(skins); }
const Skin* catalog_end() { return std::end(skins); }
std::size_t catalog_size() { return std::size(skins); }
const std::vector<Model>& catalog_models() {
    static const auto models = [] {
        std::vector<Model> result;
        for (const auto& skin : skins) {
            if (std::none_of(result.begin(), result.end(), [&](const Model& m) { return m.definition == skin.definition; }))
                result.push_back({skin.definition, skin.category, skin.model});
        }
        std::sort(result.begin(), result.end(), [](const Model& a, const Model& b) {
            return std::string(a.name) < b.name;
        });
        return result;
    }();
    return models;
}
const Skin* find_skin(int definition, int paint) {
    const auto found = std::find_if(std::begin(skins), std::end(skins), [&](const Skin& skin) {
        return skin.definition == definition && skin.paint == paint;
    });
    return found == std::end(skins) ? nullptr : found;
}
const Skin* first_skin(int definition) {
    const auto found = std::find_if(std::begin(skins), std::end(skins), [&](const Skin& skin) {
        return skin.definition == definition;
    });
    return found == std::end(skins) ? nullptr : found;
}
std::vector<const Skin*> search_skins(int definition, const std::string& query) {
    std::vector<const Skin*> result;
    const auto term = lower(query);
    for (const auto& skin : skins) {
        if (skin.definition == definition && (lower(skin.finish).find(term) != std::string::npos ||
            std::to_string(skin.paint).find(term) != std::string::npos)) result.push_back(&skin);
    }
    return result;
}
} // namespace cs2
