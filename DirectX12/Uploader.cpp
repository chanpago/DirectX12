#include "Uploader.h"
#include "GraphicsDevice.h"

#include <cstring>

// ---------------------------------------------------------------------------
// 버퍼 생성 헬퍼
// ---------------------------------------------------------------------------
// size 바이트짜리 1차원 버퍼 리소스를 heapType 힙에 initialState 상태로 만든다
static ComPtr<ID3D12Resource> CreateBuffer(ID3D12Device* device, D3D12_HEAP_TYPE heapType, UINT64 size,
	D3D12_RESOURCE_STATES initialState, const char* what)
{
	D3D12_HEAP_PROPERTIES heapProps = {};
	heapProps.Type = heapType;

	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Width = size;
	desc.Height = 1;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = DXGI_FORMAT_UNKNOWN;
	desc.SampleDesc.Count = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	desc.Flags = D3D12_RESOURCE_FLAG_NONE;

	ComPtr<ID3D12Resource> buffer;
	ThrowIfFailed(device->CreateCommittedResource(
		&heapProps, D3D12_HEAP_FLAG_NONE, &desc,
		initialState,
		nullptr,
		IID_PPV_ARGS(&buffer)), what);
	return buffer;
}

// ---------------------------------------------------------------------------
// Uploader
// ---------------------------------------------------------------------------
void Uploader::Begin(GraphicsDevice& gfx)
{
	m_gfx = &gfx;
	ID3D12Device* device = gfx.Device();

	// 처음 한 번만 만듦. 리스트는 열린 상태로 만들어지므로 바로 닫아 두고, 아래에서 Reset으로 연다
	if (!m_commandAllocator)
	{
		ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
			IID_PPV_ARGS(&m_commandAllocator)), "CreateCommandAllocator(Upload)");
		ThrowIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
			m_commandAllocator.Get(), nullptr, IID_PPV_ARGS(&m_commandList)), "CreateCommandList(Upload)");
		ThrowIfFailed(m_commandList->Close(), "CommandList::Close(Upload)");
	}

	// 기록 시작. 이전 End에서 GPU 완료까지 기다렸으므로 할당자를 Reset해도 안전
	ThrowIfFailed(m_commandAllocator->Reset(), "CommandAllocator::Reset(Upload)");
	ThrowIfFailed(m_commandList->Reset(m_commandAllocator.Get(), nullptr), "CommandList::Reset(Upload)");
}

ComPtr<ID3D12Resource> Uploader::CreateDefaultBuffer(const void* data, UINT64 size, D3D12_RESOURCE_STATES finalState)
{
	ID3D12Device* device = m_gfx->Device();

	// 1. 진짜 버퍼: DEFAULT 힙 (VRAM). CPU는 Map할 수 없음
	//    버퍼는 초기 상태를 무엇으로 넘겨도 COMMON으로 만들어지므로 COMMON으로 만들고 배리어로 바꾼다
	ComPtr<ID3D12Resource> defaultBuffer = CreateBuffer(device, D3D12_HEAP_TYPE_DEFAULT, size,
		D3D12_RESOURCE_STATE_COMMON, "CreateCommittedResource(Default)");

	// 2. 임시 버퍼: UPLOAD 힙. CPU가 Map해서 데이터를 써 둔다
	ComPtr<ID3D12Resource> uploadBuffer = CreateBuffer(device, D3D12_HEAP_TYPE_UPLOAD, size,
		D3D12_RESOURCE_STATE_GENERIC_READ, "CreateCommittedResource(Upload)");

	void* mapped = nullptr;
	D3D12_RANGE readRange = { 0, 0 }; // CPU는 읽지 않음
	ThrowIfFailed(uploadBuffer->Map(0, &readRange, &mapped), "Map(Upload)");
	memcpy(mapped, data, static_cast<size_t>(size));
	uploadBuffer->Unmap(0, nullptr);

	// 3. 복사 명령 기록 (여기서는 기록만. 실제 복사는 End에서 제출한 뒤 GPU가 실행)
	//    COMMON → COPY_DEST: 복사 대상으로 쓰겠다고 알림
	D3D12_RESOURCE_BARRIER toCopyDest = TransitionBarrier(defaultBuffer.Get(),
		D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
	m_commandList->ResourceBarrier(1, &toCopyDest);

	//    UPLOAD 버퍼의 0번 바이트부터 size 바이트를 DEFAULT 버퍼의 0번 바이트로
	m_commandList->CopyBufferRegion(defaultBuffer.Get(), 0, uploadBuffer.Get(), 0, size);

	//    COPY_DEST → finalState: 복사가 끝나야 VB/IB로 읽을 수 있음 (GPU 안의 순서는 배리어가 보장)
	D3D12_RESOURCE_BARRIER toFinal = TransitionBarrier(defaultBuffer.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST, finalState);
	m_commandList->ResourceBarrier(1, &toFinal);

	// 4. 임시 버퍼는 GPU가 복사를 끝낼 때까지 살아 있어야 하므로 End까지 보관
	m_pendingUploads.push_back(uploadBuffer);

	return defaultBuffer;
}

void Uploader::End()
{
	// 1. 기록 끝
	ThrowIfFailed(m_commandList->Close(), "CommandList::Close(Upload)");

	// 2. GPU에 제출 (바로 반환됨. GPU는 이후에 실행)
	ID3D12CommandList* lists[] = { m_commandList.Get() };
	m_gfx->CommandQueue()->ExecuteCommandLists(1, lists);

	// 3. 복사가 끝날 때까지 CPU 대기 (큐에 Signal → 펜스 값 도달까지 기다림)
	m_gfx->WaitForGpu();

	// 4. 이제 GPU가 임시 버퍼를 다 읽었으므로 해제해도 안전
	m_pendingUploads.clear();
}

void Uploader::Shutdown()
{
	m_pendingUploads.clear();
	m_commandList.Reset();
	m_commandAllocator.Reset();
	m_gfx = nullptr;
}
