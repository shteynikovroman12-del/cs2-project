#include "profile.hpp"
#include "catalog.hpp"
#include "image_decode.hpp"
#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

#include <d3d11.h>
#include <shlobj.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <unordered_map>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace {
ID3D11Device* device = nullptr;
ID3D11DeviceContext* context = nullptr;
IDXGISwapChain* swapchain = nullptr;
ID3D11RenderTargetView* render_target = nullptr;
UINT resize_width = 0, resize_height = 0;
bool dirty = false;
float scale = 1;
const ImVec4 accent{1.0f, 0.18f, 0.38f, 1.0f};
const ImVec4 muted{0.46f, 0.48f, 0.52f, 1.0f};
cs2::Profile profile;
std::filesystem::path profile_directory;
std::filesystem::path preview_directory;
char finish_search[128]{};
char profile_name[65] = "default";
std::string status = "Local profile editor. CS2 integration is not implemented.";
bool status_error = false;

template <typename T> void release(T*& object) {
    if (object) { object->Release(); object = nullptr; }
}

struct PreviewTexture {
    ID3D11ShaderResourceView* view = nullptr;
    unsigned width = 0, height = 0;
    std::uint64_t last_used = 0;
    std::string error;
    PreviewTexture() = default;
    PreviewTexture(const PreviewTexture&) = delete;
    PreviewTexture& operator=(const PreviewTexture&) = delete;
    ~PreviewTexture() { release(view); }
};
std::unordered_map<std::string, PreviewTexture> previews;

PreviewTexture& preview_texture(const cs2::Skin& skin) {
    static std::uint64_t tick = 0;
    auto found = previews.find(skin.image_file);
    if (found == previews.end()) {
        if (previews.size() >= 48) {
            const auto oldest = std::min_element(previews.begin(), previews.end(), [](const auto& a, const auto& b) {
                return a.second.last_used < b.second.last_used;
            });
            previews.erase(oldest);
        }
        auto& texture = previews.try_emplace(skin.image_file).first->second;
        cs2::RgbaImage image;
        if (cs2::decode_preview(preview_directory / skin.image_file, image, texture.error)) {
            D3D11_TEXTURE2D_DESC description{};
            description.Width = image.width;
            description.Height = image.height;
            description.MipLevels = 1;
            description.ArraySize = 1;
            description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            description.SampleDesc.Count = 1;
            description.Usage = D3D11_USAGE_IMMUTABLE;
            description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA pixels{};
            pixels.pSysMem = image.pixels.data();
            pixels.SysMemPitch = image.width * 4;
            ID3D11Texture2D* resource = nullptr;
            if (SUCCEEDED(device->CreateTexture2D(&description, &pixels, &resource)) &&
                SUCCEEDED(device->CreateShaderResourceView(resource, nullptr, &texture.view))) {
                texture.width = image.width;
                texture.height = image.height;
            } else texture.error = "Cannot create the preview texture";
            release(resource);
        }
        found = previews.find(skin.image_file);
    }
    found->second.last_used = ++tick;
    return found->second;
}

bool image_fitted(PreviewTexture& texture, ImVec2 area) {
    if (!texture.view || area.x <= 0 || area.y <= 0) return false;
    const float fit = std::min(area.x / texture.width, area.y / texture.height);
    const ImVec2 size{texture.width * fit, texture.height * fit};
    const auto cursor = ImGui::GetCursorPos();
    ImGui::SetCursorPos({cursor.x + (area.x - size.x) / 2, cursor.y + (area.y - size.y) / 2});
    ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(texture.view)), size);
    const bool clicked = ImGui::IsItemClicked();
    ImGui::SetCursorPos({cursor.x, cursor.y + area.y + ImGui::GetStyle().ItemSpacing.y});
    ImGui::Dummy({0, 0});
    return clicked;
}

bool create_render_target() {
    ID3D11Texture2D* backbuffer = nullptr;
    if (FAILED(swapchain->GetBuffer(0, IID_PPV_ARGS(&backbuffer)))) return false;
    const auto result = device->CreateRenderTargetView(backbuffer, nullptr, &render_target);
    release(backbuffer);
    return SUCCEEDED(result);
}

