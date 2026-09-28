// DirectX 12 + Dear ImGui 기초 렌더링 연습
// - Win32 창 / D3D12 디바이스 / 스왑체인 / 프레임 동기화 / ImGui
// - 렌더링 파이프라인(루트 시그니처, 셰이더, PSO, 버텍스 버퍼, 드로우)은 직접 작성

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#ifdef _DEBUG
#include <dxgidebug.h>
#endif

#include <cstdio>
#include <stdexcept>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx12.h"

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#ifdef _DEBUG
#pragma comment(lib, "dxguid.lib")
#endif

using Microsoft::WRL::ComPtr;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ---------------------------------------------------------------------------
// 설정 상수
// ---------------------------------------------------------------------------
static constexpr UINT        kNumFramesInFlight = 2;
static constexpr UINT        kNumBackBuffers    = 3;
static constexpr UINT        kSrvHeapSize       = 64;
static constexpr DXGI_FORMAT kBackBufferFormat  = DXGI_FORMAT_R8G8B8A8_UNORM;

// ---------------------------------------------------------------------------
// 에러 처리
// ---------------------------------------------------------------------------
static void ThrowIfFailed(HRESULT hr, const char* what)
{
	if (FAILED(hr))
	{
		char buf[256];
		snprintf(buf, sizeof(buf), "%s failed (HRESULT 0x%08X)", what, static_cast<unsigned>(hr));
		throw std::runtime_error(buf);
	}
}

// ---------------------------------------------------------------------------
// SRV 디스크립터 힙 할당기 (ImGui 1.92 DX12 백엔드는 텍스처별로 디스크립터를 요청함)
// ---------------------------------------------------------------------------
struct DescriptorHeapAllocator
{
	ID3D12DescriptorHeap*       Heap = nullptr;
	D3D12_CPU_DESCRIPTOR_HANDLE HeapStartCpu = {};
	D3D12_GPU_DESCRIPTOR_HANDLE HeapStartGpu = {};
	UINT                        HeapHandleIncrement = 0;
	ImVector<int>               FreeIndices;

	void Create(ID3D12Device* device, ID3D12DescriptorHeap* heap)
	{
		IM_ASSERT(Heap == nullptr && FreeIndices.empty());
		Heap = heap;
		D3D12_DESCRIPTOR_HEAP_DESC desc = heap->GetDesc();
		HeapStartCpu = Heap->GetCPUDescriptorHandleForHeapStart();
		HeapStartGpu = Heap->GetGPUDescriptorHandleForHeapStart();
		HeapHandleIncrement = device->GetDescriptorHandleIncrementSize(desc.Type);
		FreeIndices.reserve(static_cast<int>(desc.NumDescriptors));
		for (int n = static_cast<int>(desc.NumDescriptors); n > 0; n--)
			FreeIndices.push_back(n - 1);
	}

	void Destroy()
	{
		Heap = nullptr;
		FreeIndices.clear();
	}

	void Alloc(D3D12_CPU_DESCRIPTOR_HANDLE* outCpu, D3D12_GPU_DESCRIPTOR_HANDLE* outGpu)
	{
		IM_ASSERT(FreeIndices.Size > 0);
		int idx = FreeIndices.back();
		FreeIndices.pop_back();
		outCpu->ptr = HeapStartCpu.ptr + static_cast<SIZE_T>(idx) * HeapHandleIncrement;
		outGpu->ptr = HeapStartGpu.ptr + static_cast<UINT64>(idx) * HeapHandleIncrement;
	}

	void Free(D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu)
	{
		int cpuIdx = static_cast<int>((cpu.ptr - HeapStartCpu.ptr) / HeapHandleIncrement);
		int gpuIdx = static_cast<int>((gpu.ptr - HeapStartGpu.ptr) / HeapHandleIncrement);
		IM_ASSERT(cpuIdx == gpuIdx);
		(void)gpuIdx;
		FreeIndices.push_back(cpuIdx);
	}
};

