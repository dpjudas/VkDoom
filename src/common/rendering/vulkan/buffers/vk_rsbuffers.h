
#pragma once

#include "vulkan/buffers/vk_hwbuffer.h"
#include "vulkan/shaders/vk_shader.h"

class VkMatrixBufferWriter;
class VkSurfaceUniformsBufferWriter;
struct FFlatVertex;

class VkRSBuffers
{
public:
	VkRSBuffers(VulkanRenderDevice* fb);
	~VkRSBuffers();

	struct
	{
		std::unique_ptr<VulkanBuffer> VertexBuffer;
		int VertexFormat = 0;
		FFlatVertex* Vertices = nullptr;
		unsigned int ShadowDataSize = 0;
		unsigned int CurIndex = 0;
		const unsigned int BUFFER_SIZE = 2000000;
		const unsigned int BUFFER_SIZE_TO_USE = BUFFER_SIZE - 500;
		std::unique_ptr<VulkanBuffer> IndexBuffer;
	} Flatbuffer;

	struct
	{
		int UploadIndex = 0;
		int BlockAlign = 0;
		int Count = 1000;
		std::unique_ptr<VulkanBuffer> UBO;
		void* Data = nullptr;
	} Viewpoint;

	struct
	{
		int UploadIndex = 0;
		int Count = 80000;
		std::unique_ptr<VulkanBuffer> UBO;
		void* Data = nullptr;
	} Lightbuffer;

	struct
	{
		int UploadIndex = 0;
		int Count = 80000;
		std::unique_ptr<VulkanBuffer> SSO;
		void* Data = nullptr;
	} Bonebuffer;

	struct
	{
		int UploadIndex = 0;
		int Count = 10000;
		std::unique_ptr<VulkanBuffer> UBO;
		void* Data = nullptr;
	} Fogballbuffer;

	std::unique_ptr<VkMatrixBufferWriter> MatrixBuffer;
	std::unique_ptr<VkSurfaceUniformsBufferWriter> SurfaceUniformsBuffer;

	struct
	{
		const int MaxQueries = 64000;
		std::unique_ptr<VulkanQueryPool> QueryPool;
		int NextIndex = 0;
	} OcclusionQuery;
};

class VkStreamBuffer
{
public:
	VkStreamBuffer(VulkanRenderDevice* fb, size_t structSize, size_t count);
	~VkStreamBuffer();

	uint32_t NextStreamDataBlock();
	void Reset() { mStreamDataOffset = 0; }

	std::unique_ptr<VulkanBuffer> UBO;
	void* Data = nullptr;

private:
	uint32_t mBlockSize = 0;
	uint32_t mStreamDataOffset = 0;
};

class VkSurfaceUniformsBufferWriter
{
public:
	VkSurfaceUniformsBufferWriter(VulkanRenderDevice* fb);

	bool Write(const SurfaceUniforms& data);
	void Reset();

	uint32_t DataIndex() const { return mDataIndex; }
	uint32_t Offset() const { return mOffset; }

	VulkanBuffer* UBO() const { return mBuffer->UBO.get(); }

private:
	std::unique_ptr<VkStreamBuffer> mBuffer;
	uint32_t mDataIndex = MAX_SURFACE_UNIFORMS - 1;
	uint32_t mOffset = 0;
};

class VkMatrixBufferWriter
{
public:
	VkMatrixBufferWriter(VulkanRenderDevice* fb);

	bool Write(const MatricesUBO& matrices);
	void Reset();

	uint32_t Offset() const { return mOffset; }

	VulkanBuffer* UBO() const { return mBuffer->UBO.get(); }

private:
	std::unique_ptr<VkStreamBuffer> mBuffer;
	uint32_t mOffset = 0;
};
