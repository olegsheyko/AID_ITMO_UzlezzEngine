#pragma once

#include <array>
#include <string>
#include <vector>

// Выбор текстуры в стиле Object Picker: сетка превью с поиском во всплывающем окне.
class TexturePicker {
public:
    // Открывает окно выбора на текущем кадре; current — подсветить уже назначенную текстуру.
    void open(const std::string& current);
    // Рисует окно, если оно открыто. true — выбор сделан: outPath — путь или пустая строка для «без текстуры».
    bool draw(std::string& outPath);

private:
    std::array<char, 128> search_{};
    std::vector<std::string> paths_;
    std::string current_;
    bool openRequested_ = false;
    bool scrollToCurrent_ = false;
};
