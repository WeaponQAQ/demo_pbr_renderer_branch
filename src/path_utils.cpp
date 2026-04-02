#include "path_utils.h"

#include <iostream>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <limits.h>
#endif

namespace PathUtils {

static std::filesystem::path s_exeDir;

std::string getExecutableDir()
{
    return s_exeDir.string();
}

std::string resolve(const std::string& relativePath)
{
    if (relativePath.empty()) return relativePath;

    std::filesystem::path p(relativePath);
    if (p.is_absolute()) return relativePath;

    return (s_exeDir / p).string();
}

void init(const char* /*argv0*/)
{
    namespace fs = std::filesystem;

#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    DWORD len = GetModuleFileNameW(NULL, buf, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        s_exeDir = fs::path(buf).parent_path();
        return;
    }
#else
    char buf[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len != -1) {
        buf[len] = '\0';
        s_exeDir = fs::path(buf).parent_path();
        return;
    }
#endif
    // Fallback: use current working directory
    s_exeDir = fs::current_path();
    std::cerr << "[PathUtils] WARNING: could not determine executable directory, "
                 "falling back to CWD: " << s_exeDir << std::endl;
}

} // namespace PathUtils
