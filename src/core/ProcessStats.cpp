#include "core/ProcessStats.h"

// Сюда нельзя подключать core/Logger.h: windows.h определяет макрос ERROR,
// который ломает Logger::Level::ERROR.
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#if defined(_MSC_VER)
#pragma comment(lib, "psapi.lib")
#endif
#elif defined(__APPLE__)
#include <mach/mach.h>
#elif defined(__linux__)
#include <cstdio>
#include <unistd.h>
#endif

std::uint64_t processMemoryFootprintBytes() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS_EX counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters))) {
        return static_cast<std::uint64_t>(counters.PrivateUsage);
    }
    return 0;
#elif defined(__APPLE__)
    task_vm_info_data_t info{};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS) {
        return static_cast<std::uint64_t>(info.phys_footprint);
    }
    return 0;
#elif defined(__linux__)
    long pages = 0;
    long resident = 0;
    if (FILE* file = std::fopen("/proc/self/statm", "r")) {
        if (std::fscanf(file, "%ld %ld", &pages, &resident) != 2) {
            resident = 0;
        }
        std::fclose(file);
    }
    return static_cast<std::uint64_t>(resident) * static_cast<std::uint64_t>(sysconf(_SC_PAGESIZE));
#else
    return 0;
#endif
}
