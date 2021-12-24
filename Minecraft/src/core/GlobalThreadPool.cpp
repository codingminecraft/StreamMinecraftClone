#include "core/GlobalThreadPool.h"

namespace Minecraft
{
	bool CompareThreadTask::operator()(const ThreadTask& a, const ThreadTask& b) const
	{
		if (a.priority == b.priority)
		{
			return a.counter > b.counter;
		}

		return (uint8)a.priority > (uint8)b.priority;
	}

	GlobalThreadPool::GlobalThreadPool(uint32 numThreads)
		: cv(), queueMtx(), generalMtx(), doWork(true), numThreads(numThreads)
	{
		workerThreads = new std::thread[numThreads];
		for (uint32 i = 0; i < numThreads; i++)
		{
			workerThreads[i] = std::thread(&GlobalThreadPool::processLoop, this, i);
		}
	}

	void GlobalThreadPool::free()
	{
		{
			std::lock_guard lock(generalMtx);
			doWork = false;
		}
		cv.notify_all();

		for (uint32 i = 0; i < numThreads; i++)
		{
			workerThreads[i].join();
		}
		delete[] workerThreads;
	}

	void GlobalThreadPool::processLoop(uint32 threadIndex)
	{
#ifdef _USE_OPTICK
		std::string threadName = "GlobalThread_" + std::to_string(threadIndex);
		OPTICK_THREAD(threadName.c_str());
#endif

		bool shouldContinue = true;
		while (shouldContinue)
		{
			if (tasks.empty())
			{
				// Wait until we need to do some work
				std::unique_lock<std::mutex> lock(generalMtx);
				cv.wait(lock, [&] { return (!doWork || !tasks.empty()); });
				shouldContinue = doWork && !tasks.empty();
			}

			ThreadTask task;
			{
				std::lock_guard<std::mutex> queueLock(queueMtx);
				if (tasks.size() > 0)
				{
					task = tasks.top();
					tasks.pop();
				}
				else
				{
					task.fn = nullptr;
				}
			}

			if (task.fn)
			{
				{
#ifdef _USE_OPTICK
					OPTICK_EVENT_DYNAMIC(task.taskName);
#endif
					task.fn(task.data, task.dataSize);
				}
				if (task.callback)
				{
					task.callback(task.data, task.dataSize);
				}
			}
		}
	}

	void GlobalThreadPool::queueTask(TaskFunction function, const char* taskName, void* data, size_t dataSize, Priority priority, ThreadCallback callback)
	{
		static uint64 counter = 0;
		ThreadTask task;
		task.fn = function;
		task.data = data;
		task.dataSize = dataSize;
		task.priority = priority;
		task.callback = callback;
		task.taskName = taskName;
		{
			std::lock_guard<std::mutex> lockGuard(queueMtx);
			task.counter = counter++;
			tasks.push(task);
		}
	}

	void GlobalThreadPool::beginWork(bool notifyAll)
	{
		if (notifyAll)
		{
			cv.notify_all();
		}
		else
		{
			cv.notify_one();
		}
	}
}
