#include "GraphicsDevice.h"

#ifdef _DEBUG
#include <dxgidebug.h>
#endif

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#ifdef _DEBUG
#pragma comment(lib, "dxguid.lib")
#endif

// ---------------------------------------------------------------------------
// 생성 / 해제
// ---------------------------------------------------------------------------
void GraphicsDevice::Init(HWND hwnd)
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

	ThrowIfFailed(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device)), "D3D12CreateDevice");

#ifdef _DEBUG
	// 디버그 레이어가 에러를 보고하면 그 자리에서 멈춤 (VS 디버거에서 호출 스택 확인 가능)
	ComPtr<ID3D12InfoQueue> infoQueue;
	if (SUCCEEDED(m_device.As(&infoQueue)))
	{
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
	}
#endif

	// 커맨드 큐
	{
		D3D12_COMMAND_QUEUE_DESC desc = {};
		desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		ThrowIfFailed(m_device->CreateCommandQueue(&desc, IID_PPV_ARGS(&m_commandQueue)), "CreateCommandQueue");
	}

	// RTV 힙
	{
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		desc.NumDescriptors = kNumBackBuffers;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		ThrowIfFailed(m_device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_rtvHeap)), "CreateDescriptorHeap(RTV)");

		m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		D3D12_CPU_DESCRIPTOR_HANDLE handle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
		for (UINT i = 0; i < kNumBackBuffers; i++)
		{
			m_backBufferRtv[i] = handle;
			handle.ptr += m_rtvDescriptorSize;
		}
	}

	// 프레임별 커맨드 할당기 + 커맨드 리스트
	for (UINT i = 0; i < kNumFramesInFlight; i++)
	{
		ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
			IID_PPV_ARGS(&m_frameContexts[i].CommandAllocator)), "CreateCommandAllocator");
	}
	ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
		m_frameContexts[0].CommandAllocator.Get(), nullptr, IID_PPV_ARGS(&m_commandList)), "CreateCommandList");
	ThrowIfFailed(m_commandList->Close(), "CommandList::Close");

	// 펜스
	ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)), "CreateFence");
	m_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
	if (m_fenceEvent == nullptr)
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
		ThrowIfFailed(factory->CreateSwapChainForHwnd(m_commandQueue.Get(), hwnd, &desc, nullptr, nullptr, &swapChain1),
			"CreateSwapChainForHwnd");
		ThrowIfFailed(swapChain1.As(&m_swapChain), "IDXGISwapChain3");
		ThrowIfFailed(factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER), "MakeWindowAssociation");

		DXGI_SWAP_CHAIN_DESC1 actual = {};
		m_swapChain->GetDesc1(&actual);
		m_width = actual.Width;
		m_height = actual.Height;
	}

	CreateRenderTargets();

	//dsv 힙을 설정한다
	{
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		desc.NumDescriptors = 1;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		ThrowIfFailed(m_device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_dsvHeap)), "CreateDescriptorHeap(DSV)");

		m_depthBufferDsv = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
	}

	CreateDepthBuffer();
}

void GraphicsDevice::Shutdown()
{
	m_depthBuffer.Reset();
	m_dsvHeap.Reset();
	CleanupRenderTargets();
	m_swapChain.Reset();
	for (UINT i = 0; i < kNumFramesInFlight; i++)
		m_frameContexts[i].CommandAllocator.Reset();
	m_commandList.Reset();
	m_commandQueue.Reset();
	m_rtvHeap.Reset();
	m_fence.Reset();
	if (m_fenceEvent)
	{
		CloseHandle(m_fenceEvent);
		m_fenceEvent = nullptr;
	}
	m_device.Reset();

#ifdef _DEBUG
	ComPtr<IDXGIDebug1> dxgiDebug;
	if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgiDebug))))
		dxgiDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_FLAGS(DXGI_DEBUG_RLO_SUMMARY | DXGI_DEBUG_RLO_IGNORE_INTERNAL));
#endif
}

void GraphicsDevice::CreateRenderTargets()
{
	for (UINT i = 0; i < kNumBackBuffers; i++)
	{
		ThrowIfFailed(m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_backBuffers[i])), "GetBuffer");
		m_device->CreateRenderTargetView(m_backBuffers[i].Get(), nullptr, m_backBufferRtv[i]);
	}
}

void GraphicsDevice::CleanupRenderTargets()
{
	for (UINT i = 0; i < kNumBackBuffers; i++)
		m_backBuffers[i].Reset();
}

void GraphicsDevice::CreateDepthBuffer()
{
	D3D12_HEAP_PROPERTIES heapProps = {};
	heapProps.Type = D3D12_HEAP_TYPE_DEFAULT; //default

	D3D12_RESOURCE_DESC depthDesc = {};
	depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	depthDesc.Width = m_width;
	depthDesc.Height = m_height;
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

	ThrowIfFailed(m_device->CreateCommittedResource(
		&heapProps, D3D12_HEAP_FLAG_NONE, &depthDesc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&depthClearValue,
		IID_PPV_ARGS(&m_depthBuffer)), "CreatecommittedResource(Depth)");

	m_device->CreateDepthStencilView(m_depthBuffer.Get(), nullptr, m_depthBufferDsv);
}

