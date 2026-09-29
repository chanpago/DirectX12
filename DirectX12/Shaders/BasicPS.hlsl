#include "Common.hlsli"

// 래스터라이저가 보간한 정점 색을 그대로 출력
float4 PSMain(VSOutput input) : SV_TARGET
{
	return input.Color;
}
