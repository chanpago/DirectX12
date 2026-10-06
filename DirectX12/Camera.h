#pragma once

#include "D3DUtil.h"

#include <DirectXMath.h>

// 1인칭 자유 이동 카메라 (왼손 / Y-up / +Z 앞쪽)
// - 마우스 오른쪽 버튼을 누른 채로 드래그: 시선 회전
// - 오른쪽 버튼을 누른 채로 휠: 이동 속도 조절
// - WASD: 앞뒤좌우, E / Q: 위아래, Shift: 빠르게
// 입력은 ImGui IO에서 읽음. ImGui 창이 마우스 / 키보드를 쓰는 중이면 무시
class Camera
{
public:
	Camera();

	// 매 프레임 ImGuiLayer::BeginFrame 이후에 호출. deltaTime은 초 단위
	void Update(float deltaTime);

	// 처음 위치 / 방향으로 되돌림
	void Reset();

	// 월드 → 뷰
	DirectX::XMMATRIX ViewMatrix() const;

	// 뷰 → 클립. aspectRatio = 너비 / 높이
	DirectX::XMMATRIX ProjectionMatrix(float aspectRatio) const;

	// 컨트롤 패널 안에서 호출. 카메라 상태 표시 + 설정 조절
	void DrawImGui();

private:
	// yaw / pitch로 바라보는 방향 (단위 벡터)
	DirectX::XMVECTOR Forward() const;

	DirectX::XMFLOAT3 m_position = {};
	float m_yaw = 0.0f;   // Y축 기준 회전 (라디안). 0 = +Z, 양수 = 오른쪽
	float m_pitch = 0.0f; // 위아래 (라디안). 양수 = 위

	float m_moveSpeed = 3.0f;          // 초당 이동 거리
	float m_mouseSensitivity = 0.003f; // 픽셀당 회전 (라디안)
	float m_fovDegrees = 45.0f;        // 세로 시야각
	float m_nearZ = 0.1f;
	float m_farZ = 100.0f;
};
