#include "editor/AssetDatabase.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <functional>

namespace {
std::string toLower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

bool lessByName(const AssetEntry& left, const AssetEntry& right) {
    const bool leftFolder = left.type == AssetType::Folder;
    const bool rightFolder = right.type == AssetType::Folder;
    if (leftFolder != rightFolder) {
        return leftFolder;
    }
    const std::string a = toLower(left.name);
    const std::string b = toLower(right.name);
    return a != b ? a < b : left.name < right.name;
}

bool isHidden(const std::filesystem::path& path) {
    const std::string name = path.filename().string();
    return name.empty() || name.front() == '.' || name == "Thumbs.db" || name == "desktop.ini";
}

const std::vector<AssetEntry>& emptyListing() {
    static const std::vector<AssetEntry> empty;
    return empty;
}
}

AssetDatabase::AssetDatabase(std::string root)
    : root_(std::move(root)) {
    while (root_.size() > 1 && (root_.back() == '/' || root_.back() == '\\')) {
        root_.pop_back();
    }
}

bool AssetDatabase::refresh() {
    std::unordered_map<std::string, std::vector<AssetEntry>> listings;
    std::size_t files = 0;
    std::uint64_t signature = 1469598103934665603ull;
    auto mix = [&signature](std::uint64_t value) {
        signature ^= value;
        signature *= 1099511628211ull;
    };

    std::error_code error;
    if (!std::filesystem::is_directory(root_, error)) {
        const bool changed = !listings_.empty();
        listings_.clear();
        fileCount_ = 0;
        if (changed) {
            ++generation_;
        }
        return changed;
    }

    std::function<void(const std::filesystem::path&, const std::string&)> scan =
        [&](const std::filesystem::path& directory, const std::string& key) {
        std::vector<AssetEntry>& entries = listings[key];
        std::error_code iterationError;
        for (std::filesystem::directory_iterator it(directory, std::filesystem::directory_options::skip_permission_denied, iterationError), end;
             !iterationError && it != end; it.increment(iterationError)) {
            const std::filesystem::path& path = it->path();
            if (isHidden(path)) {
                continue;
            }
            AssetEntry entry;
            entry.name = path.filename().string();
            entry.path = key + "/" + entry.name;
            std::error_code statusError;
            if (it->is_directory(statusError)) {
                entry.type = AssetType::Folder;
                entries.push_back(entry);
                mix(std::hash<std::string>{}(entry.path));
                scan(path, entry.path);
                continue;
            }
            if (!it->is_regular_file(statusError)) {
                continue;
            }
            entry.extension = toLower(path.extension().string());
            entry.type = classify(path);
            entry.size = it->file_size(statusError);
            entry.modified = it->last_write_time(statusError);
            entries.push_back(entry);
            ++files;
            mix(std::hash<std::string>{}(entry.path));
            mix(static_cast<std::uint64_t>(entry.size));
            mix(static_cast<std::uint64_t>(entry.modified.time_since_epoch().count()));
        }
        std::sort(entries.begin(), entries.end(), lessByName);
    };
    scan(root_, root_);

    const bool changed = signature != signature_ || listings.size() != listings_.size();
    listings_ = std::move(listings);
    fileCount_ = files;
    signature_ = signature;
    if (changed) {
        ++generation_;
    }
    return changed;
}

bool AssetDatabase::exists(const std::string& folder) const {
    return listings_.find(folder) != listings_.end();
}

const std::vector<AssetEntry>& AssetDatabase::list(const std::string& folder) const {
    auto it = listings_.find(folder);
    return it == listings_.end() ? emptyListing() : it->second;
}

std::vector<AssetEntry> AssetDatabase::subfolders(const std::string& folder) const {
    std::vector<AssetEntry> result;
    for (const AssetEntry& entry : list(folder)) {
        if (entry.type == AssetType::Folder) {
            result.push_back(entry);
        }
    }
    return result;
}

bool AssetDatabase::hasSubfolders(const std::string& folder) const {
    const auto& entries = list(folder);
    return std::any_of(entries.begin(), entries.end(), [](const AssetEntry& entry) { return entry.type == AssetType::Folder; });
}

std::vector<AssetEntry> AssetDatabase::search(const std::string& query, std::size_t limit) const {
    std::vector<AssetEntry> result;
    const std::string needle = toLower(query);
    if (needle.empty()) {
        return result;
    }
    for (const auto& [folder, entries] : listings_) {
        (void)folder;
        for (const AssetEntry& entry : entries) {
            if (entry.type != AssetType::Folder && toLower(entry.name).find(needle) != std::string::npos) {
                result.push_back(entry);
            }
        }
    }
    std::sort(result.begin(), result.end(), [](const AssetEntry& left, const AssetEntry& right) {
        const std::string a = toLower(left.name);
        const std::string b = toLower(right.name);
        return a != b ? a < b : left.path < right.path;
    });
    if (result.size() > limit) {
        result.resize(limit);
    }
    return result;
}

