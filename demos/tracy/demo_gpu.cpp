// Adapted from damibran/tracy_samples, commit 721e9ac63443cabebec1d2514585cab1dae5eece.
// See README.md for provenance and local changes.
// Dear ImGui: standalone example application for Windows API + DirectX 12

// Learn about Dear ImGui:
// - FAQ                  https://dearimgui.com/faq
// - Getting Started      https://dearimgui.com/getting-started
// - Documentation        https://dearimgui.com/docs (same as your local docs/ folder).
// - Introduction, links and more at the top of imgui.cpp

#include "imgui.h"
#include "DemoCommon.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx12.h"
#include <d3d12.h>
#include <dxgi1_5.h>
#include <d3dcompiler.h>
#include <tchar.h>
#include <cstring>
#include <vector>
#pragma comment(lib, "d3dcompiler.lib")
#include <tracy/Tracy.hpp>
#include <tracy/TracyD3D12.hpp>

#ifdef _DEBUG
#define DX12_ENABLE_DEBUG_LAYER
#endif

#ifdef DX12_ENABLE_DEBUG_LAYER
#include <dxgidebug.h>
#pragma comment(lib, "dxguid.lib")
#endif

// Config for example app
static const int APP_NUM_FRAMES_IN_FLIGHT = 1;
static const int APP_NUM_BACK_BUFFERS = 2;
static const int APP_SRV_HEAP_SIZE = 64;

struct FrameContext
{
    ID3D12CommandAllocator* CommandAllocator;
    UINT64                      FenceValue;
};

// Simple free list based allocator
struct ExampleDescriptorHeapAllocator
{
    ID3D12DescriptorHeap* Heap = nullptr;
    D3D12_DESCRIPTOR_HEAP_TYPE  HeapType = D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
    D3D12_CPU_DESCRIPTOR_HANDLE HeapStartCpu;
    D3D12_GPU_DESCRIPTOR_HANDLE HeapStartGpu;
    UINT                        HeapHandleIncrement;
    ImVector<int>               FreeIndices;

    void Create(ID3D12Device* device, ID3D12DescriptorHeap* heap)
    {
        IM_ASSERT(Heap == nullptr && FreeIndices.empty());
        Heap = heap;
        D3D12_DESCRIPTOR_HEAP_DESC desc = heap->GetDesc();
        HeapType = desc.Type;
        HeapStartCpu = Heap->GetCPUDescriptorHandleForHeapStart();
        HeapStartGpu = Heap->GetGPUDescriptorHandleForHeapStart();
        HeapHandleIncrement = device->GetDescriptorHandleIncrementSize(HeapType);
        FreeIndices.reserve((int)desc.NumDescriptors);
        for (int n = desc.NumDescriptors; n > 0; n--)
            FreeIndices.push_back(n - 1);
    }
    void Destroy()
    {
        Heap = nullptr;
        FreeIndices.clear();
    }
    void Alloc(D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_desc_handle, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_desc_handle)
    {
        IM_ASSERT(FreeIndices.Size > 0);
        int idx = FreeIndices.back();
        FreeIndices.pop_back();
        out_cpu_desc_handle->ptr = HeapStartCpu.ptr + (idx * HeapHandleIncrement);
        out_gpu_desc_handle->ptr = HeapStartGpu.ptr + (idx * HeapHandleIncrement);
    }
    void Free(D3D12_CPU_DESCRIPTOR_HANDLE out_cpu_desc_handle, D3D12_GPU_DESCRIPTOR_HANDLE out_gpu_desc_handle)
    {
        int cpu_idx = (int)((out_cpu_desc_handle.ptr - HeapStartCpu.ptr) / HeapHandleIncrement);
        int gpu_idx = (int)((out_gpu_desc_handle.ptr - HeapStartGpu.ptr) / HeapHandleIncrement);
        IM_ASSERT(cpu_idx == gpu_idx);
        FreeIndices.push_back(cpu_idx);
    }
};

// Data
static FrameContext                 g_frameContext[APP_NUM_FRAMES_IN_FLIGHT] = {};
static UINT                         g_frameIndex = 0;

