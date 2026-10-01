#include "Mesh.h"

#include <cstdint>
#include <cstring>

// ---------------------------------------------------------------------------
// 큐브 데이터 (로컬 좌표계: 원점 중심, 한 변 1, 왼손 / Y-up / +Z 앞쪽)
// ---------------------------------------------------------------------------
// 색은 좌표 부호를 RGB로 매핑 (-0.5 → 0, +0.5 → 1)
static const Vertex kCubeVertices[] =
{
	{ { -0.5f, -0.5f, -0.5f }, { 0.0f, 0.0f, 0.0f, 1.0f } }, // 0 앞 왼 아래 (검정)
	{ { -0.5f, +0.5f, -0.5f }, { 0.0f, 1.0f, 0.0f, 1.0f } }, // 1 앞 왼 위   (초록)
	{ { +0.5f, +0.5f, -0.5f }, { 1.0f, 1.0f, 0.0f, 1.0f } }, // 2 앞 오 위   (노랑)
	{ { +0.5f, -0.5f, -0.5f }, { 1.0f, 0.0f, 0.0f, 1.0f } }, // 3 앞 오 아래 (빨강)
	{ { -0.5f, -0.5f, +0.5f }, { 0.0f, 0.0f, 1.0f, 1.0f } }, // 4 뒤 왼 아래 (파랑)
	{ { -0.5f, +0.5f, +0.5f }, { 0.0f, 1.0f, 1.0f, 1.0f } }, // 5 뒤 왼 위   (청록)
	{ { +0.5f, +0.5f, +0.5f }, { 1.0f, 1.0f, 1.0f, 1.0f } }, // 6 뒤 오 위   (흰색)
	{ { +0.5f, -0.5f, +0.5f }, { 1.0f, 0.0f, 1.0f, 1.0f } }, // 7 뒤 오 아래 (자홍)
};

// 면마다 삼각형 2개. 그 면을 큐브 바깥에서 바라봤을 때 시계방향 (= 앞면)
// DXGI_FORMAT_R16_UINT 인덱스 버퍼용
static const uint16_t kCubeIndices[] =
{
	0, 1, 2,  0, 2, 3, // 앞   (z = -0.5)
	7, 6, 5,  7, 5, 4, // 뒤   (z = +0.5)
	4, 5, 1,  4, 1, 0, // 왼쪽 (x = -0.5)
	3, 2, 6,  3, 6, 7, // 오른쪽 (x = +0.5)
	1, 5, 6,  1, 6, 2, // 위   (y = +0.5)
	4, 0, 3,  4, 3, 7, // 아래 (y = -0.5)
};
static_assert(_countof(kCubeIndices) == 36, "큐브 인덱스는 6면 x 2삼각형 x 3 = 36개");

// ---------------------------------------------------------------------------
// 버퍼 생성
// ---------------------------------------------------------------------------
// data를 size 바이트만큼 담은 UPLOAD 힙 버퍼를 만들어 반환
static ComPtr<ID3D12Resource> CreateUploadBuffer(ID3D12Device* device, const void* data, UINT64 size)
{
	// 1. 힙 속성: CPU가 쓸 수 있는 UPLOAD 힙
	D3D12_HEAP_PROPERTIES heapProps = {};
	heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

	// 2. 리소스 설명: size 바이트짜리 1차원 버퍼
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

	// 3. 생성: UPLOAD 힙은 GENERIC_READ 상태로 시작해야 함, clear value 없음
	ComPtr<ID3D12Resource> buffer;
	ThrowIfFailed(device->CreateCommittedResource(
		&heapProps, D3D12_HEAP_FLAG_NONE, &desc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&buffer)), "CreateCommittedResource(Upload)");

	// 4. Map: GPU 메모리를 CPU 포인터로 얻음. readRange {0, 0} = CPU는 읽지 않음
	void* mapped = nullptr;
	D3D12_RANGE readRange = { 0, 0 };
	ThrowIfFailed(buffer->Map(0, &readRange, &mapped), "Map");

	// 5. 복사
	memcpy(mapped, data, static_cast<size_t>(size));

	// 6. Unmap: nullptr = 전체 범위에 썼다
	buffer->Unmap(0, nullptr);

	// 7. 반환 (ComPtr이 소유권을 호출한 쪽으로 넘김)
	return buffer;
}

// ---------------------------------------------------------------------------
// Mesh
// ---------------------------------------------------------------------------
void Mesh::Init(ID3D12Device* device)
{
	// 1. 버텍스 버퍼: GPU 메모리 할당 + 정점 데이터 복사
	m_vertexBuffer = CreateUploadBuffer(device, kCubeVertices, sizeof(kCubeVertices));

	// 2. 버텍스 버퍼 뷰: 어디서, 얼마나, 몇 바이트씩 끊어 읽을지
	m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
	m_vertexBufferView.SizeInBytes = static_cast<UINT>(sizeof(kCubeVertices));
	m_vertexBufferView.StrideInBytes = sizeof(Vertex);

	// 3. 인덱스 버퍼: GPU 메모리 할당 + 인덱스 데이터 복사
	m_indexBuffer = CreateUploadBuffer(device, kCubeIndices, sizeof(kCubeIndices));

	// 4. 인덱스 버퍼 뷰: 어디서, 얼마나, 인덱스 하나가 몇 비트인지
	m_indexBufferView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
	m_indexBufferView.SizeInBytes = static_cast<UINT>(sizeof(kCubeIndices));
	m_indexBufferView.Format = DXGI_FORMAT_R16_UINT;

	// 5. 드로우할 때 쓸 인덱스 개수
	m_indexCount = _countof(kCubeIndices);
}

void Mesh::Shutdown()
{
	m_vertexBuffer.Reset();
	m_indexBuffer.Reset();
}

const D3D12_VERTEX_BUFFER_VIEW& Mesh::VertexBufferView() const
{
	return m_vertexBufferView;
}

const D3D12_INDEX_BUFFER_VIEW& Mesh::IndexBufferView() const
{
	return m_indexBufferView;
}

UINT Mesh::IndexCount() const
{
	return m_indexCount;
}
