#pragma once
#include <cstdint>
namespace cs2::bridge::build {
// Only for the client SHA-256 pinned in bridge_win32_common.hpp.
// RVAs and entry bytes checked in the supplied PE, not copied from a generic SDK.
struct Function { std::uintptr_t rva; unsigned char entry[16]; };
inline constexpr Function frame_stage{0xb6e540, {0x48, 0x89, 0x5c, 0x24, 0x18, 0x48, 0x89, 0x6c, 0x24, 0x20, 0x57, 0x48, 0x83, 0xec, 0x40, 0x48}};
inline constexpr Function clear_materials{0x14e9d00, {0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x6c, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57}};
inline constexpr Function refresh_weapon{0x803780, {0x40, 0x55, 0x56, 0x41, 0x57, 0x48, 0x8d, 0x6c, 0x24, 0xb9, 0x48, 0x81, 0xec, 0xd0, 0x00, 0x00}};
inline constexpr Function set_mesh_mask{0xac4ff0, {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x20, 0x48}};
inline constexpr Function get_paint{0x118ecd0, {0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x40, 0x8b, 0x15, 0x70, 0x1b, 0x61, 0x01}};
inline constexpr std::uintptr_t client_vtable = 0x1c65030;
inline constexpr std::uintptr_t frame_slot = 0x24;
inline constexpr std::uintptr_t entity_list = 0x271c8b8;
inline constexpr std::uintptr_t local_pawn = 0x25676d8;
inline constexpr std::uintptr_t attribute_vtable = 0x1d20360;
inline constexpr std::uintptr_t weapon_services = 0x12f0;
inline constexpr std::uintptr_t active_weapon = 0x60;
inline constexpr std::uintptr_t hud_arms = 0x1da8;
inline constexpr std::uintptr_t team = 0x3e7;
inline constexpr std::uintptr_t health = 0x34c;
inline constexpr std::uintptr_t scene_node = 0x330;
inline constexpr std::uintptr_t owner_entity = 0x520;
inline constexpr std::uintptr_t identity = 0x10;
inline constexpr std::uintptr_t identity_flags = 0x30;
inline constexpr std::uintptr_t node_owner = 0x30;
inline constexpr std::uintptr_t node_child = 0x40;
inline constexpr std::uintptr_t node_sibling = 0x48;
inline constexpr std::uintptr_t attribute_manager = 0x1290;
inline constexpr std::uintptr_t item = 0x50;
inline constexpr std::uintptr_t definition = 0x1ba;
inline constexpr std::uintptr_t item_id_high = 0x1d0;
inline constexpr std::uintptr_t disallow_soc = 0x1e9;
inline constexpr std::uintptr_t fallback_paint = 0x18a8;
inline constexpr std::uintptr_t fallback_seed = 0x18ac;
inline constexpr std::uintptr_t fallback_wear = 0x18b0;
inline constexpr std::uintptr_t attributes = 0x210; // m_AttributeList + m_Attributes; confirmed by iterator
inline constexpr std::uintptr_t mesh_mask = 0x348; // CSkeletonInstance::m_modelState + CModelState::m_MeshGroupMask
inline constexpr std::uintptr_t material_component = 0x610; // argument in native regeneration routine
} // namespace cs2::bridge::build