static ID3D12Device* g_pd3dDevice = nullptr;
static ID3D12DescriptorHeap* g_pd3dRtvDescHeap = nullptr;
static ID3D12DescriptorHeap* g_pd3dSrvDescHeap = nullptr;
static ExampleDescriptorHeapAllocator g_pd3dSrvDescHeapAlloc;
static ID3D12CommandQueue* g_pd3dCommandQueue = nullptr;
static ID3D12GraphicsCommandList* g_pd3dCommandList = nullptr;
static ID3D12Fence* g_fence = nullptr;
static HANDLE                       g_fenceEvent = nullptr;
static UINT64                       g_fenceLastSignaledValue = 0;
static IDXGISwapChain3* g_pSwapChain = nullptr;
static bool                         g_SwapChainTearingSupport = false;
static bool                         g_SwapChainOccluded = false;
static HANDLE                       g_hSwapChainWaitableObject = nullptr;
static ID3D12Resource* g_mainRenderTargetResource[APP_NUM_BACK_BUFFERS] = {};
static D3D12_CPU_DESCRIPTOR_HANDLE  g_mainRenderTargetDescriptor[APP_NUM_BACK_BUFFERS] = {};

// --- Демо 5/5 · GPU-зоны + FrameImage ---
static TracyD3D12Ctx g_tracyGpuCtx = nullptr;   // GPU-контекст Tracy (один на очередь)
static ID3D12Resource* g_readbackBuffer = nullptr;      // readback-буфер под снимок бэкбуфера
static D3D12_PLACED_SUBRESOURCE_FOOTPRINT g_readbackFootprint = {};
static UINT g_backbufferWidth = 0, g_backbufferHeight = 0;
static bool g_readbackPending = false;                  // в этом кадре скопировали бэкбуфер
static bool g_frameImageCapture = true;                 // вкл/выкл захват кадров (чекбокс в UI)
static const int FRAME_IMAGE_W = 320;                   // Tracy масштабирует картинку сам, ~320 px достаточно
static const int FRAME_IMAGE_H = 200;
static std::vector<uint8_t> g_frameImagePixels;         // RGBA8, даунскейл для FrameImage

// --- Нагрузочный GPU-пасс: фуллскрин-шейдер с тяжёлым циклом (Мандельброт) ---
static ID3D12RootSignature* g_gpuLoadRootSig = nullptr;
static ID3D12PipelineState* g_gpuLoadPso = nullptr;
static int                  g_gpuLoadIters = 150;       // итераций на пиксель; 0 = пасс выключен (слайдер в UI)

