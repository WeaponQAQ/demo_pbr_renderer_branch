#pragma once

#include <string>
#include <filesystem>

namespace PathUtils {

// Returns the directory containing the running executable.
// All relative resource paths should be resolved against this.
std::string getExecutableDir();

// Resolves a potentially relative path against the executable directory.
// If the path is already absolute, returns it unchanged.
// If relative, prepends the executable directory.
std::string resolve(const std::string& relativePath);

// Must be called once at program startup (before any resource loading)
// with argv[0] or an empty string to auto-detect.
void init(const char* argv0);

} // namespace PathUtils
