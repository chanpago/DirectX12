#pragma once

#include "D3DUtil.h"

// 셰이더에 넘길 파라미터의 틀 (종류와 순서). 여러 PSO가 공유
// - 파라미터 0: 32비트 상수 16개 = float4x4 (b0, VS 전용) → MVP 행렬용
class RootSignature
{
public:
	// SetGraphicsRoot…의 첫 인자로 쓰는 파라미터 번호
	static constexpr UINT kTransformParam = 0;

	void Init(ID3D12Device* device);
	void Shutdown();

	ID3D12RootSignature* Get() const { return m_rootSignature.Get(); }

private:
	ComPtr<ID3D12RootSignature> m_rootSignature;
};