bool create_device(HWND window) {
    DXGI_SWAP_CHAIN_DESC description{};
    description.BufferCount = 2;
    description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.OutputWindow = window;
    description.SampleDesc.Count = 1;
    description.Windowed = TRUE;
    description.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    constexpr D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    auto result = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        levels, 2, D3D11_SDK_VERSION, &description, &swapchain, &device, nullptr, &context);
    if (FAILED(result)) {
        release(swapchain); release(context); release(device);
        result = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
            levels, 2, D3D11_SDK_VERSION, &description, &swapchain, &device, nullptr, &context);
    }
    return SUCCEEDED(result) && create_render_target();
}

void destroy_device() {
    previews.clear();
    release(render_target); release(swapchain); release(context); release(device);
}

void theme() {
    ImGui::StyleColorsLight();
    auto& style = ImGui::GetStyle();
    style.WindowPadding = {20, 16};
    style.FramePadding = {10, 7};
    style.ItemSpacing = {10, 10};
    style.WindowRounding = 0;
    style.ChildRounding = 8;
    style.FrameRounding = 4;
    style.PopupRounding = 6;
    style.ScrollbarRounding = 6;
    style.WindowBorderSize = 0;
    style.ChildBorderSize = 1;
    style.FrameBorderSize = 0;
    style.Colors[ImGuiCol_WindowBg] = {0.96f, 0.96f, 0.97f, 1};
    style.Colors[ImGuiCol_ChildBg] = {1, 1, 1, 1};
    style.Colors[ImGuiCol_Border] = {0.88f, 0.89f, 0.91f, 1};
    style.Colors[ImGuiCol_Text] = {0.12f, 0.13f, 0.16f, 1};
    style.Colors[ImGuiCol_TextDisabled] = muted;
    style.Colors[ImGuiCol_FrameBg] = {0.94f, 0.94f, 0.95f, 1};
    style.Colors[ImGuiCol_FrameBgHovered] = {1, 0.91f, 0.94f, 1};
    style.Colors[ImGuiCol_FrameBgActive] = {1, 0.85f, 0.90f, 1};
    style.Colors[ImGuiCol_CheckMark] = accent;
    style.Colors[ImGuiCol_SliderGrab] = accent;
    style.Colors[ImGuiCol_SliderGrabActive] = {0.85f, 0.12f, 0.32f, 1};
    style.Colors[ImGuiCol_Button] = {0.94f, 0.94f, 0.95f, 1};
    style.Colors[ImGuiCol_ButtonHovered] = {1, 0.90f, 0.93f, 1};
    style.Colors[ImGuiCol_ButtonActive] = {1, 0.80f, 0.86f, 1};
    style.Colors[ImGuiCol_Header] = {1, 0.91f, 0.94f, 1};
    style.Colors[ImGuiCol_HeaderHovered] = {1, 0.87f, 0.91f, 1};
    style.Colors[ImGuiCol_HeaderActive] = {1, 0.82f, 0.88f, 1};
    style.ScaleAllSizes(scale);
}

void heading(const char* text) {
    ImGui::TextUnformatted(text);
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
}

void note(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, muted);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

void slider(const char* label, float& value, float low, float high, const char* format = "%.2f") {
    ImGui::TextUnformatted(label);
    const std::string id = std::string("##") + label;
    ImGui::SetNextItemWidth(-1);
    dirty |= ImGui::SliderFloat(id.c_str(), &value, low, high, format, ImGuiSliderFlags_AlwaysClamp);
}

void integer(const char* label, int& value) {
    ImGui::TextUnformatted(label);
    const std::string id = std::string("##") + label;
    ImGui::SetNextItemWidth(-1);
    dirty |= ImGui::InputInt(id.c_str(), &value);
}

void checkbox(const char* label, bool& value) { dirty |= ImGui::Checkbox(label, &value); }

void unavailable_apply() {
    ImGui::Spacing();
    ImGui::BeginDisabled();
    ImGui::Button("Apply to CS2 - unavailable", {-1, 36 * scale});
    ImGui::EndDisabled();
    note("No game connection. Editing a profile does not change CS2 or your Steam inventory.");
}

void select_skin(const cs2::Skin& skin) {
    profile.item.definition_id = skin.definition;
    profile.item.paint_kit = skin.paint;
    profile.item.wear = std::clamp(profile.item.wear, skin.min_wear, skin.max_wear);
    dirty = true;
}

void select_model(int definition) {
    if (const auto* skin = cs2::first_skin(definition)) select_skin(*skin);
    finish_search[0] = '\0';
}

