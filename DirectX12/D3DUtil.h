#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>

#include <cstdio>
#include <stdexcept>

using Microsoft::WRL::ComPtr;

// ---------------------------------------------------------------------------
// 에러 처리
// ---------------------------------------------------------------------------
inline void ThrowIfFailed(HRESULT hr, const char* what)
{
	if (FAILED(hr))
	{
		char buf[256];
		snprintf(buf, sizeof(buf), "%s failed (HRESULT 0x%08X)", what, static_cast<unsigned>(hr));
		throw std::runtime_error(buf);
	}
}

// ---------------------------------------------------------------------------
// 헬퍼
// ---------------------------------------------------------------------------
inline D3D12_RESOURCE_BARRIER TransitionBarrier(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = resource;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = before;
	barrier.Transition.StateAfter = after;
	return barrier;
}
