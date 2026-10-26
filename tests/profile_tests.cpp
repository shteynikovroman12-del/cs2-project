#include "profile.hpp"

#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::string changed(std::string text, const std::string& key, const std::string& value) {
    const auto start = text.find(key + '=');
    require(start != std::string::npos, "test field missing");
    text.replace(start, text.find('\n', start) - start, key + '=' + value);
    return text;
}

void roundtrip() {
    cs2::Profile original;
    original.item.definition_id = 5033;
    original.item.paint_kit = 10026;
    original.item.wear = 0.12345678f;
    original.item.seed = 1000;
    original.item.stattrak = true;
    original.item.stattrak_count = 2147483647;
    original.item.custom_name = u8"Тест = \"Mint\" \\ profile";
    original.item.equip_ct = false;
    original.world.sky_enabled = true;
    original.world.sky_r = 0.15f;
    original.world.sky_g = 0.45f;
    original.world.sky_b = 0.85f;
    original.world.lighting_enabled = true;
    original.world.lighting = 2.5f;
    original.world.exposure_enabled = true;
    original.world.exposure = -2;
    original.world.fov_enabled = true;
    original.world.world_fov = 110;
    original.world.scoped_fov = 70;
    original.world.viewmodel_enabled = true;
    original.world.viewmodel_fov = 80;
    original.world.offset_x = -3;
    original.world.offset_y = 4;
    original.world.offset_z = -5;
    original.world.thirdperson = true;
    original.world.thirdperson_distance = 150;
    original.world.weather_enabled = true;
    original.world.weather = 2;
    original.world.weather_density = 11;
    original.world.wetness_enabled = true;
    original.world.wetness = 0.8f;
    original.world.animation_speed = 1.25f;
    original.world.rain = 2.21f;
    original.world.leaf_sway = 10;
    original.music_kit = 10;
    original.music_stattrak = true;
    const auto text = cs2::serialize(original);
    cs2::Profile loaded;
    std::string error;
    require(cs2::deserialize(text, loaded, error), error.c_str());
    require(cs2::serialize(loaded) == text, "round trip lost profile data");
    require(loaded.item.wear == original.item.wear, "float precision was lost");
    std::string crlf;
    for (char ch : text) { if (ch == '\n') crlf += '\r'; crlf += ch; }
    require(cs2::deserialize(crlf, loaded, error), "Windows line endings rejected");
    require(cs2::serialize(loaded) == text, "Windows line endings changed values");
}

void malformed() {
    const auto baseline = cs2::serialize(cs2::Profile{});
    cs2::Profile destination;
    destination.item.custom_name = "Must survive failed loads";
    const auto before = cs2::serialize(destination);
    std::string error;
    const std::vector<std::string> cases{
        "", "format_version=2\n", baseline + "item.seed=1\n",
        baseline + "unknown=3\n", baseline + "invalid line\n",
        changed(baseline, "item.wear", "nan"),
        changed(baseline, "item.wear", "inf"),
        changed(baseline, "item.wear", "0.1junk"),
        changed(baseline, "item.seed", "1.5"),
        changed(baseline, "item.seed", "99999999999999999"),
        changed(baseline, "item.equip_t", "2"),
        changed(baseline, "item.custom_name", "unquoted"),
        changed(baseline, "item.custom_name", "\"unterminated"),
        changed(baseline, "item.custom_name", "\"ok\"junk"),
        changed(baseline, "world.world_fov", "200"),
        baseline.substr(0, baseline.find("world.rain=")),
        std::string(65537, 'x')
    };
    for (const auto& text : cases) {
        require(!cs2::deserialize(text, destination, error), "malformed profile accepted");
        require(!error.empty(), "failure has no diagnostic");
        require(cs2::serialize(destination) == before, "failed load changed live state");
    }
}

void validation() {
    cs2::Profile profile;
    require(cs2::validate(profile).empty(), "defaults are invalid");
    profile.item.wear = std::numeric_limits<float>::quiet_NaN();
    profile.world.sky_b = std::numeric_limits<float>::infinity();
    profile.item.seed = -1;
    profile.world.weather = 3;
    profile.item.custom_name = "name\nnew_field=1";
    require(cs2::validate(profile).size() == 5, "invalid inputs not all reported");
    bool rejected = false;
    try { cs2::serialize(profile); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "invalid profile was serialized");
    profile = {};
    profile.item.wear = 1;
    profile.item.seed = 1000;
    profile.item.stattrak_count = std::numeric_limits<int>::max();
    require(cs2::validate(profile).empty(), "valid boundary rejected");
    profile.item.custom_name.assign(129, 'a');
    require(!cs2::validate(profile).empty(), "oversized name accepted");
}

void filesystem() {
    const auto root = std::filesystem::temp_directory_path() /
        ("cs2-profile-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup {
        std::filesystem::path root;
        ~Cleanup() { std::error_code ec; std::filesystem::remove_all(root, ec); }
    } cleanup{root};
    const auto path = root / "nested" / std::filesystem::u8path(u8"профиль.cs2profile");
    cs2::Profile profile;
    std::string error;
    require(cs2::save_profile(path, profile, error), error.c_str());
    profile.item.custom_name = "Updated profile";
    require(cs2::save_profile(path, profile, error), "cannot replace existing profile");
    cs2::Profile loaded;
    require(cs2::load_profile(path, loaded, error), error.c_str());
    require(loaded.item.custom_name == "Updated profile", "replacement did not persist");
    profile.item.seed = -5;
    require(!cs2::save_profile(path, profile, error), "invalid save accepted");
    require(cs2::load_profile(path, loaded, error), "failed save destroyed old file");
    require(loaded.item.custom_name == "Updated profile", "failed save changed old file");
    require(!cs2::load_profile(root / "missing", loaded, error), "missing file accepted");
    require(loaded.item.custom_name == "Updated profile", "missing file changed state");
    const auto directory_target = root / "directory";
    std::filesystem::create_directories(directory_target);
    require(!cs2::save_profile(directory_target, cs2::Profile{}, error), "directory replaced by file");
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root))
        require(entry.path().string().find(".tmp-") == std::string::npos, "temporary save file leaked");
    { std::ofstream out(path, std::ios::trunc); out << std::string(65537, 'x'); }
    require(!cs2::load_profile(path, loaded, error), "oversized file accepted");
    require(loaded.item.custom_name == "Updated profile", "oversized file changed state");
}
} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2, "expected a test case");
        const std::string test = argv[1];
        if (test == "roundtrip") roundtrip();
        else if (test == "malformed") malformed();
        else if (test == "validation") validation();
        else if (test == "filesystem") filesystem();
        else throw std::runtime_error("unknown test case");
        std::cout << "PASS " << test << '\n';
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "FAIL: " << exception.what() << '\n';
        return 1;
    }
}
