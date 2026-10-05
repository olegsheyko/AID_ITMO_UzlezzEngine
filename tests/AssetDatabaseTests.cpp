#include "editor/AssetDatabase.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void writeFile(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << content;
}

void classifiesByExtension() {
    require(AssetDatabase::classify("a/stone.DDS") == AssetType::Texture, "dds is a texture, case-insensitive");
    require(AssetDatabase::classify("a/b.png") == AssetType::Texture, "png is a texture");
    require(AssetDatabase::classify("a/Walking.fbx") == AssetType::Model, "fbx is a model");
    require(AssetDatabase::classify("a/hat.mtl") == AssetType::Material, "mtl is a material");
    require(AssetDatabase::classify("a/mesh_vertex.glsl") == AssetType::Shader, "glsl is a shader");
    require(AssetDatabase::classify("assets/scenes/demo.json") == AssetType::Scene, "json in scenes is a scene");
    require(AssetDatabase::classify("assets/data/config.json") == AssetType::Json, "other json is plain JSON");
    require(AssetDatabase::classify("a/Inter.ttf") == AssetType::Font, "ttf is a font");
    require(AssetDatabase::classify("a/LICENSE") == AssetType::Other, "no extension is a generic file");
}

void badgesAndSizes() {
    AssetEntry entry;
    entry.extension = ".dds";
    entry.type = AssetType::Texture;
    require(AssetDatabase::badgeText(entry) == "DDS", "texture badge is its extension");
    entry.type = AssetType::Shader;
    require(AssetDatabase::badgeText(entry) == "GLSL", "shader badge");
    require(AssetDatabase::formatSize(512) == "512 B", "bytes");
    require(AssetDatabase::formatSize(2048) == "2.0 KB", "kilobytes");
    require(AssetDatabase::parentFolder("assets/textures/a.png") == "assets/textures", "parent folder");
}

void scansListsAndSearches(const std::filesystem::path& root) {
    std::filesystem::remove_all(root);
    writeFile(root / "textures" / "Stone.dds", "x");
    writeFile(root / "textures" / "brick.png", "xx");
    writeFile(root / "models" / "Hat.fbx", "xxx");
    writeFile(root / "scenes" / "demo.json", "{}");
    writeFile(root / "readme.txt", "hello");
    writeFile(root / ".hidden" / "secret.png", "x");
    writeFile(root / ".DS_Store", "x");

    const std::string key = root.generic_string();
    AssetDatabase database(key);
    require(database.refresh(), "first refresh reports a change");
    require(!database.refresh(), "unchanged tree reports no change");
    require(database.fileCount() == 5, "hidden files and folders are skipped, got " + std::to_string(database.fileCount()));

    const auto& top = database.list(key);
    require(top.size() == 4, "root lists three folders and one file");
    require(top[0].type == AssetType::Folder && top[0].name == "models", "folders come first, sorted by name");
    require(top[1].name == "scenes" && top[2].name == "textures", "folders sorted case-insensitively");
    require(top[3].name == "readme.txt" && top[3].size == 5, "files after folders with their size");

    const auto& textures = database.list(key + "/textures");
    require(textures.size() == 2 && textures[0].name == "brick.png" && textures[1].name == "Stone.dds", "case-insensitive order");
    require(textures[1].path == key + "/textures/Stone.dds", "paths use forward slashes");
    require(database.list(key + "/scenes")[0].type == AssetType::Scene, "scene manifests are recognised");
    require(database.hasSubfolders(key) && !database.hasSubfolders(key + "/models"), "subfolder detection");

    const auto found = database.search("ST");
    require(found.size() == 1 && found[0].name == "Stone.dds", "search is case-insensitive and skips folders");
    require(database.find(key + "/models/Hat.fbx") != nullptr, "find by path");

    writeFile(root / "models" / "Boot.obj", "x");
    require(database.refresh(), "a new file is detected");
    require(database.list(key + "/models").size() == 2, "new file is listed");
    std::filesystem::remove_all(root);
}
}

int main(int argc, char** argv) {
    const std::filesystem::path root = argc > 1 ? argv[1] : "asset-database-fixtures";
    try {
        classifiesByExtension();
        badgesAndSizes();
        scansListsAndSearches(root);
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << "\n";
        return 1;
    }
    std::cout << "AssetDatabaseTests passed\n";
    return 0;
}