// ---------------------------------------------------------------------------
// 전역 상태
// ---------------------------------------------------------------------------
struct FrameContext
{
	ComPtr<ID3D12CommandAllocator> CommandAllocator;
	UINT64                         FenceValue = 0;
};

static FrameContext                      g_frameContexts[kNumFramesInFlight];
static UINT                              g_frameIndex = 0;

static ComPtr<ID3D12Device>              g_device;
static ComPtr<ID3D12CommandQueue>        g_commandQueue;
static ComPtr<ID3D12GraphicsCommandList> g_commandList;
static ComPtr<ID3D12Fence>               g_fence;
static HANDLE                            g_fenceEvent = nullptr;
static UINT64                            g_fenceLastSignaledValue = 0;

static ComPtr<IDXGISwapChain3>           g_swapChain;
static bool                              g_swapChainOccluded = false;
static ComPtr<ID3D12DescriptorHeap>      g_rtvHeap;
static UINT                              g_rtvDescriptorSize = 0;
static ComPtr<ID3D12Resource>            g_backBuffers[kNumBackBuffers];
static D3D12_CPU_DESCRIPTOR_HANDLE       g_backBufferRtv[kNumBackBuffers] = {};

static ComPtr<ID3D12DescriptorHeap>      g_srvHeap;
static DescriptorHeapAllocator           g_srvHeapAlloc;

static UINT                              g_width = 0;
static UINT                              g_height = 0;
static UINT                              g_resizeWidth = 0;
static UINT                              g_resizeHeight = 0;

// ---------------------------------------------------------------------------
// 헬퍼
// ---------------------------------------------------------------------------
static D3D12_RESOURCE_BARRIER TransitionBarrier(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = resource;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = before;
	barrier.Transition.StateAfter = after;
	return barrier;
}

// 큐에 시그널을 넣고 GPU가 해당 지점까지 끝낼 때까지 대기
static void WaitForGpu()
{
	UINT64 fenceValue = ++g_fenceLastSignaledValue;
	ThrowIfFailed(g_commandQueue->Signal(g_fence.Get(), fenceValue), "Signal");
	if (g_fence->GetCompletedValue() < fenceValue)
	{
		ThrowIfFailed(g_fence->SetEventOnCompletion(fenceValue, g_fenceEvent), "SetEventOnCompletion");
		WaitForSingleObject(g_fenceEvent, INFINITE);
	}
}

// 이번에 사용할 프레임 컨텍스트가 GPU에서 해제될 때까지 대기
static FrameContext& WaitForNextFrameContext()
{
	FrameContext& frame = g_frameContexts[g_frameIndex % kNumFramesInFlight];
	if (g_fence->GetCompletedValue() < frame.FenceValue)
	{
		ThrowIfFailed(g_fence->SetEventOnCompletion(frame.FenceValue, g_fenceEvent), "SetEventOnCompletion");
		WaitForSingleObject(g_fenceEvent, INFINITE);
	}
	return frame;
}

// ---------------------------------------------------------------------------
// 디바이스 / 스왑체인
// ---------------------------------------------------------------------------
static void CreateRenderTargets()
{
	for (UINT i = 0; i < kNumBackBuffers; i++)
	{
		ThrowIfFailed(g_swapChain->GetBuffer(i, IID_PPV_ARGS(&g_backBuffers[i])), "GetBuffer");
		g_device->CreateRenderTargetView(g_backBuffers[i].Get(), nullptr, g_backBufferRtv[i]);
	}
}

static void CleanupRenderTargets()
{
	for (UINT i = 0; i < kNumBackBuffers; i++)
		g_backBuffers[i].Reset();
}

