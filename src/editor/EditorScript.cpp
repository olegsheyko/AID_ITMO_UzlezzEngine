#include "editor/EditorScript.h"

#include "input/InputManager.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <utility>

namespace {
// Отложенное отпускание клавиши игры, а не ImGui.
constexpr int kGameKey = -2;

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

// Та же клавиша для игры: InputManager опрашивает GLFW, и событий ImGui он не видит.
KeyCode gameKeyByName(const std::string& name) {
    static const std::pair<const char*, KeyCode> keys[] = {{"space", KeyCode::Space}, {"enter", KeyCode::Enter},
        {"left", KeyCode::Left}, {"right", KeyCode::Right}, {"up", KeyCode::Up}, {"down", KeyCode::Down},
        {"w", KeyCode::W}, {"a", KeyCode::A}, {"s", KeyCode::S}, {"d", KeyCode::D}, {"q", KeyCode::Q},
        {"e", KeyCode::E}, {"f", KeyCode::F}, {"i", KeyCode::I}, {"j", KeyCode::J}, {"k", KeyCode::K},
        {"l", KeyCode::L}, {"u", KeyCode::U}, {"o", KeyCode::O}};
    const std::string wanted = lower(name);
    for (const auto& [keyName, code] : keys) {
        if (wanted == keyName) {
            return code;
        }
    }
    return KeyCode::Unknown;
}

ImGuiKey keyByName(const std::string& name) {
    const std::string wanted = lower(name);
    for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END; ++key) {
        if (lower(ImGui::GetKeyName(static_cast<ImGuiKey>(key))) == wanted) {
            return static_cast<ImGuiKey>(key);
        }
    }
    return ImGuiKey_None;
}

// «ctrl» в сценарии — модификатор шорткатов платформы: на macOS это Cmd, который ImGui сам превращает в Ctrl.
int modifierByName(const std::string& name) {
    const std::string value = lower(name);
    if (value == "ctrl" || value == "cmd") {
#ifdef __APPLE__
        return ImGuiMod_Super;
#else
        return ImGuiMod_Ctrl;
#endif
    }
    if (value == "shift") return ImGuiMod_Shift;
    if (value == "alt") return ImGuiMod_Alt;
    return 0;
}

float number(const std::vector<std::string>& args, std::size_t index) {
    return index < args.size() ? std::strtof(args[index].c_str(), nullptr) : 0.0f;
}
}

bool EditorScript::load(const std::string& path, std::string& outError) {
    std::ifstream file(path);
    if (!file) {
        outError = "cannot open editor script " + path;
        return false;
    }
    std::string line;
    int lineNumber = 0;
    while (std::getline(file, line)) {
        ++lineNumber;
        const std::size_t hash = line.find('#');
        if (hash != std::string::npos) {
            line.erase(hash);
        }
        std::istringstream stream(line);
        Step step;
        if (!(stream >> step.frame >> step.action)) {
            continue;
        }
        if (step.action == "text") {
            std::string rest;
            std::getline(stream, rest);
            step.args.push_back(rest.empty() ? rest : rest.substr(1));
        } else {
            std::string argument;
            while (stream >> argument) {
                step.args.push_back(argument);
            }
        }
        steps_.push_back(step);
    }
    std::stable_sort(steps_.begin(), steps_.end(), [](const Step& a, const Step& b) { return a.frame < b.frame; });
    return true;
}

void EditorScript::apply(int frame) {
    ImGuiIO& io = ImGui::GetIO();
    for (auto it = pending_.begin(); it != pending_.end();) {
        if (it->frame > frame) {
            ++it;
            continue;
        }
        if (it->mouseButton == kGameKey) {
            InputManager::getInstance().setSimulatedKey(static_cast<KeyCode>(it->key), it->down);
        } else if (it->mouseButton >= 0) {
            io.AddMouseButtonEvent(it->mouseButton, it->down);
        } else {
            io.AddKeyEvent(static_cast<ImGuiKey>(it->key), it->down);
        }
        it = pending_.erase(it);
    }
    for (const Step& step : steps_) {
        if (step.frame != frame) {
            continue;
        }
        const auto& args = step.args;
        if (step.action == "move") {
            io.AddMousePosEvent(number(args, 0), number(args, 1));
        } else if (step.action == "down") {
            io.AddMouseButtonEvent(static_cast<int>(number(args, 0)), true);
        } else if (step.action == "up") {
            io.AddMouseButtonEvent(static_cast<int>(number(args, 0)), false);
        } else if (step.action == "click" || step.action == "rclick" || step.action == "dclick") {
            const int button = step.action == "rclick" ? 1 : 0;
            io.AddMousePosEvent(number(args, 0), number(args, 1));
            pending_.push_back({frame + 2, button, 0, true});
            pending_.push_back({frame + 3, button, 0, false});
            if (step.action == "dclick") {
                pending_.push_back({frame + 4, button, 0, true});
                pending_.push_back({frame + 5, button, 0, false});
            }
        } else if (step.action == "wheel") {
            io.AddMouseWheelEvent(0.0f, number(args, 0));
        } else if (step.action == "key" && !args.empty()) {
            std::stringstream combo(args[0]);
            std::string part;
            std::vector<std::string> parts;
            while (std::getline(combo, part, '+')) {
                parts.push_back(part);
            }
            for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
                if (const int modifier = modifierByName(parts[i])) {
                    io.AddKeyEvent(static_cast<ImGuiKey>(modifier), true);
                    // Модификатор отпускаем после клавиши, чтобы шорткат успел сработать.
                    pending_.push_back({frame + 2, -1, modifier, false});
                }
            }
            const std::string name = parts.empty() ? std::string() : parts.back();
            const ImGuiKey key = keyByName(name);
            if (key != ImGuiKey_None) {
                io.AddKeyEvent(key, true);
                pending_.push_back({frame + 1, -1, key, false});
            }
            // Без модификаторов клавиша уходит и в игру; держим два кадра, чтобы опрос ввода её застал.
            const KeyCode gameKey = gameKeyByName(name);
            if (parts.size() == 1 && gameKey != KeyCode::Unknown) {
                InputManager::getInstance().setSimulatedKey(gameKey, true);
                pending_.push_back({frame + 2, kGameKey, static_cast<int>(gameKey), false});
            }
        } else if (step.action == "text" && !args.empty()) {
            io.AddInputCharactersUTF8(args[0].c_str());
        }
    }
}

std::vector<std::string> EditorScript::screenshots(int frame) const {
    std::vector<std::string> result;
    for (const Step& step : steps_) {
        if (step.frame == frame && step.action == "shot" && !step.args.empty()) {
            result.push_back(step.args[0]);
        }
    }
    return result;
}

bool EditorScript::quitAt(int frame) const {
    return std::any_of(steps_.begin(), steps_.end(), [frame](const Step& step) { return step.frame == frame && step.action == "quit"; });
}
