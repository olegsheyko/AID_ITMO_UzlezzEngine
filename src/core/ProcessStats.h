#pragma once

#include <cstdint>

// Сколько памяти занимает процесс, байт; 0 — если платформа не поддержана.
// macOS: phys_footprint — то же, что «Память» в Мониторинге системы; на Apple Silicon
// сюда попадают и GPU-текстуры, память общая. Windows: PrivateUsage. Linux: RSS.
std::uint64_t processMemoryFootprintBytes();