const AssetEntry* AssetDatabase::find(const std::string& path) const {
    const auto& entries = list(parentFolder(path));
    for (const AssetEntry& entry : entries) {
        if (entry.path == path) {
            return &entry;
        }
    }
    return nullptr;
}

AssetType AssetDatabase::classify(const std::filesystem::path& path) {
    const std::string extension = toLower(path.extension().string());
    static const char* textures[] = {".png", ".jpg", ".jpeg", ".tga", ".bmp", ".dds", ".hdr", ".psd", ".gif", ".pic", ".ppm", ".pgm", ".pnm"};
    static const char* models[] = {".fbx", ".obj", ".gltf", ".glb", ".dae", ".3ds", ".blend", ".ply", ".stl"};
    static const char* shaders[] = {".glsl", ".vert", ".frag", ".vs", ".fs", ".geom", ".comp", ".hlsl"};
    static const char* texts[] = {".txt", ".md", ".log", ".csv", ".ini", ".cfg", ".xml", ".yaml", ".yml"};
    static const char* fonts[] = {".ttf", ".otf"};
    static const char* audio[] = {".wav", ".mp3", ".ogg", ".flac"};
    static const char* scripts[] = {".lua", ".py", ".js"};
    auto matches = [&extension](const auto& list) {
        return std::any_of(std::begin(list), std::end(list), [&extension](const char* candidate) { return extension == candidate; });
    };
    if (matches(textures)) return AssetType::Texture;
    if (matches(models)) return AssetType::Model;
    if (extension == ".mtl") return AssetType::Material;
    if (matches(shaders)) return AssetType::Shader;
    if (extension == ".scene") return AssetType::Scene;
    if (extension == ".json") {
        // Манифесты сцен движка лежат в папке scenes, префабы — в prefabs.
        const std::string parent = toLower(path.parent_path().filename().string());
        if (parent == "scenes") return AssetType::Scene;
        if (parent == "prefabs") return AssetType::Prefab;
        return AssetType::Json;
    }
    if (matches(texts)) return AssetType::Text;
    if (matches(fonts)) return AssetType::Font;
    if (matches(audio)) return AssetType::Audio;
    if (matches(scripts)) return AssetType::Script;
    return AssetType::Other;
}

bool AssetDatabase::isLoadableTexture(const std::string& extension) {
    static const char* loadable[] = {".dds", ".png", ".jpg", ".jpeg", ".bmp", ".tga", ".gif", ".hdr", ".psd", ".pic"};
    const std::string value = toLower(extension);
    return std::any_of(std::begin(loadable), std::end(loadable), [&value](const char* candidate) { return value == candidate; });
}

const char* AssetDatabase::typeName(AssetType type) {
    switch (type) {
    case AssetType::Folder: return "Folder";
    case AssetType::Texture: return "Texture";
    case AssetType::Model: return "Model";
    case AssetType::Material: return "Material";
    case AssetType::Shader: return "Shader";
    case AssetType::Scene: return "Scene";
    case AssetType::Prefab: return "Prefab";
    case AssetType::Json: return "JSON";
    case AssetType::Text: return "Text";
    case AssetType::Font: return "Font";
    case AssetType::Audio: return "Audio";
    case AssetType::Script: return "Script";
    case AssetType::Other: return "File";
    }
    return "File";
}

std::string AssetDatabase::badgeText(const AssetEntry& entry) {
    switch (entry.type) {
    case AssetType::Folder: return "";
    case AssetType::Shader: return "GLSL";
    case AssetType::Scene: return "SCENE";
    case AssetType::Prefab: return "PREFAB";
    case AssetType::Material: return "MTL";
    case AssetType::Audio: return "AUDIO";
    case AssetType::Font: return "FONT";
    default:
        break;
    }
    std::string text = entry.extension.size() > 1 ? entry.extension.substr(1) : std::string("FILE");
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return text.size() > 5 ? text.substr(0, 5) : text;
}

std::string AssetDatabase::parentFolder(const std::string& path) {
    const std::size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

std::string AssetDatabase::formatSize(std::uintmax_t bytes) {
    char buffer[32] = {};
    if (bytes < 1024) {
        std::snprintf(buffer, sizeof(buffer), "%llu B", static_cast<unsigned long long>(bytes));
    } else if (bytes < 1024ull * 1024ull) {
        std::snprintf(buffer, sizeof(buffer), "%.1f KB", static_cast<double>(bytes) / 1024.0);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.2f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    }
    return buffer;
}
