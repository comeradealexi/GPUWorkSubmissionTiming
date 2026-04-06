#include <d3dx12.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <format>
//#include <vulkan/vulkan.hpp>
#include <wrl/client.h>
using namespace Microsoft::WRL;
void d3d12();
void vulkan();

struct Average
{
	UINT64 operator += (UINT64 rhs)
	{
		value += rhs;
		count++;
		return value;
	}
	UINT64 calculate_average()
	{
		return value / count;
	}
	UINT64 value = 0;
	UINT64 count = 0;
};
#define CheckHR(x) { HRESULT _hr = x; if (FAILED(_hr)) { std::cout<< "HR FAILURE\n"; return; } }

int main()
{
	vulkan();
	d3d12();
}

void d3d12()
{
#ifdef _DEBUG
	// Enable the D3D12 debug layer.
	{
		ComPtr<ID3D12Debug> debugController;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
		{
			debugController->EnableDebugLayer();
		}
	}
#endif

	ComPtr<ID3D12Device> Device;
	CheckHR(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&Device)));

	ComPtr<ID3D12Resource> GPUMemory;
	ComPtr<ID3D12Resource> UploadMemory;
	ComPtr<ID3D12Resource> ReadbackMemory;
	ComPtr<ID3D12Resource> QueryReadbackMemory;
	CD3DX12_HEAP_PROPERTIES GPUHeapProperties(D3D12_HEAP_TYPE_DEFAULT);
	CD3DX12_HEAP_PROPERTIES UploadHeapProperties(D3D12_HEAP_TYPE_UPLOAD);
	CD3DX12_HEAP_PROPERTIES ReadbackHeapProperties(D3D12_HEAP_TYPE_READBACK);
	CD3DX12_RESOURCE_DESC rd = CD3DX12_RESOURCE_DESC::Buffer(1024);
	CheckHR(Device->CreateCommittedResource(&GPUHeapProperties, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&GPUMemory)));
	CheckHR(Device->CreateCommittedResource(&UploadHeapProperties, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&UploadMemory)));
	CheckHR(Device->CreateCommittedResource(&ReadbackHeapProperties, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&ReadbackMemory)));
	CheckHR(Device->CreateCommittedResource(&ReadbackHeapProperties, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&QueryReadbackMemory)));

	ComPtr<ID3D12CommandQueue> CommandQueue;
	ComPtr<ID3D12CommandAllocator> CommandAllocator;
	ComPtr<ID3D12GraphicsCommandList> CommandList;

	D3D12_COMMAND_QUEUE_DESC CommandQueueDesc = {};
	CommandQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
	CommandQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	CheckHR(Device->CreateCommandQueue(&CommandQueueDesc, IID_PPV_ARGS(&CommandQueue)));
	CheckHR(Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&CommandAllocator)));
	CheckHR(Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE, CommandAllocator.Get(), nullptr, IID_PPV_ARGS(&CommandList)));	

	LARGE_INTEGER CPUFrequency;
	QueryPerformanceFrequency(&CPUFrequency);

	UINT64 GPUFrequency;
	CommandQueue->GetTimestampFrequency(&GPUFrequency);

	ComPtr<ID3D12QueryHeap> QueryHeap;
	D3D12_QUERY_HEAP_DESC queryHeapDesc = {};
	queryHeapDesc.Count = 2;
	queryHeapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
	CheckHR(Device->CreateQueryHeap(&queryHeapDesc, IID_PPV_ARGS(&QueryHeap)));


	ComPtr<ID3D12Fence> Fence;
	CheckHR(Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&Fence)));
	HANDLE CPUEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

	Average CPUTimeSubmission;
	Average CPUTimeFenceWait;
	Average CPUTimePreSubToAfterWait;
	Average CPUSubmissionToGPUStarted;
	Average CPUSubmissionToGPUEnded;
	Average GPUStartedToGPUEnded;


	UINT64 FenceValueExpected = 0;
	while (true)
	{
		if (FenceValueExpected == 1024*128)
			break;

		FenceValueExpected++;
		Fence->SetEventOnCompletion(FenceValueExpected, CPUEvent);

		CommandList->EndQuery(QueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);

		{ // Barriers
			CD3DX12_RESOURCE_BARRIER ResourceBarriers[2] =
			{
				CD3DX12_RESOURCE_BARRIER::Transition(UploadMemory.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE),
				CD3DX12_RESOURCE_BARRIER::Transition(GPUMemory.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST)
			};
			CommandList->ResourceBarrier(2, ResourceBarriers);
		}
		CommandList->CopyResource(GPUMemory.Get(), UploadMemory.Get());
		{ // Barriers
			CD3DX12_RESOURCE_BARRIER ResourceBarriers[3] =
			{
				CD3DX12_RESOURCE_BARRIER::Transition(UploadMemory.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON),
				CD3DX12_RESOURCE_BARRIER::Transition(GPUMemory.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE),
				CD3DX12_RESOURCE_BARRIER::Transition(ReadbackMemory.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST)
			};
			CommandList->ResourceBarrier(3, ResourceBarriers);
		}
		CommandList->CopyResource(ReadbackMemory.Get(), GPUMemory.Get());
		{ // Barriers
			CD3DX12_RESOURCE_BARRIER ResourceBarriers[2] =
			{
				CD3DX12_RESOURCE_BARRIER::Transition(GPUMemory.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON),
				CD3DX12_RESOURCE_BARRIER::Transition(ReadbackMemory.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON)
			};
			CommandList->ResourceBarrier(2, ResourceBarriers);
		}
		CommandList->EndQuery(QueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
		CommandList->ResolveQueryData(QueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, QueryReadbackMemory.Get(), 0);
		CommandList->Close();;

		UINT64 GpuStartTimestamp;
		UINT64 CpuStartTimestamp;
		CheckHR(CommandQueue->GetClockCalibration(&GpuStartTimestamp, &CpuStartTimestamp));


		ID3D12CommandList* const CommandListSubmission = CommandList.Get();
		CommandQueue->ExecuteCommandLists(1, &CommandListSubmission);
		CommandQueue->Signal(Fence.Get(), FenceValueExpected);
		LARGE_INTEGER CPUTimeAfterSubmission;
		QueryPerformanceCounter(&CPUTimeAfterSubmission);
		WaitForSingleObject(CPUEvent, INFINITE);
		LARGE_INTEGER CPUTimeAfterSignalled;
		QueryPerformanceCounter(&CPUTimeAfterSignalled);
		CommandList->Reset(CommandAllocator.Get(), nullptr);

		D3D12_RANGE Range = { 0, sizeof(UINT64) * 2 };
		UINT64* QueryReadbackMemoryPtr;
		QueryReadbackMemory->Map(0, &Range, (void**)&QueryReadbackMemoryPtr);
		UINT64 GPUStartTime = QueryReadbackMemoryPtr[0];
		UINT64 GPUEndTime = QueryReadbackMemoryPtr[1];
		QueryReadbackMemory->Unmap(0, nullptr);

		CPUTimeSubmission += CPUTimeAfterSubmission.QuadPart - CpuStartTimestamp;
		CPUTimeFenceWait += CPUTimeAfterSignalled.QuadPart - CPUTimeAfterSubmission.QuadPart;
		CPUTimePreSubToAfterWait += CPUTimeAfterSignalled.QuadPart - CpuStartTimestamp;

		CPUSubmissionToGPUStarted += GPUStartTime - GpuStartTimestamp;
		CPUSubmissionToGPUEnded += GPUEndTime - GpuStartTimestamp;
		GPUStartedToGPUEnded += GPUEndTime - GPUStartTime;
	}

	std::cout << "CPU Submission Time (us): " << (static_cast<float>((CPUTimeSubmission.calculate_average()) * 1000000) / static_cast<float>(CPUFrequency.QuadPart)) << "\n";
	std::cout << "CPU Fence Wait  (us): " << (static_cast<float>((CPUTimeFenceWait.calculate_average()) * 1000000) / static_cast<float>(CPUFrequency.QuadPart)) << "\n";
	std::cout << "CPU Before Submission To After Signal Wait (us): " << (static_cast<float>((CPUTimePreSubToAfterWait.calculate_average()) * 1000000) / static_cast<float>(CPUFrequency.QuadPart)) << "\n";

	std::cout << "CPU Submission to GPU Started (us): " << (static_cast<float>((CPUSubmissionToGPUStarted.calculate_average()) * 1000000) / static_cast<float>(GPUFrequency)) << "\n";
	std::cout << "CPU Submission to GPU Ended (us): " << (static_cast<float>((CPUSubmissionToGPUEnded.calculate_average()) * 1000000) / static_cast<float>(GPUFrequency)) << "\n";
	std::cout << "GPU Started To GPU Ended (us): " << (static_cast<float>((GPUStartedToGPUEnded.calculate_average()) * 1000000) / static_cast<float>(GPUFrequency)) << "\n";
}

void vulkan()
{

}