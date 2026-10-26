#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace cs2 {

// These are desired local settings, not a report of game or inventory state.
struct ItemSettings {
    int definition_id = 7;
    int paint_kit = 0;
    float wear = 0.0001f;
    int seed = 1;
    bool stattrak = false;
    int stattrak_count = 0;
    std::string custom_name;
    bool equip_t = true;
    bool equip_ct = true;
};

struct WorldSettings {
    bool sky_enabled = false;
    float sky_r = 1, sky_g = 1, sky_b = 1;
    bool lighting_enabled = false;
    float lighting = 1;
    bool exposure_enabled = false;
    float exposure = 0;
    bool fov_enabled = false;
    float world_fov = 90, scoped_fov = 90;
    bool viewmodel_enabled = false;
    float viewmodel_fov = 68;
    float offset_x = 0, offset_y = 0, offset_z = 0;
    bool thirdperson = false;
    float thirdperson_distance = 100;
    bool weather_enabled = false;
    int weather = 0;
    float weather_density = 1;
    bool wetness_enabled = false;
    float wetness = 0, animation_speed = 1, rain = 0, leaf_sway = 0;
};

struct Profile {
    ItemSettings item;
    WorldSettings world;
    int music_kit = 0;
    bool music_stattrak = false;
};

// Bounds are editor input limits, not promises of values supported by CS2.
std::vector<std::string> validate(const Profile& profile);
std::string serialize(const Profile& profile);
// All reads are transactional: failure leaves the destination untouched.
bool deserialize(const std::string& text, Profile& destination, std::string& error);
bool save_profile(const std::filesystem::path& path, const Profile& profile, std::string& error);
bool load_profile(const std::filesystem::path& path, Profile& destination, std::string& error);

} // namespace cs2
