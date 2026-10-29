#include "bridge_win32_common.hpp"
#include <iostream>

int main(int argc, char** argv) {
    using namespace cs2::bridge;
    if (argc != 2) return 1;
    const auto pid = GetCurrentProcessId();
    Handle mutex(CreateMutexW(nullptr, FALSE, mutex_name(pid).c_str()));
    Handle mapping(CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(Shared), map_name(pid).c_str()));
    if (!mutex.value || !mapping.value) return 2;
    auto* state = static_cast<Shared*>(MapViewOfFile(mapping.value, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shared)));
    if (!state) return 3;
    *state = {};
    state->signature = magic; state->protocol = version; state->bytes = sizeof(Shared);
    state->editor_pid = pid; state->heartbeat = GetTickCount64();
    const auto library = LoadLibraryW(std::filesystem::path(argv[1]).c_str());
    if (!library) { std::cerr << "LoadLibrary failed: " << GetLastError() << '\n'; return 4; }
    bool refused = false;
    const auto deadline = GetTickCount64() + 8000;
    while (GetTickCount64() < deadline) {
        if (WaitForSingleObject(mutex.value, 100) == WAIT_OBJECT_0) {
            refused = state->state == State::Failed && state->frames == 0 && state->updates == 0 &&
                      std::strstr(state->message, "Unsupported CS2 module hashes") != nullptr;
            ReleaseMutex(mutex.value);
            if (refused) break;
        }
        Sleep(10);
    }
    // Do not unload: the bridge worker may still be returning. Process exit
    // destroys the fixture after the observed handshake.
    UnmapViewOfFile(state);
    if (!refused) { std::cerr << "Bridge did not reject the unsupported host\n"; return 5; }
    const auto temp = std::filesystem::temp_directory_path() / ("cs2-bridge-hash-" + std::to_string(pid));
    { std::ofstream file(temp, std::ios::binary); file << "abc"; }
    std::string hash;
    const bool correct = sha256(temp, hash) && hash == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    std::filesystem::remove(temp);
    if (!correct) { std::cerr << "SHA256 mismatch\n"; return 6; }
    std::cout << "Unsupported host rejected without hooks; SHA256 known vector passed.\n";
    return 0;
}
