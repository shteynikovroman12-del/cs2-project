#pragma once
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace cs2::bridge {
constexpr std::uint32_t magic = 0x43533242;
constexpr std::uint32_t version = 2026102801;
enum class Command : std::uint32_t { None, ApplyPaint, Restore };
enum class State : std::uint32_t { Starting, Ready, Waiting, Updated, Restored, Failed };
struct Request {
    Command command{};
    std::int32_t definition{}, paint{}, seed{};
    float wear{};
    std::uint32_t teams{}; // bit 0 = T, bit 1 = CT
};
// All access, including status/heartbeat, is protected by the named mutex.
// No STL objects, native pointers or arbitrary executable commands in the protocol.
struct Shared {
    std::uint32_t signature{}, protocol{}, bytes{}, editor_pid{};
    std::uint64_t heartbeat{};
    std::uint32_t request_id{}, acknowledged_id{};
    Request request{};
    State state{};
    std::uint32_t frames{}, updates{};
    std::int32_t observed_definition{}, observed_paint{};
    char message[256]{};
};
static_assert(std::is_trivially_copyable_v<Shared>);
static_assert(sizeof(Request) == 24);
static_assert(sizeof(Shared) == 336);
inline bool valid_header(const Shared& shared) {
    return shared.signature == magic && shared.protocol == version && shared.bytes == sizeof(Shared);
}
} // namespace cs2::bridge
