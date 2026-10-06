#include "ImGuiLayer.h"

#include <stdexcept>

#include "imgui_impl_win32.h"
#include "imgui_impl_dx12.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ---------------------------------------------------------------------------
// SRV 디스크립터 힙 할당기 (ImGui 1.92 DX12 백엔드는 텍스처별로 디스크립터를 요청함)
// ---------------------------------------------------------------------------
void ImGuiLayer::DescriptorHeapAllocator::Create(ID3D12Device* device, ID3D12DescriptorHeap* heap)
{
	IM_ASSERT(Heap == nullptr && FreeIndices.empty());
	Heap = heap;
	D3D12_DESCRIPTOR_HEAP_DESC desc = heap->GetDesc();
	HeapStartCpu = Heap->GetCPUDescriptorHandleForHeapStart();
	HeapStartGpu = Heap->GetGPUDescriptorHandleForHeapStart();
	HeapHandleIncrement = device->GetDescriptorHandleIncrementSize(desc.Type);
	FreeIndices.reserve(static_cast<int>(desc.NumDescriptors));
	for (int n = static_cast<int>(desc.NumDescriptors); n > 0; n--)
		FreeIndices.push_back(n - 1);
}

void ImGuiLayer::DescriptorHeapAllocator::Destroy()
{
	Heap = nullptr;
	FreeIndices.clear();
}

void ImGuiLayer::DescriptorHeapAllocator::Alloc(D3D12_CPU_DESCRIPTOR_HANDLE* outCpu, D3D12_GPU_DESCRIPTOR_HANDLE* outGpu)
{
	IM_ASSERT(FreeIndices.Size > 0);
	int idx = FreeIndices.back();
	FreeIndices.pop_back();
	outCpu->ptr = HeapStartCpu.ptr + static_cast<SIZE_T>(idx) * HeapHandleIncrement;
	outGpu->ptr = HeapStartGpu.ptr + static_cast<UINT64>(idx) * HeapHandleIncrement;
}

void ImGuiLayer::DescriptorHeapAllocator::Free(D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu)
{
	int cpuIdx = static_cast<int>((cpu.ptr - HeapStartCpu.ptr) / HeapHandleIncrement);
	int gpuIdx = static_cast<int>((gpu.ptr - HeapStartGpu.ptr) / HeapHandleIncrement);
	IM_ASSERT(cpuIdx == gpuIdx);
	(void)gpuIdx;
	FreeIndices.push_back(cpuIdx);
}

// ---------------------------------------------------------------------------
// ImGuiLayer
// ---------------------------------------------------------------------------
float ImGuiLayer::EnableDpiAwareness()
{
	ImGui_ImplWin32_EnableDpiAwareness();
	return ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY));
}

bool ImGuiLayer::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	return ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam) != 0;
}

void ImGuiLayer::Init(HWND hwnd, ID3D12Device* device, ID3D12CommandQueue* commandQueue,
	UINT numFramesInFlight, DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat, float dpiScale)
{
	// ImGui용 SRV 힙 (shader-visible)
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heapDesc.NumDescriptors = kSrvHeapSize;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	if (FAILED(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_srvHeap))))
		throw std::runtime_error("CreateDescriptorHeap(ImGui SRV) failed");
	m_srvHeapAlloc.Create(device, m_srvHeap.Get());

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.ConfigNavCaptureKeyboard = false; // 창 포커스만으로는 키보드를 뺏지 않음 (카메라 WASD가 막히지 않게)

	ImGui::StyleColorsDark();
	ImGuiStyle& style = ImGui::GetStyle();
	style.ScaleAllSizes(dpiScale);
	style.FontScaleDpi = dpiScale;

	ImGui_ImplWin32_Init(hwnd);

	ImGui_ImplDX12_InitInfo initInfo;
	initInfo.Device = device;
	initInfo.CommandQueue = commandQueue;
	initInfo.NumFramesInFlight = static_cast<int>(numFramesInFlight);
	initInfo.RTVFormat = rtvFormat;
	initInfo.DSVFormat = dsvFormat; // ImGui PSO도 붙어 있는 DSV와 포맷이 같아야 함 (깊이 테스트는 안 함)
	initInfo.UserData = this;
	initInfo.SrvDescriptorHeap = m_srvHeap.Get();
	initInfo.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* outCpu, D3D12_GPU_DESCRIPTOR_HANDLE* outGpu)
	{
		static_cast<ImGuiLayer*>(info->UserData)->m_srvHeapAlloc.Alloc(outCpu, outGpu);
	};
	initInfo.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu)
	{
		static_cast<ImGuiLayer*>(info->UserData)->m_srvHeapAlloc.Free(cpu, gpu);
	};
	ImGui_ImplDX12_Init(&initInfo);

	m_initialized = true;
}

void ImGuiLayer::Shutdown()
{
	if (m_initialized)
	{
		ImGui_ImplDX12_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();
		m_initialized = false;
	}
	m_srvHeapAlloc.Destroy();
	m_srvHeap.Reset();
}

void ImGuiLayer::BeginFrame()
{
	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();
}

void ImGuiLayer::Render(ID3D12GraphicsCommandList* commandList)
{
	ImGui::Render();

	ID3D12DescriptorHeap* heaps[] = { m_srvHeap.Get() };
	commandList->SetDescriptorHeaps(1, heaps);
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);
}
