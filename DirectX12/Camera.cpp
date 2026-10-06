#include "Camera.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>

using namespace DirectX;

// 처음 위치: 앞쪽(-Z) 약간 위. 원점을 바라보도록 pitch를 아래로
static const XMFLOAT3 kStartPosition = { 0.0f, 1.5f, -3.0f };

// 위아래로 90도까지 돌리면 forward와 up이 평행해져서 LookTo 계산이 깨짐
static const float kMaxPitch = XMConvertToRadians(89.0f);

// 이동 속도 범위 (컨트롤 패널 슬라이더와 같음)
static const float kMinMoveSpeed = 0.5f;
static const float kMaxMoveSpeed = 20.0f;

Camera::Camera()
{
	Reset();
}

void Camera::Reset()
{
	m_position = kStartPosition;
	m_yaw = 0.0f;
	m_pitch = -std::atan2(kStartPosition.y, -kStartPosition.z); // 원점을 향하는 각도
}

XMVECTOR Camera::Forward() const
{
	const float cosPitch = std::cos(m_pitch);
	return XMVectorSet(cosPitch * std::sin(m_yaw), std::sin(m_pitch), cosPitch * std::cos(m_yaw), 0.0f);
}

void Camera::Update(float deltaTime)
{
	const ImGuiIO& io = ImGui::GetIO();

	// 1. 시선 회전: 오른쪽 버튼 드래그. 마우스가 오른쪽(+x)이면 오른쪽, 아래(+y)면 아래를 봄
	if (!io.WantCaptureMouse && ImGui::IsMouseDown(ImGuiMouseButton_Right))
	{
		m_yaw += io.MouseDelta.x * m_mouseSensitivity;
		m_pitch -= io.MouseDelta.y * m_mouseSensitivity;
		m_pitch = std::clamp(m_pitch, -kMaxPitch, kMaxPitch);

		// 드래그 중 휠: 한 칸에 20%씩 빠르게 / 느리게 (곱으로 바꿔야 느릴 때도 세밀하게 조절됨)
		if (io.MouseWheel != 0.0f)
		{
			m_moveSpeed *= std::pow(1.2f, io.MouseWheel);
			m_moveSpeed = std::clamp(m_moveSpeed, kMinMoveSpeed, kMaxMoveSpeed);
		}
	}

	// 2. 이동: 수평 방향(forward의 xz)과 오른쪽, 월드 위쪽 기준
	if (io.WantCaptureKeyboard)
		return;

	const XMVECTOR forward = XMVectorSet(std::sin(m_yaw), 0.0f, std::cos(m_yaw), 0.0f);
	const XMVECTOR right = XMVectorSet(std::cos(m_yaw), 0.0f, -std::sin(m_yaw), 0.0f);
	const XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	XMVECTOR move = XMVectorZero();
	if (ImGui::IsKeyDown(ImGuiKey_W)) move += forward;
	if (ImGui::IsKeyDown(ImGuiKey_S)) move -= forward;
	if (ImGui::IsKeyDown(ImGuiKey_D)) move += right;
	if (ImGui::IsKeyDown(ImGuiKey_A)) move -= right;
	if (ImGui::IsKeyDown(ImGuiKey_E)) move += up;
	if (ImGui::IsKeyDown(ImGuiKey_Q)) move -= up;

	// 대각선으로 갈 때 빨라지지 않게 정규화
	if (XMVectorGetX(XMVector3LengthSq(move)) > 0.0f)
	{
		const float speed = m_moveSpeed * (ImGui::IsKeyDown(ImGuiKey_LeftShift) ? 3.0f : 1.0f);
		const XMVECTOR position = XMLoadFloat3(&m_position) + XMVector3Normalize(move) * speed * deltaTime;
		XMStoreFloat3(&m_position, position);
	}
}

XMMATRIX Camera::ViewMatrix() const
{
	const XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
	return XMMatrixLookToLH(XMLoadFloat3(&m_position), Forward(), up);
}

XMMATRIX Camera::ProjectionMatrix(float aspectRatio) const
{
	return XMMatrixPerspectiveFovLH(XMConvertToRadians(m_fovDegrees), aspectRatio, m_nearZ, m_farZ);
}

void Camera::DrawImGui()
{
	if (!ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
		return;

	ImGui::TextDisabled("RMB drag: look, RMB + wheel: speed, WASD: move, E/Q: up/down, Shift: fast");
	ImGui::DragFloat3("Position", &m_position.x, 0.05f);
	ImGui::Text("Yaw %.1f, Pitch %.1f (deg)", XMConvertToDegrees(m_yaw), XMConvertToDegrees(m_pitch));
	ImGui::SliderFloat("Move Speed", &m_moveSpeed, kMinMoveSpeed, kMaxMoveSpeed);
	ImGui::SliderFloat("FOV (deg)", &m_fovDegrees, 20.0f, 120.0f);
	if (ImGui::Button("Reset Camera"))
		Reset();
}
