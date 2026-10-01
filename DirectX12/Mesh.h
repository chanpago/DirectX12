#pragma once

#include "D3DUtil.h"

#include <DirectXMath.h>

// 정점 하나의 메모리 배치. 아래 세 곳과 반드시 일치해야 함
// - Common.hlsli의 VSInput (시맨틱, 타입)
// - PSO 입력 레이아웃 (시맨틱, 포맷, 오프셋)
// - 버텍스 버퍼 뷰의 StrideInBytes (= sizeof(Vertex))
struct Vertex
{
	DirectX::XMFLOAT3 Position; // 오프셋 0  → POSITION, DXGI_FORMAT_R32G32B32_FLOAT
	DirectX::XMFLOAT4 Color;    // 오프셋 12 → COLOR,    DXGI_FORMAT_R32G32B32A32_FLOAT
};
static_assert(sizeof(Vertex) == 28, "Vertex 크기가 입력 레이아웃과 다름");

// 큐브 하나의 버텍스 / 인덱스 버퍼 (UPLOAD 힙)와 뷰를 소유
class Mesh
{
public:
	// 큐브 버퍼 생성
	void Init(ID3D12Device* device);

	// 리소스 해제
	void Shutdown();

	// 드로우할 때 커맨드 리스트에 넘길 것들
	const D3D12_VERTEX_BUFFER_VIEW& VertexBufferView() const;
	const D3D12_INDEX_BUFFER_VIEW& IndexBufferView() const;
	UINT IndexCount() const;

private:
	ComPtr<ID3D12Resource>   m_vertexBuffer;      // 정점 데이터가 담긴 GPU 메모리
	ComPtr<ID3D12Resource>   m_indexBuffer;       // 인덱스 데이터가 담긴 GPU 메모리
	D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView = {};
	D3D12_INDEX_BUFFER_VIEW  m_indexBufferView = {};
	UINT                     m_indexCount = 0;
};
