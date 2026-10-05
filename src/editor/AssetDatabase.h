#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

enum class AssetType {
    Folder,
    Texture,
    Model,
    Material,
    Shader,
    Scene,
    Json,
    Text,
    Font,
    Audio,
    Script,
    Other
};

struct AssetEntry {
    std::string path;       // путь от рабочей папки через '/', например assets/textures/stone.dds
    std::string name;       // имя файла с расширением
    std::string extension;  // в нижнем регистре, с точкой
    AssetType type = AssetType::Other;
    std::uintmax_t size = 0;
    std::filesystem::file_time_type modified{};
};

// Дерево файлов проекта для Content Browser. Без ImGui и GPU, чтобы его можно было тестировать отдельно.
class AssetDatabase {
public:
    explicit AssetDatabase(std::string root = "assets");

    // Перечитывает дерево с диска. Возвращает true, если что-то поменялось.
    bool refresh();
    const std::string& root() const { return root_; }
    bool exists(const std::string& folder) const;

    // Содержимое папки: сначала подпапки, потом файлы, по имени без учёта регистра.
    const std::vector<AssetEntry>& list(const std::string& folder) const;
    std::vector<AssetEntry> subfolders(const std::string& folder) const;
    bool hasSubfolders(const std::string& folder) const;
    // Рекурсивный поиск по имени без учёта регистра; папки не входят.
    std::vector<AssetEntry> search(const std::string& query, std::size_t limit = 500) const;
    const AssetEntry* find(const std::string& path) const;
    std::size_t fileCount() const { return fileCount_; }
    std::uint64_t generation() const { return generation_; }

    static AssetType classify(const std::filesystem::path& path);
    static const char* typeName(AssetType type);
    // Короткая метка для плашки на плитке: PNG, FBX, GLSL...
    static std::string badgeText(const AssetEntry& entry);
    static std::string parentFolder(const std::string& path);
    static std::string formatSize(std::uintmax_t bytes);

private:
    std::string root_;
    std::unordered_map<std::string, std::vector<AssetEntry>> listings_;
    std::size_t fileCount_ = 0;
    std::uint64_t generation_ = 0;
    std::uint64_t signature_ = 0;
};
