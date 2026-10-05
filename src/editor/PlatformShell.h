#pragma once

#include <string>

// Системные действия над файлами проекта: открыть в приложении по умолчанию, показать в Finder/Проводнике.
// Команды запускаются без shell, так что имя файла не может стать частью команды.
namespace PlatformShell {
bool openFile(const std::string& path);
bool revealInFileManager(const std::string& path);
std::string fileManagerName();
std::string absolutePath(const std::string& path);
} // namespace PlatformShell
