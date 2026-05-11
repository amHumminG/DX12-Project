#pragma once

#include "DX12.h"

#include <d3d12.h>

class ConstantBuffer {
public:
    ConstantBuffer(Microsoft::WRL::ComPtr<ID3D12Device2> device,Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap,
        uint32_t descriptorIndex, const void *bufferData, uint32_t bufferSize);

    ~ConstantBuffer() = default;

    void Update(const void *bufferData, uint32_t bufferSize);

    D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle() const;

private:
    uint32_t Align256(uint32_t size);

    Microsoft::WRL::ComPtr<ID3D12Device2> m_devicePtr;

    Microsoft::WRL::ComPtr<ID3D12Resource> m_constantBuffer;

    uint8_t *m_mappedData = nullptr;

    uint32_t m_bufferSize = 0;
    uint32_t m_alignedBufferSize = 0;

    D3D12_GPU_DESCRIPTOR_HANDLE m_GPUDescriptorHandle{};
};