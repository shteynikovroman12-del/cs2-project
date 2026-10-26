#include "catalog.hpp"
#include <cstring>
#include <iostream>
#include <set>
#include <stdexcept>
#include <cmath>
#include <limits>

namespace {
void require(bool result, const char* message) { if (!result) throw std::runtime_error(message); }
}
int main() {
    try {
        require(cs2::catalog_size() > 2000, "catalog unexpectedly empty or truncated");
        std::set<std::pair<int, int>> keys;
        std::set<std::string> files;
        for (auto skin = cs2::catalog_begin(); skin != cs2::catalog_end(); ++skin) {
            require(keys.insert({skin->definition, skin->paint}).second, "ambiguous definition/paint lookup");
            require(files.insert(skin->image_file).second, "two different finishes share an image filename");
            require(skin->image_file == std::to_string(skin->definition) + '_' + std::to_string(skin->paint) + ".png",
                "image does not correspond to its item pair");
            require(skin->min_wear >= 0 && skin->max_wear <= 1 && skin->min_wear <= skin->max_wear, "invalid wear bounds");
        }
        const auto redline = cs2::find_skin(7, 282);
        require(redline && std::strcmp(redline->finish, "Redline") == 0, "AK-47 Redline mapping is wrong");
        const auto cool_mint = cs2::find_skin(5033, 10028);
        require(cool_mint && std::strcmp(cool_mint->finish, "Cool Mint") == 0, "Moto Cool Mint mapping is wrong");
        require(cool_mint->category == cs2::Category::Gloves, "gloves category is wrong");
        const auto phase1 = cs2::find_skin(507, 418);
        const auto phase2 = cs2::find_skin(507, 419);
        require(phase1 && phase2 && std::strcmp(phase1->finish, phase2->finish) != 0, "Doppler phases indistinguishable");
        require(cs2::search_skins(7, "rEdLiNe").size() == 1, "case-insensitive finish search failed");
        require(cs2::search_skins(7, "282").size() == 1, "paint ID search failed");
        require(cs2::search_skins(5033, "Redline").empty(), "search leaks finishes from a different model");
        require(cs2::find_skin(7, 10028) == nullptr, "invalid pair displays an unrelated preview");
        require(cs2::find_skin(500, 0) != nullptr, "vanilla knife missing");
        require(cs2::first_skin(-1) == nullptr, "unknown model has a default preview");
        require(cs2::catalog_models().size() == 63, "catalog model count mismatch");
        require(cs2::wear_state(std::nextafter(0.07f, 0.f)) == 0 && cs2::wear_state(0.07f) == 1,
            "Factory New / Minimal Wear boundary incorrect");
        require(cs2::wear_state(0.15f) == 2 && cs2::wear_state(0.38f) == 3 && cs2::wear_state(0.45f) == 4,
            "wear condition boundary incorrect");
        const auto fresh = cs2::skin_wear_preview(*redline, 0.1f);
        const auto worn = cs2::skin_wear_preview(*redline, 0.7f);
        require(fresh.state == 1 && worn.state == 4 && std::strcmp(fresh.file, worn.file) != 0,
            "wear slider does not change Redline preview");
        require(std::strcmp(cs2::skin_wear_preview(*cool_mint, 0.06f).file,
            cs2::skin_wear_preview(*cool_mint, 0.8f).file) != 0, "glove wear preview does not change");
        require(cs2::skin_wear_preview(*redline, -1).state == 1, "wear below skin minimum not clamped");
        require(cs2::skin_wear_preview(*redline, std::numeric_limits<float>::quiet_NaN()).state == -1,
            "non-finite wear indexes an image state");
        require(cs2::skin_wear_preview(*cs2::find_skin(500, 0), 0.9f).state == -1, "vanilla knife has synthetic wear");
        const std::pair<cs2::Category, std::size_t> counts[] = {
            {cs2::Category::Agent, 63}, {cs2::Category::Music, 102},
            {cs2::Category::Sticker, 11134}, {cs2::Category::Charm, 78}
        };
        std::set<std::pair<int, int>> item_keys;
        for (const auto& expected : counts)
            require(cs2::search_items(expected.first, "").size() == expected.second, "new category count mismatch");
        for (auto item = cs2::item_catalog_begin(); item != cs2::item_catalog_end(); ++item) {
            require(item_keys.emplace(static_cast<int>(item->category), item->id).second, "duplicate catalog item");
            require(item->image_file && item->image_file[0], "item has no preview");
        }
        const auto music = cs2::find_item(cs2::Category::Music, 3);
        require(music && music->stattrak_image, "music StatTrak variant was lost");
        require(cs2::find_item(cs2::Category::Sticker, 8553) != nullptr, "Train Gold sticker missing");
        require(cs2::find_item(cs2::Category::Charm, 1) != cs2::find_item(cs2::Category::Music, 1),
            "item categories collide on the same ID");
        require(cs2::search_items(cs2::Category::Agent, "darryl", "Terrorist").size() > 0, "agent search/group filter failed");
        require(cs2::search_items(cs2::Category::Agent, "darryl", "Counter-Terrorist").empty(), "agent team filter leaked");
        require(cs2::search_items(cs2::Category::Sticker, "train", "Gold").size() > 0, "sticker search/effect filter failed");
        const auto all_files = cs2::catalog_image_files();
        require(all_files.size() == 16934, "image manifest/reference count mismatch");
        for (const auto& file : all_files)
            require(file.find('/') == std::string::npos && file.find('\\') == std::string::npos,
                "preview filename can escape its directory");
        std::cout << "PASS " << cs2::catalog_size() << " distinct skin/image pairs; search and phase mapping\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "FAIL: " << exception.what() << '\n';
        return 1;
    }
}
