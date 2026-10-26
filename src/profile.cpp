#include "profile.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <sstream>
#include <stdexcept>
#include <type_traits>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace cs2 {
namespace {
constexpr std::size_t max_profile_bytes = 65536;

template <typename P, typename Visitor>
void fields(P& p, Visitor visit, bool include_v2 = true) {
#define FIELD(name) visit(#name, p.name)
    FIELD(item.definition_id); FIELD(item.paint_kit); FIELD(item.wear);
    FIELD(item.seed); FIELD(item.stattrak); FIELD(item.stattrak_count);
    FIELD(item.custom_name); FIELD(item.equip_t); FIELD(item.equip_ct);
    FIELD(music_kit); FIELD(music_stattrak);
    FIELD(world.sky_enabled); FIELD(world.sky_r); FIELD(world.sky_g); FIELD(world.sky_b);
    FIELD(world.lighting_enabled); FIELD(world.lighting);
    FIELD(world.exposure_enabled); FIELD(world.exposure);
    FIELD(world.fov_enabled); FIELD(world.world_fov); FIELD(world.scoped_fov);
    FIELD(world.viewmodel_enabled); FIELD(world.viewmodel_fov);
    FIELD(world.offset_x); FIELD(world.offset_y); FIELD(world.offset_z);
    FIELD(world.thirdperson); FIELD(world.thirdperson_distance);
    FIELD(world.weather_enabled); FIELD(world.weather); FIELD(world.weather_density);
    FIELD(world.wetness_enabled); FIELD(world.wetness); FIELD(world.animation_speed);
    FIELD(world.rain); FIELD(world.leaf_sway);
    if (include_v2) { FIELD(agent_id); FIELD(sticker_id); FIELD(charm_id); }
#undef FIELD
}

template <typename T>
bool parse_value(const std::string& input, T& output) {
    std::istringstream stream(input);
    stream.imbue(std::locale::classic());
    if constexpr (std::is_same_v<T, std::string>) {
        if (input.empty() || input.front() != '"') return false;
        stream >> std::quoted(output);
    } else if constexpr (std::is_same_v<T, bool>) {
        if (input != "0" && input != "1") return false;
        output = input == "1";
        return true;
    } else {
        stream >> output;
    }
    if (stream.fail()) return false;
    stream >> std::ws;
    return stream.eof();
}
} // namespace

std::vector<std::string> validate(const Profile& p) {
    std::vector<std::string> errors;
    const auto range = [&](const char* key, double value, double low, double high) {
        if (!std::isfinite(value) || value < low || value > high)
            errors.emplace_back(std::string(key) + ": value outside editor limits");
    };
    range("Item definition", p.item.definition_id, 1, 65535);
    range("Paint kit", p.item.paint_kit, 0, 1000000);
    range("Wear", p.item.wear, 0, 1);
    range("Seed", p.item.seed, 0, 1000);
    range("StatTrak count", p.item.stattrak_count, 0, 2147483647);
    range("Music kit", p.music_kit, 0, 1000000);
    range("Agent", p.agent_id, 0, 65535);
    range("Sticker", p.sticker_id, 0, 1000000);
    range("Charm", p.charm_id, 0, 1000000);
    if (p.item.custom_name.size() > 128)
        errors.emplace_back("Custom name: maximum 128 UTF-8 bytes");
    for (unsigned char ch : p.item.custom_name) {
        if (ch < 32 || ch == 127) {
            errors.emplace_back("Custom name: control characters are not allowed");
            break;
        }
    }
    range("Sky red", p.world.sky_r, 0, 1);
    range("Sky green", p.world.sky_g, 0, 1);
    range("Sky blue", p.world.sky_b, 0, 1);
    range("Lighting", p.world.lighting, 0, 10);
    range("Exposure", p.world.exposure, -10, 10);
    range("World FOV", p.world.world_fov, 30, 150);
    range("Scoped FOV", p.world.scoped_fov, 10, 150);
    range("Viewmodel FOV", p.world.viewmodel_fov, 40, 100);
    range("Viewmodel X", p.world.offset_x, -10, 10);
    range("Viewmodel Y", p.world.offset_y, -10, 10);
    range("Viewmodel Z", p.world.offset_z, -10, 10);
    range("Thirdperson distance", p.world.thirdperson_distance, 30, 300);
    range("Weather type", p.world.weather, 0, 2);
    range("Weather density", p.world.weather_density, 0, 20);
    range("Wetness", p.world.wetness, 0, 1);
    range("Animation speed", p.world.animation_speed, 0, 5);
    range("Rain intensity", p.world.rain, 0, 5);
    range("Leaf sway", p.world.leaf_sway, 0, 20);
    return errors;
}

