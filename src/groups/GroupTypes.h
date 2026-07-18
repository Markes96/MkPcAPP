#pragma once
#include <string>
#include <vector>

namespace groups {

struct LaunchEntry {
    // Path as the user picked it via the file dialog -- a .exe, a .lnk
    // shortcut, or any other executable format (e.g. a .bat, or a Java
    // launcher's own wrapper -- see GroupEditorDialog, which doesn't
    // filter by extension since cases like Minecraft don't launch via a
    // bare .exe).
    std::string path;
    // The exe that will actually run: equal to `path` unless `path` is a
    // .lnk, in which case this is its resolved target. Empty if a .lnk
    // couldn't be resolved (broken shortcut) -- GroupLauncher skips such
    // an entry with an error rather than trying to launch an empty path.
    std::string resolvedExePath;
    // Optional command-line arguments, "" by default.
    std::string args;
};

struct LaunchGroup {
    std::string id;
    std::string name;
    std::vector<LaunchEntry> entries;
};

} // namespace groups