void model_list(int category) {
    heading("Models");
    for (const auto& model : cs2::catalog_models()) {
        if (static_cast<int>(model.category) != category) continue;
        if (ImGui::Selectable(model.name, profile.item.definition_id == model.definition,
            0, {0, 28 * scale})) select_model(model.definition);
    }
    ImGui::Spacing();
    note("Catalog: CSGO-API / ByMykel");
}

void finish_list() {
    heading("Paint kits");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##finish_search", "Search name or ID...", finish_search, sizeof(finish_search));
    const auto matches = cs2::search_skins(profile.item.definition_id, finish_search);
    ImGui::TextDisabled("%d finishes", static_cast<int>(matches.size()));
    ImGui::BeginChild("finish_rows", {0, 0});
    static int previous_model = -1;
    if (previous_model != profile.item.definition_id) {
        ImGui::SetScrollY(0);
        previous_model = profile.item.definition_id;
    }
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(matches.size()), 32 * scale + ImGui::GetStyle().ItemSpacing.y);
    while (clipper.Step()) {
        for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index) {
            const auto& skin = *matches[index];
            ImGui::PushID(skin.paint);
            const auto position = ImGui::GetCursorScreenPos();
            const auto rgb = skin.rarity_rgb;
            const auto color = IM_COL32((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255, 255);
            ImGui::GetWindowDrawList()->AddRectFilled(position, {position.x + 3 * scale, position.y + 28 * scale}, color);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10 * scale);
            if (ImGui::Selectable(skin.finish, profile.item.paint_kit == skin.paint, 0, {0, 32 * scale}))
                select_skin(skin);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                ImGui::BeginTooltip();
                ImGui::Text("%s | %s", skin.model, skin.finish);
                auto& texture = preview_texture(skin);
                if (texture.view) image_fitted(texture, {240 * scale, 150 * scale});
                else ImGui::TextUnformatted("Image unavailable");
                ImGui::TextDisabled("Paint kit %d", skin.paint);
                ImGui::EndTooltip();
            }
            ImGui::PopID();
        }
    }
    if (matches.empty()) note("No matching finishes for this model.");
    ImGui::EndChild();
}

