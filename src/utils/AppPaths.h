#pragma once

#include <QString>

namespace AppPaths {

// Base writable directory for user application data
// Linux: ~/.local/share/Ensemble
// Windows: C:/Users/<User>/AppData/Local/Ensemble
// macOS: ~/Library/Application Support/Ensemble
QString appDataDir();

// Directory for user configuration/settings
QString appConfigDir();

// Path to persistent debug log file
QString logFilePath();

// Path to user session data
QString sessionFilePath();

// Ensure all application directories exist on disk
void ensureDirectoriesExist();

} // namespace AppPaths
