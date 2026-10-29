#pragma once
#include "bridge_protocol.hpp"
#include <windows.h>
#include <bcrypt.h>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace cs2::bridge {
inline std::wstring map_name(DWORD pid) { return L"Local\\CS2Profiles.Bridge." + std::to_wstring(pid); }
inline std::wstring mutex_name(DWORD pid) { return map_name(pid) + L".Mutex"; }
struct Handle {
    HANDLE value{};
    explicit Handle(HANDLE h = nullptr) : value(h) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
inline bool sha256(const std::filesystem::path& path, std::string& result) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_HASH_HANDLE hash{};
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return false;
    bool ok = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
    char buffer[65536];
    while (ok && input) {
        input.read(buffer, sizeof(buffer));
        const auto count = input.gcount();
        if (count) ok = BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer), static_cast<ULONG>(count), 0) >= 0;
    }
    unsigned char digest[32]{};
    ok = ok && input.eof() && BCryptFinishHash(hash, digest, sizeof(digest), 0) >= 0;
    if (hash) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!ok) return false;
    constexpr char hex[] = "0123456789abcdef";
    result.clear();
    for (auto value : digest) { result += hex[value >> 4]; result += hex[value & 15]; }
    return true;
}
struct ModuleIdentity { const wchar_t* name; const char* hash; };
inline constexpr ModuleIdentity identities[] = {
    {L"client.dll", "ccaf4e4926babc052994502b19ec61198a11008a39dce712921b93dd993be9f5"},
    {L"engine2.dll", "e8533af07adc3426eac95391cb70804a0a27f8391c03d36f1cfa06f50d901d29"},
    {L"schemasystem.dll", "2d2d04c4aa2ed8aca1c9b57f3f0d82278907e898156c417a2dd38f48c63c44f0"}
};
inline void set_status(Shared& s, State state, const char* message) {
    s.state = state;
    strncpy_s(s.message, sizeof(s.message), message, _TRUNCATE);
}
} // namespace cs2::bridge
