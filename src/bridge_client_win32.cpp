#include "bridge_client_win32.hpp"
#include "bridge_core.hpp"
#include "bridge_win32_common.hpp"
#include <tlhelp32.h>
#include <chrono>
#include <stdexcept>

namespace cs2::bridge {
namespace {
struct Lock {
    HANDLE mutex{};
    bool acquired{};
    Lock(HANDLE h, DWORD timeout) : mutex(h) {
        auto r = WaitForSingleObject(h, timeout);
        acquired = r == WAIT_OBJECT_0 || r == WAIT_ABANDONED;
    }
    ~Lock() { if (acquired) ReleaseMutex(mutex); }
};
[[noreturn]] void fail(const char* reason) { throw std::runtime_error(reason); }
std::vector<MODULEENTRY32W> modules(DWORD pid) {
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid));
    if (snapshot.value == INVALID_HANDLE_VALUE) fail("Cannot inspect CS2 modules. Restart the editor with the same privileges as CS2.");
    MODULEENTRY32W module{};
    module.dwSize = sizeof(module);
    std::vector<MODULEENTRY32W> result;
    if (Module32FirstW(snapshot.value, &module)) {
        do { result.push_back(module); } while (Module32NextW(snapshot.value, &module));
    }
    return result;
}
const MODULEENTRY32W* find_module(const std::vector<MODULEENTRY32W>& list, const wchar_t* name) {
    for (const auto& module : list) if (_wcsicmp(module.szModule, name) == 0) return &module;
    return nullptr;
}
DWORD game_pid() {
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (snapshot.value == INVALID_HANDLE_VALUE) fail("Cannot list running processes.");
    PROCESSENTRY32W process{};
    process.dwSize = sizeof(process);
    DWORD pid{};
    if (Process32FirstW(snapshot.value, &process)) {
        do {
            if (_wcsicmp(process.szExeFile, L"cs2.exe") != 0) continue;
            if (pid) fail("More than one CS2 process is running. Close the extra instance.");
            pid = process.th32ProcessID;
        } while (Process32NextW(snapshot.value, &process));
    }
    if (!pid) fail("CS2 is not running. Start it, load a practice map, then connect.");
    return pid;
}
bool owner_running(DWORD pid) {
    if (!pid) return false;
    Handle process(OpenProcess(SYNCHRONIZE, FALSE, pid));
    if (!process.value) return GetLastError() != ERROR_INVALID_PARAMETER;
    return WaitForSingleObject(process.value, 0) != WAIT_OBJECT_0;
}
} // namespace

struct Connection {
    Handle mutex, mapping, process;
    Shared* shared{};
    DWORD pid{};
    ~Connection() { if (shared) UnmapViewOfFile(shared); }
};

