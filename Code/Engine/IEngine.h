// The "fastbird engine" library is distributed under the MIT license:
// 
// Copyright (c) 2013 fastbird(jungwan82@naver.com)
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy
//  of this software and associated documentation files (the "Software"), to deal
//  in the Software without restriction, including without limitation the rights
//  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//  copies of the Software, and to permit persons to whom the Software is
//  furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
//  all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
//  THE SOFTWARE.

#pragma once
#ifndef IEngine_header_included
#define IEngine_header_included

#include <CommonLib/SmartPtr.h>
#include <Engine/IInputListener.h>

#define FB_DEFAULT_DEBUG_ARG "%s(%d): %s() - %s", __FILE__, __LINE__, __FUNCTION__
#define FB_LOG(msg) fastbird::IEngine::Log(FB_DEFAULT_DEBUG_ARG, (msg));
#define FB_LOG_LAST_ERROR() fastbird::IEngine::LogLastError(__FILE__, __LINE__, __FUNCTION__)
namespace fastbird
{
	struct GlobalEnv;
	class IRenderer;
	class ICamera;
	class IScene;
	class IFont;
	class Vec2I;
	class Vec3;
	class IUIObject;
	class IMeshObject;
	class IMeshGroup;
	class IParticleEmitter;

	class CLASS_DECLSPEC_ENGINE IEngine : public ReferenceCounter
	{
	public:
		static IEngine* CreateInstance();

		virtual ~IEngine(){}
		virtual void GetGlobalEnv(GlobalEnv** outGloblEnv) = 0;
		virtual HWND CreateEngineWindow(int x, int y, int width, int height, 
			const char* title, WNDPROC winProc)
			{
				int a=0;
				a++;
				return 0;
		}
		virtual HWND GetWindowHandle() const = 0;
		virtual int InitSwapChain(HWND handle, int width, int height) = 0;
		enum RENDERER_TYPE 
		{ 
			D3D9=0, 
			D3D11=1, 
			OPENGL=2
		};
		virtual bool InitEngine(int rendererType) = 0;

		virtual inline IRenderer* GetRenderer() const = 0;
		virtual inline IScene* GetScene() const = 0;
		virtual void SetSceneOverride(IScene* pScene) = 0;

		virtual void UpdateInput() = 0;
		virtual void UpdateFrame(float dt) = 0;

		virtual ICamera* CreateAndRegisterCamera(const char* cameraName) = 0;
		virtual void RegisterCamera(const char* cameraName, ICamera* pCamera) = 0;
		virtual ICamera* GetCamera(int idx) = 0;
		virtual ICamera* GetCamera(const char* cameraName) = 0;
		
		// Terrain
		// numVertX * numVertY = (2^n+1) * (2^m+1)
		virtual bool CreateTerrain(int numVertX, int numVertY, float distance, const char* heightmapFile = 0) = 0;
		virtual bool CreateSkyBox() = 0;

		virtual void AddInputListener(IInputListener* pInputListener, 
			IInputListener::INPUT_LISTEN_CATEGORY category, int priority) = 0;
		virtual void RemoveInputListener(IInputListener* pInputListener) = 0;

		virtual void RegisterUIs(std::vector<IUIObject*>& uiobj) = 0;
		virtual void UnregisterUIs() = 0;

		struct MeshImportDesc
		{
			MeshImportDesc()
				: yzSwap(false), oppositeCull(true),
				useIndexBuffer(false), mergeMaterialGroups(false),
				keepMeshData(false), generateTangent(true)
			{
			}
			bool yzSwap;
			bool oppositeCull;
			bool useIndexBuffer;
			bool mergeMaterialGroups;
			bool keepMeshData;
			bool generateTangent;
		};
		virtual IMeshObject* GetMeshObject(const char* daeFilePath, 
			bool reload = false, const MeshImportDesc& desc = MeshImportDesc()) = 0;
		virtual IMeshGroup* GetMeshGroup(const char* daeFilePath, 
			bool reload = false, const MeshImportDesc& desc = MeshImportDesc()) = 0;
		virtual IParticleEmitter* GetParticleEmitter(const char* file) = 0;
		virtual IParticleEmitter* GetParticleEmitter(unsigned id) = 0;

#ifdef _FBENGINE_FOR_WINDOWS_
		// return processed
		static LRESULT WinProc( HWND window, UINT msg, WPARAM wp, LPARAM lp );
#elif _FBENGINE_FOR_LINUX_

#endif

		static void Log(const char* szFmt, ...);
		static void Error(const char* szFmt, ...);
		static void LogLastError(const char* file, int line, const char* function);
	};

	typedef SmartPtr<IEngine> EnginePtr;
};

#endif