#pragma once

#include "D3DUtil.h"

#include <DirectXMath.h>

// 인스턴스 하나의 데이터. BasicVS.hlsl의 InstanceData와 반드시 일치해야 함
// (StructuredBuffer는 C++ 구조체를 그대로 배열로 읽으므로 크기 / 순서가 같아야 함)
struct InstanceData
{
	DirectX::XMFLOAT4X4 World; // 로컬 → 월드 (전치하지 않음. 셰이더에서 row_major로 받음)
};
static_assert(sizeof(InstanceData) == 64, "InstanceData 크기가 셰이더와 다름");

// 인스턴스 데이터를 담는 StructuredBuffer (UPLOAD 힙, 계속 Map해 둠)
// - CPU가 매 프레임 새로 쓰므로 프레임 수(kNumFramesInFlight)만큼 따로 둠
//   GPU가 이전 프레임 버퍼를 읽는 중에 CPU가 덮어쓰지 않게 하기 위함
// - 루트 SRV(t0)로 바인딩하므로 디스크립터 힙이 필요 없음
class InstanceBuffer
{
public:
	static constexpr UINT kMaxFrames = 3;

	// maxInstances: 한 프레임에 쓸 수 있는 최대 인스턴스 수
	void Init(ID3D12Device* device, UINT numFrames, UINT maxInstances);
	void Shutdown();

	// frameIndex번 버퍼에 인스턴스 데이터를 복사하고, 그 버퍼의 GPU 주소를 반환
	// GPU가 그 버퍼를 다 쓴 뒤(= GraphicsDevice::BeginFrame 이후)에 호출해야 함
	D3D12_GPU_VIRTUAL_ADDRESS Upload(UINT frameIndex, const InstanceData* data, UINT count);

private:
	ComPtr<ID3D12Resource> m_buffers[kMaxFrames];
	InstanceData*          m_mapped[kMaxFrames] = {}; // Map해 둔 CPU 포인터
	UINT                   m_numFrames = 0;
	UINT                   m_maxInstances = 0;
};
