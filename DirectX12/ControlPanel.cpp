#include "ControlPanel.h"

#include "imgui.h"

void ControlPanel::Draw(UINT backBufferWidth, UINT backBufferHeight)
{
	ImGuiIO& io = ImGui::GetIO();

	ImGui::Begin("Control Panel");
	ImGui::Text("%.1f FPS (%.3f ms/frame)", io.Framerate, 1000.0f / io.Framerate);
	ImGui::Text("Back buffer: %u x %u", backBufferWidth, backBufferHeight);
	ImGui::Separator();
	ImGui::ColorEdit3("Clear Color", m_clearColor); // 알파(m_clearColor[3])는 1로 고정
	ImGui::Checkbox("Show ImGui Demo Window", &m_showDemoWindow);
	ImGui::End();

	if (m_showDemoWindow)
		ImGui::ShowDemoWindow(&m_showDemoWindow);
}
