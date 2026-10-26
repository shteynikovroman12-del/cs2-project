#include "catalog.hpp"
#include <cstring>
#include <iostream>
#include <set>
#include <stdexcept>

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
        std::cout << "PASS " << cs2::catalog_size() << " distinct skin/image pairs; search and phase mapping\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "FAIL: " << exception.what() << '\n';
        return 1;
    }
}
