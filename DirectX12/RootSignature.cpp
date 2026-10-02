#include "RootSignature.h"

// 이 init에서는 desc의 파라미터 정의, 직렬화, m_rootsignature객체생성을 담당함
// rootsignature는 값자체는없고 셰이더가 받을 입력의 형식만 정한 틀이다. 실제 값(mvp행렬)은 그릴때
// SetGraphicsroot32Bitconstants로 넣는다
// 이 Rootsignature가 담고있는정보(2가지)는 다음과같다.
// 슬롯0번, b0레지스터에 32비트값16개(4x4)를 루트 상수로 넘기고 vs만 볼 수 있게 한다.
// 플래그, IA 입력 레이아웃(버텍스버퍼로 정점받기)을 허용한다.
void RootSignature::Init(ID3D12Device* device)
{
	// 1. 파라미터 0: 루트 상수. 힙이나 버퍼 없이 값 자체를 커맨드 리스트에 직접 기록
	
	D3D12_ROOT_PARAMETER params[1] = {};

	// 이슬롯에 값자체를 넣겠다는 뜻. uav, srv, dsv와 다르게 디스크립터를 쓰지않고 값을 넣겠다는뜻
	params[kTransformParam].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;

	// hlsl의 register(b0)과 연결됨 
	params[kTransformParam].Constants.ShaderRegister = 0;  // b0
	params[kTransformParam].Constants.RegisterSpace = 0;

	// 32비트값 16개(float 16개를 사용한다는뜻 4x4)
	params[kTransformParam].Constants.Num32BitValues = 16; // float4x4 = 32비트 × 16

	// 이 코드의 뜻은 이 파라미터(mvp행렬)을 어느 셰이더 단계에서 볼 수 있게 할지 정하는 설정
	// 지금의 설정은 버텍스 셰이더에서만 보이게한다.
	// D3D12_SHADER_VISIBILITY_ALL, D3D12_SHADER_VISIBILITY_PIXEL 두가지가있는데
	// 여기서는 vertex만씀 이렇게 좁히면 필요한 단꼐에만 데이터를 전달하니까 드라이버가 최적화할 여지가 생김
	params[kTransformParam].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

	// 2. 루트 시그니처 설명. 버텍스 버퍼(입력 레이아웃)를 쓰려면 플래그 필수
	D3D12_ROOT_SIGNATURE_DESC desc = {};
	desc.NumParameters = _countof(params);
	desc.pParameters = params;
	desc.NumStaticSamplers = 0;
	desc.pStaticSamplers = nullptr;


	// 버텍스 버퍼를 쓸거에요 라고 gpu에 알려준다 
	desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	// 3. 직렬화: 설명 구조체 → GPU 드라이버가 읽는 바이너리 blob
	// 이부분에서 직렬화가 일어난다. 흩어진 정보를 포인터 없는 바이트 덩어리 하나로 묶어서 signature blob에 담는다
	// 애초에 CreateRootsignature가 blob만 받는다.
	// API설계상 구조체(desc)가 아니라 바이트 덩어리를 입력으로 받게 되어있다.
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
	// 실제로 ram의 힙영역에 rootsignature에 생성된다. 생성하고나서 m_rootsignature에 주소값을 박는다
	ThrowIfFailed(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
		IID_PPV_ARGS(&m_rootSignature)), "CreateRootSignature");
}

void RootSignature::Shutdown()
{
	m_rootSignature.Reset();
}
