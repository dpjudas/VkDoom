
#pragma once

#include <functional>
#include <mutex>
#include <thread>
#include <condition_variable>

class VulkanRenderDevice;

class VkQueuedWork
{
public:
	virtual ~VkQueuedWork() = default;
	virtual void RunOnWorker() = 0;
	virtual void RunOnMainThread() = 0;
};

class VkWorkerThread
{
public:
	VkWorkerThread(VulkanRenderDevice* fb);
	~VkWorkerThread();

	void AddWork(std::unique_ptr<VkQueuedWork> work);
	void BeginFrame();

private:
	void WorkerMain();

	VulkanRenderDevice* fb = nullptr;

	std::thread thread;
	std::mutex mutex;
	std::condition_variable CondVar;
	bool StopFlag = false;
	std::vector<std::unique_ptr<VkQueuedWork>> WorkList;
	std::vector<std::unique_ptr<VkQueuedWork>> CompletedWorkList;
	std::exception_ptr ExceptionThrown;
};
