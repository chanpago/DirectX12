#include "Common.hlsli"

// 정점 위치를 그대로 클립 공간 좌표로 사용 (MVP 행렬은 나중에 여기서 곱함)
VSOutput VSMain(VSInput input)
{
	VSOutput output;
	output.Position = float4(input.Position, 1.0f);
	output.Color = input.Color;
	return output;
}
