// DirectX 12 + Dear ImGui 기초 렌더링 연습
// - Win32 창 / D3D12 디바이스 / 스왑체인 / 프레임 동기화
// - ImGui는 ImGuiLayer로 분리
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

#include "ImGuiLayer.h"

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#ifdef _DEBUG
#pragma comment(lib, "dxguid.lib")
#endif

using Microsoft::WRL::ComPtr;

// ---------------------------------------------------------------------------
// 설정 상수
// ---------------------------------------------------------------------------
static constexpr UINT        kNumFramesInFlight = 2;
static constexpr UINT        kNumBackBuffers    = 3;
static constexpr DXGI_FORMAT kBackBufferFormat  = DXGI_FORMAT_R8G8B8A8_UNORM;
static constexpr wchar_t     kWindowClassName[] = L"DirectX12Sample";
static constexpr DXGI_FORMAT kDepthFormat		= DXGI_FORMAT_D32_FLOAT;
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
// 전역 상태
// ---------------------------------------------------------------------------
struct FrameContext
{
	ComPtr<ID3D12CommandAllocator> CommandAllocator;
	UINT64                         FenceValue = 0;
};

static FrameContext                      g_frameContexts[kNumFramesInFlight];
static UINT                              g_frameIndex = 0;
static FrameContext*                     g_currentFrame = nullptr;
static UINT                              g_currentBackBuffer = 0;

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

static ComPtr<ID3D12DescriptorHeap>		 g_dsvHeap;
static ComPtr<ID3D12Resource>			 g_depthBuffer;
static D3D12_CPU_DESCRIPTOR_HANDLE		 g_depthBufferDsv = {};

static UINT                              g_width = 0;
static UINT                              g_height = 0;
static UINT                              g_resizeWidth = 0;
static UINT                              g_resizeHeight = 0;

static ImGuiLayer                        g_imgui;

// UI로 조절하는 값
static float                             g_clearColor[3] = { 0.10f, 0.12f, 0.16f };
static bool                              g_showDemoWindow = false;

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

// 종료 시 사용. 초기화 도중 실패했거나 디바이스가 제거된 경우에도 예외를 던지지 않음
static void WaitForGpuOnExit()
{
	if (!g_commandQueue || !g_fence || !g_fenceEvent)
		return;
	try
	{
		WaitForGpu();
	}
	catch (const std::exception&)
	{
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

static void CreateDepthBuffer()
{
	D3D12_HEAP_PROPERTIES heapProps = {};
	heapProps.Type = D3D12_HEAP_TYPE_DEFAULT; //default

	D3D12_RESOURCE_DESC depthDesc = {};
	depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	depthDesc.Width = g_width;
	depthDesc.Height = g_height;
	depthDesc.DepthOrArraySize = 1;
	depthDesc.MipLevels = 1;
	depthDesc.Format = kDepthFormat;
	depthDesc.SampleDesc.Count = 1;
	depthDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_CLEAR_VALUE depthClearValue = {};
	depthClearValue.Format = kDepthFormat;
	depthClearValue.DepthStencil.Depth = 1.0f;
	depthClearValue.DepthStencil.Stencil = 0;

	ThrowIfFailed(g_device->CreateCommittedResource(
		&heapProps, D3D12_HEAP_FLAG_NONE, &depthDesc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&depthClearValue,
		IID_PPV_ARGS(&g_depthBuffer)), "CreatecommittedResource(Depth)");
	
	g_device->CreateDepthStencilView(g_depthBuffer.Get(), nullptr, g_depthBufferDsv);
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

	//dsv 힙을 설정한다
	{
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		desc.NumDescriptors = 1;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		ThrowIfFailed(g_device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&g_dsvHeap)), "CreateDescriptorHeap(DSV)");
		
		g_depthBufferDsv = g_dsvHeap->GetCPUDescriptorHandleForHeapStart();
	}

	CreateDepthBuffer();


}

static void CleanupDeviceD3D()
{
	g_depthBuffer.Reset();
	g_dsvHeap.Reset();
	CleanupRenderTargets();
	g_swapChain.Reset();
	for (UINT i = 0; i < kNumFramesInFlight; i++)
		g_frameContexts[i].CommandAllocator.Reset();
	g_commandList.Reset();
	g_commandQueue.Reset();
	g_rtvHeap.Reset();
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
	g_depthBuffer.Reset();
	ThrowIfFailed(g_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0), "ResizeBuffers");
	g_width = width;
	g_height = height;
	CreateRenderTargets();
	CreateDepthBuffer();
}

// ---------------------------------------------------------------------------
// 프레임
// ---------------------------------------------------------------------------
// 렌더링할 수 있는 상태인지 확인하고, 대기 중인 리사이즈를 적용
static bool PrepareFrame(HWND hwnd)
{
	// 창이 가려졌거나 최소화된 경우 렌더링 쉬기
	if ((g_swapChainOccluded && g_swapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) || IsIconic(hwnd))
	{
		Sleep(10);
		return false;
	}
	g_swapChainOccluded = false;

	if (g_resizeWidth != 0 && g_resizeHeight != 0)
	{
		ResizeSwapChain(g_resizeWidth, g_resizeHeight);
		g_resizeWidth = g_resizeHeight = 0;
	}
	return true;
}

