#include "BasicPipeline.h"

// 빌드 때 FXC가 만든 셰이더 바이트코드 (g_BasicVS, g_BasicPS)
#include "BasicVS.h"
#include "BasicPS.h"

void BasicPipeline::Init(ID3D12Device* device, ID3D12RootSignature* rootSignature, DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat)
{
	// 1. 입력 레이아웃: Vertex 구조체(Mesh.h)와 Common.hlsli의 VSInput에 맞춤
	const D3D12_INPUT_ELEMENT_DESC inputLayout[] =
	{
		// 시맨틱,   인덱스, 포맷,                           슬롯, 오프셋, 분류,                                       인스턴스 스텝
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

	// 2. 래스터라이저: 면을 채워서 그리고, 컬링 없음 (시계방향 = 앞면)
	D3D12_RASTERIZER_DESC rasterizer = {};
	rasterizer.FillMode = D3D12_FILL_MODE_SOLID;
	rasterizer.CullMode = D3D12_CULL_MODE_NONE;
	rasterizer.FrontCounterClockwise = FALSE;
	rasterizer.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
	rasterizer.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
	rasterizer.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
	rasterizer.DepthClipEnable = TRUE;
	rasterizer.MultisampleEnable = FALSE;
	rasterizer.AntialiasedLineEnable = FALSE;
	rasterizer.ForcedSampleCount = 0;
	rasterizer.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

	// 3. 블렌드: 섞지 않고 픽셀 셰이더 결과로 덮어씀
	D3D12_BLEND_DESC blend = {};
	blend.AlphaToCoverageEnable = FALSE;
	blend.IndependentBlendEnable = FALSE;
	D3D12_RENDER_TARGET_BLEND_DESC& rtBlend = blend.RenderTarget[0];
	rtBlend.BlendEnable = FALSE;
	rtBlend.LogicOpEnable = FALSE;
	rtBlend.SrcBlend = D3D12_BLEND_ONE;
	rtBlend.DestBlend = D3D12_BLEND_ZERO;
	rtBlend.BlendOp = D3D12_BLEND_OP_ADD;
	rtBlend.SrcBlendAlpha = D3D12_BLEND_ONE;
	rtBlend.DestBlendAlpha = D3D12_BLEND_ZERO;
	rtBlend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
	rtBlend.LogicOp = D3D12_LOGIC_OP_NOOP;
	rtBlend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	// 4. 깊이 / 스텐실: 새 픽셀의 깊이가 버퍼 값보다 작을(가까울) 때만 그리고, 그 깊이를 기록
	//    스텐실은 안 쓰지만 값은 유효한 enum으로 채워 둠
	D3D12_DEPTH_STENCIL_DESC depthStencil = {};
	depthStencil.DepthEnable = TRUE;
	depthStencil.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	depthStencil.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	depthStencil.StencilEnable = FALSE;
	depthStencil.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
	depthStencil.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
	const D3D12_DEPTH_STENCILOP_DESC defaultStencilOp =
		{ D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_ALWAYS };
	depthStencil.FrontFace = defaultStencilOp;
	depthStencil.BackFace = defaultStencilOp;

	// 5. PSO 설명: 위 설정 전부 + 루트 시그니처 + 셰이더 + 출력 포맷
	D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
	desc.pRootSignature = rootSignature;
	desc.VS = { g_BasicVS, sizeof(g_BasicVS) };
	desc.PS = { g_BasicPS, sizeof(g_BasicPS) };
	desc.InputLayout = { inputLayout, _countof(inputLayout) };
	desc.RasterizerState = rasterizer;
	desc.BlendState = blend;
	desc.DepthStencilState = depthStencil;
	desc.SampleMask = UINT_MAX;                                   // 모든 샘플에 기록
	desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	desc.NumRenderTargets = 1;
	desc.RTVFormats[0] = rtvFormat;                               // 백버퍼 포맷과 같아야 함
	desc.DSVFormat = dsvFormat;                                   // 깊이 버퍼 포맷과 같아야 함
	desc.SampleDesc.Count = 1;

	// 6. 생성: 이 시점에 드라이버가 바이트코드를 이 GPU용 기계어로 컴파일
	ThrowIfFailed(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&m_pipelineState)),
		"CreateGraphicsPipelineState(Basic)");
}

void BasicPipeline::Shutdown()
{
	m_pipelineState.Reset();
}
