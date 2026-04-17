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

struct CSVDataEntry
{
	static constexpr const char*  Header = "Memory Size, Upload To GPU Time (us), Upload To GPU Speed (GiB/s), GPU To GPU Time (us), GPU To GPU Speed (GiB/s), GPU To Readback Time (us), GPU To Readback Speed (GiB/s)\n";
	UINT64 MemorySize;
	float UploadToGPU_Time;
	float UploadToGPU_GBPS;
	float GPUToGPU_Time;
	float GPUToGPU_GBPS;
	float GPUToReadback_Time;
	float GPUToReadback_GBPS;
};

template <typename T>
static T& operator <<(T& LHS, const CSVDataEntry& RHS)
{
	LHS << RHS.MemorySize << "," << RHS.UploadToGPU_Time << "," << RHS.UploadToGPU_GBPS << "," << RHS.GPUToGPU_Time << "," << RHS.GPUToGPU_GBPS << "," << RHS.GPUToReadback_Time << "," << RHS.GPUToReadback_GBPS << "\n";
	return LHS;
}

#define CheckHR(x) { HRESULT _hr = x; if (FAILED(_hr)) { std::cout<< "HR FAILURE\n"; __debugbreak(); return; } }
enum class CopyResourceMethod { CopyResource, ComputeShader };
void CopyResource(GPUTimestamps Timestamp, ComPtr<ID3D12QueryHeap> QueryHeap, ComPtr<ID3D12GraphicsCommandList> CommandList, ComPtr<ID3D12Resource> Destination, ComPtr<ID3D12Resource> Source)
{
	CommandList->EndQuery(QueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, static_cast<UINT>((Timestamp * 2) + 0));

	{ // Barriers
		CD3DX12_RESOURCE_BARRIER ResourceBarriers[2];
		UINT BarrierCount = 1;
		if (Timestamp == UploadToGPU)
		{
			ResourceBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(Destination.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
		}
		else if (Timestamp == GPUToReadBack)
		{
			ResourceBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(Source.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
		}
		else
		{
			ResourceBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(Source.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE); 			
			ResourceBarriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(Destination.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
			BarrierCount = 2;
		}
		CommandList->ResourceBarrier(BarrierCount, ResourceBarriers);
	}

	CommandList->CopyResource(Destination.Get(), Source.Get());

	{ // Barriers
		CD3DX12_RESOURCE_BARRIER ResourceBarriers[2];
		UINT BarrierCount = 1;
		if (Timestamp == UploadToGPU)
		{
			ResourceBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(Destination.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
		}
		else if (Timestamp == GPUToReadBack)
		{
			ResourceBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(Source.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
		}
		else
		{
			ResourceBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(Source.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON );
			ResourceBarriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(Destination.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON );
			BarrierCount = 2;
		}
		CommandList->ResourceBarrier(BarrierCount, ResourceBarriers);
	}

	CommandList->EndQuery(QueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, static_cast<UINT>((Timestamp * 2) + 1));
}

void CopyResourceCompute(UINT64 BufferSize, GPUTimestamps Timestamp, ComPtr<ID3D12QueryHeap> QueryHeap, ComPtr<ID3D12GraphicsCommandList> CommandList, ComPtr<ID3D12RootSignature> ComputeRootSignature, ComPtr<ID3D12PipelineState> ComputePipelineState, ComPtr<ID3D12Resource> Destination, ComPtr<ID3D12Resource> Source)
{
	CommandList->EndQuery(QueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, static_cast<UINT>((Timestamp * 2) + 0));

	{ // Barriers
		CD3DX12_RESOURCE_BARRIER ResourceBarriers[2];
		UINT BarrierCount = 1;
		if (Timestamp == UploadToGPU)
		{
			ResourceBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(Destination.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		}
		else if (Timestamp == GPUToReadBack)
		{
			ResourceBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(Source.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		}
		else
		{
			ResourceBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(Source.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			ResourceBarriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(Destination.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			BarrierCount = 2;
		}
		CommandList->ResourceBarrier(BarrierCount, ResourceBarriers);
	}

	{
		CommandList->SetComputeRootSignature(ComputeRootSignature.Get());
		CommandList->SetPipelineState(ComputePipelineState.Get());
		CommandList->SetComputeRootShaderResourceView(0, Source->GetGPUVirtualAddress());
		CommandList->SetComputeRootUnorderedAccessView(1, Destination->GetGPUVirtualAddress());
		CommandList->Dispatch(static_cast<UINT>(BufferSize / 4), 1, 1);

		CD3DX12_RESOURCE_BARRIER UAVBarrier = CD3DX12_RESOURCE_BARRIER::UAV(Destination.Get());
		CommandList->ResourceBarrier(1, &UAVBarrier);

	}

	{ // Barriers
		CD3DX12_RESOURCE_BARRIER ResourceBarriers[2];
		UINT BarrierCount = 1;

		if (Timestamp == UploadToGPU)
		{
			ResourceBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(Destination.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
		}
		else if (Timestamp == GPUToReadBack)
		{
			ResourceBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(Source.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
		}
		else
		{
			ResourceBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(Source.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
			ResourceBarriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(Destination.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
			BarrierCount = 2;
		}
		CommandList->ResourceBarrier(BarrierCount, ResourceBarriers);
	}

	CommandList->EndQuery(QueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, static_cast<UINT>((Timestamp * 2) + 1));
}

const std::chrono::duration RUN_TIME_PER_TEST = std::chrono::seconds(1);

int main()
{
	vulkan();
	d3d12();
}

void d3d12_run_memory_test(ComPtr<ID3D12Device> Device, const UINT64 GPUBufferSize, CSVDataEntry& CSVData)
{
	CSVData.MemorySize = GPUBufferSize;
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
		CD3DX12_RESOURCE_DESC ResourceDescGPU = CD3DX12_RESOURCE_DESC::Buffer(GPUBufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
		CD3DX12_RESOURCE_DESC ResourceDesc = CD3DX12_RESOURCE_DESC::Buffer(GPUBufferSize);
		CheckHR(Device->CreateCommittedResource(&GPUHeapProperties, D3D12_HEAP_FLAG_NONE, &ResourceDescGPU, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&GPUMemoryA)));
		CheckHR(Device->CreateCommittedResource(&GPUHeapProperties, D3D12_HEAP_FLAG_NONE, &ResourceDescGPU, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&GPUMemoryB)));
		CheckHR(Device->CreateCommittedResource(&UploadHeapProperties, D3D12_HEAP_FLAG_NONE, &ResourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&UploadMemory)));
		CheckHR(Device->CreateCommittedResource(&ReadbackHeapProperties, D3D12_HEAP_FLAG_NONE, &ResourceDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&ReadbackMemory)));
	}
	
	// Set UPLOAD memory to unique value
	const int UniqueValue = rand();
	std::cout << "UniqueValue: " << UniqueValue << "\n";
	{
		D3D12_RANGE Range = { 0, GPUBufferSize };
		int* MappedPointer;
		CheckHR(UploadMemory->Map(0, &Range, (void**) &MappedPointer));
		for (int i = 0; i < GPUBufferSize / sizeof(int); i++)
		{
			MappedPointer[i] = UniqueValue;
		}
		UploadMemory->Unmap(0, &Range);
	}

	{
		CD3DX12_HEAP_PROPERTIES ReadbackHeapProperties(D3D12_HEAP_TYPE_READBACK);
		CD3DX12_RESOURCE_DESC QueryResourceDesc = CD3DX12_RESOURCE_DESC::Buffer(1024 * 1024);
		CheckHR(Device->CreateCommittedResource(&ReadbackHeapProperties, D3D12_HEAP_FLAG_NONE, &QueryResourceDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&QueryReadbackMemory)));
	}

	
	ComPtr<ID3D12RootSignature> ComputeRootSignature;
	{
		CD3DX12_ROOT_PARAMETER1 RootParameters[2] = {};
		RootParameters[0].InitAsShaderResourceView(0, 0/*, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC*/);
		RootParameters[1].InitAsUnorderedAccessView(0, 0/*, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC*/);
		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC RootSigDesc = {};
		RootSigDesc.Init_1_1(2, RootParameters);
		ComPtr<ID3DBlob> RootSignatureBlob;
		ComPtr<ID3DBlob> Error;
		CheckHR(D3DX12SerializeVersionedRootSignature(&RootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1_1, &RootSignatureBlob, &Error));
		CheckHR(Device->CreateRootSignature(0, RootSignatureBlob->GetBufferPointer(), RootSignatureBlob->GetBufferSize(), IID_PPV_ARGS(&ComputeRootSignature)));
	}

	ComPtr<ID3D12PipelineState> ComputePipelineState;
	{
		D3D12_COMPUTE_PIPELINE_STATE_DESC ComputePSODesc = {};
		std::ifstream ComputeShaderFile("ComputeShader.cso", std::ios::binary);
		if (!ComputeShaderFile.good()) { CheckHR(E_FAIL); }
		std::vector<char> fileContents((std::istreambuf_iterator<char>(ComputeShaderFile)), std::istreambuf_iterator<char>());
		ComputePSODesc.pRootSignature = ComputeRootSignature.Get();
		ComputePSODesc.CS = CD3DX12_SHADER_BYTECODE(fileContents.data(), fileContents.size());
		CheckHR(Device->CreateComputePipelineState(&ComputePSODesc, IID_PPV_ARGS(&ComputePipelineState)));
	}

	ComPtr<ID3D12CommandQueue> CommandQueue;
	ComPtr<ID3D12CommandAllocator> CommandAllocator;
	ComPtr<ID3D12GraphicsCommandList> CommandList;
	
	D3D12_COMMAND_QUEUE_DESC CommandQueueDesc = {};
	CommandQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
	CommandQueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_HIGH;
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

	UINT64 FenceValueExpected = 0;
	const std::chrono::steady_clock::time_point StartTime = std::chrono::steady_clock::now();
	while ((std::chrono::steady_clock::now() - StartTime) < RUN_TIME_PER_TEST)
	{
		FenceValueExpected++;

		// Command List Recording
		{
			CommandList->EndQuery(QueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, (GPUTimestamps::AllGPUWork * 2) + 0);
			{
				if (true)
				{
					// UploadToGPU
					CopyResourceCompute(GPUBufferSize, GPUTimestamps::UploadToGPU, QueryHeap, CommandList, ComputeRootSignature, ComputePipelineState, GPUMemoryA, UploadMemory);

					// GPUToGPU
					CopyResourceCompute(GPUBufferSize, GPUTimestamps::GPUToGPU, QueryHeap, CommandList, ComputeRootSignature, ComputePipelineState, GPUMemoryB, GPUMemoryA);

					// GPUToReadback (Can't bind READBACK as UAV so fall back to normal CopyResource)
					CopyResource(GPUTimestamps::GPUToReadBack, QueryHeap, CommandList, ReadbackMemory, GPUMemoryB);
				}
				else
				{
					// UploadToGPU
					CopyResource(GPUTimestamps::UploadToGPU, QueryHeap, CommandList, GPUMemoryA, UploadMemory);

					// GPUToGPU
					CopyResource(GPUTimestamps::GPUToGPU, QueryHeap, CommandList, GPUMemoryB, GPUMemoryA);

					// GPUToReadback
					CopyResource(GPUTimestamps::GPUToReadBack, QueryHeap, CommandList, ReadbackMemory, GPUMemoryB);
				}

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
		CheckHR(CommandQueue->Signal(Fence.Get(), FenceValueExpected));

		LARGE_INTEGER CPUTimeAfterSubmission;
		QueryPerformanceCounter(&CPUTimeAfterSubmission);

		// Wait for GPU to complete
		CheckHR(Fence->SetEventOnCompletion(FenceValueExpected, CPUEvent));
		WaitForSingleObjectEx(CPUEvent, INFINITE, FALSE);

		LARGE_INTEGER CPUTimeAfterSignalled;
		QueryPerformanceCounter(&CPUTimeAfterSignalled);

		// Reset command list for next recording
		CheckHR(CommandAllocator->Reset());
		CheckHR(CommandList->Reset(CommandAllocator.Get(), nullptr));

		// Ensure our data passed through the entire process correctly
		{
			D3D12_RANGE Range = { 0, GPUBufferSize };
			int* MappedPointer;
			CheckHR(ReadbackMemory->Map(0, &Range, (void**) &MappedPointer));
			std::cout << "Mapped Pointer Address: " << MappedPointer << "\n";
			for (int i = 0; i < GPUBufferSize / sizeof(int); i++)
			{
				if (MappedPointer[i] != UniqueValue)
				{
					std::cout << "READBACK Memory does not match UPLOAD memory.\n";
					CheckHR(E_FAIL);
				}
			}
			Range.End = 0;
			ReadbackMemory->Unmap(0, &Range);
		}

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
	std::cout << std::format("\nTiming Results: Run Count: {:<6} Memory Size: {} B / {} KiB / {} MiB\n", FenceValueExpected, GPUBufferSize, GPUBufferSize / 1024, (GPUBufferSize / 1024) / 1024);
	std::cout << "CPU Submission Time (us):                        " << (static_cast<float>((CPUTimeSubmission.calculate_average()) * 1000000) / static_cast<float>(CPUFrequency.QuadPart)) << "\n";
	std::cout << "CPU Fence Wait  (us):                            " << (static_cast<float>((CPUTimeFenceWait.calculate_average()) * 1000000) / static_cast<float>(CPUFrequency.QuadPart)) << "\n";
	std::cout << "CPU Before Submission To After Signal Wait (us): " << (static_cast<float>((CPUTimePreSubToAfterWait.calculate_average()) * 1000000) / static_cast<float>(CPUFrequency.QuadPart)) << "\n";

	std::cout << "CPU Submission to GPU Started (us): " << (static_cast<float>((CPUSubmissionToGPUStarted.calculate_average()) * 1000000) / static_cast<float>(GPUFrequency)) << "\n";
	std::cout << "CPU Submission to GPU Ended (us):   " << (static_cast<float>((CPUSubmissionToGPUEnded.calculate_average()) * 1000000) / static_cast<float>(GPUFrequency)) << "\n";
	std::cout << "GPU Started To GPU Ended (us):      " << (static_cast<float>((GPUStartedToGPUEnded.calculate_average()) * 1000000) / static_cast<float>(GPUFrequency)) << "\n";

	float CopyUploadToGPUTime = CSVData.UploadToGPU_Time = (static_cast<float>((GPUCopyUploadToGPU.calculate_average()) * 1000000) / static_cast<float>(GPUFrequency));
	float CopyUploadToGPUGBs = CSVData.UploadToGPU_GBPS = 1000000.0f / (CopyUploadToGPUTime * GPUBuffer1GBRatio);
	std::cout << std::format("GPU Copy Upload To GPU (us):   {:6.1f} ({:5.1f} GiB/s)\n", CopyUploadToGPUTime, CopyUploadToGPUGBs);

	float CopyGPUToGPUTime = CSVData.GPUToGPU_Time = (static_cast<float>((GPUCopyGPUToGPU.calculate_average()) * 1000000) / static_cast<float>(GPUFrequency));
	float CopyGPUToGPUGBs = CSVData.GPUToGPU_GBPS = 1000000.0f / (CopyGPUToGPUTime * GPUBuffer1GBRatio);
	std::cout << std::format("GPU Copy GPU To GPU (us):      {:6.1f} ({:5.1f} GiB/s)\n", CopyGPUToGPUTime, CopyGPUToGPUGBs);

	float CopyGPUToReadbackTime = CSVData.GPUToReadback_Time = (static_cast<float>((GPUCopyGPUToReadback.calculate_average()) * 1000000) / static_cast<float>(GPUFrequency));
	float CopyGPUToReadbackGBs = CSVData.GPUToReadback_GBPS = 1000000.0f / (CopyGPUToReadbackTime * GPUBuffer1GBRatio);
	std::cout << std::format("GPU Copy GPU To Readback (us): {:6.1f} ({:5.1f} GiB/s)\n", CopyGPUToReadbackTime, CopyGPUToReadbackGBs);

}

void d3d12()
{

#ifdef _DEBUG
	// Enable the D3D12 debug layer.
	{
		ComPtr<ID3D12Debug> DebugController;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&DebugController))))
		{
			DebugController->EnableDebugLayer();
			ComPtr<ID3D12Debug1> DebugController1;
			if (SUCCEEDED(DebugController->QueryInterface(IID_PPV_ARGS(&DebugController1))))
			{
				DebugController1->SetEnableGPUBasedValidation(TRUE);
			}
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

	UINT64 MemorySizes[] = {
 		1024,
		1024 * 16,
		1024 * 32,
 		1024 * 64, 
		1024 * 128,
 		1024 * 256,
		1024 * 384,
 		1024 * 512, 
		1024 * 768,
 		1024 * 1024 * 1, 
 		1024 * 1024 * 2, 
		1024 * 1024 * 3,
 		1024 * 1024 * 4, 
		1024 * 1024 * 6,
 		1024 * 1024 * 8, 
		1024 * 1024 * 12,
 		1024 * 1024 * 16, 
		1024 * 1024 * 20,
		1024 * 1024 * 24, 
 		1024 * 1024 * 32, 
		1024 * 1024 * 48,
		1024 * 1024 * 64,
		1024 * 1024 * 96,
		1024 * 1024 * 128,
		1024 * 1024 * 256,
		1024 * 1024 * 512,
 		1024 * 1024 * 1024 
	};

	std::vector<CSVDataEntry> CSVDataList;

	for (UINT64 MemorySize : MemorySizes)
	{
		std::cout << "Starting Test: " << MemorySize << "\n";
		CSVDataEntry CSVData = {};
		d3d12_run_memory_test(Device, MemorySize, CSVData);
		CSVDataList.emplace_back(CSVData);
	}

	std::ofstream OutWriteFile("Results.csv", std::ios::binary);
	if (OutWriteFile.good())
	{
		OutWriteFile << CSVDataEntry::Header;
		for (const CSVDataEntry& Entry : CSVDataList)
		{
			OutWriteFile << Entry;
		}
	}
}

void vulkan()
{

}