#pragma once

#include "D3DUtil.h"

#include <dxgi1_4.h>

// D3D12 디바이스 / 커맨드 큐 / 스왑체인 / 렌더 타깃 / 깊이 버퍼 / 프레임 동기화를 묶은 클래스
// - 렌더링 파이프라인(루트 시그니처, PSO, 버퍼, 드로우)은 여기 넣지 않음
class GraphicsDevice
{
public:
	static constexpr UINT        kNumFramesInFlight = 2;
	static constexpr UINT        kNumBackBuffers    = 3;
	static constexpr DXGI_FORMAT kBackBufferFormat  = DXGI_FORMAT_R8G8B8A8_UNORM;
	static constexpr DXGI_FORMAT kDepthFormat       = DXGI_FORMAT_D32_FLOAT;

	void Init(HWND hwnd);
	void Shutdown();

	// 큐에 시그널을 넣고 GPU가 해당 지점까지 끝낼 때까지 대기
	void WaitForGpu();
	// 종료 시 사용. 초기화 도중 실패했거나 디바이스가 제거된 경우에도 예외를 던지지 않음
	void WaitForGpuOnExit();

	// 창이 가려져 있어서 렌더링을 쉬어야 하면 true
	bool IsOccluded();
	void Resize(UINT width, UINT height);

	// 커맨드 리스트 기록 시작: 백버퍼를 렌더 타깃으로 전환하고 컬러/깊이 클리어, RTV+DSV/뷰포트/시저 설정
	ID3D12GraphicsCommandList* BeginFrame(const float clearColor[4]);
	// 커맨드 리스트 기록 종료: PRESENT로 전환, 제출, Present, 펜스 시그널
	void EndFrame();

	ID3D12Device*              Device() const       { return m_device.Get(); }
	ID3D12CommandQueue*        CommandQueue() const { return m_commandQueue.Get(); }
	ID3D12GraphicsCommandList* CommandList() const  { return m_commandList.Get(); }
	UINT                       Width() const        { return m_width; }
	UINT                       Height() const       { return m_height; }
	D3D12_CPU_DESCRIPTOR_HANDLE CurrentBackBufferRtv() const { return m_backBufferRtv[m_currentBackBuffer]; }
	D3D12_CPU_DESCRIPTOR_HANDLE DepthBufferDsv() const       { return m_depthBufferDsv; }

private:
	struct FrameContext
	{
		ComPtr<ID3D12CommandAllocator> CommandAllocator;
		UINT64                         FenceValue = 0;
	};

	void CreateRenderTargets();
	void CleanupRenderTargets();
	void CreateDepthBuffer();
	// 이번에 사용할 프레임 컨텍스트가 GPU에서 해제될 때까지 대기
	FrameContext& WaitForNextFrameContext();

	FrameContext                      m_frameContexts[kNumFramesInFlight];
	UINT                              m_frameIndex = 0;
	FrameContext*                     m_currentFrame = nullptr;
	UINT                              m_currentBackBuffer = 0;

	ComPtr<ID3D12Device>              m_device;
	ComPtr<ID3D12CommandQueue>        m_commandQueue;
	ComPtr<ID3D12GraphicsCommandList> m_commandList;
	ComPtr<ID3D12Fence>               m_fence;
	HANDLE                            m_fenceEvent = nullptr;
	UINT64                            m_fenceLastSignaledValue = 0;

	ComPtr<IDXGISwapChain3>           m_swapChain;
	bool                              m_swapChainOccluded = false;
	ComPtr<ID3D12DescriptorHeap>      m_rtvHeap;
	UINT                              m_rtvDescriptorSize = 0;
	ComPtr<ID3D12Resource>            m_backBuffers[kNumBackBuffers];
	D3D12_CPU_DESCRIPTOR_HANDLE       m_backBufferRtv[kNumBackBuffers] = {};

	ComPtr<ID3D12DescriptorHeap>      m_dsvHeap;
	ComPtr<ID3D12Resource>            m_depthBuffer;
	D3D12_CPU_DESCRIPTOR_HANDLE       m_depthBufferDsv = {};

	UINT                              m_width = 0;
	UINT                              m_height = 0;
};
