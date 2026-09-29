// VS/PS가 공유하는 구조체

// 버텍스 버퍼 → VS 입력
// 시맨틱(POSITION, COLOR)은 PSO 입력 레이아웃의 SemanticName과 같아야 함
struct VSInput
{
	float3 Position : POSITION;
	float4 Color    : COLOR;
};

// VS 출력 → (래스터라이저에서 보간) → PS 입력
struct VSOutput
{
	float4 Position : SV_POSITION; // 클립 공간 좌표
	float4 Color    : COLOR;
};