void skin_preview(const cs2::Skin* skin) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4{0.94f, 0.95f, 0.97f, 1});
    ImGui::BeginChild("preview", {0, 220 * scale}, ImGuiChildFlags_Borders);
    bool zoom = false;
    if (skin) {
        auto& texture = preview_texture(*skin);
        if (texture.view) zoom = image_fitted(texture, ImGui::GetContentRegionAvail());
        else {
            ImGui::TextColored(accent, "IMAGE UNAVAILABLE");
            note(texture.error.c_str());
            note("Extract the complete ZIP, including the previews folder next to the program.");
            if (ImGui::Button("Retry image")) previews.erase(skin->image_file);
        }
    } else note("Select a paint kit to see its image. No preview is shown for unknown model/paint combinations.");
    ImGui::EndChild();
    ImGui::PopStyleColor();
    if (skin) {
        ImGui::TextWrapped("%s | %s", skin->model, skin->finish);
        ImGui::TextDisabled("Paint kit %d / click image to enlarge", skin->paint);
    }
    note("Catalog image. Wear and seed do not change this preview; exact patterns and wear vary in CS2.");
    if (zoom) ImGui::OpenPopup("Skin preview");
    const auto display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowSize({std::min(820 * scale, display.x * 0.9f), std::min(650 * scale, display.y * 0.9f)},
        ImGuiCond_Appearing);
    bool open = true;
    if (ImGui::BeginPopupModal("Skin preview", &open, ImGuiWindowFlags_NoSavedSettings)) {
        if (skin) {
            ImGui::TextWrapped("%s | %s", skin->model, skin->finish);
            auto& texture = preview_texture(*skin);
            auto area = ImGui::GetContentRegionAvail();
            area.y -= 80 * scale;
            if (texture.view) image_fitted(texture, area);
            note("Static catalog image. Exact seed and wear are not rendered.");
        }
        if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void skin_page() {
    static int category = 0;
    static int last_definition = -1;
    static int remembered_models[] = {7, 500, 5033};
    if (last_definition != profile.item.definition_id) {
        for (const auto& model : cs2::catalog_models()) {
            if (model.definition == profile.item.definition_id) {
                category = static_cast<int>(model.category);
                remembered_models[category] = model.definition;
                break;
            }
        }
        last_definition = profile.item.definition_id;
    }
    const auto width = ImGui::GetContentRegionAvail().x;
    const auto height = ImGui::GetContentRegionAvail().y;
    ImGui::BeginChild("categories", {125 * scale, height}, ImGuiChildFlags_Borders);
    const char* labels[] = {"Weapons", "Knives", "Gloves", "Music"};
    note("CATEGORIES");
    ImGui::Spacing();
    for (int index = 0; index < 4; ++index) {
        if (ImGui::Selectable(labels[index], category == index, 0, {0, 30 * scale})) {
            category = index;
            if (index < 3 && profile.item.definition_id != remembered_models[index]) select_model(remembered_models[index]);
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();
    if (category == 3) {
        ImGui::BeginChild("music", {0, height}, ImGuiChildFlags_Borders);
        heading("Music kit");
        note("Local desired settings. Music catalog, cover art and audio preview are not connected.");
        ImGui::Spacing();
        integer("Music kit ID", profile.music_kit);
        checkbox("StatTrak MVP", profile.music_stattrak);
        unavailable_apply();
        ImGui::EndChild();
        return;
    }
    const auto remainder = width - 125 * scale - ImGui::GetStyle().ItemSpacing.x;
    ImGui::BeginChild("models", {remainder * 0.22f, height}, ImGuiChildFlags_Borders);
    model_list(category);
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("paint", {remainder * 0.30f, height}, ImGuiChildFlags_Borders);
    finish_list();
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("item_settings", {0, height}, ImGuiChildFlags_Borders);
    heading("Preview & settings");
    const auto* selected = cs2::find_skin(profile.item.definition_id, profile.item.paint_kit);
    skin_preview(selected);
    slider("Wear", profile.item.wear, selected ? selected->min_wear : 0, selected ? selected->max_wear : 1, "%.4f");
    integer("Seed", profile.item.seed);
    checkbox("StatTrak", profile.item.stattrak);
    if (profile.item.stattrak) integer("StatTrak count", profile.item.stattrak_count);
    char name[129]{};
    std::memcpy(name, profile.item.custom_name.data(), std::min(profile.item.custom_name.size(), sizeof(name) - 1));
    ImGui::TextUnformatted("Custom name");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##custom_name", name, sizeof(name))) { profile.item.custom_name = name; dirty = true; }
    checkbox("T", profile.item.equip_t);
    ImGui::SameLine();
    checkbox("CT", profile.item.equip_ct);
    if (ImGui::CollapsingHeader("Manual IDs")) {
        integer("Definition ID", profile.item.definition_id);
        integer("Paint kit ID", profile.item.paint_kit);
    }
    unavailable_apply();
    ImGui::EndChild();
}

void world_page() {
    auto& world = profile.world;
    const auto width = (ImGui::GetContentRegionAvail().x - 2 * ImGui::GetStyle().ItemSpacing.x) / 3;
    const auto height = ImGui::GetContentRegionAvail().y;
    ImGui::BeginChild("environment", {width, height}, ImGuiChildFlags_Borders);
    heading("Environment");
    checkbox("Enable sky modulation", world.sky_enabled);
    float color[] = {world.sky_r, world.sky_g, world.sky_b};
    ImGui::SetNextItemWidth(-1);
    if (ImGui::ColorEdit3("##sky_color", color, ImGuiColorEditFlags_NoInputs)) {
        world.sky_r = color[0]; world.sky_g = color[1]; world.sky_b = color[2]; dirty = true;
    }
    ImGui::SameLine(); ImGui::TextUnformatted("Sky color");
    ImGui::Spacing();
    checkbox("Override lighting", world.lighting_enabled);
    slider("Lighting intensity", world.lighting, 0, 10);
    checkbox("Override exposure", world.exposure_enabled);
    slider("Exposure", world.exposure, -10, 10);
    ImGui::Spacing();
    note("All values on this page are saved locally only. Limits are editor limits, not verified CS2 capabilities.");
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("camera", {width, height}, ImGuiChildFlags_Borders);
    heading("Camera & view");
    checkbox("Override world FOV", world.fov_enabled);
    slider("World FOV", world.world_fov, 30, 150, "%.0f deg");
    slider("Scoped FOV", world.scoped_fov, 10, 150, "%.0f deg");
    ImGui::Separator();
    checkbox("Override viewmodel", world.viewmodel_enabled);
    slider("Viewmodel FOV", world.viewmodel_fov, 40, 100, "%.0f deg");
    slider("Viewmodel offset X", world.offset_x, -10, 10);
    slider("Viewmodel offset Y", world.offset_y, -10, 10);
    slider("Viewmodel offset Z", world.offset_z, -10, 10);
    ImGui::Separator();
    checkbox("Enable thirdperson", world.thirdperson);
    slider("Thirdperson distance", world.thirdperson_distance, 30, 300, "%.0f");
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("particles", {0, height}, ImGuiChildFlags_Borders);
    heading("Particles");
    checkbox("Enable weather", world.weather_enabled);
    ImGui::SetNextItemWidth(-1);
    dirty |= ImGui::Combo("##weather_type", &world.weather, "Rain\0Heavy rain\0Snow\0");
    slider("Weather density", world.weather_density, 0, 20);
    ImGui::Spacing();
    heading("Surface wetness & wind");
    checkbox("Override surface wetness", world.wetness_enabled);
    slider("Wetness intensity", world.wetness, 0, 1);
    slider("Animation speed", world.animation_speed, 0, 5);
    slider("Rain intensity", world.rain, 0, 5);
    slider("Leaf sway", world.leaf_sway, 0, 20);
    unavailable_apply();
    ImGui::EndChild();
}

bool valid_profile_name() {
    if (profile_name[0] == '\0') return false;
    for (const char* ch = profile_name; *ch; ++ch) {
        if (!((*ch >= 'a' && *ch <= 'z') || (*ch >= 'A' && *ch <= 'Z') ||
              (*ch >= '0' && *ch <= '9') || *ch == '-' || *ch == '_')) return false;
    }
    return true;
}

std::filesystem::path current_path() {
    // Prefix avoids reserved Windows device names, such as CON and NUL.
    return profile_directory / (std::string("profile-") + profile_name + ".cs2profile");
}

void save() {
    std::string error;
    if (!valid_profile_name()) {
        status = "Profile name must contain only letters, numbers, '-' or '_'."; status_error = true; return;
    }
    if (cs2::save_profile(current_path(), profile, error)) {
        status = "Saved local profile: " + std::string(profile_name); status_error = false; dirty = false;
    } else { status = "Save failed: " + error; status_error = true; }
}

void load() {
    std::string error;
    if (!valid_profile_name()) {
        status = "Invalid profile name."; status_error = true; return;
    }
    if (cs2::load_profile(current_path(), profile, error)) {
        status = "Loaded local profile: " + std::string(profile_name); status_error = false; dirty = false;
    } else { status = "Load failed: " + error; status_error = true; }
}

void settings_page() {
    ImGui::BeginChild("settings", {0, 0}, ImGuiChildFlags_Borders);
    heading("Profiles");
    note("Profiles are independent local files. Saving does not equip an item or apply world settings.");
    ImGui::Spacing();
    ImGui::SetNextItemWidth(300 * scale);
    ImGui::InputText("Profile name", profile_name, sizeof(profile_name));
    if (!valid_profile_name()) ImGui::TextColored(accent, "Use letters, numbers, '-' and '_' only.");
    if (ImGui::Button("Save profile", {140 * scale, 0})) save();
    ImGui::SameLine();
    if (ImGui::Button("Load profile", {140 * scale, 0})) {
        if (dirty) ImGui::OpenPopup("Discard local changes?"); else load();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset defaults", {140 * scale, 0})) ImGui::OpenPopup("Reset profile?");
    if (ImGui::BeginPopupModal("Discard local changes?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Loading replaces your unsaved settings.");
        if (ImGui::Button("Load and discard")) { load(); ImGui::CloseCurrentPopup(); }
        ImGui::SameLine(); if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopupModal("Reset profile?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Restore editor defaults? Saved files will remain unchanged.");
        if (ImGui::Button("Reset")) { profile = {}; dirty = true; ImGui::CloseCurrentPopup(); }
        ImGui::SameLine(); if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::Spacing();
    note(profile_directory.u8string().c_str());
    ImGui::Spacing();
    heading("Skin previews");
    ImGui::Text("%d finishes / %d models", static_cast<int>(cs2::catalog_size()), static_cast<int>(cs2::catalog_models().size()));
    note("Images are loaded from the previews folder beside the program. Catalog data: ByMykel / CSGO-API. "
         "Item artwork belongs to Valve and the respective creators. This is a static image viewer, not a 3D renderer.");
    note(preview_directory.u8string().c_str());
    ImGui::Spacing();
    heading("Integration status");
    ImGui::TextColored(accent, "NOT IMPLEMENTED");
    note("This application does not attach to CS2, modify the game, read your inventory, or communicate with Steam. "
         "There is no implemented game backend in this version. Premier and matchmaking behavior has not been tested.");
    ImGui::Spacing();
    heading("Validation");
    const auto errors = cs2::validate(profile);
    if (errors.empty()) note("Local profile values are valid. This does not verify game item IDs or compatibility.");
    for (const auto& error : errors) ImGui::TextColored(accent, "%s", error.c_str());
    ImGui::EndChild();
}

void draw() {
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("CS2 Profiles", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::TextColored(accent, "CS2");
    ImGui::SameLine(); ImGui::TextUnformatted("/ PROFILES");
    ImGui::SameLine(210 * scale);
    static int tab = 0;
    const char* tabs[] = {"SKINS", "WORLD", "SETTINGS"};
    for (int index = 0; index < 3; ++index) {
        if (index > 0) ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, tab == index ? accent : muted);
        if (ImGui::Button(tabs[index], {100 * scale, 0})) tab = index;
        ImGui::PopStyleColor();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", dirty ? "Unsaved changes" : "Local profile");
    ImGui::Spacing();
    const auto position = ImGui::GetCursorScreenPos();
    const auto width = ImGui::GetContentRegionAvail().x;
    ImGui::GetWindowDrawList()->AddRectFilledMultiColor(position, {position.x + width, position.y + 2 * scale},
        IM_COL32(255, 46, 97, 255), IM_COL32(113, 116, 255, 255), IM_COL32(113, 116, 255, 255), IM_COL32(255, 46, 97, 255));
    ImGui::Dummy({0, 5 * scale});
    note("PROFILE EDITOR  /  CS2 is not connected. Settings are not applied to the game.");
    ImGui::Spacing();
    const float footer_height = 56 * scale;
    ImGui::BeginChild("content", {0, -footer_height});
    if (tab == 0) skin_page();
    else if (tab == 1) world_page();
    else settings_page();
    ImGui::EndChild();
    ImGui::Separator();
    if (ImGui::Button("Save", {80 * scale, 0})) save();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, status_error ? accent : muted);
    ImGui::TextWrapped("%s", status.c_str());
    ImGui::PopStyleColor();
    ImGui::End();
}

LRESULT WINAPI window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_CLOSE && dirty) {
        if (MessageBoxW(window, L"Discard unsaved profile changes and close?", L"CS2 Profiles",
            MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) return 0;
    }
    if (ImGui_ImplWin32_WndProcHandler(window, message, wparam, lparam)) return 1;
    switch (message) {
    case WM_SIZE:
        if (wparam != SIZE_MINIMIZED) { resize_width = LOWORD(lparam); resize_height = HIWORD(lparam); }
        return 0;
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
        info->ptMinTrackSize = {static_cast<LONG>(900 * scale), static_cast<LONG>(650 * scale)};
        return 0;
    }
    case WM_SYSCOMMAND:
        if ((wparam & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
} // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR command_line, int show) {
    const bool smoke_test = std::string(command_line) == "--preview-smoke-test";
    wchar_t executable[32768]{};
    const DWORD path_length = GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
    if (!path_length || path_length >= std::size(executable)) {
        MessageBoxW(nullptr, L"Cannot locate the application folder.", L"CS2 Profiles", MB_ICONERROR);
        return 1;
    }
    preview_directory = std::filesystem::path(executable).parent_path() / L"previews";
    PWSTR appdata = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &appdata))) {
        MessageBoxW(nullptr, L"Cannot locate LocalAppData for profile storage.", L"CS2 Profiles", MB_ICONERROR);
        return 1;
    }
    profile_directory = std::filesystem::path(appdata) / L"CS2Project" / L"profiles";
    CoTaskMemFree(appdata);
    ImGui_ImplWin32_EnableDpiAwareness();
    scale = ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY));
    WNDCLASSEXW window_class{sizeof(WNDCLASSEXW), CS_CLASSDC, window_proc, 0, 0, instance,
        nullptr, LoadCursorW(nullptr, IDC_ARROW), nullptr, nullptr, L"CS2ProfilesWindow", nullptr};
    if (!RegisterClassExW(&window_class)) return 1;
    const auto window = CreateWindowW(window_class.lpszClassName, L"CS2 Profiles - local editor / no game integration",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, static_cast<int>(1180 * scale),
        static_cast<int>(820 * scale), nullptr, nullptr, instance, nullptr);
    if (!window || !create_device(window)) {
        MessageBoxW(window, L"Could not initialize the Direct3D 11 window.", L"CS2 Profiles", MB_ICONERROR);
        destroy_device();
        if (window) DestroyWindow(window);
        UnregisterClassW(window_class.lpszClassName, instance);
        return 1;
    }
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    ImFontConfig font_config;
    font_config.SizePixels = 16 * scale;
    // Segoe UI provides Cyrillic input/display on standard Windows installations.
    wchar_t windows_directory[MAX_PATH]{};
    if (GetWindowsDirectoryW(windows_directory, MAX_PATH)) {
        const auto font_path = std::filesystem::path(windows_directory) / L"Fonts" / L"segoeui.ttf";
        std::error_code error;
        if (std::filesystem::exists(font_path, error))
            io.Fonts->AddFontFromFileTTF(font_path.u8string().c_str(), font_config.SizePixels,
                &font_config, io.Fonts->GetGlyphRangesCyrillic());
    }
    if (io.Fonts->Fonts.empty()) io.Fonts->AddFontDefault(&font_config);
    theme();
    const bool platform_ready = ImGui_ImplWin32_Init(window);
    const bool renderer_ready = platform_ready && ImGui_ImplDX11_Init(device, context);
    if (!renderer_ready) {
        if (platform_ready) ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext(); destroy_device(); DestroyWindow(window);
        UnregisterClassW(window_class.lpszClassName, instance);
        return 1;
    }
    std::error_code path_error;
    if (!smoke_test && std::filesystem::exists(current_path(), path_error)) load();
    ShowWindow(window, show);
    UpdateWindow(window);
    bool done = false;
    bool graphics_failed = false;
    int smoke_frames = 0;
    while (!done) {
        MSG message;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message); DispatchMessageW(&message);
            if (message.message == WM_QUIT) done = true;
        }
        if (done) break;
        if (IsIconic(window)) { WaitMessage(); continue; }
        if (resize_width && resize_height) {
            context->OMSetRenderTargets(0, nullptr, nullptr);
            release(render_target);
            if (FAILED(swapchain->ResizeBuffers(0, resize_width, resize_height, DXGI_FORMAT_UNKNOWN, 0)) ||
                !create_render_target()) { graphics_failed = true; break; }
            resize_width = resize_height = 0;
        }
        ImGui_ImplDX11_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame();
        if (smoke_test) {
            const cs2::Skin* sample = smoke_frames < 2 ? cs2::find_skin(7, 282) :
                smoke_frames < 4 ? cs2::find_skin(7, 302) : cs2::find_skin(5033, 10028);
            if (!sample) { graphics_failed = true; break; }
            select_skin(*sample);
        }
        draw();
        ImGui::Render();
        const float clear[] = {0.96f, 0.96f, 0.97f, 1};
        context->OMSetRenderTargets(1, &render_target, nullptr);
        context->ClearRenderTargetView(render_target, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        const auto result = swapchain->Present(1, 0);
        if (FAILED(result)) { graphics_failed = true; break; }
        if (smoke_test) {
            const auto* sample = cs2::find_skin(profile.item.definition_id, profile.item.paint_kit);
            if (!sample || !preview_texture(*sample).view) { graphics_failed = true; break; }
            if (++smoke_frames == 6) done = true;
        }
        if (result == DXGI_STATUS_OCCLUDED) Sleep(30);
    }
    if (graphics_failed && !smoke_test) MessageBoxW(window, L"The graphics device failed. Unsaved settings could not be saved automatically.",
        L"CS2 Profiles", MB_ICONERROR);
    ImGui_ImplDX11_Shutdown(); ImGui_ImplWin32_Shutdown(); ImGui::DestroyContext();
    destroy_device();
    if (IsWindow(window)) DestroyWindow(window);
    UnregisterClassW(window_class.lpszClassName, instance);
    return graphics_failed ? 1 : 0;
}
