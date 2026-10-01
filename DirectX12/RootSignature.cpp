#include "RootSignature.h"

void RootSignature::Init(ID3D12Device* device)
{
	// 1. 파라미터 0: 루트 상수. 힙이나 버퍼 없이 값 자체를 커맨드 리스트에 직접 기록
	D3D12_ROOT_PARAMETER params[1] = {};
	params[kTransformParam].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	params[kTransformParam].Constants.ShaderRegister = 0;  // b0
	params[kTransformParam].Constants.RegisterSpace = 0;
	params[kTransformParam].Constants.Num32BitValues = 16; // float4x4 = 32비트 × 16
	params[kTransformParam].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

	// 2. 루트 시그니처 설명. 버텍스 버퍼(입력 레이아웃)를 쓰려면 플래그 필수
	D3D12_ROOT_SIGNATURE_DESC desc = {};
	desc.NumParameters = _countof(params);
	desc.pParameters = params;
	desc.NumStaticSamplers = 0;
	desc.pStaticSamplers = nullptr;
	desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	// 3. 직렬화: 설명 구조체 → GPU 드라이버가 읽는 바이너리 blob
	ComPtr<ID3DBlob> signature;
	ComPtr<ID3DBlob> error;
	HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error);
	if (FAILED(hr))
	{
		// 설명이 잘못되면 error blob에 이유가 문자열로 들어 있음
		if (error)
			OutputDebugStringA(static_cast<const char*>(error->GetBufferPointer()));
		ThrowIfFailed(hr, "D3D12SerializeRootSignature");
	}

	// 4. 생성
	ThrowIfFailed(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
		IID_PPV_ARGS(&m_rootSignature)), "CreateRootSignature");
}

void RootSignature::Shutdown()
{
	m_rootSignature.Reset();
}
