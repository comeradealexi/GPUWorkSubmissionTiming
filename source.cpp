#include <d3dx12.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <format>
#include <chrono>
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
enum GPUTimestamps : UINT64 { AllGPUWork, UploadToGPU, GPUToGPU, GPUToReadBack, EnumMax };

#define CheckHR(x) { HRESULT _hr = x; if (FAILED(_hr)) { std::cout<< "HR FAILURE\n"; return; } }
void CopyResource(GPUTimestamps Timestamp, ComPtr<ID3D12QueryHeap> QueryHeap, ComPtr<ID3D12GraphicsCommandList> CommandList, ComPtr<ID3D12Resource> Destination, ComPtr<ID3D12Resource> Source)
{
	CommandList->EndQuery(QueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, static_cast<UINT>((Timestamp * 2) + 0));

	{ // Barriers
		CD3DX12_RESOURCE_BARRIER ResourceBarriers[2] =
		{
			CD3DX12_RESOURCE_BARRIER::Transition(Source.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE),
			CD3DX12_RESOURCE_BARRIER::Transition(Destination.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST)
		};
		CommandList->ResourceBarrier(2, ResourceBarriers);
	}
	CommandList->CopyResource(Destination.Get(), Source.Get());
	{ // Barriers
		CD3DX12_RESOURCE_BARRIER ResourceBarriers[2] =
		{
			CD3DX12_RESOURCE_BARRIER::Transition(Source.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON),
			CD3DX12_RESOURCE_BARRIER::Transition(Destination.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON)
		};
		CommandList->ResourceBarrier(2, ResourceBarriers);
	}

	CommandList->EndQuery(QueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, static_cast<UINT>((Timestamp * 2) + 1));
}

const std::chrono::duration RUN_TIME_PER_TEST = std::chrono::seconds(10);

int main()
{
	vulkan();
	d3d12();
}

