#include "ConstantBuffer.h"

#include "Helpers.h"

#include <cstring>

ConstantBuffer::ConstantBuffer(Microsoft::WRL::ComPtr<ID3D12Device2> device, Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap,
    uint32_t descriptorIndex, const void *bufferData, uint32_t bufferSize) {
    m_devicePtr = device;

    m_bufferSize = bufferSize;
    m_alignedBufferSize = Align256(bufferSize);

    // Create upload heap buffer.
    CD3DX12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE_UPLOAD);
    CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(m_alignedBufferSize);

    ThrowIfFailed(m_devicePtr->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_constantBuffer)));

    // Persistently map the buffer.
    ThrowIfFailed(m_constantBuffer->Map(0, nullptr, reinterpret_cast<void **>(&m_mappedData)));

    // Copy initial data.
    memcpy(m_mappedData, bufferData, bufferSize);

    // Create CBV.
    UINT descriptorSize = m_devicePtr->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandle(descriptorHeap->GetCPUDescriptorHandleForHeapStart(), descriptorIndex, descriptorSize);

    CD3DX12_GPU_DESCRIPTOR_HANDLE gpuHandle(descriptorHeap->GetGPUDescriptorHandleForHeapStart(), descriptorIndex, descriptorSize);

    m_GPUDescriptorHandle = gpuHandle;

    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
    cbvDesc.BufferLocation = m_constantBuffer->GetGPUVirtualAddress();
    cbvDesc.SizeInBytes = m_alignedBufferSize;

    m_devicePtr->CreateConstantBufferView(&cbvDesc, cpuHandle);
}

void ConstantBuffer::Update(const void *bufferData, uint32_t bufferSize) {
    memcpy(m_mappedData, bufferData, bufferSize);
}

D3D12_GPU_DESCRIPTOR_HANDLE ConstantBuffer::GetGPUDescriptorHandle() const {
    return m_GPUDescriptorHandle;
}

uint32_t ConstantBuffer::Align256(uint32_t size) {
    return (size + 255) & ~255;
}