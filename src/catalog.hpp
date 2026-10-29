#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cs2 {
enum class Category { Weapon, Knife, Gloves, Agent, Music, Sticker, Charm };
struct Skin {
    int definition;
    int paint;
    Category category;
    const char* model;
    const char* finish;
    const char* image_file;
    float min_wear;
    float max_wear;
    std::uint32_t rarity_rgb;
    const char* wear_images[5];
    bool legacy_model;
};
struct WearPreview { const char* file; int state; bool fallback; };
struct CatalogItem {
    Category category;
    int id;
    const char* name;
    const char* group;
    const char* image_file;
    const char* stattrak_image;
    std::uint32_t rarity_rgb;
};
struct Model { int definition; Category category; const char* name; };
const Skin* catalog_begin();
const Skin* catalog_end();
std::size_t catalog_size();
const std::vector<Model>& catalog_models();
const Skin* find_skin(int definition, int paint);
const Skin* first_skin(int definition);
std::vector<const Skin*> search_skins(int definition, const std::string& query);
int wear_state(float wear);
const char* wear_state_name(int state);
WearPreview skin_wear_preview(const Skin& skin, float wear);
const CatalogItem* item_catalog_begin();
const CatalogItem* item_catalog_end();
const CatalogItem* find_item(Category category, int id);
std::vector<const CatalogItem*> search_items(Category category, const std::string& query, const std::string& group = {});
std::vector<std::string> item_groups(Category category);
std::vector<std::string> catalog_image_files();
} // namespace cs2