// Forward declarations of helper functions
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
void WaitForPendingOperations();
FrameContext* WaitForNextFrameContext();
void EnsureReadbackBuffer(ID3D12Resource* backbuffer);
void SendFrameImage();
bool CreateGpuLoadPass();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Main code
int main(int argc, char** argv)
{
    DemoOptions options(argc, argv);
    if (!options.valid) return options.help ? 0 : 2;
    tracy::SetThreadName("Main");
    TracySetProgramName("demo_gpu");
    // Make process DPI aware and obtain main monitor scale
    ImGui_ImplWin32_EnableDpiAwareness();
    float main_scale = ImGui_ImplWin32_GetDpiScaleForMonitor(::MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY));

    // Create application window
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"ImGui Example", nullptr };
    ::RegisterClassExW(&wc);
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"demo_gpu - Tracy / ImGui / D3D12", WS_OVERLAPPEDWINDOW, 100, 100, (int)(1000 * main_scale), (int)(700 * main_scale), nullptr, nullptr, wc.hInstance, nullptr);

    // Initialize Direct3D
    if (!CreateDeviceD3D(hwnd))
    {
        std::fprintf(stderr, "D3D12 device/swapchain initialization failed\n");
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    // Show the window
    if (!options.hidden) ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    // --- Демо 5/5: GPU-контекст создаётся один раз, после device и очереди ---
    g_tracyGpuCtx = TracyD3D12Context(g_pd3dDevice, g_pd3dCommandQueue);
    TracyD3D12ContextName(g_tracyGpuCtx, "D3D12 graphics", 14);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsLight();

    // Setup scaling
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(main_scale);        // Bake a fixed style scale. (until we have a solution for dynamic style scaling, changing this requires resetting Style + calling this again)

    // Setup Platform/Renderer backends
    ImGui_ImplWin32_Init(hwnd);

    ImGui_ImplDX12_InitInfo init_info = {};
    init_info.Device = g_pd3dDevice;
    init_info.CommandQueue = g_pd3dCommandQueue;
    init_info.NumFramesInFlight = APP_NUM_FRAMES_IN_FLIGHT;
    init_info.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    init_info.DSVFormat = DXGI_FORMAT_UNKNOWN;
    // Allocating SRV descriptors (for textures) is up to the application, so we provide callbacks.
    // (current version of the backend will only allocate one descriptor, future versions will need to allocate more)
    init_info.SrvDescriptorHeap = g_pd3dSrvDescHeap;
    init_info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_handle, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_handle) { return g_pd3dSrvDescHeapAlloc.Alloc(out_cpu_handle, out_gpu_handle); };
    init_info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle, D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle) { return g_pd3dSrvDescHeapAlloc.Free(cpu_handle, gpu_handle); };
    if (!ImGui_ImplDX12_Init(&init_info)) { std::fprintf(stderr, "ImGui D3D12 init failed\n"); return 1; }

    // Before 1.91.6: our signature was using a single descriptor. From 1.92, specifying SrvDescriptorAllocFn/SrvDescriptorFreeFn will be required to benefit from new features.
    //ImGui_ImplDX12_Init(g_pd3dDevice, APP_NUM_FRAMES_IN_FLIGHT, DXGI_FORMAT_R8G8B8A8_UNORM, g_pd3dSrvDescHeap, g_pd3dSrvDescHeap->GetCPUDescriptorHandleForHeapStart(), g_pd3dSrvDescHeap->GetGPUDescriptorHandleForHeapStart());

    // Load Fonts
    // - If fonts are not explicitly loaded, Dear ImGui will select an embedded font: either AddFontDefaultVector() or AddFontDefaultBitmap().
    //   This selection is based on (style.FontSizeBase * style.FontScaleMain * style.FontScaleDpi) reaching a small threshold.
    // - You can load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
    // - If a file cannot be loaded, AddFont functions will return a nullptr. Please handle those errors in your code (e.g. use an assertion, display an error and quit).
    // - Read 'docs/FONTS.md' for more instructions and details.
    // - Use '#define IMGUI_ENABLE_FREETYPE' in your imconfig file to use FreeType for higher quality font rendering.
    // - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
    //style.FontSizeBase = 20.0f;
    //io.Fonts->AddFontDefaultVector();
    //io.Fonts->AddFontDefaultBitmap();
    //io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\segoeui.ttf");
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf");
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf");
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf");
    //ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf");
    //IM_ASSERT(font != nullptr);

    // Our state
    bool show_demo_window = false;
    bool show_another_window = false;
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    // Main loop
    bool done = false;
    int exitCode = 0;
    while (!done && !options.expired())
    {
        ZoneScopedN("Frame");
        TracyD3D12NewFrame(g_tracyGpuCtx);   // новая «рамка» GPU-таймлайна для этого кадра

        // Poll and handle messages (inputs, window resize, etc.)
        // See the WndProc() function below for our to dispatch events to the Win32 backend.
        {
            ZoneScopedN("PeekMessage");
            MSG msg;
            while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
            {
                ::TranslateMessage(&msg);
                ::DispatchMessage(&msg);
                if (msg.message == WM_QUIT)
                    done = true;
            }
            if (done)
                break;
        }

        // Handle window screen locked
        if (!options.hidden && ((g_SwapChainOccluded && g_pSwapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) || ::IsIconic(hwnd)))
        {
            ::Sleep(10);
            continue;
        }
        g_SwapChainOccluded = false;

        // Start the Dear ImGui frame
        {
            ZoneScopedN("ImGuiFrame");
            ImGui_ImplDX12_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();


            DemoPanel("demo_gpu | D3D12 GPU zones", "5 / 5", "Expand the D3D12 graphics queue. Compare CPU GpuLoadPass with GPU load shader.");
            ImGui::SliderInt("GPU iterations per pixel", &g_gpuLoadIters, 0, 1200);
            ImGui::Checkbox("Frame images (one per 60 frames)", &g_frameImageCapture);
            ImGui::TextUnformatted("0 disables the fractal pass. Increase gradually and inspect GPU duration.");
            ImGui::TextUnformatted("Frame images use synchronous readback in this teaching demo.");
            if (options.automatic) g_gpuLoadIters = std::fmod(options.elapsed(), 4.0) < 2.0 ? 100 : 500;
            ImGui::End();
        }

        // Simulated game workload for profiling demo
        {
            ZoneScopedNC("GameLogic", tracy::Color::Crimson); // red zone with name
            // Simulate some work so the zone is visible in Tracy
            volatile double dummy = 0.0;
            for (int i = 0; i < 50000; ++i)
                dummy += i * 0.000001;
            (void)dummy;
        }

        // Rendering
        {
            ZoneScopedN("Render");
            ImGui::Render();

            FrameContext* frameCtx = WaitForNextFrameContext();
            UINT backBufferIdx = g_pSwapChain->GetCurrentBackBufferIndex();
            frameCtx->CommandAllocator->Reset();

            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            barrier.Transition.pResource = g_mainRenderTargetResource[backBufferIdx];
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
            g_pd3dCommandList->Reset(frameCtx->CommandAllocator, nullptr);
            g_pd3dCommandList->ResourceBarrier(1, &barrier);

            // Render Dear ImGui graphics
            const float clear_color_with_alpha[4] = { clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w };
            {
                // --- Демо 5/5: GPU-зона. Ляжет на тот же таймлайн, что и CPU-зоны ---
                TracyD3D12Zone(g_tracyGpuCtx, g_pd3dCommandList, "Clear + setup");
                g_pd3dCommandList->ClearRenderTargetView(g_mainRenderTargetDescriptor[backBufferIdx], clear_color_with_alpha, 0, nullptr);
                g_pd3dCommandList->OMSetRenderTargets(1, &g_mainRenderTargetDescriptor[backBufferIdx], FALSE, nullptr);
                g_pd3dCommandList->SetDescriptorHeaps(1, &g_pd3dSrvDescHeap);
            }
            {
                // --- Нагрузочный GPU-пасс: фуллскрин-фрактал, жрёт GPU пропорционально g_gpuLoadIters ---
                if (g_gpuLoadIters > 0 && g_gpuLoadPso)
                {
                    TracyD3D12Zone(g_tracyGpuCtx, g_pd3dCommandList, "GPU load shader");
                    ZoneScopedN("GpuLoadPass");   // CPU-сторона того же пасса

                    D3D12_RESOURCE_DESC bbDesc = g_mainRenderTargetResource[backBufferIdx]->GetDesc();
                    D3D12_VIEWPORT vp = { 0.f, 0.f, (float)bbDesc.Width, (float)bbDesc.Height, 0.f, 1.f };
                    D3D12_RECT scissor = { 0, 0, (LONG)bbDesc.Width, (LONG)bbDesc.Height };
                    g_pd3dCommandList->RSSetViewports(1, &vp);
                    g_pd3dCommandList->RSSetScissorRects(1, &scissor);

                    float params[4] = { (float)ImGui::GetTime(), (float)g_gpuLoadIters, (float)bbDesc.Width, (float)bbDesc.Height };
                    g_pd3dCommandList->SetPipelineState(g_gpuLoadPso);
                    g_pd3dCommandList->SetGraphicsRootSignature(g_gpuLoadRootSig);
                    g_pd3dCommandList->SetGraphicsRoot32BitConstants(0, 4, params, 0);
                    g_pd3dCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                    g_pd3dCommandList->DrawInstanced(3, 1, 0, 0);   // один фуллскрин-треугольник
                }
            }
            {
                TracyD3D12Zone(g_tracyGpuCtx, g_pd3dCommandList, "ImGui render");
                ZoneScopedN("ImGui_ImplDX12_RenderDrawData");   // CPU-зона рядом — удобно сравнивать
                ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_pd3dCommandList);
            }

            // --- FrameImage: раз в 60 кадров копируем бэкбуфер в readback-буфер ---
            const bool captureThisFrame = g_frameImageCapture && (g_frameIndex % 60 == 0);
            if (captureThisFrame)
            {
                EnsureReadbackBuffer(g_mainRenderTargetResource[backBufferIdx]);

                barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
                barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
                g_pd3dCommandList->ResourceBarrier(1, &barrier);

                D3D12_TEXTURE_COPY_LOCATION dst = {};
                dst.pResource = g_readbackBuffer;
                dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                dst.PlacedFootprint = g_readbackFootprint;
                D3D12_TEXTURE_COPY_LOCATION src = {};
                src.pResource = g_mainRenderTargetResource[backBufferIdx];
                src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                src.SubresourceIndex = 0;
                g_pd3dCommandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

                barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
                barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
                g_readbackPending = true;
            }
            else
            {
                barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
                barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
            }
            g_pd3dCommandList->ResourceBarrier(1, &barrier);
            g_pd3dCommandList->Close();

            g_pd3dCommandQueue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&g_pd3dCommandList);
            g_pd3dCommandQueue->Signal(g_fence, ++g_fenceLastSignaledValue);
            frameCtx->FenceValue = g_fenceLastSignaledValue;

            // Дожидаемся GPU и отправляем картинку в Tracy.
            // NB: это синхронный readback — в проде делают асинхронно через ring buffer,
            // но раз в 60 кадров один фенс-вейт на демо не страшен (и сам по себе нагляден).
            if (g_readbackPending)
            {
                g_readbackPending = false;
                g_fence->SetEventOnCompletion(g_fenceLastSignaledValue, g_fenceEvent);
                ::WaitForSingleObject(g_fenceEvent, INFINITE);
                SendFrameImage();
            }
        }

        if (options.screenshot && g_frameIndex == 3) {
            const bool saved = DemoScreenshot(g_pd3dDevice, g_pd3dCommandQueue,
                g_mainRenderTargetResource[g_pSwapChain->GetCurrentBackBufferIndex()], options.screenshot);
            if (!saved) { std::fprintf(stderr, "Screenshot failed\n"); exitCode = 1; break; }
        }

        // Present
        {
            ZoneScopedN("Present");
            //HRESULT hr = g_pSwapChain->Present(1, 0);   // Present with vsync
            HRESULT hr = g_pSwapChain->Present(0, g_SwapChainTearingSupport ? DXGI_PRESENT_ALLOW_TEARING : 0); // Present without vsync
            g_SwapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);
            g_frameIndex++;
            FrameMark;
            if (options.hidden) ::Sleep(8);
        }

        TracyPlot("FPS", io.Framerate);
        TracyPlot("GPU iter", (float) g_gpuLoadIters);
        TracyD3D12Collect(g_tracyGpuCtx);   // забираем результаты GPU-таймстампов в конце кадра
    }

    WaitForPendingOperations();

    // Cleanup
    TracyD3D12Destroy(g_tracyGpuCtx);
    g_tracyGpuCtx = nullptr;
    if (g_readbackBuffer) { g_readbackBuffer->Release(); g_readbackBuffer = nullptr; }
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

    std::printf("demo_gpu: completed %u frames\n", g_frameIndex);
    return exitCode;
}

