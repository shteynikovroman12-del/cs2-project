#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cs2 {
enum class Category { Weapon, Knife, Gloves };
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
};
struct Model { int definition; Category category; const char* name; };
const Skin* catalog_begin();
const Skin* catalog_end();
std::size_t catalog_size();
const std::vector<Model>& catalog_models();
const Skin* find_skin(int definition, int paint);
const Skin* first_skin(int definition);
std::vector<const Skin*> search_skins(int definition, const std::string& query);
} // namespace cs2