// ---------------------------------------------------------------------------
// 동기화
// ---------------------------------------------------------------------------
void GraphicsDevice::WaitForGpu()
{
	UINT64 fenceValue = ++m_fenceLastSignaledValue;
	ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), fenceValue), "Signal");
	if (m_fence->GetCompletedValue() < fenceValue)
	{
		ThrowIfFailed(m_fence->SetEventOnCompletion(fenceValue, m_fenceEvent), "SetEventOnCompletion");
		WaitForSingleObject(m_fenceEvent, INFINITE);
	}
}

void GraphicsDevice::WaitForGpuOnExit()
{
	if (!m_commandQueue || !m_fence || !m_fenceEvent)
		return;
	try
	{
		WaitForGpu();
	}
	catch (const std::exception&)
	{
	}
}

GraphicsDevice::FrameContext& GraphicsDevice::WaitForNextFrameContext()
{
	FrameContext& frame = m_frameContexts[m_frameIndex % kNumFramesInFlight];
	if (m_fence->GetCompletedValue() < frame.FenceValue)
	{
		ThrowIfFailed(m_fence->SetEventOnCompletion(frame.FenceValue, m_fenceEvent), "SetEventOnCompletion");
		WaitForSingleObject(m_fenceEvent, INFINITE);
	}
	return frame;
}

// ---------------------------------------------------------------------------
// 스왑체인
// ---------------------------------------------------------------------------
bool GraphicsDevice::IsOccluded()
{
	if (m_swapChainOccluded && m_swapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED)
		return true;
	m_swapChainOccluded = false;
	return false;
}

void GraphicsDevice::Resize(UINT width, UINT height)
{
	WaitForGpu();
	CleanupRenderTargets();
	m_depthBuffer.Reset();
	ThrowIfFailed(m_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0), "ResizeBuffers");
	m_width = width;
	m_height = height;
	CreateRenderTargets();
	CreateDepthBuffer();
}

// ---------------------------------------------------------------------------
// 프레임
// ---------------------------------------------------------------------------
ID3D12GraphicsCommandList* GraphicsDevice::BeginFrame(const float clearColor[4])
{
	m_currentFrame = &WaitForNextFrameContext();
	m_currentBackBuffer = m_swapChain->GetCurrentBackBufferIndex();
	ID3D12Resource* backBuffer = m_backBuffers[m_currentBackBuffer].Get();

	ThrowIfFailed(m_currentFrame->CommandAllocator->Reset(), "CommandAllocator::Reset");
	ThrowIfFailed(m_commandList->Reset(m_currentFrame->CommandAllocator.Get(), nullptr), "CommandList::Reset");

	D3D12_RESOURCE_BARRIER toRenderTarget = TransitionBarrier(backBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	m_commandList->ResourceBarrier(1, &toRenderTarget);

	m_commandList->ClearRenderTargetView(m_backBufferRtv[m_currentBackBuffer], clearColor, 0, nullptr);
	m_commandList->ClearDepthStencilView(m_depthBufferDsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr); // 1.0 = 가장 먼 값
	m_commandList->OMSetRenderTargets(1, &m_backBufferRtv[m_currentBackBuffer], FALSE, &m_depthBufferDsv);

	D3D12_VIEWPORT viewport = { 0.0f, 0.0f, static_cast<float>(m_width), static_cast<float>(m_height), 0.0f, 1.0f };
	D3D12_RECT scissor = { 0, 0, static_cast<LONG>(m_width), static_cast<LONG>(m_height) };
	m_commandList->RSSetViewports(1, &viewport);
	m_commandList->RSSetScissorRects(1, &scissor);

	return m_commandList.Get();
}

void GraphicsDevice::EndFrame()
{
	// 이번 프레임에 그린 백 버퍼를 꺼냄 
	ID3D12Resource* backBuffer = m_backBuffers[m_currentBackBuffer].Get();

	// 렌더 타겟 : gpu가 그림을 그려넣는 대상의 이미지. 픽셀셰이더가 계산한 색이 최종적으로 여기에 써짐. 지금 코드에서는 스왑체인의 백버퍼가 렌더타겟
	// beginframe의 OMSetRenderTargets가 이번에 이 백버퍼에 그려라라고 지정함

	// present : 다 그린 이미지를 화면에 내보내는것 스왑체인의 present가 그일을 함. present상태는 그리기가 끝났고 화면에 내보낼 준비가 된 상태
	D3D12_RESOURCE_BARRIER toPresent = TransitionBarrier(backBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
	m_commandList->ResourceBarrier(1, &toPresent);

	// 이 프레임의 명령은 어기까지. 리스트를 닫음
	ThrowIfFailed(m_commandList->Close(), "CommandList::Close");

	// gpu에 제출. 커맨드 큐에 리스트를 넣음 
	ID3D12CommandList* lists[] = { m_commandList.Get() };
	m_commandQueue->ExecuteCommandLists(1, lists);

	// 화면에 표시요청 "이 백버퍼를 화면에 보여주고, 다음 백버퍼로 넘겨라" 라는 요청 이것도 큐에 들어가서 그리기 작업이 끝나고 처리됨
	HRESULT hr = m_swapChain->Present(1, 0); // VSync
	m_swapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);
	ThrowIfFailed(hr, "Present");

	// 펜스로 이 프레임 완료 표시 예약
	// signal로 큐에 앞의 작업이 다 끝나면 펜스값을 이 번호로 바꿔라 라는 명령을 넣음
	UINT64 fenceValue = ++m_fenceLastSignaledValue;
	ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), fenceValue), "Signal");
	m_currentFrame->FenceValue = fenceValue;
	m_frameIndex++;
}
