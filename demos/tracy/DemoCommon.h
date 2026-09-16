#pragma once

#include <imgui.h>
#include <tracy/Tracy.hpp>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <d3d12.h>
#include <wrl/client.h>
#include <fstream>
#include <vector>

struct DemoOptions {
    bool hidden = false;
    bool automatic = false;
    bool help = false;
    bool valid = true;
    double seconds = 0;
    const char* screenshot = nullptr;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();

    DemoOptions(int argc, char** argv) {
        for (int i = 1; i < argc; ++i) {
            if (std::strcmp(argv[i], "--hidden") == 0) hidden = true;
            else if (std::strcmp(argv[i], "--auto") == 0) automatic = true;
            else if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) screenshot = argv[++i];
            else if (std::strcmp(argv[i], "--help") == 0) { help = true; valid = false; }
            else if (std::strcmp(argv[i], "--seconds") == 0 && i + 1 < argc) {
                char* end = nullptr;
                seconds = std::strtod(argv[++i], &end);
                if (*end != '\0' || !std::isfinite(seconds) || seconds <= 0) valid = false;
            } else valid = false;
        }
        if (!valid) std::printf("Usage: demo_* [--seconds N] [--auto] [--hidden] [--screenshot file.bmp]\n"
            "No arguments: interactive demo. --auto exercises controls without user input.\n");
    }
    double elapsed() const {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    }
    bool expired() const { return seconds > 0 && elapsed() >= seconds; }
};

inline void DemoBurn(int iterations) {
    volatile double result = 0;
    for (int i = 0; i < iterations; ++i) result += i * 0.000001;
}

inline void DemoPanel(const char* title, const char* number, const char* instructions) {
    ImGui::SetNextWindowPos(ImVec2(24, 24), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(650, 320), ImGuiCond_FirstUseEver);
    ImGui::Begin(title);
    ImGui::Text("TRACY LIVE DEMO %s", number);
#ifdef TRACY_ENABLE
    ImGui::Text("Tracy 0.14.1 | %s", TracyIsConnected ? "Connected" : "Connect profiler to 127.0.0.1:8086");
#else
    ImGui::TextUnformatted("Profiling disabled. Build Release with ENGINE_ENABLE_TRACY=ON.");
#endif
    ImGui::TextWrapped("%s", instructions);
    ImGui::Text("%.2f ms/frame | %.0f FPS", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
    ImGui::Separator();
}

// Optional one-shot readback for reproducible visual checks. Not used during a live demo.
inline bool DemoScreenshot(ID3D12Device* device, ID3D12CommandQueue* queue, ID3D12Resource* image, const char* path) {
    using Microsoft::WRL::ComPtr;
    const auto desc = image->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 size = 0;
    device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &size);
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC buffer{};
    buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Width = size;
    buffer.Height = 1;
    buffer.DepthOrArraySize = 1;
    buffer.MipLevels = 1;
    buffer.SampleDesc.Count = 1;
    buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> readback;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence;
    if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback))) ||
        FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))) ||
        FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list))) ||
        FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) return false;
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition = {image, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_SOURCE};
    list->ResourceBarrier(1, &barrier);
    D3D12_TEXTURE_COPY_LOCATION from{};
    from.pResource = image;
    from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION to{};
    to.pResource = readback.Get();
    to.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    to.PlacedFootprint = footprint;
    list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    list->ResourceBarrier(1, &barrier);
    if (FAILED(list->Close())) return false;
    HANDLE ready = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!ready) return false;
    ID3D12CommandList* submitted[] = {list.Get()};
    queue->ExecuteCommandLists(1, submitted);
    const bool queued = SUCCEEDED(queue->Signal(fence.Get(), 1)) && SUCCEEDED(fence->SetEventOnCompletion(1, ready));
    const bool complete = queued && WaitForSingleObject(ready, INFINITE) == WAIT_OBJECT_0;
    CloseHandle(ready);
    if (!complete) return false;
    void* mapped = nullptr;
    if (FAILED(readback->Map(0, nullptr, &mapped))) return false;
    std::vector<unsigned char> pixels(static_cast<size_t>(desc.Width) * desc.Height * 4);
    for (UINT y = 0; y < desc.Height; ++y) {
        const auto* row = static_cast<const unsigned char*>(mapped) + footprint.Offset + size_t(y) * footprint.Footprint.RowPitch;
        for (UINT x = 0; x < desc.Width; ++x) {
            const size_t pixel = (size_t(y) * desc.Width + x) * 4;
            pixels[pixel] = row[x * 4 + 2]; pixels[pixel + 1] = row[x * 4 + 1];
            pixels[pixel + 2] = row[x * 4]; pixels[pixel + 3] = 255;
        }
    }
    readback->Unmap(0, nullptr);
    BITMAPFILEHEADER file{};
    file.bfType = 0x4D42;
    file.bfOffBits = sizeof(file) + sizeof(BITMAPINFOHEADER);
    file.bfSize = file.bfOffBits + static_cast<DWORD>(pixels.size());
    BITMAPINFOHEADER info{};
    info.biSize = sizeof(info); info.biWidth = static_cast<LONG>(desc.Width); info.biHeight = -static_cast<LONG>(desc.Height);
    info.biPlanes = 1; info.biBitCount = 32; info.biCompression = BI_RGB;
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(&file), sizeof(file));
    output.write(reinterpret_cast<const char*>(&info), sizeof(info));
    output.write(reinterpret_cast<const char*>(pixels.data()), pixels.size());
    return output.good();
}
