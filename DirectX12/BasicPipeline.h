#pragma once

#include "D3DUtil.h"

// BasicVS / BasicPS + Vertex(POSITION, COLOR) 입력으로 그리는 PSO
// - 컬링 없음, 블렌드 없음, 깊이 테스트 켬 (더 가까운 픽셀만 남김)
class BasicPipeline
{
public:
	// rootSignature: PSO가 참조할 루트 시그니처. 드로우할 때도 같은 것을 설정해야 함
	// rtvFormat / dsvFormat: 드로우할 때 붙어 있는 렌더 타깃 / 깊이 버퍼의 포맷과 같아야 함
	void Init(ID3D12Device* device, ID3D12RootSignature* rootSignature, DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat);
	void Shutdown();

	ID3D12PipelineState* Get() const { return m_pipelineState.Get(); }

private:
	ComPtr<ID3D12PipelineState> m_pipelineState;
};
