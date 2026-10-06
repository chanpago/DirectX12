#include "Common.hlsli"

// 루트 시그니처 파라미터 0 (32비트 상수 16개 = float4x4). 프레임당 한 번, 모든 인스턴스가 공유
// C++(DirectXMath)은 행 우선, HLSL cbuffer 기본은 열 우선이라 row_major로 받음 (C++에서 전치 안 함)
cbuffer Frame : register(b0)
{
	row_major float4x4 g_viewProj; // 월드 → 뷰 → 클립 공간
};

// 루트 시그니처 파라미터 1 (루트 SRV). 인스턴스마다 하나씩
// C++ InstanceData(InstanceBuffer.h)와 크기 / 순서가 같아야 함
struct InstanceData
{
	row_major float4x4 World; // 로컬 → 월드
};
StructuredBuffer<InstanceData> g_instances : register(t0);

// instanceId: DrawIndexedInstanced가 같은 메시를 반복할 때 몇 번째 반복인지 (0 ~ instanceCount-1)
//             입력 조립기가 자동으로 채워 줌
VSOutput VSMain(VSInput input, uint instanceId : SV_InstanceID)
{
	VSOutput output;
	float4x4 world = g_instances[instanceId].World;              // 내 번호의 World 행렬
	float4 worldPosition = mul(float4(input.Position, 1.0f), world); // 로컬 → 월드
	output.Position = mul(worldPosition, g_viewProj);            // 월드 → 클립 (행 벡터 × 행렬)
	output.Color = input.Color;
	return output;
}
