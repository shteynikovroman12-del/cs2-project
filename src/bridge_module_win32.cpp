#include "bridge_core.hpp"
#include "bridge_build.hpp"
#include "bridge_win32_common.hpp"
#include "catalog.hpp"
#include <cstring>
#include <stdexcept>
#include <deque>

namespace {
using namespace cs2::bridge;
std::uintptr_t base{};
Shared* shared{};
HANDLE shared_mutex{}, mapping{};
using Frame = void(__fastcall*)(void*, int);
Frame original_frame{};
Request desired{};
bool enabled{}, faulted{}, updating{};
std::uint32_t generation{}, applied_generation{};
// Native material work may outlive the immediate call. Keep borrowed buffers
// alive until process exit, even if restoring the original vector fails.
// Bound storage and stop explicitly rather than accumulate without a limit.
std::deque<std::vector<Attribute>> retained_attributes;

template<class T> bool read(std::uintptr_t at, T& out) {
    SIZE_T bytes{};
    return at && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(at), &out, sizeof(T), &bytes) && bytes == sizeof(T);
}
template<class T> T value(std::uintptr_t at) { T out{}; read(at, out); return out; }
template<class T> bool write(std::uintptr_t at, const T& data) {
    SIZE_T bytes{};
    return at && WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(at), &data, sizeof(T), &bytes) && bytes == sizeof(T);
}
std::uintptr_t entity(std::uint32_t handle) {
    if (handle == 0xffffffffu) return 0;
    const auto list = value<std::uintptr_t>(base + build::entity_list);
    if (!list) return 0;
    const auto page = value<std::uintptr_t>(list + 0x10 + 8 * ((handle & 0x7fff) >> 9));
    if (!page) return 0;
    const auto entry = page + 0x70 * (handle & 0x1ff);
    const auto pointer = value<std::uintptr_t>(entry);
    if (!pointer || value<std::uintptr_t>(pointer + build::identity) != entry) return 0;
    std::uint32_t raw{}, flags{};
    if (!read(entry + 0x10, raw) || !read(entry + build::identity_flags, flags) || raw == 0xffffffffu) return 0;
    const auto actual = (raw & 0x7fff) | (((raw >> 15) - (flags & 1)) << 15);
    return actual == handle ? pointer : 0;
}
std::uintptr_t hud_weapon(std::uintptr_t pawn, std::uintptr_t weapon) {
    const auto arms = entity(value<std::uint32_t>(pawn + build::hud_arms));
    if (!arms) return 0;
    const auto root = value<std::uintptr_t>(arms + build::scene_node);
    if (!root) return 0;
    auto node = value<std::uintptr_t>(root + build::node_child);
    // Bound traversal: no recursive waits on partially created view models.
    for (unsigned i = 0; node && i < 64; ++i) {
        const auto owner = value<std::uintptr_t>(node + build::node_owner);
        if (owner && entity(value<std::uint32_t>(owner + build::owner_entity)) == weapon) return owner;
        node = value<std::uintptr_t>(node + build::node_sibling);
    }
    return 0;
}
struct Snapshot {
    bool valid{};
    std::uint32_t handle{};
    std::uintptr_t weapon{}, node{}, hud{}, hud_node{};
    std::int32_t paint{}, seed{}, high{};
    float wear{};
    bool disallow{};
    std::uint64_t mask{}, hud_mask{};
} saved;
void set_mesh(std::uintptr_t node, std::uint64_t mask) {
    reinterpret_cast<void(__fastcall*)(void*, std::uint64_t)>(base + build::set_mesh_mask.rva)(reinterpret_cast<void*>(node), mask);
}
void refresh(std::uintptr_t weapon) {
    reinterpret_cast<void(__fastcall*)(void*, bool)>(base + build::clear_materials.rva)(reinterpret_cast<void*>(weapon + build::material_component), true);
    reinterpret_cast<void(__fastcall*)(void*, bool)>(base + build::refresh_weapon.rva)(reinterpret_cast<void*>(weapon), false);
}
bool restore_fields() {
    if (!saved.valid) return true;
    if (entity(saved.handle) != saved.weapon) { saved = {}; return true; }
    const auto item = saved.weapon + build::attribute_manager + build::item;
    // Attempt every restoration even if an earlier write fails.
    bool ok = write(saved.weapon + build::fallback_paint, saved.paint);
    ok = write(saved.weapon + build::fallback_seed, saved.seed) && ok;
    ok = write(saved.weapon + build::fallback_wear, saved.wear) && ok;
    ok = write(item + build::item_id_high, saved.high) && ok;
    ok = write(item + build::disallow_soc, saved.disallow) && ok;
    return ok;
}
void restore() {
    if (!saved.valid) return;
    if (!restore_fields()) throw std::runtime_error("Could not restore weapon fields. Restart CS2 before reconnecting.");
    if (!saved.valid) return;
    if (value<std::uintptr_t>(saved.weapon + build::scene_node) == saved.node) set_mesh(saved.node, saved.mask);
    const auto pawn = value<std::uintptr_t>(base + build::local_pawn);
    if (pawn && hud_weapon(pawn, saved.weapon) == saved.hud && saved.hud &&
        value<std::uintptr_t>(saved.hud + build::scene_node) == saved.hud_node) set_mesh(saved.hud_node, saved.hud_mask);
    refresh(saved.weapon);
    saved = {};
}
// No C++ objects requiring unwinding in this SEH boundary. Never leave the
// game vector pointing at our borrowed storage after a successful restoration.
// On restoration failure the storage remains alive and the bridge stops.
bool refresh_with_attributes(std::uintptr_t weapon, std::uintptr_t item,
                             Attribute* attrs, std::size_t count, int paint) {
    AttributeVector old{};
    if (!read(item + build::attributes, old)) return false;
    const AttributeVector temporary{count, reinterpret_cast<std::uintptr_t>(attrs)};
    if (!write(item + build::attributes, temporary)) return false;
    bool ok = false, restored = false;
    __try {
        const int observed = reinterpret_cast<int(__fastcall*)(void*)>(base + build::get_paint.rva)(reinterpret_cast<void*>(item));
        shared->observed_paint = observed;
        if (observed == paint) { refresh(weapon); ok = true; }
    } __finally {
        restored = write(item + build::attributes, old);
    }
    return ok && restored;
}
void update() {
    if (faulted) return; // Never retry native calls after a fault.
    if (!valid_header(*shared)) { enabled = false; restore(); return; }
    ++shared->frames;
    if (shared->request_id != shared->acknowledged_id) {
        const auto request = shared->request;
        shared->acknowledged_id = shared->request_id;
        std::string error;
        if (faulted) { set_status(*shared, State::Failed, "Bridge stopped after an error. Restart CS2."); return; }
        if (!validate_request(request, error)) {
            enabled = false;
            restore();
            set_status(*shared, State::Failed, error.c_str()); return;
        }
        restore();
        desired = request;
        enabled = request.command == Command::ApplyPaint;
        ++generation;
        set_status(*shared, enabled ? State::Waiting : State::Restored,
                   enabled ? "Waiting for the selected weapon in your hands." : "Original weapon fields restored; native material refresh requested.");
    }
    if (GetTickCount64() - shared->heartbeat > 5000) {
        if (enabled || saved.valid) {
            enabled = false; restore();
            set_status(*shared, State::Restored, "Editor heartbeat expired; original fields restored.");
        }
        return;
    }
    if (!enabled || faulted) return;
    const auto pawn = value<std::uintptr_t>(base + build::local_pawn);
    std::uint8_t team{};
    std::int32_t health{};
    if (!pawn || !read(pawn + build::team, team) || !read(pawn + build::health, health) || health <= 0 || health > 1000 || !team_enabled(desired.teams, team)) {
        restore(); set_status(*shared, State::Waiting, "Waiting for a living player on the selected team."); return;
    }
    const auto services = value<std::uintptr_t>(pawn + build::weapon_services);
    if (!services) return;
    const auto handle = value<std::uint32_t>(services + build::active_weapon);
    const auto weapon = entity(handle);
    if (!weapon || entity(value<std::uint32_t>(weapon + build::owner_entity)) != pawn) {
        restore(); set_status(*shared, State::Waiting, "Waiting for your active weapon."); return;
    }
    const auto item = weapon + build::attribute_manager + build::item;
    std::uint16_t definition{};
    if (!read(item + build::definition, definition)) throw std::runtime_error("Cannot read active item definition.");
    shared->observed_definition = definition;
    if (definition != desired.definition) {
        restore(); set_status(*shared, State::Waiting, "Hold the same weapon model selected in the editor."); return;
    }
    const auto node = value<std::uintptr_t>(weapon + build::scene_node);
    const auto hud = hud_weapon(pawn, weapon);
    const auto hud_node = hud ? value<std::uintptr_t>(hud + build::scene_node) : 0;
    if (!node || !hud_node) { set_status(*shared, State::Waiting, "Waiting for weapon and first-person models."); return; }
    if (saved.valid && saved.handle == handle && saved.weapon == weapon && applied_generation == generation &&
        saved.hud == hud && saved.hud_node == hud_node && saved.node == node) {
        if (value<std::int32_t>(weapon + build::fallback_paint) != desired.paint ||
            value<std::int32_t>(weapon + build::fallback_seed) != desired.seed ||
            value<float>(weapon + build::fallback_wear) != desired.wear)
            set_status(*shared, State::Waiting, "Client changed weapon fields after Apply. Press Apply again to retry; visual persistence is unverified.");
        return;
    }
    restore();
    Snapshot snapshot{};
    snapshot.weapon = weapon; snapshot.handle = handle; snapshot.node = node;
    snapshot.hud = hud; snapshot.hud_node = hud_node;
    if (!read(weapon + build::fallback_paint, snapshot.paint) || !read(weapon + build::fallback_seed, snapshot.seed) ||
        !read(weapon + build::fallback_wear, snapshot.wear) || !read(item + build::item_id_high, snapshot.high) ||
        !read(item + build::disallow_soc, snapshot.disallow) || !read(node + build::mesh_mask, snapshot.mask) ||
        !read(hud_node + build::mesh_mask, snapshot.hud_mask)) throw std::runtime_error("Cannot snapshot original weapon state.");
    AttributeVector current{};
    if (!read(item + build::attributes, current) || current.size > 128 || (current.size && !current.data))
        throw std::runtime_error("Unexpected item attribute vector.");
    std::vector<Attribute> old(static_cast<std::size_t>(current.size));
    SIZE_T count{};
    if (current.size && (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(current.data), old.data(), old.size() * sizeof(Attribute), &count) || count != old.size() * sizeof(Attribute)))
        throw std::runtime_error("Cannot read original item attributes.");
    if (retained_attributes.size() >= 256)
        throw std::runtime_error("Experimental material buffer limit reached. Restart CS2 before another test.");
    retained_attributes.push_back(paint_attributes(old.data(), old.size(), desired, base + build::attribute_vtable));
    auto& attrs = retained_attributes.back();
    snapshot.valid = true;
    saved = snapshot;
    const auto* skin = cs2::find_skin(desired.definition, desired.paint);
    const std::uint64_t mask = skin->legacy_model ? 2 : 1;
    if (!write(weapon + build::fallback_paint, desired.paint) || !write(weapon + build::fallback_seed, desired.seed) ||
        !write(weapon + build::fallback_wear, desired.wear) || !write(item + build::item_id_high, std::int32_t{-1}) ||
        !write(item + build::disallow_soc, true)) throw std::runtime_error("Writing weapon parameters failed.");
    set_mesh(node, mask);
    set_mesh(hud_node, mask);
    if (!refresh_with_attributes(weapon, item, attrs.data(), attrs.size(), desired.paint))
        throw std::runtime_error("Client did not read back the requested paint, or temporary attributes could not be restored.");
    applied_generation = generation;
    ++shared->updates;
    set_status(*shared, State::Updated, "Client read back the paint ID and ran material refresh. Visual result requires checking in CS2.");
}
void failure(const char* text) {
    enabled = false; faulted = true;
    restore_fields();
    set_status(*shared, State::Failed, text);
}
void update_cpp() {
    try { update(); }
    catch (const std::exception& error) { failure(error.what()); }
    catch (...) { failure("Unexpected bridge error. Restart CS2."); }
}
void update_guarded() {
    __try { update_cpp(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { failure("Native client call faulted. Bridge disabled; restart CS2 before retrying."); }
}
void __fastcall frame(void* self, int stage) {
    original_frame(self, stage);
    if (stage != 6 || !shared || !shared_mutex) return;
    const auto acquired = WaitForSingleObject(shared_mutex, 0);
    if (acquired != WAIT_OBJECT_0 && acquired != WAIT_ABANDONED) return;
    // The Windows mutex is recursive. A native refresh must not cause a nested
    // callback to replace the same temporary attribute vector a second time.
    if (!updating) {
        updating = true;
        update_guarded();
        updating = false;
    }
    ReleaseMutex(shared_mutex);
}
bool check_function(const build::Function& function) {
    unsigned char bytes[16]{};
    return read(base + function.rva, bytes) && std::memcmp(bytes, function.entry, sizeof(bytes)) == 0;
}
DWORD start_cpp() {
    const auto pid = GetCurrentProcessId();
    shared_mutex = OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, mutex_name(pid).c_str());
    mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, map_name(pid).c_str());
    if (!shared_mutex || !mapping) return 1;
    shared = static_cast<Shared*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shared)));
    if (!shared) return 1;
    const auto acquired = WaitForSingleObject(shared_mutex, 3000);
    if (acquired != WAIT_OBJECT_0 && acquired != WAIT_ABANDONED) return 1;
    if (!valid_header(*shared)) { ReleaseMutex(shared_mutex); return 1; }
    for (const auto& identity : identities) {
        wchar_t path[32768]{};
        const auto module = GetModuleHandleW(identity.name);
        std::string hash;
        if (!module || !GetModuleFileNameW(module, path, 32768) || !sha256(path, hash) || hash != identity.hash) {
            set_status(*shared, State::Failed, "Unsupported CS2 module hashes. No hooks installed.");
            ReleaseMutex(shared_mutex); return 1;
        }
    }
    base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll"));
    for (const auto& function : {build::frame_stage, build::clear_materials, build::refresh_weapon, build::set_mesh_mask, build::get_paint}) {
        if (!check_function(function)) {
            set_status(*shared, State::Failed, "Loaded function bytes differ from the analyzed build. No hooks installed.");
            ReleaseMutex(shared_mutex); return 1;
        }
    }
    auto create = reinterpret_cast<void*(__cdecl*)(const char*, int*)>(GetProcAddress(reinterpret_cast<HMODULE>(base), "CreateInterface"));
    auto client = create ? create("Source2Client002", nullptr) : nullptr;
    if (!client || value<std::uintptr_t>(reinterpret_cast<std::uintptr_t>(client)) != base + build::client_vtable) {
        set_status(*shared, State::Failed, "Unexpected Source2Client002 interface. No hooks installed.");
        ReleaseMutex(shared_mutex); return 1;
    }
    auto slot = reinterpret_cast<void**>(base + build::client_vtable + build::frame_slot * sizeof(void*));
    auto expected = reinterpret_cast<void*>(base + build::frame_stage.rva);
    DWORD protection{};
    if (value<void*>(reinterpret_cast<std::uintptr_t>(slot)) != expected || !VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &protection)) {
        set_status(*shared, State::Failed, "Frame callback is unavailable or already modified. No hook installed.");
        ReleaseMutex(shared_mutex); return 1;
    }
    original_frame = reinterpret_cast<Frame>(expected);
    const bool installed = InterlockedCompareExchangePointer(slot, reinterpret_cast<void*>(&frame), expected) == expected;
    DWORD ignored{};
    if (!VirtualProtect(slot, sizeof(void*), protection, &ignored)) {
        faulted = true;
        set_status(*shared, State::Failed, "Cannot restore callback page protection. Bridge disabled; restart CS2.");
        ReleaseMutex(shared_mutex); return 1;
    }
    set_status(*shared, installed ? State::Ready : State::Failed,
               installed ? "Bridge connected to the pinned CS2 build. Weapon paint integration is experimental." : "Frame callback changed during installation.");
    ReleaseMutex(shared_mutex);
    return installed ? 0 : 1;
}
DWORD start_caught() {
    try { return start_cpp(); }
    catch (...) {
        if (shared) set_status(*shared, State::Failed, "Bridge initialization failed. Restart CS2 before retrying.");
        if (shared_mutex) ReleaseMutex(shared_mutex);
        return 1;
    }
}
DWORD WINAPI start(void*) {
    __try { return start_caught(); }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        faulted = true;
        if (shared) set_status(*shared, State::Failed, "Native initialization fault. Restart CS2 before retrying.");
        if (shared_mutex) ReleaseMutex(shared_mutex);
        return 1;
    }
}
} // namespace

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        // Keep thread notifications enabled: the MSVC build uses the static CRT.
        // The module stays loaded until CS2 exits, so an in-flight frame callback
        // can never point into unloaded code. No worker threads perform game calls.
        HANDLE thread = CreateThread(nullptr, 0, start, nullptr, 0, nullptr);
        if (thread) CloseHandle(thread);
    }
    return TRUE;
}
