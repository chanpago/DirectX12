// DirectX 12 + Dear ImGui 기초 렌더링 연습
// - D3D12 디바이스 / 스왑체인 / 프레임 동기화: GraphicsDevice
// - ImGui 백엔드: ImGuiLayer, 컨트롤 패널 UI: ControlPanel
// - 렌더링 파이프라인(루트 시그니처, 셰이더, PSO, 버텍스 버퍼, 드로우)은 직접 작성

#include "D3DUtil.h"
#include "GraphicsDevice.h"
#include "ImGuiLayer.h"
#include "ControlPanel.h"
#include "Mesh.h"
#include "RootSignature.h"
#include "BasicPipeline.h"

using namespace DirectX;

static constexpr wchar_t kWindowClassName[] = L"DirectX12Sample";

// WM_SIZE로 들어온 새 크기. 메인 루프에서 적용 후 0으로 되돌림
static UINT g_resizeWidth = 0;
static UINT g_resizeHeight = 0;

// ---------------------------------------------------------------------------
// 렌더링
// ---------------------------------------------------------------------------
// 큐브의 MVP 행렬 (셰이더로 넘기기 위해 전치된 상태로 반환)
// 왼손 좌표계, Y-up, +Z 앞쪽
static XMFLOAT4X4 ComputeCubeMvp(float angleRadians, float aspectRatio)
{
	// Model(World): 로컬 → 월드. Y축으로 회전
	XMMATRIX world = XMMatrixRotationY(angleRadians);

	// View: 월드 → 카메라. 앞쪽(-Z) 약간 위에서 원점을 바라봄
	XMVECTOR eye    = XMVectorSet(0.0f, 1.5f, -3.0f, 1.0f);
	XMVECTOR target = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
	XMVECTOR up     = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
	XMMATRIX view = XMMatrixLookAtLH(eye, target, up);

	// Projection: 카메라 → 클립 공간. 세로 시야각 45도, 화면 비율, near / far
	XMMATRIX proj = XMMatrixPerspectiveFovLH(XM_PIDIV4, aspectRatio, 0.1f, 100.0f);

	// 행 벡터 규약: v * World * View * Proj
	XMMATRIX mvp = world * view * proj;

	XMFLOAT4X4 result;
	//XMStoreFloat4x4(&result, XMMatrixTranspose(mvp));
	XMStoreFloat4x4(&result, mvp);
	return result;
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

	GraphicsDevice gfx;
	ImGuiLayer     imgui;
	ControlPanel   controlPanel;
	Mesh           cube;
	RootSignature  rootSignature;
	BasicPipeline  basicPipeline;

	float cubeAngle = 0.0f; // 라디안

	int exitCode = 0;
	try
	{
		gfx.Init(hwnd);
		cube.Init(gfx.Device());
		rootSignature.Init(gfx.Device());

		// 이 부분은 PSO(Pipeline State Object)를 만드는 호출
		// 루트 시그니처가 셰이더 입력의 틀이었다면, PSO는 이 물체를 어떻게 그릴지데 대한 설정 전부를 하나로 묶은 객체
		basicPipeline.Init(gfx.Device(), rootSignature.Get(), GraphicsDevice::kBackBufferFormat, GraphicsDevice::kDepthFormat);
		imgui.Init(hwnd, gfx.Device(), gfx.CommandQueue(), GraphicsDevice::kNumFramesInFlight,
			GraphicsDevice::kBackBufferFormat, GraphicsDevice::kDepthFormat, dpiScale);

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

			// 업데이트: 회전 각도 누적 → MVP 계산
			cubeAngle += XMConvertToRadians(controlPanel.RotationSpeed()) * ImGui::GetIO().DeltaTime;
			const float aspectRatio = static_cast<float>(gfx.Width()) / static_cast<float>(gfx.Height());
			const XMFLOAT4X4 mvp = ComputeCubeMvp(cubeAngle, aspectRatio);

			ID3D12GraphicsCommandList* commandList = gfx.BeginFrame(controlPanel.ClearColor());
			// 큐브 드로우: 틀(루트 시그니처) → 처리 방법(PSO) → 데이터(버퍼) → 그리기
			commandList->SetGraphicsRootSignature(rootSignature.Get());
			commandList->SetPipelineState(basicPipeline.Get());
			commandList->SetGraphicsRoot32BitConstants(RootSignature::kTransformParam, 16, &mvp, 0);
			commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			commandList->IASetVertexBuffers(0, 1, &cube.VertexBufferView());
			commandList->IASetIndexBuffer(&cube.IndexBufferView());
			commandList->DrawIndexedInstanced(cube.IndexCount(), 1, 0, 0, 0);

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
	// 직접 만든 파이프라인 리소스 해제 (GPU 대기 후, gfx.Shutdown 이전에)
	basicPipeline.Shutdown();
	rootSignature.Shutdown();
	cube.Shutdown();
	gfx.Shutdown();
	DestroyAppWindow(hwnd, hInstance);

	return exitCode;
}
