#pragma once

#include "D3DUtil.h"

// 셰이더에 넘길 파라미터의 틀 (종류와 순서). 여러 PSO가 공유
// - 파라미터 0: 32비트 상수 16개 = float4x4 (b0, VS 전용) → View * Projection 행렬 (프레임당 한 번)
// - 파라미터 1: 루트 SRV (t0, VS 전용) → 인스턴스별 World 행렬이 담긴 StructuredBuffer의 GPU 주소
class RootSignature
{
public:
	// SetGraphicsRoot…의 첫 인자로 쓰는 파라미터 번호
	static constexpr UINT kViewProjParam = 0;
	static constexpr UINT kInstanceParam = 1;

	void Init(ID3D12Device* device);
	void Shutdown();

	ID3D12RootSignature* Get() const { return m_rootSignature.Get(); }

private:
	ComPtr<ID3D12RootSignature> m_rootSignature;
};
