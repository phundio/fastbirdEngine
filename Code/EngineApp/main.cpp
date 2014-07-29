#include "StdAfx.h"
#include "InputHandler.h"
#include "CameraMan.h"
#include <Engine/IVoxelizer.h>
#include <Engine/IRenderToTexture.h>
#include <CommonLib/threads.h>
#include "QuickSortTask.h"
#include <CommonLib/Profiler.h>
using namespace fastbird;

#define RUN_PARALLEL_EXAMPLE 1

fastbird::GlobalEnv* gEnv = 0;
CameraMan* gCameraMan = 0;
InputHandler* gInputHandler = 0;
TaskScheduler* gTaskSchedular = 0;
SmartPtr<IMeshObject> gMeshObject;
std::vector< SmartPtr<IMeshObject> > gVoxels;

//-----------------------------------------------------------------------------
void UpdateFrame()
{
	gEnv->pTimer->Tick();
	float elapsedTime = gpTimer->GetDeltaTime();
	gEnv->pEngine->UpdateInput();
	gCameraMan->Update(elapsedTime);
	gEnv->pEngine->UpdateFrame(elapsedTime);
}

//-----------------------------------------------------------------------------
void RunGame()
{
	MSG msg={};
	while(msg.message != WM_QUIT)
	{
		if (PeekMessage( &msg, NULL, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else
		{
			UpdateFrame();
		}
	}
}

//-----------------------------------------------------------------------------
LRESULT CALLBACK WinProc( HWND window, UINT msg, WPARAM wp, LPARAM lp )
{
	switch(msg)
	{
	case WM_CLOSE:
		{
			PostQuitMessage(0);
		}
		break;

	}
	return IEngine::WinProc(window, msg, wp, lp);
}

	//-----------------------------------------------------------------------------
	void InitEngine()
	{
		fastbird::IEngine* pEngine = ::Create_fastbird_Engine();
		gEnv = gFBEnv;
		pEngine->CreateEngineWindow(0, 0, 1600, 900, "Game", WinProc);
		pEngine->InitEngine(fastbird::IEngine::D3D11);
		pEngine->InitSwapChain(gEnv->pEngine->GetWindowHandle(), 1600, 900);
		gpTimer = gEnv->pTimer;

		if (gEnv->pRenderer)
		{
			gEnv->pRenderer->SetClearColor(0, 0, 0, 1);
		}
		gInputHandler = new InputHandler();
		gCameraMan = new CameraMan(gEnv->pRenderer->GetCamera());
		gEnv->pEngine->AddInputListener(gInputHandler, IInputListener::INPUT_LISTEN_PRIORITY_INTERACT, 0);

		gTaskSchedular = new TaskScheduler(8);
	}

//-----------------------------------------------------------------------------
int main()
{
	_CrtSetDbgFlag ( _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF );

	std::vector<int> test;
	test.push_back(0);
	test.push_back(0);
	
	
	//----------------------------------------------------------------------------
	// 1. How to init engine.
	//----------------------------------------------------------------------------
	InitEngine();

	//----------------------------------------------------------------------------
	// 2. How to create sky -  see Data/Materials/skybox.material
	//----------------------------------------------------------------------------
	gEnv->pEngine->CreateSkyBox();

	//----------------------------------------------------------------------------
	// 3. How to load model file and material.
	//----------------------------------------------------------------------------
	gMeshObject = gEnv->pEngine->GetMeshObject("data/objects/CommandModule/CommandModule.dae"); // using collada file.
	gMeshObject->AttachToScene();
	gMeshObject->SetMaterial("data/objects/CommandModule/CommandModule.material");

	//----------------------------------------------------------------------------
	// 4. How to use voxelizer (Need to include <Engine/IVoxelizer.h>)
	//----------------------------------------------------------------------------
	SmartPtr<IVoxelizer> voxelizer = IVoxelizer::CreateVoxelizer();
	bool ret = voxelizer->RunVoxelizer("data/objects/etc/spaceship.dae", 64, false, true);
	assert(ret);
	SmartPtr<IMeshObject> voxelObject = gEnv->pEngine->GetMeshObject("data/objects/etc/cube.dae");
	const IVoxelizer::HULLS& h = voxelizer->GetHulls();
	Vec3 offset(30, 0, 0);
	for each(auto v in h)
	{
		gVoxels.push_back((IMeshObject*)voxelObject->Clone());
		IMeshObject* m = gVoxels.back();
		m->SetPos(offset + v*2.0f);
		m->AttachToScene();
	}
	voxelizer = 0;
	voxelObject = 0;

	//----------------------------------------------------------------------------
	// 5. How to use Render To Texture (Need to include <Engine/IRenderToTexture.h>)
	//----------------------------------------------------------------------------
	SmartPtr<IRenderToTexture> rtt = gEnv->pRenderer->CreateRenderToTexture(false);
	rtt->GetScene()->AttachObject(gMeshObject);
	ICamera* pRTTCam = rtt->GetCamera();
	pRTTCam->SetPos(Vec3(-5, 0, 0));
	pRTTCam->SetDir(Vec3(1, 0, 0));
	rtt->SetColorTextureDesc(1024, 1024, PIXEL_FORMAT_R8G8B8A8_UNORM, true, false);
	rtt->Render();
	rtt->GetRenderTargetTexture()->SaveToFile("rtt.png");
	rtt = 0;
	
#if RUN_PARALLEL_EXAMPLE
	//----------------------------------------------------------------------------
	// 6. How to parallel computing. (Need to include <CommonLib/threads.h>)
	// reference : Efficient and Scalable Multicore Programming
	//----------------------------------------------------------------------------
	int numInts = INT_MAX/100;
	int* pInts = new int[numInts];
	{
		//------------------------------------------------------------------------
		// 7. how to profile (Need to include <CommonLib/Profiler.h>)
		//------------------------------------------------------------------------
		Profiler pro("RandomGeneration");
		DWORD random = 0;
		for (int i = 0; i < numInts; i++)
		{
			pInts[i] = random & 0x7fffffff;
			random = random * 196314165 + 907633515;
		}
	}
	QuickSortTask* pQuickSort = new QuickSortTask(pInts, 0, numInts, 0);
	// single threaded
	{
		Profiler pro("QuickSort_SingleThread.");
		pQuickSort->Execute(0);
	}
	for (int i = 0; i<numInts - 1; i++)
	{
		assert(pInts[i] <= pInts[i + 1]);
	}
	delete pQuickSort;
	
	DWORD random = 0;
	for (int i = 0; i < numInts; i++)
	{
		pInts[i] = random & 0x7fffffff;
		random = random * 196314165 + 907633515;
	}
	// multi threaded.
	pQuickSort = new QuickSortTask(pInts, 0, numInts, 0);
	gTaskSchedular->AddTask(pQuickSort);
	{
		Profiler pro("QuickSort_MultiThread.");
		pQuickSort->Sync(); // wait to finish
	}
	for (int i = 0; i<numInts - 1; i++)
	{
		assert(pInts[i] <= pInts[i + 1]);
	}
	delete pQuickSort;

	delete[] pInts;
#endif // RUN_PARALLEL_EXAMPLE
	//----------------------------------------------------------------------------

	//----------------------------------------------------------------------------
	// Entering game loop.
	//----------------------------------------------------------------------------
	RunGame();
	gTaskSchedular->Finalize();

	gMeshObject = 0;
	gVoxels.clear();
	SAFE_DELETE(gCameraMan);
	SAFE_DELETE(gInputHandler);
	SAFE_DELETE(gTaskSchedular);

	if (gEnv)
	{
		Destroy_fastbird_Engine();
		Log("Engine Destroyed.");
	}
}

//---------------------------------------------------------------------------
namespace fastbird
{
	void Error(const char* szFmt, ...)
	{
		char buf[2048];
		va_list args;
		va_start(args, szFmt);
		vsprintf_s(buf, 2048, szFmt, args);
		va_end(args);
		gEnv->pEngine->Error(buf);
		//assert(0);
	}

	void Log(const char* szFmt, ...)
	{
		char buf[2048];
		va_list args;
		va_start(args, szFmt);
		vsprintf_s(buf, 2048, szFmt, args);
		va_end(args);
		gEnv->pEngine->Log(buf);
	}
} // namespace fastbird