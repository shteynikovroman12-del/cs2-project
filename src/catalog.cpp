#include "catalog.hpp"
#include <algorithm>
#include <cctype>
#include <iterator>
#include <cmath>
#include <set>

namespace cs2 {
namespace {
const Skin skins[] = {
#include "generated/skins.inc"
};
const CatalogItem items[] = {
#include "generated/items.inc"
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
int wear_state(float wear) {
    if (!std::isfinite(wear)) return -1;
    if (wear < 0.07f) return 0;
    if (wear < 0.15f) return 1;
    if (wear < 0.38f) return 2;
    if (wear < 0.45f) return 3;
    return 4;
}
const char* wear_state_name(int state) {
    constexpr const char* names[] = {"Factory New", "Minimal Wear", "Field-Tested", "Well-Worn", "Battle-Scarred"};
    return state >= 0 && state < 5 ? names[state] : "Not applicable";
}
WearPreview skin_wear_preview(const Skin& skin, float wear) {
    const int desired = wear_state(std::clamp(wear, skin.min_wear, skin.max_wear));
    if (desired < 0) return {skin.image_file, -1, true};
    if (skin.wear_images[desired]) return {skin.wear_images[desired], desired, false};
    for (int distance = 1; distance < 5; ++distance) {
        for (int state : {desired - distance, desired + distance}) {
            if (state >= 0 && state < 5 && skin.wear_images[state]) return {skin.wear_images[state], state, true};
        }
    }
    return {skin.image_file, -1, false};
}
const CatalogItem* item_catalog_begin() { return std::begin(items); }
const CatalogItem* item_catalog_end() { return std::end(items); }
const CatalogItem* find_item(Category category, int id) {
    const auto found = std::find_if(std::begin(items), std::end(items), [&](const CatalogItem& item) {
        return item.category == category && item.id == id;
    });
    return found == std::end(items) ? nullptr : found;
}
std::vector<const CatalogItem*> search_items(Category category, const std::string& query, const std::string& group) {
    std::vector<const CatalogItem*> result;
    const auto term = lower(query);
    for (const auto& item : items) {
        if (item.category == category && (group.empty() || group == item.group) &&
            (lower(item.name).find(term) != std::string::npos || std::to_string(item.id).find(term) != std::string::npos))
            result.push_back(&item);
    }
    return result;
}
std::vector<std::string> item_groups(Category category) {
    std::set<std::string> groups;
    for (const auto& item : items) if (item.category == category && item.group[0]) groups.insert(item.group);
    return {groups.begin(), groups.end()};
}
std::vector<std::string> catalog_image_files() {
    std::set<std::string> files;
    for (const auto& skin : skins) {
        files.insert(skin.image_file);
        for (const char* file : skin.wear_images) if (file) files.insert(file);
    }
    for (const auto& item : items) {
        files.insert(item.image_file);
        if (item.stattrak_image) files.insert(item.stattrak_image);
    }
    return {files.begin(), files.end()};
}
} // namespace cs2