static void CreateDeviceD3D(HWND hwnd)
{
	UINT dxgiFactoryFlags = 0;
#ifdef _DEBUG
	ComPtr<ID3D12Debug> debugController;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
	{
		debugController->EnableDebugLayer();
		dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
	}
#endif

	ComPtr<IDXGIFactory4> factory;
	ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)), "CreateDXGIFactory2");

	ThrowIfFailed(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&g_device)), "D3D12CreateDevice");

	// 커맨드 큐
	{
		D3D12_COMMAND_QUEUE_DESC desc = {};
		desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		ThrowIfFailed(g_device->CreateCommandQueue(&desc, IID_PPV_ARGS(&g_commandQueue)), "CreateCommandQueue");
	}

	// RTV 힙
	{
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		desc.NumDescriptors = kNumBackBuffers;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		ThrowIfFailed(g_device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&g_rtvHeap)), "CreateDescriptorHeap(RTV)");

		g_rtvDescriptorSize = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		D3D12_CPU_DESCRIPTOR_HANDLE handle = g_rtvHeap->GetCPUDescriptorHandleForHeapStart();
		for (UINT i = 0; i < kNumBackBuffers; i++)
		{
			g_backBufferRtv[i] = handle;
			handle.ptr += g_rtvDescriptorSize;
		}
	}

	// ImGui용 SRV 힙 (shader-visible)
	{
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		desc.NumDescriptors = kSrvHeapSize;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		ThrowIfFailed(g_device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&g_srvHeap)), "CreateDescriptorHeap(SRV)");
		g_srvHeapAlloc.Create(g_device.Get(), g_srvHeap.Get());
	}

	// 프레임별 커맨드 할당기 + 커맨드 리스트
	for (UINT i = 0; i < kNumFramesInFlight; i++)
	{
		ThrowIfFailed(g_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
			IID_PPV_ARGS(&g_frameContexts[i].CommandAllocator)), "CreateCommandAllocator");
	}
	ThrowIfFailed(g_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
		g_frameContexts[0].CommandAllocator.Get(), nullptr, IID_PPV_ARGS(&g_commandList)), "CreateCommandList");
	ThrowIfFailed(g_commandList->Close(), "CommandList::Close");

	// 펜스
	ThrowIfFailed(g_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence)), "CreateFence");
	g_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
	if (g_fenceEvent == nullptr)
		ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()), "CreateEvent");

	// 스왑체인
	{
		DXGI_SWAP_CHAIN_DESC1 desc = {};
		desc.BufferCount = kNumBackBuffers;
		desc.Width = 0;
		desc.Height = 0;
		desc.Format = kBackBufferFormat;
		desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		desc.SampleDesc.Count = 1;
		desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
		desc.Scaling = DXGI_SCALING_STRETCH;

		ComPtr<IDXGISwapChain1> swapChain1;
		ThrowIfFailed(factory->CreateSwapChainForHwnd(g_commandQueue.Get(), hwnd, &desc, nullptr, nullptr, &swapChain1),
			"CreateSwapChainForHwnd");
		ThrowIfFailed(swapChain1.As(&g_swapChain), "IDXGISwapChain3");
		ThrowIfFailed(factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER), "MakeWindowAssociation");

		DXGI_SWAP_CHAIN_DESC1 actual = {};
		g_swapChain->GetDesc1(&actual);
		g_width = actual.Width;
		g_height = actual.Height;
	}

	CreateRenderTargets();
}

static void CleanupDeviceD3D()
{
	CleanupRenderTargets();
	g_swapChain.Reset();
	for (UINT i = 0; i < kNumFramesInFlight; i++)
		g_frameContexts[i].CommandAllocator.Reset();
	g_commandList.Reset();
	g_commandQueue.Reset();
	g_rtvHeap.Reset();
	g_srvHeapAlloc.Destroy();
	g_srvHeap.Reset();
	g_fence.Reset();
	if (g_fenceEvent)
	{
		CloseHandle(g_fenceEvent);
		g_fenceEvent = nullptr;
	}
	g_device.Reset();

#ifdef _DEBUG
	ComPtr<IDXGIDebug1> dxgiDebug;
	if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgiDebug))))
		dxgiDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_FLAGS(DXGI_DEBUG_RLO_SUMMARY | DXGI_DEBUG_RLO_IGNORE_INTERNAL));
