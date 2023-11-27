
#include "vk_workerthread.h"

VkWorkerThread::VkWorkerThread(VulkanRenderDevice* fb) : fb(fb)
{
	thread = std::thread([=]() { WorkerMain(); });
}

VkWorkerThread::~VkWorkerThread()
{
	std::unique_lock lock(mutex);
	StopFlag = true;
	lock.unlock();
	CondVar.notify_all();
	thread.join();
}

void VkWorkerThread::AddWork(std::unique_ptr<VkQueuedWork> work)
{
	std::unique_lock lock(mutex);
	WorkList.emplace_back(std::move(work));
	lock.unlock();
	CondVar.notify_all();
}

void VkWorkerThread::BeginFrame()
{
	if (ExceptionThrown)
	{
		std::rethrow_exception(ExceptionThrown);
	}

	std::unique_lock lock(mutex);
	std::vector<std::unique_ptr<VkQueuedWork>> list;
	list.swap(CompletedWorkList);
	lock.unlock();

	for (std::unique_ptr<VkQueuedWork>& work : list)
	{
		work->RunOnMainThread();
	}
}

void VkWorkerThread::WorkerMain()
{
	try
	{
		while (true)
		{
			std::unique_lock lock(mutex);
			CondVar.wait(lock, [&]() { return StopFlag || !WorkList.empty(); });
			if (StopFlag)
				break;
			std::vector<std::unique_ptr<VkQueuedWork>> list;
			list.swap(WorkList);
			lock.unlock();

			for (std::unique_ptr<VkQueuedWork>& work : list)
			{
				work->RunOnWorker();

				std::unique_lock lock2(mutex);
				CompletedWorkList.emplace_back(std::move(work));
			}
		}
	}
	catch (...)
	{
		std::unique_lock lock(mutex);
		ExceptionThrown = std::current_exception();
	}
}
