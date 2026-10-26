#include "catalog.hpp"
#include "image_decode.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>

namespace {
void require(bool result, const std::string& message) { if (!result) throw std::runtime_error(message); }
std::uint64_t fingerprint(const cs2::RgbaImage& image) {
    std::uint64_t hash = 14695981039346656037ull;
    for (auto byte : image.pixels) { hash ^= byte; hash *= 1099511628211ull; }
    return hash;
}
}
int main(int argc, char** argv) {
    try {
        require(argc == 2, "expected preview directory");
        const auto root = std::filesystem::u8path(argv[1]);
        cs2::RgbaImage image;
        std::string error;
        std::set<std::uint64_t> sample_images;
        int checked = 0;
        for (auto skin = cs2::catalog_begin(); skin != cs2::catalog_end(); ++skin) {
            const bool loaded = cs2::decode_preview(root / skin->image_file, image, error);
            require(loaded, std::string(skin->image_file) + ": " + error);
            require(image.pixels.size() == static_cast<std::size_t>(image.width) * image.height * 4,
                "wrong RGBA pixel buffer size");
            bool visible = false, transparent = false;
            for (std::size_t index = 3; index < image.pixels.size(); index += 4) {
                visible |= image.pixels[index] != 0;
                transparent |= image.pixels[index] != 255;
            }
            require(visible, std::string(skin->image_file) + " is entirely invisible");
            if ((skin->definition == 7 && (skin->paint == 282 || skin->paint == 302)) ||
                (skin->definition == 5033 && skin->paint == 10028)) {
                sample_images.insert(fingerprint(image));
                require(transparent, "sample preview lost alpha transparency");
            }
            ++checked;
        }
        require(sample_images.size() == 3, "distinct representative skins decoded to the same image");
        require(!cs2::decode_preview(root / "missing-image.png", image, error), "missing image was accepted");
        require(image.pixels.empty() && image.width == 0, "failed load retained the previous skin image");
        const auto temp = std::filesystem::temp_directory_path() /
            ("cs2-preview-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(temp);
        struct Cleanup {
            std::filesystem::path path;
            ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); }
        } cleanup{temp};
        const auto unicode = temp / std::filesystem::u8path(u8"превью.png");
        std::filesystem::copy_file(root / "7_282.png", unicode);
        require(cs2::decode_preview(unicode, image, error), "Unicode preview path failed");
        { std::ofstream broken(unicode, std::ios::binary | std::ios::trunc); broken << "not a PNG"; }
        require(!cs2::decode_preview(unicode, image, error), "corrupt preview was accepted");
        require(image.pixels.empty(), "corrupt image left a stale preview");
        std::cout << "PASS " << checked << " decoded previews, alpha, distinct skins, corrupt/missing files, Unicode paths\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "FAIL: " << exception.what() << '\n';
        return 1;
    }
}
