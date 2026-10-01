#include "Common.hlsli"

// 루트 시그니처 파라미터 0 (32비트 상수 16개 = float4x4)
// C++(DirectXMath)은 행 우선, HLSL cbuffer 기본은 열 우선이라 C++에서 전치해서 넘김
cbuffer Transform : register(b0)
{
	float4x4 g_mvp; // 로컬 → 월드 → 뷰 → 클립 공간
};

// 로컬 좌표 정점을 MVP로 클립 공간 좌표로 변환
VSOutput VSMain(VSInput input)
{
	VSOutput output;
	output.Position = mul(float4(input.Position, 1.0f), g_mvp); // 행 벡터 × 행렬 (DirectXMath와 같은 순서)
	output.Color = input.Color;
	return output;
}