// Helper functions

bool CreateDeviceD3D(HWND hWnd)
{
    // Setup swap chain
    // This is a basic setup. Optimally could handle fullscreen mode differently. See #8979 for suggestions.
    DXGI_SWAP_CHAIN_DESC1 sd;
    {
        ZeroMemory(&sd, sizeof(sd));
        sd.BufferCount = APP_NUM_BACK_BUFFERS;
        sd.Width = 0;
        sd.Height = 0;
        sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.SampleDesc.Count = 1;
        sd.SampleDesc.Quality = 0;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        sd.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
        sd.Scaling = DXGI_SCALING_STRETCH;
        sd.Stereo = FALSE;
    }

    // [DEBUG] Enable debug interface
#ifdef DX12_ENABLE_DEBUG_LAYER
    ID3D12Debug* pdx12Debug = nullptr;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&pdx12Debug))))
        pdx12Debug->EnableDebugLayer();
#endif

    // Create device
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
    if (D3D12CreateDevice(nullptr, featureLevel, IID_PPV_ARGS(&g_pd3dDevice)) != S_OK)
        return false;

    // [DEBUG] Setup debug interface to break on any warnings/errors
#ifdef DX12_ENABLE_DEBUG_LAYER
    if (pdx12Debug != nullptr)
    {
        ID3D12InfoQueue* pInfoQueue = nullptr;
        g_pd3dDevice->QueryInterface(IID_PPV_ARGS(&pInfoQueue));
        pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
        pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
        pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);

        // Disable breaking on this warning because of a suspected bug in the D3D12 SDK layer, see #9084 for details.
        const int D3D12_MESSAGE_ID_FENCE_ZERO_WAIT_ = 1424; // not in all copies of d3d12sdklayers.h
        D3D12_MESSAGE_ID disabledMessages[] = { (D3D12_MESSAGE_ID)D3D12_MESSAGE_ID_FENCE_ZERO_WAIT_ };
        D3D12_INFO_QUEUE_FILTER filter = {};
        filter.DenyList.NumIDs = 1;
        filter.DenyList.pIDList = disabledMessages;
        pInfoQueue->AddStorageFilterEntries(&filter);

        pInfoQueue->Release();
        pdx12Debug->Release();
    }