namespace {
ConnectResult establish() {
    try {
        const DWORD pid = game_pid();
        const auto loaded = modules(pid);
        for (const auto& identity : identities) {
            const auto* module = find_module(loaded, identity.name);
            std::string hash;
            if (!module || !sha256(module->szExePath, hash) || hash != identity.hash)
                fail("Unsupported CS2 build. This module requires the three DLL hashes from build 26144732; nothing was injected.");
        }
        wchar_t exe[32768]{};
        const auto length = GetModuleFileNameW(nullptr, exe, 32768);
        if (!length || length >= 32768) fail("Cannot resolve the editor directory.");
        const auto path = std::filesystem::path(exe).parent_path() / L"cs2_bridge.dll";
        if (!std::filesystem::is_regular_file(path)) fail("cs2_bridge.dll is missing. Extract both EXE and DLL into the same folder.");
        auto c = std::make_shared<Connection>();
        c->pid = pid;
        c->process.value = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!c->process.value) fail("Cannot open CS2 to monitor its lifetime.");
        c->mutex.value = CreateMutexW(nullptr, FALSE, mutex_name(pid).c_str());
        if (!c->mutex.value) fail("Cannot create bridge mutex.");
        bool resident = find_module(loaded, L"cs2_bridge.dll") != nullptr;
        {
            Lock lock(c->mutex.value, 3000);
            if (!lock.acquired) fail("Bridge is busy. Restart CS2 before reconnecting.");
            c->mapping.value = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(Shared), map_name(pid).c_str());
            const bool existing = GetLastError() == ERROR_ALREADY_EXISTS;
            if (!c->mapping.value) fail("Cannot create shared bridge state.");
            c->shared = static_cast<Shared*>(MapViewOfFile(c->mapping.value, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shared)));
            if (!c->shared) fail("Cannot map shared bridge state.");
            if (existing) {
                if (!valid_header(*c->shared)) fail("Incompatible bridge protocol. Restart CS2 after updating the editor.");
                if (c->shared->editor_pid != GetCurrentProcessId() && owner_running(c->shared->editor_pid))
                    fail("Another editor owns this CS2 connection. Close that editor first.");
                if (c->shared->state == State::Failed) fail("The resident bridge reported an error. Restart CS2 before retrying.");
                if (!resident) fail("Stale bridge mapping. Restart CS2 before retrying.");
                // A new editor session must not replay an unacknowledged apply.
                c->shared->request = Request{Command::Restore};
                ++c->shared->request_id;
            } else {
                if (resident) fail("Resident module has no valid shared state. Restart CS2 before retrying.");
                *c->shared = {};
                c->shared->signature = magic;
                c->shared->protocol = version;
                c->shared->bytes = sizeof(Shared);
                set_status(*c->shared, State::Starting, "Loading experimental bridge...");
            }
            c->shared->editor_pid = GetCurrentProcessId();
            c->shared->heartbeat = GetTickCount64();
        }
        if (!resident) {
            Handle process(OpenProcess(PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_WRITE |
                                       PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid));
            if (!process.value) fail("Windows denied loading the bridge into CS2. No bypass is attempted.");
            const auto local_function = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
            HMODULE owner{};
            if (!local_function || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCWSTR>(local_function), &owner)) fail("Cannot resolve the Windows library loader.");
            wchar_t owner_path[32768]{};
            if (!GetModuleFileNameW(owner, owner_path, 32768)) fail("Cannot resolve the loader's owner module.");
            const auto owner_name = std::filesystem::path(owner_path).filename().wstring();
            const auto* remote_owner = find_module(loaded, owner_name.c_str());
            if (!remote_owner) fail("The loader module is not present in CS2.");
            // Account for ASLR and forwarded kernel32 exports. Both processes use
            // the same system DLL, but their module bases need not be identical.
            const auto offset = reinterpret_cast<std::uintptr_t>(local_function) - reinterpret_cast<std::uintptr_t>(owner);
            if (offset >= remote_owner->modBaseSize) fail("Unexpected loader function offset.");
            const auto remote_function = reinterpret_cast<LPTHREAD_START_ROUTINE>(remote_owner->modBaseAddr + offset);
            const auto text = path.wstring();
            const auto bytes = (text.size() + 1) * sizeof(wchar_t);
            void* remote_text = VirtualAllocEx(process.value, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            if (!remote_text) fail("Cannot allocate the DLL path in CS2.");
            SIZE_T written{};
            if (!WriteProcessMemory(process.value, remote_text, text.c_str(), bytes, &written) || written != bytes) {
                VirtualFreeEx(process.value, remote_text, 0, MEM_RELEASE);
                fail("Cannot write the DLL path in CS2.");
            }
            Handle thread(CreateRemoteThread(process.value, nullptr, 0, remote_function, remote_text, 0, nullptr));
            if (!thread.value) {
                VirtualFreeEx(process.value, remote_text, 0, MEM_RELEASE);
                fail("Windows refused the loader thread. No bypass is attempted.");
            }
            if (WaitForSingleObject(thread.value, 10000) != WAIT_OBJECT_0) {
                // The loader may still use this path. Do not free its memory or
                // terminate its thread; process exit reclaims the allocation.
                fail("DLL loading timed out. Restart CS2 before trying again.");
            }
            VirtualFreeEx(process.value, remote_text, 0, MEM_RELEASE);
            if (!find_module(modules(pid), L"cs2_bridge.dll")) fail("Windows could not load cs2_bridge.dll. Check that both files came from the same x64 package.");
        }
        const auto deadline = GetTickCount64() + 10000;
        while (GetTickCount64() < deadline) {
            if (WaitForSingleObject(c->process.value, 0) == WAIT_OBJECT_0) fail("CS2 exited during connection.");
            {
                Lock lock(c->mutex.value, 0);
                if (lock.acquired) {
                    if (!valid_header(*c->shared)) fail("Bridge handshake is incompatible.");
                    c->shared->heartbeat = GetTickCount64();
                    if (c->shared->state == State::Failed) {
                        const std::string message(c->shared->message, strnlen(c->shared->message, sizeof(c->shared->message)));
                        throw std::runtime_error(message);
                    }
                    if (c->shared->state != State::Starting) return {std::move(c), {}};
                }
            }
            Sleep(20); // Background connection task; never blocks the UI.
        }
        fail("Bridge handshake timed out. Restart CS2 before retrying.");
    } catch (const std::exception& e) { return {nullptr, e.what()}; }
    catch (...) { return {nullptr, "Unexpected connection error."}; }
}
} // namespace

