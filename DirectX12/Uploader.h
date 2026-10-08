#pragma once

#include "D3DUtil.h"

#include <vector>

class GraphicsDevice;

// 정적 데이터(메시 VB/IB 등)를 DEFAULT 힙 버퍼로 올리는 클래스
// - CPU는 DEFAULT 힙에 직접 쓸 수 없으므로: 임시 UPLOAD 버퍼에 쓰기 → GPU가 CopyBufferRegion으로 복사
// - 복사 명령은 이 클래스의 커맨드 리스트에 모아서 기록하고, End에서 한 번에 제출 + 완료 대기
// - 임시 UPLOAD 버퍼는 GPU 복사가 끝날 때까지 살아 있어야 하므로 End까지 여기서 보관
//
// 사용법:
//   uploader.Begin(gfx);
//   cube.Init(uploader);   // 안에서 CreateDefaultBuffer 호출 (기록만)
//   uploader.End();        // 제출 → 대기 → 임시 버퍼 해제
class Uploader
{
public:
	// 업로드 시작: 업로드 전용 커맨드 리스트를 열어 기록할 준비
	void Begin(GraphicsDevice& gfx);

	// size 바이트짜리 DEFAULT 힙 버퍼를 만들고, data를 복사하는 명령을 기록
	// - 반환 시점에는 아직 복사되지 않음 (End 이후에 완료)
	// - finalState: 복사 후 버퍼를 둘 상태 (VB: VERTEX_AND_CONSTANT_BUFFER, IB: INDEX_BUFFER)
	ComPtr<ID3D12Resource> CreateDefaultBuffer(const void* data, UINT64 size, D3D12_RESOURCE_STATES finalState);

	// 기록 종료: Close → 제출 → GPU 완료 대기 → 임시 UPLOAD 버퍼 해제
	void End();

	// 리소스 해제
	void Shutdown();

private:
	GraphicsDevice*                     m_gfx = nullptr;
	// GraphicsDevice의 프레임용 리스트와 따로 둠
	// → 프레임 루프와 상관없이 언제든 열고 닫을 수 있고, gfx.Init의 Close를 건드리지 않아도 됨
	ComPtr<ID3D12CommandAllocator>      m_commandAllocator;
	ComPtr<ID3D12GraphicsCommandList>   m_commandList;
	// End에서 GPU 복사가 끝날 때까지 살려 둘 임시 UPLOAD 버퍼들
	std::vector<ComPtr<ID3D12Resource>> m_pendingUploads;
};
