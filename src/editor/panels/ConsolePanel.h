#pragma once

#include "core/Logger.h"

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class ConsolePanel {
public:
    // Забирает новые строки лога; вызывается каждый кадр, даже когда окно закрыто, — для статус-бара.
    void poll();
    void draw();
    void clear();

    std::size_t count(Logger::Level level) const;
    const Logger::Entry* latest() const { return entries_.empty() ? nullptr : &entries_.back(); }

    bool open = true;
    bool collapse = false;
    bool autoScroll = true;
    bool showInfo = true;
    bool showWarnings = true;
    bool showErrors = true;

private:
    struct Row {
        std::size_t entry = 0;
        int repeat = 1;
    };

    bool passes(const Logger::Entry& entry) const;
    void rebuildRows();
    void drawToolbar();
    void drawRows(float height);
    void drawDetails();

    std::vector<Logger::Entry> entries_;
    std::uint64_t cursor_ = 0;
    std::array<std::size_t, 3> counts_{};
    std::vector<Row> rows_;
    std::unordered_map<std::string, std::size_t> collapsedIndex_;
    std::array<char, 128> search_{};
    std::string lastSearch_;
    bool lastCollapse_ = false;
    bool lastFilters_[3] = {true, true, true};
    bool dirty_ = true;
    bool scrollToBottom_ = false;
    std::size_t selected_ = SIZE_MAX;
    float detailsHeight_ = 86.0f;
};