void d3d12_run_memory_test(ComPtr<ID3D12Device> Device, UINT64 GPUBufferSize)
{
	ComPtr<ID3D12Resource> GPUMemoryA;
	ComPtr<ID3D12Resource> GPUMemoryB;
	ComPtr<ID3D12Resource> UploadMemory;
	ComPtr<ID3D12Resource> ReadbackMemory;
	ComPtr<ID3D12Resource> QueryReadbackMemory;
	const float GPUBuffer1GBRatio = float(1024 * 1024 * 1024) / float(GPUBufferSize);
	{
		CD3DX12_HEAP_PROPERTIES GPUHeapProperties(D3D12_HEAP_TYPE_DEFAULT);
		CD3DX12_HEAP_PROPERTIES UploadHeapProperties(D3D12_HEAP_TYPE_UPLOAD);
		CD3DX12_HEAP_PROPERTIES ReadbackHeapProperties(D3D12_HEAP_TYPE_READBACK);
		CD3DX12_RESOURCE_DESC ResourceDesc = CD3DX12_RESOURCE_DESC::Buffer(GPUBufferSize);
		CheckHR(Device->CreateCommittedResource(&GPUHeapProperties, D3D12_HEAP_FLAG_NONE, &ResourceDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&GPUMemoryA)));
		CheckHR(Device->CreateCommittedResource(&GPUHeapProperties, D3D12_HEAP_FLAG_NONE, &ResourceDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&GPUMemoryB)));
		CheckHR(Device->CreateCommittedResource(&UploadHeapProperties, D3D12_HEAP_FLAG_NONE, &ResourceDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&UploadMemory)));
		CheckHR(Device->CreateCommittedResource(&ReadbackHeapProperties, D3D12_HEAP_FLAG_NONE, &ResourceDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&ReadbackMemory)));
	}

	{
		CD3DX12_HEAP_PROPERTIES ReadbackHeapProperties(D3D12_HEAP_TYPE_READBACK);
		CD3DX12_RESOURCE_DESC QueryResourceDesc = CD3DX12_RESOURCE_DESC::Buffer(1024 * 1024);
		CheckHR(Device->CreateCommittedResource(&ReadbackHeapProperties, D3D12_HEAP_FLAG_NONE, &QueryResourceDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&QueryReadbackMemory)));
	}

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
	queryHeapDesc.Count = 1024;
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
	Average GPUCopyUploadToGPU;
	Average GPUCopyGPUToGPU;
	Average GPUCopyGPUToReadback;


	const UINT64 RunCount = 64;
	UINT64 FenceValueExpected = 0;
	while (true)
	{
		if (FenceValueExpected == RunCount)
			break;

		FenceValueExpected++;
		Fence->SetEventOnCompletion(FenceValueExpected, CPUEvent);

		// Command List Recording
		{
			CommandList->EndQuery(QueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, (GPUTimestamps::AllGPUWork * 2) + 0);
			{
				// UploadToGPU
				CopyResource(GPUTimestamps::UploadToGPU, QueryHeap, CommandList, GPUMemoryA, UploadMemory);

				// GPUToGPU
				CopyResource(GPUTimestamps::GPUToGPU, QueryHeap, CommandList, GPUMemoryB, GPUMemoryA);

				// GPUToReadback
				CopyResource(GPUTimestamps::GPUToReadBack, QueryHeap, CommandList, ReadbackMemory, GPUMemoryB);
			}
			CommandList->EndQuery(QueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, (GPUTimestamps::AllGPUWork * 2) + 1);
			CommandList->ResolveQueryData(QueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, GPUTimestamps::EnumMax * 2, QueryReadbackMemory.Get(), 0);
			CommandList->Close();
		}

		// Get synced timer values for GPU and CPU
		UINT64 GpuStartTimestamp;
		UINT64 CpuStartTimestamp;
		CheckHR(CommandQueue->GetClockCalibration(&GpuStartTimestamp, &CpuStartTimestamp));

		// Submit GPU Work
		ID3D12CommandList* const CommandListSubmission = CommandList.Get();
		CommandQueue->ExecuteCommandLists(1, &CommandListSubmission);
		CommandQueue->Signal(Fence.Get(), FenceValueExpected);

		LARGE_INTEGER CPUTimeAfterSubmission;
		QueryPerformanceCounter(&CPUTimeAfterSubmission);

		// Wait for GPU to complete
		WaitForSingleObject(CPUEvent, INFINITE);

		LARGE_INTEGER CPUTimeAfterSignalled;
		QueryPerformanceCounter(&CPUTimeAfterSignalled);

		// Reset command list for next recording
		CommandList->Reset(CommandAllocator.Get(), nullptr);

		// Collect timing data
		D3D12_RANGE Range = { 0, sizeof(UINT64) * GPUTimestamps::EnumMax };
		UINT64* QueryReadbackMemoryPtr;
		QueryReadbackMemory->Map(0, &Range, (void**)&QueryReadbackMemoryPtr);
		{
			UINT64 GPUStartTime = QueryReadbackMemoryPtr[(GPUTimestamps::AllGPUWork * 2) + 0];
			UINT64 GPUEndTime = QueryReadbackMemoryPtr[(GPUTimestamps::AllGPUWork * 2) + 1];

			CPUTimeSubmission += CPUTimeAfterSubmission.QuadPart - CpuStartTimestamp;
			CPUTimeFenceWait += CPUTimeAfterSignalled.QuadPart - CPUTimeAfterSubmission.QuadPart;
			CPUTimePreSubToAfterWait += CPUTimeAfterSignalled.QuadPart - CpuStartTimestamp;

			CPUSubmissionToGPUStarted += GPUStartTime - GpuStartTimestamp;
			CPUSubmissionToGPUEnded += GPUEndTime - GpuStartTimestamp;
			GPUStartedToGPUEnded += GPUEndTime - GPUStartTime;

			GPUCopyUploadToGPU += QueryReadbackMemoryPtr[(GPUTimestamps::UploadToGPU * 2) + 1] - QueryReadbackMemoryPtr[(GPUTimestamps::UploadToGPU * 2) + 0];
			GPUCopyGPUToGPU += QueryReadbackMemoryPtr[(GPUTimestamps::GPUToGPU * 2) + 1] - QueryReadbackMemoryPtr[(GPUTimestamps::GPUToGPU * 2) + 0];
			GPUCopyGPUToReadback += QueryReadbackMemoryPtr[(GPUTimestamps::GPUToReadBack * 2) + 1] - QueryReadbackMemoryPtr[(GPUTimestamps::GPUToReadBack * 2) + 0];
		}
		QueryReadbackMemory->Unmap(0, nullptr);
	}
	std::cout << std::format("\nTiming Results: ({} - {} KiB)\n", GPUBufferSize, GPUBufferSize / 1024);
	std::cout << "CPU Submission Time (us): " << (static_cast<float>((CPUTimeSubmission.calculate_average()) * 1000000) / static_cast<float>(CPUFrequency.QuadPart)) << "\n";
	std::cout << "CPU Fence Wait  (us): " << (static_cast<float>((CPUTimeFenceWait.calculate_average()) * 1000000) / static_cast<float>(CPUFrequency.QuadPart)) << "\n";
	std::cout << "CPU Before Submission To After Signal Wait (us): " << (static_cast<float>((CPUTimePreSubToAfterWait.calculate_average()) * 1000000) / static_cast<float>(CPUFrequency.QuadPart)) << "\n";

	std::cout << "CPU Submission to GPU Started (us): " << (static_cast<float>((CPUSubmissionToGPUStarted.calculate_average()) * 1000000) / static_cast<float>(GPUFrequency)) << "\n";
	std::cout << "CPU Submission to GPU Ended (us): " << (static_cast<float>((CPUSubmissionToGPUEnded.calculate_average()) * 1000000) / static_cast<float>(GPUFrequency)) << "\n";
	std::cout << "GPU Started To GPU Ended (us): " << (static_cast<float>((GPUStartedToGPUEnded.calculate_average()) * 1000000) / static_cast<float>(GPUFrequency)) << "\n";

	float CopyUploadToGPUTime = (static_cast<float>((GPUCopyUploadToGPU.calculate_average()) * 1000000) / static_cast<float>(GPUFrequency));
	float CopyUploadToGPUGBs = 1000000.0f / (CopyUploadToGPUTime * GPUBuffer1GBRatio);
	std::cout << "GPU Copy Upload To GPU (us): " << CopyUploadToGPUTime << " (" << CopyUploadToGPUGBs << "GiB/s)" << "\n";

	float CopyGPUToGPUTime = (static_cast<float>((GPUCopyGPUToGPU.calculate_average()) * 1000000) / static_cast<float>(GPUFrequency));
	float CopyGPUToGPUGBs = 1000000.0f / (CopyGPUToGPUTime * GPUBuffer1GBRatio);
	std::cout << "GPU Copy GPU To GPU (us): " << CopyGPUToGPUTime << " (" << CopyGPUToGPUGBs << "GiB/s)" << "\n";

	float CopyGPUToReadbackTime = (static_cast<float>((GPUCopyGPUToReadback.calculate_average()) * 1000000) / static_cast<float>(GPUFrequency));
	float CopyGPUToReadbackGBs = 1000000.0f / (CopyGPUToReadbackTime * GPUBuffer1GBRatio);
	std::cout << "GPU Copy GPU To Readback (us): " << CopyGPUToReadbackTime << " (" << CopyGPUToReadbackGBs << "GiB/s)" << "\n";
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

	D3D12_FEATURE_DATA_ARCHITECTURE1 DataArchitecture1 = {};
	CheckHR(Device->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE1, &DataArchitecture1, sizeof(DataArchitecture1)));

	std::cout << "UMA: " << DataArchitecture1.UMA << "\n";
	std::cout << "CacheCoherentUMA: " << DataArchitecture1.CacheCoherentUMA << "\n";
	std::cout << "IsolatedMMU: " << DataArchitecture1.IsolatedMMU << "\n";

	UINT64 MemorySizes[] = { 1025 * 64, 1025 * 256, 1025*512, 1024 * 1024 * 1, 1024 * 1024 * 2, 1024 * 1024 * 4, 1024 * 1024 * 8, 1024 * 1024 * 16, 1024 * 1024 * 24, 1024 * 1024 * 32, 1024 * 1024 * 64, 1024 * 1024 * 128, 1024 * 1024 * 256, 1024 * 1024 * 512, 1024 * 1024 * 1024 };

	for (UINT64 MemorySize : MemorySizes)
	{
		d3d12_run_memory_test(Device, MemorySize);
	}
}

void vulkan()
{

}