#include "InstanceBuffer.h"

#include <cstring>

void InstanceBuffer::Init(ID3D12Device* device, UINT numFrames, UINT maxInstances)
{
	if (numFrames > kMaxFrames)
		throw std::runtime_error("InstanceBuffer: numFrames > kMaxFrames");

	m_numFrames = numFrames;
	m_maxInstances = maxInstances;

	// 1. 힙 속성: CPU가 매 프레임 쓰므로 UPLOAD 힙
	D3D12_HEAP_PROPERTIES heapProps = {};
	heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

	// 2. 리소스 설명: InstanceData × maxInstances 바이트짜리 버퍼
	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Width = static_cast<UINT64>(sizeof(InstanceData)) * maxInstances;
	desc.Height = 1;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = DXGI_FORMAT_UNKNOWN;
	desc.SampleDesc.Count = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	desc.Flags = D3D12_RESOURCE_FLAG_NONE;

	// 현재 백 버퍼 수 만큼 만든다. gpu가 이전 프레임 버퍼를 읽는 동안 cpu가 다음 프레임 데이터를 쓰려면 버퍼가 따로 있어야함
	// m_buffers[0], m_mapped[0]  ← 짝수 프레임용
	// m_buffers[1], m_mapped[1]  ← 홀수 프레임용
	for (UINT i = 0; i < numFrames; ++i)
	{
		// 3. 생성: UPLOAD 힙은 GENERIC_READ (셰이더 리소스로 읽기 포함)
		// Committed Resource: 메모리(힙)와 리소스를 한 번에 만든다는 뜻. 힙을 따로 만들어 그 안에 리소스를 배치하는 방식(Placed Resource)도 있는데, Committed가 가장 간단
		ThrowIfFailed(device->CreateCommittedResource(
			&heapProps,									// 어느 메모리에: UPLOAD 힙 (반복문 위에서 설정)
			D3D12_HEAP_FLAG_NONE,						// 힙 추가 옵션: 없음
			&desc,										// 어떤 리소스: 6400바이트짜리 버퍼 (위에서 설정)
			D3D12_RESOURCE_STATE_GENERIC_READ,			// UPLOAD 힙 리소스는 반드시 이 상태로 만들어야 하고, 다른 상태로 바꿀 수도 없음 GPU가 읽을 수 있는 여러 상태(정점 버퍼, 상수 버퍼, 셰이더 리소스, 복사 원본 등)를 모두 합친 상태
			nullptr,
			IID_PPV_ARGS(&m_buffers[i])),				// 결과를 받을 곳
			"CreateCommittedResource(Instance)");

		// 4. Map해 두고 Unmap하지 않음 (UPLOAD 힙은 Map한 채로 GPU가 읽어도 됨)
		//    매 프레임 Map / Unmap 하는 비용을 아낌
		D3D12_RANGE readRange = { 0, 0 }; // CPU는 읽지 않음
		void* mapped = nullptr;
		ThrowIfFailed(m_buffers[i]->Map(0, &readRange, &mapped), "Map(Instance)");
		m_mapped[i] = static_cast<InstanceData*>(mapped);

		// Unmap하지 않는 이유 (Mesh와의 차이)
		// UPLOAD 힙은 Map한 상태로 두어도 GPU가 읽을 수 있음(persistent mapping). 그래서 Init에서 한 번 Map한 포인터를 m_mapped[i]에 계속 들고 있다가, 매 프레임 Upload()에서 그 포인터에 memcpy
		// Unmap은 Shutdown()에서 버퍼를 해제하기 직전에 한 번
	}
}

void InstanceBuffer::Shutdown()
{
	for (UINT i = 0; i < m_numFrames; ++i)
	{
		if (m_buffers[i])
			m_buffers[i]->Unmap(0, nullptr);
		m_buffers[i].Reset();
		m_mapped[i] = nullptr;
	}
	m_numFrames = 0;
}

D3D12_GPU_VIRTUAL_ADDRESS InstanceBuffer::Upload(UINT frameIndex, const InstanceData* data, UINT count)
{
	if (count > m_maxInstances)
		throw std::runtime_error("InstanceBuffer: count > maxInstances");

	memcpy(m_mapped[frameIndex], data, sizeof(InstanceData) * count);
	return m_buffers[frameIndex]->GetGPUVirtualAddress();
}
