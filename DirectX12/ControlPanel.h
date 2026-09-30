#pragma once

#include "D3DUtil.h"

// 매 프레임 그리는 ImGui 컨트롤 패널과, UI로 조절하는 값
class ControlPanel
{
public:
	// ImGuiLayer::BeginFrame 이후에 호출
	void Draw(UINT backBufferWidth, UINT backBufferHeight);

	const float* ClearColor() const { return m_clearColor; }

private:
	float m_clearColor[4] = { 0.10f, 0.12f, 0.16f, 1.0f };
	bool  m_showDemoWindow = false;
};