Client::~Client() {
    if (pending_.valid()) { auto result = pending_.get(); connection_ = std::move(result.connection); }
    if (!connection_) return;
    Lock lock(connection_->mutex.value, 100);
    if (lock.acquired && valid_header(*connection_->shared)) {
        connection_->shared->request = Request{Command::Restore};
        ++connection_->shared->request_id;
        connection_->shared->heartbeat = 0;
        connection_->shared->editor_pid = 0;
    }
}
void Client::connect() {
    if (connecting() || connected()) return;
    message_ = "Checking the running build and loading the bridge...";
    pending_ = std::async(std::launch::async, establish);
}
void Client::poll() {
    if (pending_.valid() && pending_.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
        auto result = pending_.get();
        connection_ = std::move(result.connection);
        if (!connection_) message_ = result.error;
    }
    if (!connection_) return;
    if (WaitForSingleObject(connection_->process.value, 0) == WAIT_OBJECT_0) {
        connection_.reset(); snapshot_ = {};
        message_ = "CS2 exited. Start the game again to reconnect.";
        return;
    }
    Lock lock(connection_->mutex.value, 0);
    if (!lock.acquired) return;
    if (!valid_header(*connection_->shared) || connection_->shared->editor_pid != GetCurrentProcessId()) {
        message_ = "Bridge ownership or protocol changed. Restart CS2.";
        snapshot_.state = State::Failed;
        return;
    }
    connection_->shared->heartbeat = GetTickCount64();
    snapshot_ = *connection_->shared;
    message_.assign(snapshot_.message, strnlen(snapshot_.message, sizeof(snapshot_.message)));
}
bool Client::send(const Request& request) {
    if (!usable()) { message_ = "No usable CS2 connection. See Settings / Experimental integration."; return false; }
    Lock lock(connection_->mutex.value, 0);
    if (!lock.acquired) { message_ = "Bridge is busy. Try again."; return false; }
    auto& state = *connection_->shared;
    if (!valid_header(state) || state.editor_pid != GetCurrentProcessId() || state.state == State::Failed) {
        message_ = "Bridge is unavailable. Restart CS2."; return false;
    }
    if (state.request_id != state.acknowledged_id) { message_ = "Previous command is waiting for a game frame."; return false; }
    state.request = request;
    ++state.request_id;
    state.heartbeat = GetTickCount64();
    message_ = "Command queued for the next game frame.";
    return true;
}
bool Client::apply(const Profile& profile) {
    Request request{};
    if (!make_request(profile, request, message_)) return false;
    return send(request);
}
bool Client::restore() { return send(Request{Command::Restore}); }
} // namespace cs2::bridge