#endif
}

static void ResizeSwapChain(UINT width, UINT height)
{
	WaitForGpu();
	CleanupRenderTargets();
	ThrowIfFailed(g_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0), "ResizeBuffers");
	g_width = width;
	g_height = height;
	CreateRenderTargets();
}

// ---------------------------------------------------------------------------
// Win32
// ---------------------------------------------------------------------------
static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
		return true;

	switch (msg)
	{
	case WM_SIZE:
		if (wParam == SIZE_MINIMIZED)
			return 0;
		g_resizeWidth = static_cast<UINT>(LOWORD(lParam));
		g_resizeHeight = static_cast<UINT>(HIWORD(lParam));
		return 0;
	case WM_SYSCOMMAND:
		if ((wParam & 0xfff0) == SC_KEYMENU) // ALT 메뉴 비활성화
			return 0;
		break;
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// 진입점
// ---------------------------------------------------------------------------
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int)
{
	ImGui_ImplWin32_EnableDpiAwareness();
	const float mainScale = ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY));

	WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, hInstance, nullptr, LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr, L"DirectX12Sample", nullptr };
	RegisterClassExW(&wc);
	HWND hwnd = CreateWindowW(wc.lpszClassName, L"DirectX 12 + ImGui", WS_OVERLAPPEDWINDOW,
		100, 100, static_cast<int>(1280 * mainScale), static_cast<int>(800 * mainScale), nullptr, nullptr, wc.hInstance, nullptr);

	try
	{
		CreateDeviceD3D(hwnd);
		// TODO: 렌더링 파이프라인 생성 (루트 시그니처, 셰이더, PSO, 버텍스 버퍼 등)
	}
	catch (const std::exception& e)
	{
		MessageBoxA(hwnd, e.what(), "Initialization Error", MB_OK | MB_ICONERROR);
		CleanupDeviceD3D();
		DestroyWindow(hwnd);
		UnregisterClassW(wc.lpszClassName, wc.hInstance);
		return 1;
	}

	ShowWindow(hwnd, SW_SHOWDEFAULT);
	UpdateWindow(hwnd);

	// ImGui 초기화
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

	ImGui::StyleColorsDark();
	ImGuiStyle& style = ImGui::GetStyle();
	style.ScaleAllSizes(mainScale);
	style.FontScaleDpi = mainScale;

	ImGui_ImplWin32_Init(hwnd);

	ImGui_ImplDX12_InitInfo initInfo;
	initInfo.Device = g_device.Get();
	initInfo.CommandQueue = g_commandQueue.Get();
	initInfo.NumFramesInFlight = kNumFramesInFlight;
	initInfo.RTVFormat = kBackBufferFormat;
	initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;
	initInfo.SrvDescriptorHeap = g_srvHeap.Get();
	initInfo.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE* outCpu, D3D12_GPU_DESCRIPTOR_HANDLE* outGpu) { g_srvHeapAlloc.Alloc(outCpu, outGpu); };
	initInfo.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu) { g_srvHeapAlloc.Free(cpu, gpu); };
	ImGui_ImplDX12_Init(&initInfo);

	// UI로 조절하는 값
	float clearColor[3] = { 0.10f, 0.12f, 0.16f };
	bool showDemoWindow = false;

	int exitCode = 0;
	try
	{
		bool done = false;
		while (!done)
		{
			MSG msg;
			while (PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE))
			{
				TranslateMessage(&msg);
				DispatchMessageW(&msg);
				if (msg.message == WM_QUIT)
					done = true;
			}
			if (done)
				break;

			// 창이 가려졌거나 최소화된 경우 렌더링 쉬기
			if ((g_swapChainOccluded && g_swapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) || IsIconic(hwnd))
			{
				Sleep(10);
				continue;
			}
			g_swapChainOccluded = false;

			if (g_resizeWidth != 0 && g_resizeHeight != 0)
			{
				ResizeSwapChain(g_resizeWidth, g_resizeHeight);
				g_resizeWidth = g_resizeHeight = 0;
			}

			// ---- UI ----
			ImGui_ImplDX12_NewFrame();
			ImGui_ImplWin32_NewFrame();
			ImGui::NewFrame();

			ImGui::Begin("Control Panel");
			ImGui::Text("%.1f FPS (%.3f ms/frame)", io.Framerate, 1000.0f / io.Framerate);
			ImGui::Text("Back buffer: %u x %u", g_width, g_height);
			ImGui::Separator();
			ImGui::ColorEdit3("Clear Color", clearColor);
			ImGui::Checkbox("Show ImGui Demo Window", &showDemoWindow);
			ImGui::End();

			if (showDemoWindow)
				ImGui::ShowDemoWindow(&showDemoWindow);

			ImGui::Render();

			// ---- 커맨드 기록 ----
			FrameContext& frame = WaitForNextFrameContext();
			UINT backBufferIdx = g_swapChain->GetCurrentBackBufferIndex();
			ID3D12Resource* backBuffer = g_backBuffers[backBufferIdx].Get();

			ThrowIfFailed(frame.CommandAllocator->Reset(), "CommandAllocator::Reset");
			ThrowIfFailed(g_commandList->Reset(frame.CommandAllocator.Get(), nullptr), "CommandList::Reset");

			D3D12_RESOURCE_BARRIER toRenderTarget = TransitionBarrier(backBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
			g_commandList->ResourceBarrier(1, &toRenderTarget);

			const float clear[4] = { clearColor[0], clearColor[1], clearColor[2], 1.0f };
			g_commandList->ClearRenderTargetView(g_backBufferRtv[backBufferIdx], clear, 0, nullptr);
			g_commandList->OMSetRenderTargets(1, &g_backBufferRtv[backBufferIdx], FALSE, nullptr);

			D3D12_VIEWPORT viewport = { 0.0f, 0.0f, static_cast<float>(g_width), static_cast<float>(g_height), 0.0f, 1.0f };
			D3D12_RECT scissor = { 0, 0, static_cast<LONG>(g_width), static_cast<LONG>(g_height) };
			g_commandList->RSSetViewports(1, &viewport);
			g_commandList->RSSetScissorRects(1, &scissor);

			// TODO: 직접 만든 파이프라인으로 드로우

			// ImGui
			ID3D12DescriptorHeap* heaps[] = { g_srvHeap.Get() };
			g_commandList->SetDescriptorHeaps(1, heaps);
			ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_commandList.Get());

			D3D12_RESOURCE_BARRIER toPresent = TransitionBarrier(backBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
			g_commandList->ResourceBarrier(1, &toPresent);
			ThrowIfFailed(g_commandList->Close(), "CommandList::Close");

			ID3D12CommandList* lists[] = { g_commandList.Get() };
			g_commandQueue->ExecuteCommandLists(1, lists);

			// ---- Present ----
			HRESULT hr = g_swapChain->Present(1, 0); // VSync
			g_swapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);
			ThrowIfFailed(hr, "Present");

			UINT64 fenceValue = ++g_fenceLastSignaledValue;
			ThrowIfFailed(g_commandQueue->Signal(g_fence.Get(), fenceValue), "Signal");
			frame.FenceValue = fenceValue;
			g_frameIndex++;
		}
	}
	catch (const std::exception& e)
	{
		MessageBoxA(hwnd, e.what(), "Runtime Error", MB_OK | MB_ICONERROR);
		exitCode = 1;
	}

	// ---- 종료 ----
	WaitForGpu();

	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

	// TODO: 직접 만든 파이프라인 리소스 해제 (CleanupDeviceD3D 이전에)
	CleanupDeviceD3D();
	DestroyWindow(hwnd);
	UnregisterClassW(wc.lpszClassName, wc.hInstance);

	return exitCode;
}