#endif

    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        desc.NumDescriptors = APP_NUM_BACK_BUFFERS;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        desc.NodeMask = 1;
        if (g_pd3dDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&g_pd3dRtvDescHeap)) != S_OK)
            return false;

        SIZE_T rtvDescriptorSize = g_pd3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = g_pd3dRtvDescHeap->GetCPUDescriptorHandleForHeapStart();
        for (UINT i = 0; i < APP_NUM_BACK_BUFFERS; i++)
        {
            g_mainRenderTargetDescriptor[i] = rtvHandle;
            rtvHandle.ptr += rtvDescriptorSize;
        }
    }

    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.NumDescriptors = APP_SRV_HEAP_SIZE;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (g_pd3dDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&g_pd3dSrvDescHeap)) != S_OK)
            return false;
        g_pd3dSrvDescHeapAlloc.Create(g_pd3dDevice, g_pd3dSrvDescHeap);
    }

    {
        D3D12_COMMAND_QUEUE_DESC desc = {};
        desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        desc.NodeMask = 1;
        if (g_pd3dDevice->CreateCommandQueue(&desc, IID_PPV_ARGS(&g_pd3dCommandQueue)) != S_OK)
            return false;
    }

    for (UINT i = 0; i < APP_NUM_FRAMES_IN_FLIGHT; i++)
        if (g_pd3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_frameContext[i].CommandAllocator)) != S_OK)
            return false;

    if (g_pd3dDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_frameContext[0].CommandAllocator, nullptr, IID_PPV_ARGS(&g_pd3dCommandList)) != S_OK ||
        g_pd3dCommandList->Close() != S_OK)
        return false;

    if (g_pd3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence)) != S_OK)
        return false;

    g_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (g_fenceEvent == nullptr)
        return false;

    {
        IDXGIFactory5* dxgiFactory = nullptr;
        IDXGISwapChain1* swapChain1 = nullptr;
        if (CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory)) != S_OK)
            return false;

        BOOL allow_tearing = TRUE;
        dxgiFactory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow_tearing, sizeof(allow_tearing));
        g_SwapChainTearingSupport = (allow_tearing == TRUE);
        if (g_SwapChainTearingSupport)
            sd.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

        if (dxgiFactory->CreateSwapChainForHwnd(g_pd3dCommandQueue, hWnd, &sd, nullptr, nullptr, &swapChain1) != S_OK)
            return false;
        if (swapChain1->QueryInterface(IID_PPV_ARGS(&g_pSwapChain)) != S_OK)
            return false;
        if (g_SwapChainTearingSupport)
            dxgiFactory->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER);

        swapChain1->Release();
        dxgiFactory->Release();
        g_pSwapChain->SetMaximumFrameLatency(APP_NUM_BACK_BUFFERS);
        g_hSwapChainWaitableObject = g_pSwapChain->GetFrameLatencyWaitableObject();
    }

    CreateRenderTarget();
    if (!CreateGpuLoadPass()) return false;
    return true;
}