std::string serialize(const Profile& p) {
    const auto errors = validate(p);
    if (!errors.empty()) throw std::invalid_argument(errors.front());
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << "format_version=2\n" << std::setprecision(std::numeric_limits<float>::max_digits10);
    fields(p, [&](const char* key, const auto& value) {
        stream << key << '=';
        if constexpr (std::is_same_v<std::decay_t<decltype(value)>, std::string>)
            stream << std::quoted(value);
        else stream << value;
        stream << '\n';
    });
    return stream.str();
}

bool deserialize(const std::string& text, Profile& destination, std::string& error) {
    error.clear();
    if (text.size() > max_profile_bytes) { error = "Profile exceeds 64 KiB"; return false; }
    std::map<std::string, std::string> values;
    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        const auto separator = line.find('=');
        if (separator == std::string::npos || separator == 0) {
            error = "Invalid profile line"; return false;
        }
        if (!values.emplace(line.substr(0, separator), line.substr(separator + 1)).second) {
            error = "Duplicate profile field"; return false;
        }
    }
    const auto version = values.find("format_version");
    if (version == values.end() || (version->second != "1" && version->second != "2")) {
        error = "Missing or unsupported profile version"; return false;
    }
    const bool legacy = version->second == "1";
    values.erase(version);
    Profile candidate;
    bool valid = true;
    fields(candidate, [&](const char* key, auto& value) {
        if (!valid) return;
        const auto found = values.find(key);
        if (found == values.end() || !parse_value(found->second, value)) {
            error = std::string("Missing or invalid field: ") + key;
            valid = false;
            return;
        }
        values.erase(found);
    }, !legacy);
    if (!valid) return false;
    if (!values.empty()) { error = "Unknown profile field: " + values.begin()->first; return false; }
    const auto errors = validate(candidate);
    if (!errors.empty()) { error = errors.front(); return false; }
    destination = std::move(candidate);
    return true;
}

bool save_profile(const std::filesystem::path& path, const Profile& profile, std::string& error) {
    error.clear();
    std::filesystem::path temporary;
    try {
        const auto text = serialize(profile);
        if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
        static std::atomic<unsigned long> counter{0};
        temporary = path;
        temporary += ".tmp-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
            + "-" + std::to_string(counter.fetch_add(1));
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            output.exceptions(std::ios::badbit | std::ios::failbit);
            output << text;
            output.close();
        }
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Cannot replace profile; Windows error " + std::to_string(GetLastError()));
#else
        std::filesystem::rename(temporary, path);
#endif
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        if (!temporary.empty()) {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
        }
        return false;
    }
}

bool load_profile(const std::filesystem::path& path, Profile& destination, std::string& error) {
    error.clear();
    try {
        std::ifstream input(path, std::ios::binary);
        if (!input) { error = "Cannot open profile"; return false; }
        std::string text(max_profile_bytes + 1, '\0');
        input.read(text.data(), static_cast<std::streamsize>(text.size()));
        if (input.bad()) { error = "Cannot read profile"; return false; }
        text.resize(static_cast<std::size_t>(input.gcount()));
        return deserialize(text, destination, error);
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}
} // namespace cs2
