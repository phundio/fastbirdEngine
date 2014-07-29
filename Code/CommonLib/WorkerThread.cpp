#include <CommonLib/StdAfx.h>
#include <CommonLib/WorkerThread.h>
#include <CommonLib/Task.h>
#include <CommonLib/TaskScheduler.h>

namespace fastbird
{

WorkerThread::WorkerThread(TaskScheduler* scheduler)
	: mCurrentTask(NULL)
	, mScheduler(scheduler)
{
    mTasksEvent = CreateSyncEvent();

    static DWORD ThreadID = 0;
    static char ThreadName[128];
    sprintf(ThreadName, "worker_thread_%d", ThreadID++);

    CreateThread(256 * 1024, ThreadName);
}

WorkerThread::~WorkerThread()
{
	SAFE_DELETE(mTasksEvent);
}

bool WorkerThread::Run()
{
    // Wait for the task trigger event
    mTasksEvent->Wait();

    // Loop executing ready tasks
    while(mCurrentTask)
    {
        // Execute current task
        mCurrentTask->Trigger(mScheduler);

        // Try to get another task
        mCurrentTask = mScheduler->GetNextReadyTask(this);
    }

    // Run a scheduler slice
    mScheduler->SchedulerSlice();

	return !*mForcedExit;
}

void WorkerThread::SetTask(Task* t)
{
    mCurrentTask = t;
    mTasksEvent->Trigger();
}

}