void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->SetFullscreenState(false, nullptr); g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_hSwapChainWaitableObject != nullptr) { CloseHandle(g_hSwapChainWaitableObject); }
    for (UINT i = 0; i < APP_NUM_FRAMES_IN_FLIGHT; i++)
        if (g_frameContext[i].CommandAllocator) { g_frameContext[i].CommandAllocator->Release(); g_frameContext[i].CommandAllocator = nullptr; }
    if (g_pd3dCommandQueue) { g_pd3dCommandQueue->Release(); g_pd3dCommandQueue = nullptr; }
    if (g_pd3dCommandList) { g_pd3dCommandList->Release(); g_pd3dCommandList = nullptr; }
    if (g_pd3dRtvDescHeap) { g_pd3dRtvDescHeap->Release(); g_pd3dRtvDescHeap = nullptr; }
    if (g_pd3dSrvDescHeap) { g_pd3dSrvDescHeap->Release(); g_pd3dSrvDescHeap = nullptr; }
    if (g_gpuLoadPso) { g_gpuLoadPso->Release(); g_gpuLoadPso = nullptr; }
    if (g_gpuLoadRootSig) { g_gpuLoadRootSig->Release(); g_gpuLoadRootSig = nullptr; }
    if (g_fence) { g_fence->Release(); g_fence = nullptr; }
    if (g_fenceEvent) { CloseHandle(g_fenceEvent); g_fenceEvent = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }

#ifdef DX12_ENABLE_DEBUG_LAYER
    IDXGIDebug1* pDebug = nullptr;
    if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&pDebug))))
    {
        pDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_SUMMARY);
        pDebug->Release();
    }
#endif
}

void CreateRenderTarget()
{
    for (UINT i = 0; i < APP_NUM_BACK_BUFFERS; i++)
    {
        ID3D12Resource* pBackBuffer = nullptr;
        g_pSwapChain->GetBuffer(i, IID_PPV_ARGS(&pBackBuffer));
        g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, g_mainRenderTargetDescriptor[i]);
        g_mainRenderTargetResource[i] = pBackBuffer;
    }
}

void CleanupRenderTarget()
{
    WaitForPendingOperations();

    for (UINT i = 0; i < APP_NUM_BACK_BUFFERS; i++)
        if (g_mainRenderTargetResource[i]) { g_mainRenderTargetResource[i]->Release(); g_mainRenderTargetResource[i] = nullptr; }
}

