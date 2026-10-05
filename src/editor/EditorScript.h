#pragma once

#include <string>
#include <vector>

// Сценарий ввода для проверки редактора без человека: по кадрам подаёт ImGui мышь и клавиатуру
// и просит снять скриншот. Формат — строка на шаг: "<кадр> <действие> [аргументы]".
//   move x y | down b | up b | click x y | drag x1 y1 x2 y2 | wheel dy | key Name | key ctrl+D | text строка
//   shot файл.png | quit
// Координаты — в точках окна, кнопки мыши 0/1/2. '#' — комментарий.
class EditorScript {
public:
    bool load(const std::string& path, std::string& outError);
    bool empty() const { return steps_.empty(); }
    // Вызывается до NewFrame: ставит в очередь ImGui события этого кадра.
    void apply(int frame);
    // Скриншоты, запрошенные на этом кадре (снимаются после рендера кадра).
    std::vector<std::string> screenshots(int frame) const;
    bool quitAt(int frame) const;

private:
    struct Step {
        int frame = 0;
        std::string action;
        std::vector<std::string> args;
    };
    // Отложенное событие: клик разносится на кадры «навести — нажать — отпустить», как у живой руки.
    struct Pending {
        int frame = 0;
        int mouseButton = -1;
        int key = 0;
        bool down = false;
    };
    std::vector<Step> steps_;
    std::vector<Pending> pending_;
};
