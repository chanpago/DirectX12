#include "Mesh.h"
#include "Uploader.h"

#include <cstdint>

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
// Mesh
// ---------------------------------------------------------------------------
void Mesh::Init(Uploader& uploader)
{
	// 1. 버텍스 버퍼: DEFAULT 힙에 할당 + 정점 데이터 복사 명령 기록 (복사 후 VB로 읽는 상태)
	m_vertexBuffer = uploader.CreateDefaultBuffer(kCubeVertices, sizeof(kCubeVertices),
		D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);

	// 2. 버텍스 버퍼 뷰: 어디서, 얼마나, 몇 바이트씩 끊어 읽을지
	m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
	m_vertexBufferView.SizeInBytes = static_cast<UINT>(sizeof(kCubeVertices));
	m_vertexBufferView.StrideInBytes = sizeof(Vertex);

	// 3. 인덱스 버퍼: DEFAULT 힙에 할당 + 인덱스 데이터 복사 명령 기록 (복사 후 IB로 읽는 상태)
	m_indexBuffer = uploader.CreateDefaultBuffer(kCubeIndices, sizeof(kCubeIndices),
		D3D12_RESOURCE_STATE_INDEX_BUFFER);

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
