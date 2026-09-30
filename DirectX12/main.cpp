// DirectX 12 + Dear ImGui 기초 렌더링 연습
// - D3D12 디바이스 / 스왑체인 / 프레임 동기화: GraphicsDevice
// - ImGui 백엔드: ImGuiLayer, 컨트롤 패널 UI: ControlPanel
// - 렌더링 파이프라인(루트 시그니처, 셰이더, PSO, 버텍스 버퍼, 드로우)은 직접 작성

#include "D3DUtil.h"
#include "GraphicsDevice.h"
#include "ImGuiLayer.h"
#include "ControlPanel.h"

static constexpr wchar_t kWindowClassName[] = L"DirectX12Sample";

// WM_SIZE로 들어온 새 크기. 메인 루프에서 적용 후 0으로 되돌림
static UINT g_resizeWidth = 0;
static UINT g_resizeHeight = 0;

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

	GraphicsDevice gfx;
	ImGuiLayer     imgui;
	ControlPanel   controlPanel;

	int exitCode = 0;
	try
	{
		gfx.Init(hwnd);
		// TODO: 렌더링 파이프라인 생성 (루트 시그니처, 셰이더, PSO, 버텍스 버퍼 등)
		imgui.Init(hwnd, gfx.Device(), gfx.CommandQueue(), GraphicsDevice::kNumFramesInFlight,
			GraphicsDevice::kBackBufferFormat, dpiScale);

		ShowWindow(hwnd, SW_SHOWDEFAULT);
		UpdateWindow(hwnd);

		while (PumpMessages())
		{
			// 창이 가려졌거나 최소화된 경우 렌더링 쉬기
			if (gfx.IsOccluded() || IsIconic(hwnd))
			{
				Sleep(10);
				continue;
			}
			if (g_resizeWidth != 0 && g_resizeHeight != 0)
			{
				gfx.Resize(g_resizeWidth, g_resizeHeight);
				g_resizeWidth = g_resizeHeight = 0;
			}

			imgui.BeginFrame();
			controlPanel.Draw(gfx.Width(), gfx.Height());

			ID3D12GraphicsCommandList* commandList = gfx.BeginFrame(controlPanel.ClearColor());
			// TODO: 직접 만든 파이프라인으로 드로우
			imgui.Render(commandList);
			gfx.EndFrame();
		}
	}
	catch (const std::exception& e)
	{
		MessageBoxA(hwnd, e.what(), "Error", MB_OK | MB_ICONERROR);
		exitCode = 1;
	}

	gfx.WaitForGpuOnExit();
	imgui.Shutdown();
	// TODO: 직접 만든 파이프라인 리소스 해제 (gfx.Shutdown 이전에)
	gfx.Shutdown();
	DestroyAppWindow(hwnd, hInstance);

	return exitCode;
}
