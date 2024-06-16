#pragma once

#include <cstdlib>
#include <vector>

template<typename T>
class IRBlockAllocator
{
public:
	IRBlockAllocator() = default;

	~IRBlockAllocator()
	{
		for (void* buffer : buffers)
		{
			size_t pos = 0;
			while (true)
			{
				void* header = static_cast<char*>(buffer) + pos;
				size_t totalsize = *static_cast<size_t*>(header);
				if (totalsize == 0)
					break;

				void* obj = static_cast<char*>(buffer) + pos + 16;
				static_cast<T*>(obj)->~T();

				pos += totalsize;
			}

			freeblock(buffer);
		}
	}

	void* alloc(size_t size)
	{
		size_t totalsize = (size + 15) / 16 * 16 + 16;

		if (buffers.empty() || pos + totalsize + 16 > blocksize)
		{
			buffers.push_back(allocblock());
			*static_cast<size_t*>(buffers.back()) = 0;
			pos = 0;
		}

		void* header = static_cast<char*>(buffers.back()) + pos;
		void* data = static_cast<char*>(buffers.back()) + pos + 16;
		void* header2 = static_cast<char*>(buffers.back()) + pos + totalsize;
		*static_cast<size_t*>(header) = totalsize;
		*static_cast<size_t*>(header2) = 0;
		pos += totalsize;
		return data;
	}

private:
	void* allocblock()
	{
#ifdef _MSC_VER
		return _aligned_malloc(blocksize, 16);
#else
		return std::aligned_alloc(16, blocksize);
#endif
	}

	void freeblock(void* buffer)
	{
#ifdef _MSC_VER
		_aligned_free(buffer);
#else
		std::free(buffer);
#endif
	}

	std::vector<void*> buffers;
	size_t pos = 0;

	static const int blocksize = 64 * 1024;

	IRBlockAllocator(const IRBlockAllocator&) = delete;
	IRBlockAllocator& operator=(const IRBlockAllocator&) = delete;
};
