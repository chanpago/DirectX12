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
#include "Camera.h"
#include "InstanceBuffer.h"

using namespace DirectX;

static constexpr wchar_t kWindowClassName[] = L"DirectX12Sample";

// WM_SIZE로 들어온 새 크기. 메인 루프에서 적용 후 0으로 되돌림
static UINT g_resizeWidth = 0;
static UINT g_resizeHeight = 0;

// ---------------------------------------------------------------------------
// 렌더링
// ---------------------------------------------------------------------------
// 왼손 좌표계, Y-up, +Z 앞쪽. 행렬은 전치하지 않음 (셰이더에서 row_major로 받음)

// 큐브 격자: kGridSize × kGridSize개를 XZ 평면에 kGridSpacing 간격으로 배치
static constexpr UINT  kGridSize     = 10;
static constexpr UINT  kNumInstances = kGridSize * kGridSize;
static constexpr float kGridSpacing  = 2.0f;

// View * Projection (프레임당 한 번, 모든 인스턴스가 공유)
static XMFLOAT4X4 ComputeViewProj(const Camera& camera, float aspectRatio)
{
	// View: 월드 → 카메라. Projection: 카메라 → 클립 공간
	XMMATRIX view = camera.ViewMatrix();
	XMMATRIX proj = camera.ProjectionMatrix(aspectRatio);

	XMFLOAT4X4 result;
	XMStoreFloat4x4(&result, view * proj);
	return result;
}

// 인스턴스마다 World 행렬 계산. x는 가운데 정렬, z는 원점에서 앞쪽(+Z)으로 늘어놓음
// 큐브마다 회전 위상을 조금씩 다르게 해서 각자 다른 각도로 돌게 함
static void BuildInstances(float angleRadians, InstanceData* out)
{
	const float halfWidth = (kGridSize - 1) * kGridSpacing * 0.5f;

	for (UINT z = 0; z < kGridSize; ++z)
	{
		for (UINT x = 0; x < kGridSize; ++x)
		{
			const UINT  index = z * kGridSize + x;
			const float phase = static_cast<float>(index) * 0.15f;

			// 행 벡터 규약: v * Rotation * Translation (제자리에서 돌고 → 격자 위치로 이동)
			XMMATRIX world = XMMatrixRotationY(angleRadians + phase)
				* XMMatrixTranslation(x * kGridSpacing - halfWidth, 0.0f, z * kGridSpacing);
			XMStoreFloat4x4(&out[index].World, world);
		}
	}
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
	Camera         camera;
	InstanceBuffer instanceBuffer;

	float cubeAngle = 0.0f; // 라디안
	InstanceData instances[kNumInstances];

	int exitCode = 0;
	try
	{
		gfx.Init(hwnd);
		cube.Init(gfx.Device());
		instanceBuffer.Init(gfx.Device(), GraphicsDevice::kNumFramesInFlight, kNumInstances);
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
			controlPanel.Draw(gfx.Width(), gfx.Height(), camera);

			// 업데이트: 카메라 이동 / 회전, 큐브 회전 각도 누적 → ViewProj / 인스턴스 World 계산
			const float deltaTime = ImGui::GetIO().DeltaTime;
			camera.Update(deltaTime);
			cubeAngle += XMConvertToRadians(controlPanel.RotationSpeed()) * deltaTime;
			const float aspectRatio = static_cast<float>(gfx.Width()) / static_cast<float>(gfx.Height());
			const XMFLOAT4X4 viewProj = ComputeViewProj(camera, aspectRatio);
			BuildInstances(cubeAngle, instances);

			ID3D12GraphicsCommandList* commandList = gfx.BeginFrame(controlPanel.ClearColor());

			// 인스턴스 데이터 업로드. BeginFrame이 이 프레임 번호의 GPU 작업이 끝나길 기다렸으므로
			// 이제 이 프레임 번호의 버퍼를 덮어써도 안전함
			const D3D12_GPU_VIRTUAL_ADDRESS instanceAddress = instanceBuffer.Upload(gfx.FrameIndex(), instances, kNumInstances);

			// 큐브 드로우: 틀(루트 시그니처) → 처리 방법(PSO) → 데이터(버퍼) → 그리기
			commandList->SetGraphicsRootSignature(rootSignature.Get());
			commandList->SetPipelineState(basicPipeline.Get());
			commandList->SetGraphicsRoot32BitConstants(RootSignature::kViewProjParam, 16, &viewProj, 0);
			commandList->SetGraphicsRootShaderResourceView(RootSignature::kInstanceParam, instanceAddress);
			commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			commandList->IASetVertexBuffers(0, 1, &cube.VertexBufferView());
			commandList->IASetIndexBuffer(&cube.IndexBufferView());
			// 같은 큐브 메시를 kNumInstances번 반복. 반복마다 VS의 SV_InstanceID가 0, 1, 2, ...
			commandList->DrawIndexedInstanced(cube.IndexCount(), kNumInstances, 0, 0, 0);

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
	instanceBuffer.Shutdown();
	cube.Shutdown();
	gfx.Shutdown();
	DestroyAppWindow(hwnd, hInstance);

	return exitCode;
}
