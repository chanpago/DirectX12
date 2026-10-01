#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>

#include "imgui.h"

// ImGui 초기화 / 프레임 / 렌더 / 종료를 한곳에 모은 래퍼
// - ImGui 폰트 텍스처용 SRV 디스크립터 힙을 직접 소유
class ImGuiLayer
{
public:
	// 창 생성 전에 호출. 주 모니터의 DPI 배율을 반환
	static float EnableDpiAwareness();

	// WndProc 맨 앞에서 호출. true면 ImGui가 메시지를 처리한 것
	static bool HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

	// rtvFormat / dsvFormat: ImGui를 그릴 때 붙어 있는 렌더 타깃 / 깊이 버퍼의 포맷
	void Init(HWND hwnd, ID3D12Device* device, ID3D12CommandQueue* commandQueue,
		UINT numFramesInFlight, DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat, float dpiScale);
	void Shutdown();

	// 매 프레임 UI 코드 작성 전에 호출
	void BeginFrame();

	// UI 작성을 마치고 커맨드 리스트에 ImGui 드로우를 기록
	void Render(ID3D12GraphicsCommandList* commandList);

private:
	struct DescriptorHeapAllocator
	{
		ID3D12DescriptorHeap*       Heap = nullptr;
		D3D12_CPU_DESCRIPTOR_HANDLE HeapStartCpu = {};
		D3D12_GPU_DESCRIPTOR_HANDLE HeapStartGpu = {};
		UINT                        HeapHandleIncrement = 0;
		ImVector<int>               FreeIndices;

		void Create(ID3D12Device* device, ID3D12DescriptorHeap* heap);
		void Destroy();
		void Alloc(D3D12_CPU_DESCRIPTOR_HANDLE* outCpu, D3D12_GPU_DESCRIPTOR_HANDLE* outGpu);
		void Free(D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu);
	};

	static constexpr UINT kSrvHeapSize = 64;

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_srvHeap;
	DescriptorHeapAllocator                      m_srvHeapAlloc;
	bool                                         m_initialized = false;
};