// 커맨드 리스트 기록 시작: 백버퍼를 렌더 타깃으로 전환하고 클리어, 뷰포트/시저 설정
static ID3D12GraphicsCommandList* BeginFrame()
{
	g_currentFrame = &WaitForNextFrameContext();
	g_currentBackBuffer = g_swapChain->GetCurrentBackBufferIndex();
	ID3D12Resource* backBuffer = g_backBuffers[g_currentBackBuffer].Get();

	ThrowIfFailed(g_currentFrame->CommandAllocator->Reset(), "CommandAllocator::Reset");
	ThrowIfFailed(g_commandList->Reset(g_currentFrame->CommandAllocator.Get(), nullptr), "CommandList::Reset");

	D3D12_RESOURCE_BARRIER toRenderTarget = TransitionBarrier(backBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	g_commandList->ResourceBarrier(1, &toRenderTarget);

	const float clear[4] = { g_clearColor[0], g_clearColor[1], g_clearColor[2], 1.0f };
	g_commandList->ClearRenderTargetView(g_backBufferRtv[g_currentBackBuffer], clear, 0, nullptr);
	g_commandList->OMSetRenderTargets(1, &g_backBufferRtv[g_currentBackBuffer], FALSE, nullptr);

	D3D12_VIEWPORT viewport = { 0.0f, 0.0f, static_cast<float>(g_width), static_cast<float>(g_height), 0.0f, 1.0f };
	D3D12_RECT scissor = { 0, 0, static_cast<LONG>(g_width), static_cast<LONG>(g_height) };
	g_commandList->RSSetViewports(1, &viewport);
	g_commandList->RSSetScissorRects(1, &scissor);

	return g_commandList.Get();
}

// 커맨드 리스트 기록 종료: PRESENT로 전환, 제출, Present, 펜스 시그널
static void EndFrame()
{
	ID3D12Resource* backBuffer = g_backBuffers[g_currentBackBuffer].Get();
	D3D12_RESOURCE_BARRIER toPresent = TransitionBarrier(backBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
	g_commandList->ResourceBarrier(1, &toPresent);
	ThrowIfFailed(g_commandList->Close(), "CommandList::Close");

	ID3D12CommandList* lists[] = { g_commandList.Get() };
	g_commandQueue->ExecuteCommandLists(1, lists);

	HRESULT hr = g_swapChain->Present(1, 0); // VSync
	g_swapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);
	ThrowIfFailed(hr, "Present");

	UINT64 fenceValue = ++g_fenceLastSignaledValue;
	ThrowIfFailed(g_commandQueue->Signal(g_fence.Get(), fenceValue), "Signal");
	g_currentFrame->FenceValue = fenceValue;
	g_frameIndex++;
}

// ---------------------------------------------------------------------------
// UI
// ---------------------------------------------------------------------------
static void DrawUI()
{
	ImGuiIO& io = ImGui::GetIO();

	ImGui::Begin("Control Panel");
	ImGui::Text("%.1f FPS (%.3f ms/frame)", io.Framerate, 1000.0f / io.Framerate);
	ImGui::Text("Back buffer: %u x %u", g_width, g_height);
	ImGui::Separator();
	ImGui::ColorEdit3("Clear Color", g_clearColor);
	ImGui::Checkbox("Show ImGui Demo Window", &g_showDemoWindow);
	ImGui::End();

	if (g_showDemoWindow)
		ImGui::ShowDemoWindow(&g_showDemoWindow);
}

// ---------------------------------------------------------------------------
// Win32
// ---------------------------------------------------------------------------
static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (ImGuiLayer::HandleMessage(hWnd, msg, wParam, lParam))
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

static HWND CreateAppWindow(HINSTANCE hInstance, float dpiScale)
{
	WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, hInstance, nullptr, LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr, kWindowClassName, nullptr };
	RegisterClassExW(&wc);
	return CreateWindowW(kWindowClassName, L"DirectX 12 + ImGui", WS_OVERLAPPEDWINDOW,
		100, 100, static_cast<int>(1280 * dpiScale), static_cast<int>(800 * dpiScale), nullptr, nullptr, hInstance, nullptr);
}

static void DestroyAppWindow(HWND hwnd, HINSTANCE hInstance)
{
	DestroyWindow(hwnd);
	UnregisterClassW(kWindowClassName, hInstance);
}

// 대기 중인 메시지를 모두 처리. WM_QUIT를 받으면 false
static bool PumpMessages()
{
	MSG msg;
	while (PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE))
	{
		if (msg.message == WM_QUIT)
			return false;
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}
	return true;
}

// ---------------------------------------------------------------------------
// 진입점
// ---------------------------------------------------------------------------
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int)
{
	const float dpiScale = ImGuiLayer::EnableDpiAwareness();
	HWND hwnd = CreateAppWindow(hInstance, dpiScale);

	int exitCode = 0;
	try
	{
		CreateDeviceD3D(hwnd);
		// TODO: 렌더링 파이프라인 생성 (루트 시그니처, 셰이더, PSO, 버텍스 버퍼 등)
		g_imgui.Init(hwnd, g_device.Get(), g_commandQueue.Get(), kNumFramesInFlight, kBackBufferFormat, dpiScale);

		ShowWindow(hwnd, SW_SHOWDEFAULT);
		UpdateWindow(hwnd);

		while (PumpMessages())
		{
			if (!PrepareFrame(hwnd))
				continue;

			g_imgui.BeginFrame();
			DrawUI();

			ID3D12GraphicsCommandList* commandList = BeginFrame();
			// TODO: 직접 만든 파이프라인으로 드로우
			g_imgui.Render(commandList);
			EndFrame();
		}
	}
	catch (const std::exception& e)
	{
		MessageBoxA(hwnd, e.what(), "Error", MB_OK | MB_ICONERROR);
		exitCode = 1;
	}

	WaitForGpuOnExit();
	g_imgui.Shutdown();
	// TODO: 직접 만든 파이프라인 리소스 해제 (CleanupDeviceD3D 이전에)
	CleanupDeviceD3D();
	DestroyAppWindow(hwnd, hInstance);

	return exitCode;
}
