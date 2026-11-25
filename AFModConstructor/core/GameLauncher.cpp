#include "GameLauncher.h"

#include "PathUtil.h"
#include "SharedConfig.h"

#include <windows.h>

#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

namespace GameLauncher {

std::string GameDir() {
    std::error_code ec;
    const std::string path = SharedConfig::GamePath();
    if (path.empty()) return {};
    const fs::path dir = PathUtil::MakePath(path);
    if (fs::exists(dir / "game.exe", ec)) return path;
    return {};
}

bool HasAzForge(const std::string& gameDir) {
    if (gameDir.empty()) return false;
    std::error_code ec;
    const fs::path dir = PathUtil::MakePath(gameDir);
    return fs::exists(dir / "d3d8.dll", ec) && fs::exists(dir / "AzForge.dll", ec);
}

bool Launch(std::string* error) {
    const std::string dir = GameDir();
    if (dir.empty()) {
        if (error) *error = "Game path is not set or game.exe was not found";
        return false;
    }

    const fs::path gameDir = PathUtil::MakePath(dir);
    const fs::path exe = gameDir / "game.exe";
    std::wstring cmd = L"\"" + exe.wstring() + L"\"";
    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    if (!CreateProcessW(exe.c_str(), cmdBuf.data(), nullptr, nullptr, FALSE, 0,
                        nullptr, gameDir.c_str(), &si, &pi)) {
        if (error) *error = "Failed to launch game.exe (error " + std::to_string(GetLastError()) + ")";
        return false;
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

} // namespace GameLauncher