// --- Нагрузочный GPU-пасс -------------------------------------------------
// Фуллскрин-треугольник + пиксельный шейдер с тяжёлым циклом (фрактал Мандельброта).
// Нагрузка регулируется слайдером g_gpuLoadIters: число итераций цикла на пиксель.
static const char* g_gpuLoadShaderSrc = R"hlsl(
cbuffer Params : register(b0)
{
    float  g_time;    // время, сек
    float  g_iters;   // итераций цикла на пиксель (управляет нагрузкой)
    float2 g_res;     // размер бэкбуфера
};

struct PsIn
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

PsIn VsMain(uint vid : SV_VertexID)
{
    PsIn o;
    float2 p = float2((vid << 1) & 2, vid & 2);   // 0,0  2,0  0,2
    o.pos = float4(p.x * 2.0 - 1.0, 1.0 - p.y * 2.0, 0.0, 1.0);
    o.uv = p;
    return o;
}

float4 PsMain(PsIn i) : SV_Target
{
    // координаты в комплексной плоскости, медленно «дышащий» зум
    float2 uv = (i.uv - 0.5) * float2(g_res.x / g_res.y, 1.0);
    float zoom = 2.2 - 0.7 * sin(g_time * 0.13);
    float2 c = uv * zoom + float2(-0.6, 0.0);

    int maxIter = max(1, (int)g_iters);
    float2 z = float2(0.0, 0.0);
    float n = 0.0;
    for (int k = 0; k < maxIter; k++)
    {
        z = float2(z.x * z.x - z.y * z.y, 2.0 * z.x * z.y) + c;
        // лишняя тригонометрия, чтобы итерация не была дешёвой
        z += 1e-6 * float2(sin(z.y + g_time), cos(z.x - g_time));
        if (dot(z, z) > 4.0) { n = (float)k; break; }
        n = (float)k;
    }

    float t = n / (float)maxIter;
    float3 col = 0.5 + 0.5 * cos(6.2831 * (t + g_time * 0.05) + float3(0.0, 0.33, 0.67));
    return float4(col * t, 1.0);
}
)hlsl";

bool CreateGpuLoadPass()
{
    // Root signature: 4 root-константы (time, iters, res.x, res.y), без таблиц дескрипторов
    D3D12_ROOT_PARAMETER rp = {};
    rp.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rp.Constants.ShaderRegister = 0;
    rp.Constants.RegisterSpace = 0;
    rp.Constants.Num32BitValues = 4;
    rp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
    rsDesc.NumParameters = 1;
    rsDesc.pParameters = &rp;
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ID3DBlob* sigBlob = nullptr;
    ID3DBlob* errBlob = nullptr;
    if (FAILED(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob)))
        return false;
    HRESULT hr = g_pd3dDevice->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_PPV_ARGS(&g_gpuLoadRootSig));
    sigBlob->Release();
    if (FAILED(hr))
        return false;

    UINT compileFlags = 0;
#ifdef DX12_ENABLE_DEBUG_LAYER
    compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    ID3DBlob* vsBlob = nullptr;
    ID3DBlob* psBlob = nullptr;
    if (FAILED(D3DCompile(g_gpuLoadShaderSrc, strlen(g_gpuLoadShaderSrc), "GpuLoad", nullptr, nullptr, "VsMain", "vs_5_0", compileFlags, 0, &vsBlob, &errBlob)))
        return false;
    if (FAILED(D3DCompile(g_gpuLoadShaderSrc, strlen(g_gpuLoadShaderSrc), "GpuLoad", nullptr, nullptr, "PsMain", "ps_5_0", compileFlags, 0, &psBlob, &errBlob)))
    {
        vsBlob->Release();
        return false;
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = g_gpuLoadRootSig;
    psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
    psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
    // Blend: opaque (фрактал полностью перекрывает фон, ImGui рисуется поверх)
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    // Rasterizer: без cull'инга, без depth
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;
    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.DepthStencilState.StencilEnable = FALSE;
    psoDesc.InputLayout = { nullptr, 0 };   // вершины генерируются из SV_VertexID
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleMask = UINT_MAX;

    hr = g_pd3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&g_gpuLoadPso));
    vsBlob->Release();
    psBlob->Release();
    return SUCCEEDED(hr);
}

