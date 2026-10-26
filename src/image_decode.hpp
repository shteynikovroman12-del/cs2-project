#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace cs2 {
struct RgbaImage {
    unsigned width = 0;
    unsigned height = 0;
    std::vector<std::uint8_t> pixels;
};
// Decodes a bounded local image using Windows Imaging Component.
// Failure clears the output so a previous skin can never be shown as the new one.
bool decode_preview(const std::filesystem::path& path, RgbaImage& image, std::string& error);
} // namespace cs2