// Создаёт (или пересоздаёт при ресайзе окна) readback-буфер размером с бэкбуфер
void EnsureReadbackBuffer(ID3D12Resource* backbuffer)
{
    D3D12_RESOURCE_DESC bbDesc = backbuffer->GetDesc();
    if (g_readbackBuffer && g_backbufferWidth == (UINT)bbDesc.Width && g_backbufferHeight == bbDesc.Height)
        return;

    if (g_readbackBuffer) { g_readbackBuffer->Release(); g_readbackBuffer = nullptr; }

    g_backbufferWidth = (UINT)bbDesc.Width;
    g_backbufferHeight = bbDesc.Height;

    g_readbackFootprint = {};
    g_readbackFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    g_readbackFootprint.Footprint.Width = g_backbufferWidth;
    g_readbackFootprint.Footprint.Height = g_backbufferHeight;
    g_readbackFootprint.Footprint.Depth = 1;
    g_readbackFootprint.Footprint.RowPitch = (g_backbufferWidth * 4 + 255) & ~255u; // D3D12_TEXTURE_DATA_PITCH_ALIGNMENT

    D3D12_RESOURCE_DESC rbDesc = {};
    rbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rbDesc.Width = (UINT64)g_readbackFootprint.Footprint.RowPitch * g_backbufferHeight;
    rbDesc.Height = 1;
    rbDesc.DepthOrArraySize = 1;
    rbDesc.MipLevels = 1;
    rbDesc.Format = DXGI_FORMAT_UNKNOWN;
    rbDesc.SampleDesc.Count = 1;
    rbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_READBACK;
    HRESULT hr = g_pd3dDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &rbDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&g_readbackBuffer));
    IM_ASSERT(SUCCEEDED(hr));

    g_frameImagePixels.resize((size_t)FRAME_IMAGE_W * FRAME_IMAGE_H * 4);
}

// Даунскейл бэкбуфера (R8G8B8A8) до 320x200 и отправка снимка кадра в Tracy.
// Картинка появится в Tracy прямо на графике кадров — удобно искать «тот самый» лаг.
void SendFrameImage()
{
    void* mapped = nullptr;
    if (FAILED(g_readbackBuffer->Map(0, nullptr, &mapped)))
        return;

    const uint8_t* src = (const uint8_t*)mapped;
    const int w = (int)g_backbufferWidth;
    const int h = (int)g_backbufferHeight;
    const UINT pitch = g_readbackFootprint.Footprint.RowPitch;

    for (int y = 0; y < FRAME_IMAGE_H; y++)
    {
        const int sy = y * h / FRAME_IMAGE_H;
        const uint8_t* srcRow = src + (size_t)sy * pitch;
        uint8_t* dstRow = g_frameImagePixels.data() + (size_t)y * FRAME_IMAGE_W * 4;
        for (int x = 0; x < FRAME_IMAGE_W; x++)
        {
            const int sx = x * w / FRAME_IMAGE_W;
            const uint8_t* px = srcRow + (size_t)sx * 4;
            dstRow[x * 4 + 0] = px[0];
            dstRow[x * 4 + 1] = px[1];
            dstRow[x * 4 + 2] = px[2];
            dstRow[x * 4 + 3] = 255;
        }
    }
    g_readbackBuffer->Unmap(0, nullptr);

    FrameImage(g_frameImagePixels.data(), FRAME_IMAGE_W, FRAME_IMAGE_H, 0, false);
}

void WaitForPendingOperations()
{
    g_pd3dCommandQueue->Signal(g_fence, ++g_fenceLastSignaledValue);

    g_fence->SetEventOnCompletion(g_fenceLastSignaledValue, g_fenceEvent);
    ::WaitForSingleObject(g_fenceEvent, INFINITE);
}

FrameContext* WaitForNextFrameContext()
{
    ZoneScoped;
    FrameContext* frame_context = &g_frameContext[g_frameIndex % APP_NUM_FRAMES_IN_FLIGHT];
    if (g_fence->GetCompletedValue() < frame_context->FenceValue)
    {
        g_fence->SetEventOnCompletion(frame_context->FenceValue, g_fenceEvent);
        HANDLE waitableObjects[] = { g_hSwapChainWaitableObject, g_fenceEvent };
        ::WaitForMultipleObjects(2, waitableObjects, TRUE, INFINITE);
    }
    else
        ::WaitForSingleObject(g_hSwapChainWaitableObject, INFINITE);

    return frame_context;
}

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Win32 message handler
// You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
// - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
// - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
// Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_SIZE:
        if (g_pd3dDevice != nullptr && wParam != SIZE_MINIMIZED)
        {
            WaitForPendingOperations();
            CleanupRenderTarget();
            DXGI_SWAP_CHAIN_DESC1 desc = {};
            g_pSwapChain->GetDesc1(&desc);
            HRESULT result = g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), desc.Format, desc.Flags);
            IM_ASSERT(SUCCEEDED(result) && "Failed to resize swapchain.");
            CreateRenderTarget();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
            return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
