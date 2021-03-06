/*
 -----------------------------------------------------------------------------
 This source file is part of fastbird engine
 For the latest info, see http://www.jungwan.net/
 
 Copyright (c) 2013-2015 Jungwan Byun
 
 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:
 
 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.
 
 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE.
 -----------------------------------------------------------------------------
*/

#include "stdafx.h"
#include "Renderer.h"
#include "IPlatformRenderer.h"
#include "NullPlatformRenderer.h"
#include "RendererEnums.h"
#include "RendererStructs.h"
#include "Texture.h"
#include "RenderTarget.h"
#include "IPlatformTexture.h"
#include "Font.h"
#include "RendererOptions.h"
#include "VertexBuffer.h"
#include "IndexBuffer.h"
#include "Shader.h"
#include "Material.h"
#include "TextureAtlas.h"
#include "DebugHud.h"
#include "RenderStates.h"
#include "ResourceProvider.h"
#include "ResourceTypes.h"
#include "Camera.h"
#include "ConsoleRenderer.h"
#include "StarDef.h"
#include "CascadedShadowsManager.h"
#include "LuaFunctions.h"
#include "RendererKeys.h"
#include "FBCommonHeaders/SpinLock.h"
#include "FBStringLib/MurmurHash.h"
#include "FBMathLib/Frustum.h"
#include "FBConsole/Console.h"
#include "EssentialEngineData/shaders/Constants.h"
#include "EssentialEngineData/shaders/CommonDefines.h"
#include "FBStringLib/StringConverter.h"
#include "FBStringLib/StringLib.h"
#include "FBStringMathLib/StringMathConverter.h"
#include "FBCommonHeaders/VectorMap.h"
#include "FBCommonHeaders/Factory.h"
#include "FBSystemLib/ModuleHandler.h"
#include "FBSystemLib/System.h"
#include "FBFileSystem/FileSystem.h"
#include "FBSceneManager/IScene.h"
#include "FBInputManager/IInputInjector.h"
#include "FBLua/LuaObject.h"
#include "TinyXmlLib/tinyxml2.h"
#include "FBTimer/Timer.h"
#include "FBDebugLib/DebugLib.h"
#include "FBThread/Invoker.h"
#include <set>
#undef DrawText
#undef CreateDirectory
namespace fb{
	ShaderPtr GetShaderFromExistings(IPlatformShaderPtr platformShader);
	TexturePtr GetTextureFromExistings(IPlatformTexturePtr platformTexture);
	FB_DECLARE_SMART_PTR(UI3DObj);
	FB_DECLARE_SMART_PTR(UIObject);
}
using namespace fb;

static const float defaultFontSize = 20.f;
const HWindow Renderer::INVALID_HWND = (HWindow)-1;
class Renderer::Impl{
public:
	typedef fb::Factory<IPlatformRenderer>::CreateCallback CreateCallback;
	typedef std::vector<RenderTargetWeakPtr> RenderTargets;

	RendererWeakPtr mSelfPtr;
	Renderer* mSelf;

	std::string mPlatformRendererType;
	struct PlatformRendererHolder{
		IPlatformRenderer* mPlatformRenderer;
		ModuleHandle mLoadedModule;

		PlatformRendererHolder(IPlatformRenderer* platformRenderer, ModuleHandle module)
			: mPlatformRenderer(platformRenderer)
			, mLoadedModule(module)
		{
		}

		~PlatformRendererHolder(){
			typedef void(*Destroy)();
			Destroy DestroyFunction = (Destroy)ModuleHandler::GetFunction(mLoadedModule, "DeleteRenderEngine");
			if (DestroyFunction){
				DestroyFunction();
			}
			ModuleHandler::UnloadModule(mLoadedModule);
		}		
		operator IPlatformRenderer* () const { return mPlatformRenderer; }
	};
	std::shared_ptr<PlatformRendererHolder> mPlatformRenderer;
	IPlatformRendererPtr mNullRenderer;
	

	std::unordered_map<HWindowId, HWindow> mWindowHandles;
	std::unordered_map<HWindow, HWindowId> mWindowIds;
	std::unordered_map<HWindowId, Vec2I> mWindowSizes;
	HWindowId mMainWindowId;	
	VectorMap<HWindowId, RenderTargetPtr> mWindowRenderTargets;
	RenderTargetPtr mCurrentRenderTarget;
	std::vector<TexturePtr> mCurrentRTTextures;
	std::vector<size_t> mCurrentViewIndices;
	std::vector<TexturePtr> mKeptCurrentRTTextures;
	std::vector<size_t> mKeptCurrentViewIndices;
	TexturePtr mCurrentDSTexture;
	size_t mCurrentDSViewIndex;
	std::map<TextureWeakPtr, TexturePtr, std::owner_less<TextureWeakPtr>> mDepthStencilTextureOverride;
	std::stack<TextureWeakPtr> mDepthTextureStack; // backup for original depth texture before overriding;
	Vec2I mCurrentRTSize;
	std::vector<Viewport> mCurrentViewports;
	std::vector<RenderTargetPtr> mRenderTargetPool;
	RenderTargets mRenderTargets;
	RenderTargets mRenderTargetsEveryFrame;	
	ISceneWeakPtr mCurrentScene;
	CameraPtr mCamera;
	CameraPtr mCameraBackup;
	CameraPtr mOverridingCamera;
	CascadedShadowsManagerPtr mShadowManager;
	std::unordered_map<std::string, TexturePtr> mTexturesReferenceKeeper;
	Mat44 mScreenToNDCMatrix;
	OBJECT_CONSTANTS mObjectConstants;
	CameraPtr mAxisDrawingCamera;

	struct InputInfo{
		Vec2I mCurrentMousePos;
		bool mLButtonDown;

		InputInfo()
			:mCurrentMousePos(0, 0)
			, mLButtonDown(false)
		{}
	};
	InputInfo mInputInfo;
	
	struct TemporalDepthKey{
		Vec2I mSize;
		std::string mKey;

		TemporalDepthKey(const Vec2I& size, const char* key)
			: mSize(size)
		{
			if (key)
				mKey = key;
		}

		bool operator ==(const TemporalDepthKey& other) const{
			return mSize == other.mSize && mKey == other.mKey;
		}

		bool operator < (const TemporalDepthKey& other) const{
			if (mSize < other.mSize)
				return true;
			else if (mSize == other.mSize){
				if (mKey < other.mKey)
					return true;
			}
			return false;
		}
	};
	VectorMap<TemporalDepthKey, TexturePtr> mTempDepthBuffers;
	TexturePtr mEnvironmentTexture;
	TexturePtr mEnvironmentTextureOverride;
	bool mGenerateRadianceCoef;
	std::unordered_map<SystemTextures::Enum, std::vector< TextureBinding > > mSystemTextureBindings;
	FRAME_CONSTANTS			mFrameConstants;
	CAMERA_CONSTANTS		mCameraConstants;
	RENDERTARGET_CONSTANTS	mRenderTargetConstants;
	SCENE_CONSTANTS			mSceneConstants;

	DirectionalLightInfo	mDirectionalLight[2];
	VectorMap<int, FontPtr> mFonts;
	DebugHudPtr		mDebugHud;	
	RendererOptionsPtr mRendererOptions;
	RENDERER_FRAME_PROFILER mFrameProfiler;
	PRIMITIVE_TOPOLOGY mCurrentTopology;	
	const int DEFAULT_DYN_VERTEX_COUNTS=100;
	VertexBufferPtr mDynVBs[DEFAULT_INPUTS::COUNT];
	INPUT_ELEMENT_DESCS mInputLayoutDescs[DEFAULT_INPUTS::COUNT];
	ResourceProviderPtr mResourceProvider;

	// 1/4
	// x, y,    offset, weight;
	VectorMap< std::pair<DWORD, DWORD>, std::pair<std::vector<Vec4f>, std::vector<Vec4f> > > mGauss5x5;	
	InputLayoutPtr mPositionInputLayout;
	ConsoleRendererPtr mConsoleRenderer;
	TextureAtlasPtr mFontTextureAtlas;

	struct DebugRenderTarget
	{
		Vec2 mPos;
		Vec2 mSize;

		TexturePtr mTexture;
	};
	static const unsigned MaxDebugRenderTargets = 4;
	DebugRenderTarget mDebugRenderTargets[MaxDebugRenderTargets];	
	bool mLuminanceOnCpu;
	bool mUseFilmicToneMapping;
	bool m3DUIEnabled;
	Real mLuminance;
	unsigned mFrameLuminanceCalced;
	Real mFadeAlpha;

	typedef VectorMap<HWindowId, std::vector<UIObjectPtr> > UI_OBJECTS;
	UI_OBJECTS mUIObjectsToRender;
	typedef VectorMap< std::pair<HWindowId, std::string>, std::vector<UIObjectPtr>> UI_3DOBJECTS;
	UI_3DOBJECTS mUI3DObjects;
	VectorMap<std::string, RenderTargetPtr> mUI3DObjectsRTs;
	VectorMap<std::string, UI3DObjPtr> mUI3DRenderObjs;	
	unsigned mMainWindowStyle;
	bool mWindowSizeInternallyChanging;	
	Vec4f mIrradCoeff[9];

	//-----------------------------------------------------------------------
	Impl(Renderer* renderer)
		: mSelf(renderer)
		, mNullRenderer(NullPlatformRenderer::Create())
		, mCurrentTopology(PRIMITIVE_TOPOLOGY_UNKNOWN)		
		, mUseFilmicToneMapping(true)
		, mFadeAlpha(0.)
		, mLuminance(0.5f)
		, mLuminanceOnCpu(false)
		, mWindowSizeInternallyChanging(false)
		, mConsoleRenderer(ConsoleRenderer::Create())
		, mRendererOptions(RendererOptions::Create())		
		, mMainWindowStyle(0)
		, mGenerateRadianceCoef(false)
	{
		auto filepath = "_FBRenderer.log";
		FileSystem::BackupFile(filepath, 5, "Backup_Log");
		Logger::Init(filepath);
		auto& envBindings = mSystemTextureBindings[SystemTextures::Environment];
		envBindings.push_back(TextureBinding{ SHADER_TYPE_PS, 4 });
		auto& depthBindings = mSystemTextureBindings[SystemTextures::Depth];
		depthBindings.push_back(TextureBinding{ SHADER_TYPE_GS, 5 });
		depthBindings.push_back(TextureBinding{ SHADER_TYPE_PS, 5 });
		auto& cloudBindings = mSystemTextureBindings[SystemTextures::CloudVolume];
		cloudBindings.push_back(TextureBinding{SHADER_TYPE_PS, 6});
		auto& noiseBindings = mSystemTextureBindings[SystemTextures::Noise];
		noiseBindings.push_back(TextureBinding{ SHADER_TYPE_PS, 7 });
		auto& shadowBindings = mSystemTextureBindings[SystemTextures::ShadowMap];
		shadowBindings.push_back(TextureBinding{ SHADER_TYPE_PS, 8 });
		auto& ggxBindings = mSystemTextureBindings[SystemTextures::GGXPrecalc];
		ggxBindings.push_back(TextureBinding{ SHADER_TYPE_PS, 9 });
		auto& permBindings = mSystemTextureBindings[SystemTextures::Permutation];
		permBindings.push_back(TextureBinding{ SHADER_TYPE_PS, 10 });
		permBindings.push_back(TextureBinding{ SHADER_TYPE_CS, 10 });
		auto& gradiantsBindings = mSystemTextureBindings[SystemTextures::Gradiants];
		gradiantsBindings.push_back(TextureBinding{ SHADER_TYPE_PS, 11 });
		gradiantsBindings.push_back(TextureBinding{ SHADER_TYPE_CS, 11 });
		auto& permFloatBindings = mSystemTextureBindings[SystemTextures::ValueNoise];
		permFloatBindings.push_back(TextureBinding{ SHADER_TYPE_PS, 12 });
		

		if (Console::HasInstance()){
			Console::GetInstance().AddObserver(ICVarObserver::Default, mRendererOptions);
		}
		else{
			Logger::Log(FB_ERROR_LOG_ARG, "The console is not initialized!");
		}
		RegisterRendererLuaFunctions();
	}

	~Impl(){
		ClearLoadedMaterials();
		StarDef::FinalizeStatic();
		Logger::Release();
	}	

	bool PrepareRenderEngine(const char* type){
		if (!type || strlen(type) == 0){
			Logger::Log(FB_DEFAULT_LOG_ARG, "Cannot prepare a render engine : invalid arg.");
			return false;
		}
		if (mPlatformRenderer){
			Logger::Log(FB_DEFAULT_LOG_ARG, "Render engine is already prepared.");
			return true;
		}

		mPlatformRendererType = type;		
		auto module = ModuleHandler::LoadModule(mPlatformRendererType.c_str());
		if (module){
			typedef fb::IPlatformRenderer*(*Create)();
			Create createCallback = (Create)ModuleHandler::GetFunction(module, "CreateRenderEngine");
			if (createCallback){
				auto platformRenderer = createCallback();
				if (platformRenderer){
					mPlatformRenderer = std::shared_ptr<PlatformRendererHolder>(
						new PlatformRendererHolder(platformRenderer, module), 
						[](PlatformRendererHolder* obj){delete obj; });
					Logger::Log(FB_DEFAULT_LOG_ARG, FormatString("Render engine %s is prepared.", type).c_str());
					return true;
				}
				else{
					Logger::Log(FB_ERROR_LOG_ARG, FormatString("Cannot create a platform renderer(%s)", mPlatformRendererType.c_str()).c_str());
				}
			}
			else{
				Logger::Log(FB_ERROR_LOG_ARG, "Cannot find the entry point 'CreateRenderEngine()'");
			}
		}
		else{
			Logger::Log(FB_ERROR_LOG_ARG, FormatString("Failed to load a platform renderer module(%s)", mPlatformRendererType.c_str()).c_str());
		}
		return false;
	}

	void PrepareQuit() {
		GetPlatformRenderer().PrepareQuit();
	}

	IPlatformRenderer& GetPlatformRenderer() const {
		if (!mPlatformRenderer)
		{
			return *mNullRenderer.get();

		}

		return *mPlatformRenderer->mPlatformRenderer;
	}

	bool InitCanvas(HWindowId id, HWindow window, int width, int height){
		bool mainCanvas = false;
		if (mWindowHandles.empty()){
			mMainWindowId = id;
			mainCanvas = true;
		}

		mWindowHandles[id] = window;
		mWindowIds[window] = id;
		if (width == 0 || height == 0){
			width = mRendererOptions->r_resolution.x;
			height = mRendererOptions->r_resolution.y;
		}
		mWindowSizes[id] = { width, height };		
		IPlatformTexturePtr platformColorTexture;
		IPlatformTexturePtr platformDepthTexture;
				
		CanvasInitInfo ci(id, window, width, height, 0,
			PIXEL_FORMAT_R8G8B8A8_UNORM, PIXEL_FORMAT_D24_UNORM_S8_UINT);
		GetPlatformRenderer().InitCanvas(ci, platformColorTexture, platformDepthTexture);

		if (platformColorTexture && platformDepthTexture){
			RenderTargetParam param;
			param.mSize = { width, height };
			param.mWillCreateDepth = true;
			auto rt = CreateRenderTarget(param);				
			auto colorTexture = CreateTexture(platformColorTexture);
			colorTexture->SetType(TEXTURE_TYPE_RENDER_TARGET);
			rt->SetColorTexture(colorTexture);
			auto depthTexture = CreateTexture(platformDepthTexture);
			depthTexture->SetType(TEXTURE_TYPE_DEPTH_STENCIL);
			rt->SetDepthTexture(depthTexture);
			rt->SetAssociatedWindowId(id);
			
			mWindowRenderTargets[id] = rt;

			mRenderTargetConstants.gScreenSize = Vec2((Real)width, (Real)height);
			mRenderTargetConstants.gScreenRatio = width / (float)height;
			mRenderTargetConstants.rendertarget_dummy = 0;
			GetPlatformRenderer().UpdateShaderConstants(ShaderConstants::RenderTarget, &mRenderTargetConstants, sizeof(RENDERTARGET_CONSTANTS));
			if (mainCanvas){
				OnMainCavasCreated();				
			}
			return true;
		}
		else{
			Logger::Log(FB_ERROR_LOG_ARG, "Failed to create cavas");
			return false;
		}		
	}

	void OnMainCavasCreated(){
		mResourceProvider = ResourceProvider::Create();
		GetMainRenderTarget()->Bind();
		// POSITION
		{
			INPUT_ELEMENT_DESC desc("POSITION", 0, INPUT_ELEMENT_FORMAT_FLOAT3, 0, 0, INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0);
			mInputLayoutDescs[DEFAULT_INPUTS::POSITION].push_back(desc);
		}

		// POSITION_COLOR
		{
			INPUT_ELEMENT_DESC desc[] =
			{
				INPUT_ELEMENT_DESC("POSITION", 0, INPUT_ELEMENT_FORMAT_FLOAT3, 0, 0,
				INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0),
				INPUT_ELEMENT_DESC("COLOR", 0, INPUT_ELEMENT_FORMAT_UBYTE4, 0, 12,
				INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0)
			};
			mInputLayoutDescs[DEFAULT_INPUTS::POSITION_COLOR].push_back(desc[0]);
			mInputLayoutDescs[DEFAULT_INPUTS::POSITION_COLOR].push_back(desc[1]);
		}

		// POSITION_COLOR_TEXCOORD
		{
			INPUT_ELEMENT_DESC desc[] =
			{
				INPUT_ELEMENT_DESC("POSITION", 0, INPUT_ELEMENT_FORMAT_FLOAT3, 0, 0,
				INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0),
				INPUT_ELEMENT_DESC("COLOR", 0, INPUT_ELEMENT_FORMAT_UBYTE4, 0, 12,
				INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0),
				INPUT_ELEMENT_DESC("TEXCOORD", 0, INPUT_ELEMENT_FORMAT_FLOAT2, 0, 16,
				INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0),
			};
			mInputLayoutDescs[DEFAULT_INPUTS::POSITION_COLOR_TEXCOORD].push_back(desc[0]);
			mInputLayoutDescs[DEFAULT_INPUTS::POSITION_COLOR_TEXCOORD].push_back(desc[1]);
			mInputLayoutDescs[DEFAULT_INPUTS::POSITION_COLOR_TEXCOORD].push_back(desc[2]);
		}

		// POSITION_HDR_COLOR
		{
			INPUT_ELEMENT_DESC desc[] =
			{
				INPUT_ELEMENT_DESC("POSITION", 0, INPUT_ELEMENT_FORMAT_FLOAT3, 0, 0,
				INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0),
				INPUT_ELEMENT_DESC("COLOR", 0, INPUT_ELEMENT_FORMAT_FLOAT4, 0, 12,
				INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0)
			};
			mInputLayoutDescs[DEFAULT_INPUTS::POSITION_HDR_COLOR].push_back(desc[0]);
			mInputLayoutDescs[DEFAULT_INPUTS::POSITION_HDR_COLOR].push_back(desc[1]);
		}

		// POSITION_NORMAL,
		{
			INPUT_ELEMENT_DESC desc[] =
			{
				INPUT_ELEMENT_DESC("POSITION", 0, INPUT_ELEMENT_FORMAT_FLOAT3, 0, 0,
				INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0),
				INPUT_ELEMENT_DESC("NORMAL", 0, INPUT_ELEMENT_FORMAT_FLOAT3, 0, 12,
				INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0)
			};
			mInputLayoutDescs[DEFAULT_INPUTS::POSITION_NORMAL].push_back(desc[0]);
			mInputLayoutDescs[DEFAULT_INPUTS::POSITION_NORMAL].push_back(desc[1]);
		}

		//POSITION_TEXCOORD,
		{
			INPUT_ELEMENT_DESC desc[] =
			{
				INPUT_ELEMENT_DESC("POSITION", 0, INPUT_ELEMENT_FORMAT_FLOAT3, 0, 0,
				INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0),
				INPUT_ELEMENT_DESC("TEXCOORD", 0, INPUT_ELEMENT_FORMAT_FLOAT2, 0, 12,
				INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0),
			};
			mInputLayoutDescs[DEFAULT_INPUTS::POSITION_TEXCOORD].push_back(desc[0]);
			mInputLayoutDescs[DEFAULT_INPUTS::POSITION_TEXCOORD].push_back(desc[1]);
		}
		//POSITION_COLOR_TEXCOORD_BLENDINDICES,
		{
			INPUT_ELEMENT_DESC desc[] =
			{
				INPUT_ELEMENT_DESC("POSITION", 0, INPUT_ELEMENT_FORMAT_FLOAT3, 0, 0,
				INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0),
				INPUT_ELEMENT_DESC("COLOR", 0, INPUT_ELEMENT_FORMAT_UBYTE4, 0, 12,
				INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0),
				INPUT_ELEMENT_DESC("TEXCOORD", 0, INPUT_ELEMENT_FORMAT_FLOAT2, 0, 16,
				INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0),
				INPUT_ELEMENT_DESC("BLENDINDICES", 0, INPUT_ELEMENT_FORMAT_UBYTE4, 0, 24,
				INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0)
			};
			mInputLayoutDescs[DEFAULT_INPUTS::POSITION_COLOR_TEXCOORD_BLENDINDICES]
				.push_back(desc[0]);
			mInputLayoutDescs[DEFAULT_INPUTS::POSITION_COLOR_TEXCOORD_BLENDINDICES]
				.push_back(desc[1]);
			mInputLayoutDescs[DEFAULT_INPUTS::POSITION_COLOR_TEXCOORD_BLENDINDICES]
				.push_back(desc[2]);
			mInputLayoutDescs[DEFAULT_INPUTS::POSITION_COLOR_TEXCOORD_BLENDINDICES]
				.push_back(desc[3]);
		}

		//POSITION_NORMAL_TEXCOORD,
		{
			INPUT_ELEMENT_DESC desc[] =
			{
				INPUT_ELEMENT_DESC("POSITION", 0, INPUT_ELEMENT_FORMAT_FLOAT3, 0, 0,
				INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0),
				INPUT_ELEMENT_DESC("NORMAL", 0, INPUT_ELEMENT_FORMAT_FLOAT3, 0, 12,
				INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0),
				INPUT_ELEMENT_DESC("TEXCOORD", 0, INPUT_ELEMENT_FORMAT_FLOAT2, 0, 24,
				INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0),
			};
			mInputLayoutDescs[DEFAULT_INPUTS::POSITION_NORMAL_TEXCOORD].push_back(desc[0]);
			mInputLayoutDescs[DEFAULT_INPUTS::POSITION_NORMAL_TEXCOORD].push_back(desc[1]);
			mInputLayoutDescs[DEFAULT_INPUTS::POSITION_NORMAL_TEXCOORD].push_back(desc[2]);
		}

		// POSITION_VEC4,
		{
			INPUT_ELEMENT_DESC desc[] =
			{
				INPUT_ELEMENT_DESC("POSITION", 0, INPUT_ELEMENT_FORMAT_FLOAT3, 0, 0,
				INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0),
				INPUT_ELEMENT_DESC("TEXCOORD", 0, INPUT_ELEMENT_FORMAT_FLOAT4, 0, 12,
				INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0),
			};
			mInputLayoutDescs[DEFAULT_INPUTS::POSITION_VEC4].push_back(desc[0]);
			mInputLayoutDescs[DEFAULT_INPUTS::POSITION_VEC4].push_back(desc[1]);
		}

		// POSITION_VEC4_COLOR,
		{
			INPUT_ELEMENT_DESC desc[] =
			{
				INPUT_ELEMENT_DESC("POSITION", 0, INPUT_ELEMENT_FORMAT_FLOAT3, 0, 0,
				INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0),
				INPUT_ELEMENT_DESC("TEXCOORD", 0, INPUT_ELEMENT_FORMAT_FLOAT4, 0, 12,
				INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0),
				INPUT_ELEMENT_DESC("COLOR", 0, INPUT_ELEMENT_FORMAT_UBYTE4, 0, 28,
				INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0),
			};
			mInputLayoutDescs[DEFAULT_INPUTS::POSITION_VEC4].push_back(desc[0]);
			mInputLayoutDescs[DEFAULT_INPUTS::POSITION_VEC4].push_back(desc[1]);
			mInputLayoutDescs[DEFAULT_INPUTS::POSITION_VEC4].push_back(desc[2]);
		}

		//-----------------------------------------------------------------------
		mDynVBs[DEFAULT_INPUTS::POSITION] = CreateVertexBuffer(0, sizeof(DEFAULT_INPUTS::V_P),
			DEFAULT_DYN_VERTEX_COUNTS, BUFFER_USAGE_DYNAMIC, BUFFER_CPU_ACCESS_WRITE);
		mDynVBs[DEFAULT_INPUTS::POSITION_COLOR] = CreateVertexBuffer(0, sizeof(DEFAULT_INPUTS::V_PC),
			DEFAULT_DYN_VERTEX_COUNTS, BUFFER_USAGE_DYNAMIC, BUFFER_CPU_ACCESS_WRITE);
		mDynVBs[DEFAULT_INPUTS::POSITION_COLOR_TEXCOORD] = CreateVertexBuffer(0, sizeof(DEFAULT_INPUTS::V_PCT),
			DEFAULT_DYN_VERTEX_COUNTS, BUFFER_USAGE_DYNAMIC, BUFFER_CPU_ACCESS_WRITE);
		mDynVBs[DEFAULT_INPUTS::POSITION_NORMAL] = CreateVertexBuffer(0, sizeof(DEFAULT_INPUTS::V_PN),
			DEFAULT_DYN_VERTEX_COUNTS, BUFFER_USAGE_DYNAMIC, BUFFER_CPU_ACCESS_WRITE);
		mDynVBs[DEFAULT_INPUTS::POSITION_TEXCOORD] = CreateVertexBuffer(0, sizeof(DEFAULT_INPUTS::V_PT),
			DEFAULT_DYN_VERTEX_COUNTS, BUFFER_USAGE_DYNAMIC, BUFFER_CPU_ACCESS_WRITE);
		mDynVBs[DEFAULT_INPUTS::POSITION_COLOR_TEXCOORD_BLENDINDICES] =
			CreateVertexBuffer(0, sizeof(DEFAULT_INPUTS::V_PCTB),
			DEFAULT_DYN_VERTEX_COUNTS, BUFFER_USAGE_DYNAMIC, BUFFER_CPU_ACCESS_WRITE);
		mDynVBs[DEFAULT_INPUTS::POSITION_VEC4] = CreateVertexBuffer(0, sizeof(DEFAULT_INPUTS::V_PV4),
			DEFAULT_DYN_VERTEX_COUNTS, BUFFER_USAGE_DYNAMIC, BUFFER_CPU_ACCESS_WRITE);
		mDynVBs[DEFAULT_INPUTS::POSITION_VEC4_COLOR] = CreateVertexBuffer(0, sizeof(DEFAULT_INPUTS::V_PV4C),
			DEFAULT_DYN_VERTEX_COUNTS, BUFFER_USAGE_DYNAMIC, BUFFER_CPU_ACCESS_WRITE);

		//-----------------------------------------------------------------------
		static_assert(DEFAULT_INPUTS::COUNT == 10, "You may not define a new element of mInputLayoutDesc for the new description.");
		LuaLock L(LuaUtils::GetLuaState());
		LuaObject multiFontSet(L, "r_multiFont");
		if (multiFontSet.IsValid()){
			auto it = multiFontSet.GetSequenceIterator();
			LuaObject data;
			while (it.GetNext(data)){
				auto fontPath = data.GetString();
				if (!fontPath.empty()){
					LoadFont(fontPath.c_str());					
				}
			}
		}
		else{
			FontPtr font = Font::Create();
			LuaObject r_font(L, "r_font");
			std::string fontPath = r_font.GetString();
			if (fontPath.empty())
			{
				fontPath = "EssentialEngineData/fonts/nanum_myungjo_20.fnt";
			}
			LoadFont(fontPath.c_str());			
		}

		mDebugHud = DebugHud::Create();

		for (int i = 0; i <  ResourceTypes::SamplerStates::Num; ++i)
		{
			auto sampler = mResourceProvider->GetSamplerState(i);
			SetSamplerState(i, SHADER_TYPE_PS, i);
		}

		UpdateRareConstantsBuffer();
		UpdateImmutableConstantsBuffer();

		auto permTexture = mResourceProvider->GetTexture(ResourceTypes::Textures::Permutation_256_Extended, 0);
		if (permTexture) {
			SetSystemTexture(SystemTextures::Permutation, permTexture);
		}
		else {
			Logger::Log(FB_ERROR_LOG_ARG, "Cannot find permutation texture!");
		}
		auto permTextureForCS = mResourceProvider->GetTexture(ResourceTypes::Textures::Permutation_256_Extended, 1);
		if (permTextureForCS) {
			SetSystemTexture(SystemTextures::Permutation, permTextureForCS, SHADER_TYPE_CS);
		}

		auto gradTexture = mResourceProvider->GetTexture(ResourceTypes::Textures::Gradiants_256_Extended, 0);
		if (gradTexture) {
			SetSystemTexture(SystemTextures::Gradiants, gradTexture);
		}
		else {
			Logger::Log(FB_ERROR_LOG_ARG, "Cannot find gradiants texture!");
		}
		auto gradTextureforCS = mResourceProvider->GetTexture(ResourceTypes::Textures::Gradiants_256_Extended, 1);
		if (gradTextureforCS) {
			SetSystemTexture(SystemTextures::Gradiants, gradTextureforCS, SHADER_TYPE_CS);
		}

		auto valueNoiseTexture = mResourceProvider->GetTexture(ResourceTypes::Textures::ValueNoise);
		if (valueNoiseTexture) {
			SetSystemTexture(SystemTextures::ValueNoise, valueNoiseTexture);
		}
		else {
			Logger::Log(FB_ERROR_LOG_ARG, "Cannot create value noise texture!");
		}

		

		const auto& rtSize = GetMainRenderTargetSize();
		for (auto it : mFonts){
			it.second->SetRenderTargetSize(rtSize);
		}
		if (mDebugHud){
			mDebugHud->SetRenderTargetSize(rtSize);
		}

		mShadowManager = CascadedShadowsManager::Create();
		mShadowManager->CreateShadowMap();
		mShadowManager->CreateViewports();

		mScreenToNDCMatrix = MakeOrthogonalMatrix(0, 0,
			(Real)mRenderTargetConstants.gScreenSize.x,
			(Real)mRenderTargetConstants.gScreenSize.y,
			0.f, 1.0f);

		if (mRendererOptions->r_fullscreen != 0) {
			ChangeFullscreenMode(mRendererOptions->r_fullscreen);
		}
	}

	void DeinitCanvas(HWindowId id){
		auto it = mWindowHandles.find(id);
		if (it == mWindowHandles.end()){
			Logger::Log(FB_ERROR_LOG_ARG, "Cannot find the window.");
			return;
		}	
		HWindow hwnd = it->second;
		GetPlatformRenderer().DeinitCanvas(id, it->second);
		mWindowHandles.erase(it);
		auto itId = mWindowIds.find(hwnd);
		assert(itId != mWindowIds.end());
		mWindowIds.erase(itId);
		auto itRt = mWindowRenderTargets.find(id);
		assert(itRt != mWindowRenderTargets.end());
		mWindowRenderTargets.erase(itRt);
	}

	void LoadFont(const char* path){
		FontPtr font = Font::Create();
		auto err = font->Init(path);
		if (!err){
			// delete the font that has the same name;
			for (auto it = mFonts.begin(); it != mFonts.end(); /**/){
				if (strcmp(it->second->GetFilePath(), path) == 0){
					it = mFonts.erase(it);
				}
				else{
					++it;
				}
			}

			font->SetTextEncoding(Font::UTF16);
			int height = font->GetFontSize();
			if (!height){
				Logger::Log(FB_ERROR_LOG_ARG, FormatString("Loaded font(%s) has 0 size.", path).c_str());
			}
			mFonts[height] = font;
			font->SetTextureAtlas(mFontTextureAtlas);
		}
	}

	void Render3DUIsToTexture()
	{
		/*if (!m3DUIEnabled)
			return;

		RenderEventMarker mark("Render3DUIsToTexture");
		for (auto scIt : mWindowRenderTargets) {
			for (auto rtIt : mUI3DObjectsRTs) {
				if (!rtIt.second->GetEnable())
					continue;
				auto& uiObjectsIt = mUI3DObjects.Find(std::make_pair(scIt.first, rtIt.first));
				if (uiObjectsIt != mUI3DObjects.end()){
					auto& uiObjects = uiObjectsIt->second;
					auto& rt = rtIt.second;
					rt->Bind();

					for (auto& uiobj : uiObjects)
					{
						uiobj->PreRender();
						uiobj->Render();
						uiobj->PostRender();
					}

					rt->Unbind();
					rt->GetRenderTargetTexture()->GenerateMips();
				}
			}
		}*/
	}

	void RenderUI(HWindowId hwndId)
	{
		//D3DEventMarker mark("RenderUI");
		//auto& uiobjects = mUIObjectsToRender[hwndId];
		//auto it = uiobjects.begin(), itEnd = uiobjects.end();
		//for (; it != itEnd; it++)
		//{
		//	(*it)->PreRender(); // temporary :)
		//	(*it)->Render();
		//	(*it)->PostRender();
		//}
	}

	void RenderDebugRenderTargets()
	{
		auto rt = GetMainRenderTarget();
		assert(rt);
		const auto& size = rt->GetSize();
		for (int i = 0; i < MaxDebugRenderTargets; i++)
		{
			if (mDebugRenderTargets[i].mTexture)
			{
				Vec2 pixelPos = mDebugRenderTargets[i].mPos * Vec2((Real)size.x, (Real)size.y);
				Vec2 pixelSize = mDebugRenderTargets[i].mSize * Vec2((Real)size.x, (Real)size.y);
				DrawQuadWithTexture(Round(pixelPos), Round(pixelSize), Color(1, 1, 1, 1),
					mDebugRenderTargets[i].mTexture);
			}
		}
	}

	void RenderFade()
	{
		if (mFadeAlpha <= 0)
			return;
		auto mainRT = GetMainRenderTarget();
		assert(mainRT);
		DrawQuad(Vec2I(0, 0), mainRT->GetSize(), Color(0, 0, 0, mFadeAlpha));
	}

	void Render(){
		if (mGenerateRadianceCoef && mEnvironmentTexture && mEnvironmentTexture->IsReady()){
			GenerateRadianceCoef(mEnvironmentTexture);			
		}
		auto mainRT = GetMainRenderTarget();
		if (!mainRT)
			return;

		mFrameProfiler.Clear();
		auto startTime = gpTimer->GetTickCount();		
		UpdateFrameConstantsBuffer();

		for (auto pRT : mRenderTargetsEveryFrame)
		{
			auto rt = pRT.lock();
			if (rt)
				rt->Render();
		}

		if (mRendererOptions->r_debugCam) {
			auto mainCam = GetMainCamera();
			auto& t = mainCam->GetTransformation();
			auto& camPos = t.GetTranslation();
			QueueDrawText(Vec2I(500, 20), FormatString("CamPos: %f, %f, %f", camPos.x, camPos.y, camPos.z).c_str(),
				Color::White, 18.f);
		}
		Render3DUIsToTexture();
		for (auto it : mWindowRenderTargets)
		{
			RenderEventMarker mark(FormatString("Processing render target for %u", it.first).c_str());
			auto hwndId = it.first;
			auto rt = (RenderTarget*)it.second.get();
			assert(rt);
			bool rendered = rt->Render();
			if (rendered) {
				auto& observers = mSelf->mObservers_[IRendererObserver::DefaultRenderEvent];
				for (auto it = observers.begin(); it != observers.end(); /**/){
					IteratingWeakContainer(observers, it, observer);
					observer->BeforeUIRendering(hwndId, GetWindowHandle(hwndId));
				}

				for (auto it = observers.begin(); it != observers.end(); /**/){
					IteratingWeakContainer(observers, it, observer);
					observer->RenderUI(hwndId, GetWindowHandle(hwndId));
				}
				
				for (auto it = observers.begin(); it != observers.end(); /**/){
					IteratingWeakContainer(observers, it, observer);
					observer->AfterUIRendered(hwndId, GetWindowHandle(hwndId));
				}
			}
		}
		mainRT->BindTargetOnly(false);

		auto& observers = mSelf->mObservers_[IRendererObserver::DefaultRenderEvent];
		for (auto it = observers.begin(); it != observers.end(); /**/){
			IteratingWeakContainer(observers, it, observer);			
			observer->BeforeDebugHudRendering();
		}
		
		RenderDebugHud();

		for (auto it = observers.begin(); it != observers.end(); /**/)
		{
			IteratingWeakContainer(observers, it, observer);
			observer->AfterDebugHudRendered();
		}

		RenderDebugRenderTargets();

		RenderFade();

		mConsoleRenderer->Render();
		GetPlatformRenderer().Present();
		if (GetPlatformRenderer().IsDeviceRemoved()) {

		}

		auto endTime = gpTimer->GetTickCount();
		auto gap = (endTime - startTime) / (Real)gpTimer->GetFrequency();
		mFrameProfiler.UpdateFrameRate(gap, gpTimer->GetDeltaTime());
	}

	//-------------------------------------------------------------------
	// Resource Creation
	//-------------------------------------------------------------------
	RenderTargetPtr CreateRenderTarget(const RenderTargetParam& param){
		if (param.mUsePool){
			for (auto it = mRenderTargetPool.begin(); it != mRenderTargetPool.end(); it++)
			{
				if ((*it)->CheckOptions(param))
				{
					auto rt = *it;
					if (param.mEveryFrame) {
						mRenderTargetsEveryFrame.push_back(rt);
					}
					else {
						mRenderTargets.push_back(rt);
					}					
					mRenderTargetPool.erase(it);
					return rt;
				}
			}
		}

		auto rt = RenderTarget::Create();
		mRenderTargets.push_back(rt);		
		rt->SetColorTextureDesc(param.mSize.x, param.mSize.y, param.mPixelFormat, param.mShaderResourceView,
				param.mMipmap, param.mCubemap);		
		rt->SetUsePool(param.mUsePool);
		if (param.mEveryFrame)
		{
			mRenderTargetsEveryFrame.push_back(rt);
			return rt;
		}
		else
		{
			mRenderTargets.push_back(rt);
			return rt;
		}		
	}

	void KeepRenderTargetInPool(RenderTargetPtr rt){
		if (!rt)
			return;
		DeleteValuesInVector(mRenderTargetsEveryFrame, rt);
		if (!ValueExistsInVector(mRenderTargetPool, rt)){
			mRenderTargetPool.push_back(rt);
		}
	}

	SpinLockWaitSleep sPlatformTexturesLock;
	using TextureCache = std::unordered_map<std::string, IPlatformTextureWeakPtr>;
	TextureCache sPlatformTextures;

	TexturePtr CreateTexture(const char* file, const TextureCreationOption& options){
		if (!ValidCString(file)){
			Logger::Log(FB_ERROR_LOG_ARG, "Invalid arg.");
			return 0;
		}
		std::string loweredFilepath(file);
		ToLowerCase(loweredFilepath);
		TextureCache::iterator it;
		IPlatformTexturePtr cachedPlatformTexture;
		{
			EnterSpinLock<SpinLockWaitSleep> lock(sPlatformTexturesLock);
			it = sPlatformTextures.find(loweredFilepath);
			if (it != sPlatformTextures.end()) {
				cachedPlatformTexture = it->second.lock();
			}
		}
		if (cachedPlatformTexture) {
			auto texture = GetTextureFromExistings(cachedPlatformTexture);
			if (texture) {
				return texture;
			}
		}		

		IPlatformTexturePtr platformTexture = GetPlatformRenderer().CreateTexture(file, options);
		if (!platformTexture){
			Logger::Log(FB_ERROR_LOG_ARG, FormatString("Platform renderer failed to load a texture(%s)", file).c_str());
			return 0;
		}
		{
			EnterSpinLock<SpinLockWaitSleep> lock(sPlatformTexturesLock);
			sPlatformTextures[loweredFilepath] = platformTexture;
		}
		auto texture = CreateTexture(platformTexture);
		texture->SetFilePath(file);
		texture->SetType(options.textureType);
		return texture;
	}

	TexturePtr CreateTexture(void* data, int width, int height, PIXEL_FORMAT format,
		int mipLevels, BUFFER_USAGE usage, int  buffer_cpu_access, int texture_type){
		auto platformTexture = GetPlatformRenderer().CreateTexture(data, width, height, format, mipLevels, usage, buffer_cpu_access, texture_type);
		if (!platformTexture){
			Logger::Log(FB_ERROR_LOG_ARG, "Failed to create a platform texture with data.");
			return 0;
		}
		auto texture = Texture::Create();
		texture->SetPlatformTexture(platformTexture);
		texture->SetType(texture_type);
		return texture;
	}

	TexturePtr CreateTexture(IPlatformTexturePtr platformTexture){
		auto texture = Texture::Create();
		texture->SetPlatformTexture(platformTexture);
		return texture;
	}

	void ReloadTexture(TexturePtr texture, const char* filepath){
		TextureCreationOption option;
		option.async = true;
		option.generateMip = texture->GetMipGenerated();		
		IPlatformTexturePtr platformTexture = GetPlatformRenderer().CreateTexture(filepath, option);
		if (!platformTexture){
			Logger::Log(FB_ERROR_LOG_ARG, FormatString("Platform renderer failed to load a texture(%s)", filepath).c_str());
			return;
		}
		std::string loweredFilepath(filepath);
		ToLowerCase(loweredFilepath);
		{
			EnterSpinLock<SpinLockWaitSleep> lock(sPlatformTexturesLock);
			sPlatformTextures[loweredFilepath] = platformTexture;
		}
		texture->SetPlatformTexture(platformTexture);
	}

	VertexBufferPtr CreateVertexBuffer(void* data, unsigned stride,
		unsigned numVertices, BUFFER_USAGE usage, BUFFER_CPU_ACCESS_FLAG accessFlag) 
	{
		for (auto it = mVertexBufferCache.begin(); it != mVertexBufferCache.end(); ++it) {
			auto buffer = *it;
			if (buffer->IsSame(stride, numVertices, usage, accessFlag)) {
				mVertexBufferCache.erase(it);
				buffer->UpdateData(data);
				return buffer;
			}
		}

		auto platformBuffer = GetPlatformRenderer().CreateVertexBuffer(data, stride, numVertices, usage, accessFlag);
		if (!platformBuffer){
			Logger::Log(FB_ERROR_LOG_ARG, "Platform renderer failed to create a vertex buffer");
			return 0;
		}
		auto vertexBuffer = VertexBuffer::Create(stride, numVertices, usage, accessFlag);
		vertexBuffer->SetPlatformBuffer(platformBuffer);
		return vertexBuffer;
	}

	IndexBufferPtr CreateIndexBuffer(void* data, unsigned int numIndices,
		INDEXBUFFER_FORMAT format) {
		auto platformBuffer = GetPlatformRenderer().CreateIndexBuffer(data, numIndices, format);
		if (!platformBuffer){
			Logger::Log(FB_ERROR_LOG_ARG, "Platform renderer failed to create a index buffer");
			return 0;
		}
		auto indexBuffer = IndexBuffer::Create(numIndices, format);
		indexBuffer->SetPlatformBuffer(platformBuffer);
		return indexBuffer;
	}
	
	

	std::unordered_map<PlatformShaderKey, IPlatformShaderWeakPtr> sPlatformShaders;
	IPlatformShaderPtr FindPlatformShader(const PlatformShaderKey& key) {
		auto it = sPlatformShaders.find(key);
		if (it != sPlatformShaders.end()) {
			auto platformShader = it->second.lock();
			if (platformShader) {
				return platformShader;
			}
		}
		return 0;
	}

	std::unordered_map<ShaderKey, ShaderWeakPtr> sAllShaders;
	ShaderPtr FindShader(const ShaderKey& key) {
		auto it = sAllShaders.find(key);
		if (it != sAllShaders.end()) {
			auto shader = it->second.lock();
			if (shader) {
				return shader;
			}
		}
		return 0;
	}

	IPlatformShaderPtr CreatePlatformShader(const char* filepath, SHADER_TYPE shaderType,
		const SHADER_DEFINES& defines, bool ignoreCache)
	{
		IPlatformShaderPtr p;
		switch (shaderType) {
		case SHADER_TYPE_VS:
			p = GetPlatformRenderer().CreateVertexShader(filepath, defines, ignoreCache);
			break;
		case SHADER_TYPE_GS:
			p = GetPlatformRenderer().CreateGeometryShader(filepath, defines, ignoreCache);
			break;
		case SHADER_TYPE_PS:
			p = GetPlatformRenderer().CreatePixelShader(filepath, defines, ignoreCache);
			break;
		case SHADER_TYPE_CS:
			p = GetPlatformRenderer().CreateComputeShader(filepath, defines, ignoreCache);
			break;
		default:
			Logger::Log(FB_ERROR_LOG_ARG, "Unsupported.");
		}
		PlatformShaderKey key(filepath, shaderType, defines);
		sPlatformShaders[key] = p;
		return p;
	}

	ShaderPtr CreateShader(const char* filepath, int shaders) {
		CreateShader(filepath, shaders, SHADER_DEFINES());
	}
	ShaderPtr CreateShader(const char* filepath, int shaders,
		const SHADER_DEFINES& defines) 
	{
		if (!ValidCString(filepath)){
			Logger::Log(FB_ERROR_LOG_ARG, "Invalid arg.");
			return 0;
		}		

		SHADER_DEFINES sortedDefines(defines);		
		std::sort(sortedDefines.begin(), sortedDefines.end());
		
		auto key = ShaderKey(filepath, shaders, sortedDefines);
		auto shader = FindShader(key);
		if (shader){						
			return shader;
		}

		IPlatformShaderPtr pvs;
		IPlatformShaderPtr pgs;
		IPlatformShaderPtr pps;
		IPlatformShaderPtr pcs;
		if (shaders & SHADER_TYPE_VS) {
			pvs = CreatePlatformShader(filepath, SHADER_TYPE_VS, sortedDefines, false);
		}
		if (shaders & SHADER_TYPE_GS) {
			pgs = CreatePlatformShader(filepath, SHADER_TYPE_GS, sortedDefines, false);
		}
		if (shaders & SHADER_TYPE_PS) {
			pps = CreatePlatformShader(filepath, SHADER_TYPE_PS, sortedDefines, false);
		}
		if (shaders & SHADER_TYPE_CS) {
			pcs = CreatePlatformShader(filepath, SHADER_TYPE_CS, sortedDefines, false);
		}
		if (pvs || pgs || pps || pcs) {
			auto shader = Shader::Create();
			shader->SetPlatformShader(pvs, SHADER_TYPE_VS);
			shader->SetPlatformShader(pgs, SHADER_TYPE_GS);
			shader->SetPlatformShader(pps, SHADER_TYPE_PS);
			shader->SetPlatformShader(pcs, SHADER_TYPE_CS);			
			ShaderKey shaderKey(filepath, shaders, sortedDefines);
			sAllShaders[shaderKey] = shader;			
			return shader;
		}
		else {
			Logger::Log(FB_ERROR_LOG_ARG, FormatString("Failed to create a shader(%s)", filepath).c_str());
			return 0;
		}
	}

	ShaderPtr CreateShader(const StringVector& filepaths, const SHADER_DEFINES& defines) {
		if (filepaths.size() != SHADER_TYPE_COUNT) {
			Logger::Log(FB_ERROR_LOG_ARG, "Invalid arg.");
		}

		SHADER_DEFINES sortedDefines(defines);
		std::sort(sortedDefines.begin(), sortedDefines.end());

		auto shader = Shader::Create();
		for (int i = 0; i < SHADER_TYPE_COUNT; ++i) {
			auto filepath = filepaths[i];
			if (filepath.empty())
				continue;

			PlatformShaderKey key(filepath.c_str(), ShaderType(i), sortedDefines);
			auto pshader = FindPlatformShader(key);
			if (pshader) {
				shader->SetPlatformShader(pshader, ShaderType(i));
			}
			else {
				IPlatformShaderPtr pshader = CreatePlatformShader(filepath.c_str(), ShaderType(i), sortedDefines, false);
				shader->SetPlatformShader(pshader, ShaderType(i));
			}			
		}
		return shader;
	}

	bool CreateShader(const ShaderPtr& integratedShader, const char* filepath, SHADER_TYPE shader, const SHADER_DEFINES& defines) {
		if (!ValidCString(filepath)) {
			Logger::Log(FB_ERROR_LOG_ARG, "Invalid arg.");
			return 0;
		}
		SHADER_DEFINES sortedDefines(defines);
		std::sort(sortedDefines.begin(), sortedDefines.end());
		PlatformShaderKey key(filepath, shader, sortedDefines);
		auto p = FindPlatformShader(key);
		if (p) {
			integratedShader->SetPlatformShader(p, shader);
			return true;
		}

		p = CreatePlatformShader(filepath, shader, sortedDefines, false);
		integratedShader->SetPlatformShader(p, shader);
		return p!=nullptr;
	}

	ShaderPtr CompileComputeShader(const char* code, const char* entry, const SHADER_DEFINES& defines) {
		auto platformShader = GetPlatformRenderer().CompileComputeShader(code, entry, defines);
		if (platformShader) {
			auto shader = Shader::Create();
			shader->SetPlatformShader(platformShader, platformShader->GetShaderType());
			return shader;
		}
		else {
			return nullptr;
		}
	}

	/*ShaderPtr CreateShader(const StringVector& filepaths, const SHADER_DEFINES& defines) {
		if (filepaths.size() != SHADER_TYPE_COUNT) {
			Logger::Log(FB_ERROR_LOG_ARG, "Invalid arg.");
			return nullptr;
		}
		int shaders = 0;
		bool valid = false;
		int index = 0;
		for (auto& path : filepaths) {
			if (!path.empty()) {
				valid = true;
				shaders |= 1 << index;
			}
			++index;
		}
		if (!valid) {
			Logger::Log(FB_ERROR_LOG_ARG, "Invalid filepaths.");
			return nullptr;
		}
		SHADER_DEFINES sortedDefines(defines);
		std::string combinedPath(FormatString("%s,%s,%s,%s,%s,%s",
			filepaths[0].c_str(), filepaths[1].c_str(), filepaths[2].c_str(), 
			filepaths[3].c_str(), filepaths[4].c_str(), filepaths[5].c_str()));
		std::sort(sortedDefines.begin(), sortedDefines.end());
		auto loweredPath = ToLowerCase(combinedPath.c_str());
		auto key = ShaderCreationInfo(loweredPath.c_str(), shaders, sortedDefines);
		auto platformShader = FindPlatformShader(key);
		if (platformShader) {
			auto shader = GetShaderFromExistings(platformShader);
			if (shader) {
				assert(shader->GetShaderDefines() == sortedDefines);
				return shader;
			}
		}
		if (!platformShader)
			platformShader = GetPlatformRenderer().CreateShader(filepaths, shaders, sortedDefines, false);
		if (platformShader) {
			auto shader = Shader::Create();
			shader->SetPlatformShader(platformShader);
			shader->SetShaderDefines(sortedDefines);
			shader->SetPath(combinedPath.c_str());
			shader->SetBindingShaders(shaders);
			sPlatformShaders[key] = platformShader;
			return shader;
		}
		Logger::Log(FB_ERROR_LOG_ARG, FormatString("Failed to create a shader(%s)", loweredPath.c_str()).c_str());
		return 0;


	}*/

	void ReloadShader(const char* filepath){
		for (auto it : sPlatformShaders) {
			auto platformShader = it.second.lock();
			if (platformShader && platformShader->IsRelatedFile(filepath)) {
				auto shaderType = it.first.GetShaderType();
				auto hlslPath = it.first.GetFilePath();
				auto& defines = it.first.GetShaderDefines();
				platformShader->Reload(defines);		
			}
		}		
	}

	std::unordered_map<std::string, MaterialPtr> sLoadedMaterials;
	std::unordered_map<std::string, MaterialWeakPtr> sLoadedMaterialsWeak;
	void ClearLoadedMaterials() {
		sLoadedMaterials.clear();
		sLoadedMaterialsWeak.clear();
	}

	void CleanUnusingMaterials() {
		sLoadedMaterials.clear();
		for (auto it = sLoadedMaterialsWeak.begin(); it != sLoadedMaterialsWeak.end(); /**/) {
			auto mat = it->second.lock();
			if (mat) {
				sLoadedMaterials[it->first] = mat;
				++it;
			}
			else {
				auto curIt = it++;				
				sLoadedMaterialsWeak.erase(curIt);
			}
		}
	}
	MaterialPtr CreateMaterial(const char* file){
		if (!ValidCString(file)){
			Logger::Log(FB_ERROR_LOG_ARG, "Invalid arg.");
			return 0;
		}
		std::string loweredPath(file);
		ToLowerCase(loweredPath);		
		auto it = sLoadedMaterials.find(loweredPath);
		if (it != sLoadedMaterials.end()){
			auto material = it->second;
			if (material){
				return material->Clone();
			}
		}
		auto material = Material::Create();
		if (!material->LoadFromFile(file))
			return 0;

		sLoadedMaterials[loweredPath] = material;
		sLoadedMaterialsWeak[loweredPath] = material;
		return material->Clone();
	}

	bool ReloadMaterial(const char* file) {
		std::string loweredPath(file);
		ToLowerCase(loweredPath);
		auto it = sLoadedMaterials.find(loweredPath);
		if (it != sLoadedMaterials.end()) {
			auto material = it->second;
			if (material) {
				material->Reload();
				return true;
			}
		}		
		return false;
	}
	
	std::unordered_map<unsigned, InputLayoutWeakPtr> sInputLayouts;
	// use this if you are sure there is instance of the descs.
	InputLayoutPtr CreateInputLayout(const INPUT_ELEMENT_DESCS& descs, ShaderPtr shader){
		if (!shader || descs.empty()){
			Logger::Log(FB_ERROR_LOG_ARG, "Invalid param.");
			return 0;
		}
		unsigned size;
		void* data = shader->GetVSByteCode(size);
		if (!data){
			Logger::Log(FB_ERROR_LOG_ARG, "Invalid shader");
			return nullptr;
		}
		auto descsSize = sizeof(INPUT_ELEMENT_DESCS) * descs.size();
		auto totalSize = descsSize + size;
		ByteArray temp(totalSize);
		memcpy(&temp[0], &descs[0], sizeof(descs));
		memcpy(&temp[0] + descsSize, data, size);
		unsigned key = murmur3_32((const char*)&temp[0], totalSize, murmurSeed);
		auto it = sInputLayouts.find(key);
		if (it != sInputLayouts.end()){
			auto inputLayout = it->second.lock();
			if (inputLayout)
				return inputLayout;
		}
		auto platformInputLayout = GetPlatformRenderer().CreateInputLayout(descs, data, size);
		if (!platformInputLayout)
			return nullptr;
		auto inputLayout = InputLayout::Create();
		inputLayout->SetPlatformInputLayout(platformInputLayout);
		sInputLayouts[key] = inputLayout;
		return inputLayout;
	}

	InputLayoutPtr GetInputLayout(DEFAULT_INPUTS::Enum e, ShaderPtr shader){
		const auto& desc = GetInputElementDesc(e);
		return CreateInputLayout(desc, shader);
	}

	std::unordered_map<RASTERIZER_DESC, RasterizerStateWeakPtr> sRasterizerStates;
	RasterizerStatePtr CreateRasterizerState(const RASTERIZER_DESC& desc){
		auto it = sRasterizerStates.find(desc);
		if (it != sRasterizerStates.end()){
			auto state = it->second.lock();
			if (state){
				return state;
			}
		}
		auto platformRaster = GetPlatformRenderer().CreateRasterizerState(desc);
		auto raster = RasterizerState::Create();
		raster->SetPlatformState(platformRaster);
		sRasterizerStates[desc] = raster;
		return raster;
	}

	VectorMap<BLEND_DESC, BlendStateWeakPtr> sBlendStates;
	BlendStatePtr CreateBlendState(const BLEND_DESC& desc){		
		auto it = sBlendStates.find(desc);
		if (it != sBlendStates.end()){
			auto state = it->second.lock();
			if (state){
				return state;
			}
		}
		auto platformState = GetPlatformRenderer().CreateBlendState(desc);
		auto state = BlendState::Create();
		state->SetPlatformState(platformState);
		sBlendStates[desc] = state;
		return state;
		
	}

	std::unordered_map<DEPTH_STENCIL_DESC, DepthStencilStateWeakPtr> sDepthStates;
	DepthStencilStatePtr CreateDepthStencilState(const DEPTH_STENCIL_DESC& desc){		
		auto it = sDepthStates.find(desc);
		if (it != sDepthStates.end()){
			auto state = it->second.lock();
			if (state){
				return state;
			}
		}
		auto platformState = GetPlatformRenderer().CreateDepthStencilState(desc);
		auto state = DepthStencilState::Create();
		state->SetPlatformState(platformState);
		sDepthStates[desc] = state;
		return state;
	}

	std::unordered_map<SAMPLER_DESC, SamplerStateWeakPtr> sSamplerStates;
	SamplerStatePtr CreateSamplerState(const SAMPLER_DESC& desc){
		auto it = sSamplerStates.find(desc);
		if (it != sSamplerStates.end()){
			auto state = it->second.lock();
			if (state){
				return state;
			}
		}
		auto platformState = GetPlatformRenderer().CreateSamplerState(desc);
		auto state = SamplerState::Create();
		state->SetPlatformState(platformState);
		sSamplerStates[desc] = state;
		return state;

	}

	// holding strong pointer
	std::unordered_map<std::string, TextureAtlasPtr> sTextureAtlas;
	TextureAtlasPtr GetTextureAtlas(const char* path){		
		auto it = sTextureAtlas.find(path);
		if (it != sTextureAtlas.end()){
			return it->second;
		}

		auto pdoc = FileSystem::LoadXml(path);		
		if (pdoc->Error())
		{
			const char* errMsg = pdoc->GetErrorStr1();
			if (ValidCString(errMsg)){
				Logger::Log(FB_ERROR_LOG_ARG, FormatString("%s(%s)", errMsg, path));
			}
			else{
				Logger::Log(FB_ERROR_LOG_ARG, FormatString("Cannot load texture atlas(%s)", path));
			}
			return 0;
		}

		tinyxml2::XMLElement* pRoot = pdoc->FirstChildElement("TextureAtlas");
		if (!pRoot)
		{
			return 0;
		}

		const char* szBuffer = pRoot->Attribute("file");
		TextureAtlasPtr pTextureAtlas;
		if (szBuffer)
		{			
			pTextureAtlas = TextureAtlas::Create();
			pTextureAtlas->SetPath(path);
			TextureCreationOption option;
			option.async = false;
			option.generateMip = false;
			pTextureAtlas->SetTexture(CreateTexture(szBuffer, option));
			sTextureAtlas[path] = pTextureAtlas;
			if (!pTextureAtlas->GetTexture())
			{
				Logger::Log(FB_ERROR_LOG_ARG, FormatString("Texture for atlas(%s) is not found", szBuffer).c_str());
				return pTextureAtlas;
			}
		}
		else
		{
			Logger::Log(FB_ERROR_LOG_ARG, "Invalid TextureAtlas format! No Texture Defined.");
			return 0;
		}

		auto texture = pTextureAtlas->GetTexture();		
		if (!texture) {
			Logger::Log(FB_ERROR_LOG_ARG, "Texture atlas doesn't have the platform texture.");
			return nullptr;
		}

		Vec2I textureSize = texture->GetSize();
		bool divideUVLater = false;
		if (textureSize.x == 0 || textureSize.y == 0) {
			textureSize.x = 1;
			textureSize.y = 1;
			divideUVLater = true;
		}
		tinyxml2::XMLElement* pRegionElem = pRoot->FirstChildElement("region");
		while (pRegionElem)
		{
			szBuffer = pRegionElem->Attribute("name");
			if (!szBuffer)
			{
				Logger::Log(FB_DEFAULT_LOG_ARG, "No name for texture atlas region");
				continue;
			}

			auto pRegion = pTextureAtlas->AddRegion(szBuffer);				
			pRegion->mID = pRegionElem->UnsignedAttribute("id");
			pRegion->mStart.x = pRegionElem->IntAttribute("x");
			pRegion->mStart.y = pRegionElem->IntAttribute("y");
			pRegion->mSize.x = pRegionElem->IntAttribute("width");
			pRegion->mSize.y = pRegionElem->IntAttribute("height");
			Vec2 start((Real)pRegion->mStart.x, (Real)pRegion->mStart.y);
			Vec2 end(start.x + pRegion->mSize.x, start.y + pRegion->mSize.y);
			pRegion->mUVStart = start / textureSize;
			pRegion->mUVEnd = end / textureSize;
			pRegionElem = pRegionElem->NextSiblingElement();			
		}

		struct Divider {
			TextureAtlasPtr mTextureAtlas;
			Divider(TextureAtlasPtr& textureAtlas)
				: mTextureAtlas(textureAtlas) 
			{
			}
			
			void operator()() {
				auto t = mTextureAtlas->GetTexture();
				if (!t->IsReady()) {
					Invoker::GetInstance().InvokeAtEnd(Divider(mTextureAtlas));
					return;
				}
				Vec2 textureSize((Real)t->GetWidth(), (Real)t->GetHeight());
				if (textureSize.x == 0.f || textureSize.y == 0.f) {
					Logger::Log(FB_ERROR_LOG_ARG, FormatString(
						"TextureAtlas(%s) has size 0,0.", mTextureAtlas->GetPath()).c_str());
				}
				auto it = mTextureAtlas->GetIterator();
				while (it.HasMoreElement()) {
					auto region = it.GetNext().second;
					assert(region);
					region->mUVStart = region->mUVStart / textureSize;
					region->mUVEnd = region->mUVEnd / textureSize;
				}				
			}
		};
		if (divideUVLater) {
			Invoker::GetInstance().InvokeAtEnd(Divider(pTextureAtlas));
		}

		return pTextureAtlas;
	}

	TextureAtlasRegionPtr GetTextureAtlasRegion(const char* path, const char* region){
		auto pTextureAtlas = GetTextureAtlas(path);
		if (pTextureAtlas)
		{
			return pTextureAtlas->GetRegion(region);
		}

		return 0;
	}

	TexturePtr GetTemporalDepthBuffer(const Vec2I& size, const char* key){
		TemporalDepthKey depthkey(size, key);
		auto it = mTempDepthBuffers.find(depthkey);
		if (it == mTempDepthBuffers.end())
		{
			auto depthBuffer = CreateTexture(0, size.x, size.y, PIXEL_FORMAT_D32_FLOAT, 1, BUFFER_USAGE_DEFAULT,
				BUFFER_CPU_ACCESS_NONE, TEXTURE_TYPE_DEPTH_STENCIL);
			mTempDepthBuffers.insert(std::make_pair(depthkey, depthBuffer));
			return depthBuffer;
		}
		return it->second;
	}

	std::vector<VertexBufferPtr> mVertexBufferCache;
	void Cache(VertexBufferPtr buffer) {
		if (!buffer)
			return;

		auto usage = buffer->GetBufferUsage();
		if (usage == BUFFER_USAGE_IMMUTABLE) {
			Logger::Log(FB_DEFAULT_LOG_ARG, "Cannot cache the immutable buffer");
			return;
		}
		mVertexBufferCache.push_back(buffer);
	}
	

	//-------------------------------------------------------------------
	// Resource Bindings
	//-------------------------------------------------------------------	
	void SetRenderTarget(TexturePtr pRenderTargets[], size_t rtViewIndex[], int num,
		TexturePtr pDepthStencil, size_t dsViewIndex){
		assert(num <= 4);
		if (mCurrentDSTexture == pDepthStencil && mCurrentDSViewIndex == dsViewIndex){
			if (mCurrentRTTextures.size() == num && mCurrentViewIndices.size() == num){
				bool same = true;
				for (int i = 0; i < num && same; ++i){
					if (mCurrentRTTextures[i] != pRenderTargets[i] || 
						mCurrentViewIndices[i] != rtViewIndex[i]){
						same = false;
					}
				}
				if (same){
					return;
				}
			}
		}
		static TIME_PRECISION time = 0;
		static std::set<TextureWeakPtr, std::owner_less<TextureWeakPtr>> usedRenderTargets;
		if (GetRendererOptions()->r_numRenderTargets && gpTimer)
		{
			for (int i = 0; i<num; i++)
			{
				usedRenderTargets.insert(pRenderTargets[i]);
			}
			if (gpTimer->GetTime() - time > 5)
			{
				time = gpTimer->GetTime();
				Logger::Log(FB_DEFAULT_LOG_ARG, FormatString("used RenderTargets", usedRenderTargets.size()).c_str());				
			}
		}
		mCurrentRTTextures.clear();
		mCurrentViewIndices.clear();		
		std::copy(pRenderTargets, pRenderTargets + num, std::back_inserter(mCurrentRTTextures));
		std::copy(rtViewIndex, rtViewIndex + num, std::back_inserter(mCurrentViewIndices));
		mCurrentDSTexture = pDepthStencil;
		mCurrentDSViewIndex = dsViewIndex;
		IPlatformTexturePtr platformRTs[4] = { 0 };
		for (int i = 0; i < num; ++i){
			platformRTs[i] = pRenderTargets[i] ? pRenderTargets[i]->GetPlatformTexture() : 0;					
		}

		GetPlatformRenderer().SetRenderTarget(platformRTs, rtViewIndex, num,
			pDepthStencil ? pDepthStencil->GetPlatformTexture() : 0, dsViewIndex);

		if (pRenderTargets && num>0 && pRenderTargets[0])
		{
			mCurrentRTSize = pRenderTargets[0]->GetSize();
		}
		else
		{
			mCurrentRTSize = GetMainRenderTargetSize();
		}

		for (auto it : mFonts){
			it.second->SetRenderTargetSize(mCurrentRTSize);
		}

		UpdateRenderTargetConstantsBuffer();
	}

	void UnbindRenderTarget(TexturePtr renderTargetTexture){
		auto rtTextures = mCurrentRTTextures;
		auto viewIndices = mCurrentViewIndices;
		auto size = rtTextures.size();
		bool removed = false;
		for (unsigned i = 0; i < size; i++){
			if (rtTextures[i] == renderTargetTexture){
				rtTextures[i] = 0;
				viewIndices[i] = 0;
				removed = true;
			}
		}
		auto dsTexture = mCurrentDSTexture;
		auto dsViewIndex = mCurrentDSViewIndex;
		if (dsTexture == renderTargetTexture)
		{
			removed = true;
			dsTexture = 0;
			dsViewIndex = 0;
		}
		if (removed){
			SetRenderTarget(&rtTextures[0], &viewIndices[0], size, dsTexture, dsViewIndex);
		}
	}

	void SetRenderTargetAtSlot(TexturePtr pRenderTarget, size_t viewIndex, size_t slot) {
		auto rtTextures = mCurrentRTTextures;
		auto viewIndices = mCurrentViewIndices;
		assert(rtTextures.size() == viewIndices.size());
		while (rtTextures.size() <= slot) {
			rtTextures.push_back(0);
			viewIndices.push_back(0);
		}
		rtTextures[slot] = pRenderTarget;
		viewIndices[slot] = viewIndex;

		SetRenderTarget(&rtTextures[0], &viewIndices[0], rtTextures.size(), mCurrentDSTexture, mCurrentDSViewIndex);
	}

	void SetDepthTarget(const TexturePtr& pDepthStencil, size_t dsViewIndex) {
		if (mCurrentDSTexture == pDepthStencil && mCurrentDSViewIndex == dsViewIndex)
			return;
		mCurrentDSTexture = pDepthStencil;
		mCurrentDSViewIndex = dsViewIndex;
		GetPlatformRenderer().SetDepthTarget(pDepthStencil ? pDepthStencil->GetPlatformTexture() : 0, dsViewIndex);
	}

	void OverrideDepthTarget(bool enable) {
		if (enable && !mCurrentDSTexture) {
			Logger::Log(FB_ERROR_LOG_ARG, "No ds texture bound.");
			return;
		}
		for (auto it = mDepthStencilTextureOverride.begin(); it != mDepthStencilTextureOverride.end();) {
			auto curIt = it++;
			if (curIt->first.expired()) {
				mDepthStencilTextureOverride.erase(curIt);
			}
		}
		TexturePtr overridingDSTexture;
		if (enable) {
			auto it = mDepthStencilTextureOverride.find(mCurrentDSTexture);
			if (it == mDepthStencilTextureOverride.end()) {
				auto width = mCurrentDSTexture->GetWidth();
				auto height = mCurrentDSTexture->GetHeight();
				auto format = mCurrentDSTexture->GetFormat();
				auto type = mCurrentDSTexture->GetType();
				overridingDSTexture = CreateTexture(0, width, height, format,
					1, BUFFER_USAGE_DEFAULT, BUFFER_CPU_ACCESS_NONE, type);
				mDepthStencilTextureOverride[mCurrentDSTexture] = overridingDSTexture;
			}
			else {
				overridingDSTexture = it->second;
			}
		}

		if (enable) {
			mDepthTextureStack.push(mCurrentDSTexture);
			SetDepthTarget(overridingDSTexture, mCurrentDSViewIndex);
		}
		else {
			if (mDepthTextureStack.empty()) {
				Logger::Log(FB_ERROR_LOG_ARG, "Invalid depth stack!");
				return;
			}
			auto originalDST = mDepthTextureStack.top().lock();
			mDepthTextureStack.pop();
			SetDepthTarget(originalDST, mCurrentDSViewIndex);
		}
	}

	void UnbindColorTargetAndKeep() {
		mKeptCurrentRTTextures = mCurrentRTTextures;
		mKeptCurrentViewIndices = mCurrentViewIndices;
		mCurrentRTTextures.clear();
		mCurrentViewIndices.clear();
		
		GetPlatformRenderer().SetRenderTarget(0, 0, 0,
			mCurrentDSTexture ? mCurrentDSTexture->GetPlatformTexture() : 0, mCurrentDSViewIndex);
	}

	void RebindKeptColorTarget() {
		if (!mKeptCurrentViewIndices.empty()) {
			SetRenderTarget(&mKeptCurrentRTTextures[0], &mKeptCurrentViewIndices[0], mKeptCurrentViewIndices.size(),
				mCurrentDSTexture, mCurrentDSViewIndex);
			mKeptCurrentRTTextures.clear();
			mKeptCurrentViewIndices.clear();
		}
		else {
			SetRenderTarget(0, 0, 0, mCurrentDSTexture, mCurrentDSViewIndex);
			Logger::Log(FB_ERROR_LOG_ARG, "No kept RenderTarget found");
		}
	}

	const std::vector<Viewport>& GetViewports() {
		return mCurrentViewports;
	}
	void SetViewports(const Viewport viewports[], int num){
		mCurrentViewports.resize(num);
		memcpy(&mCurrentViewports[0], viewports, num * sizeof(Viewport));
		GetPlatformRenderer().SetViewports(viewports, num);
	}

	void SetScissorRects(const Rect rects[], int num){
		GetPlatformRenderer().SetScissorRects(rects, num);
	}

	void SetVertexBuffers(unsigned int startSlot, unsigned int numBuffers,
		VertexBufferPtr pVertexBuffers[], unsigned int strides[], unsigned int offsets[]) {
		static const unsigned int numMaxVertexInputSlot = 32; //D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT (32)
		IPlatformVertexBuffer const *  platformBuffers[numMaxVertexInputSlot];
		numBuffers = std::min(numMaxVertexInputSlot, numBuffers);		
		for (unsigned i = 0; i < numBuffers; ++i){
			platformBuffers[i] = pVertexBuffers[i] ? pVertexBuffers[i]->GetPlatformBuffer().get() : 0;
		}
		GetPlatformRenderer().SetVertexBuffers(startSlot, numBuffers, platformBuffers, strides, offsets);
	}

	void SetPrimitiveTopology(PRIMITIVE_TOPOLOGY pt){
		if (mCurrentTopology == pt)
			return;
		GetPlatformRenderer().SetPrimitiveTopology(pt);
		mCurrentTopology = pt;
	}

	void SetTextures(TexturePtr pTextures[], int num, SHADER_TYPE shaderType, int startSlot){
		for (int i = 0; i < num; ++i){
			if (pTextures[i])
				pTextures[i]->Bind(shaderType, startSlot + i);
		}
	}

	std::unordered_map<SystemTextures::Enum, TextureWeakPtr> sSystemTextures;
	void SetSystemTexture(SystemTextures::Enum type, TexturePtr texture, int shader_type_mask = ~SHADER_TYPE_CS){
		auto it = mSystemTextureBindings.find(type);
		if (it == mSystemTextureBindings.end()){
			Logger::Log(FB_ERROR_LOG_ARG, FormatString("Cannot find the binding information for the system texture(%d)", type).c_str());
			return;
		}
		if (texture){
			bool binded = false;
			for (const auto& binding : it->second){
				if (shader_type_mask & binding.mShader) {
					texture->Bind(binding.mShader, binding.mSlot);
					binded = true;
				}
			}
			if (binded && shader_type_mask != SHADER_TYPE_CS)
				sSystemTextures[type] = texture;
		}
		else{
			for (const auto& binding : it->second){
				UnbindTexture(binding.mShader, binding.mSlot);				
			}
		}
	}

	void BindSystemTexture(SystemTextures::Enum systemTexture) {
		auto it = sSystemTextures.find(systemTexture);
		if (it != sSystemTextures.end()) {
			auto t = it->second.lock();
			if (t)
				SetSystemTexture(systemTexture, it->second.lock());
		}
	}
	
	void BindDepthTexture(bool set){
		auto mainRt = GetMainRenderTarget();
		if (mainRt){
			mainRt->BindDepthTexture(set);
		}
	}

	void SetDepthWriteShader(){
		auto depthWriteShader = mResourceProvider->GetShader(ResourceTypes::Shaders::DepthWriteVSPS);
		if (depthWriteShader){
			SetPositionInputLayout();
			depthWriteShader->Bind(true);
		}
	}

	void SetDepthWriteShaderCloud(){
		auto cloudDepthWriteShader = mResourceProvider->GetShader(ResourceTypes::Shaders::CloudDepthWriteVSPS);
		if (cloudDepthWriteShader){
			SetPositionInputLayout();
			cloudDepthWriteShader->Bind(true);
		}
	}

	void SetPositionInputLayout(){
		if (!mPositionInputLayout)
		{
			auto shader = mResourceProvider->GetShader(ResourceTypes::Shaders::ShadowMapShader);
			if (!shader){
				//fall-back
				shader = mResourceProvider->GetShader(ResourceTypes::Shaders::DepthWriteVSPS);
			}
			if (shader)
				mPositionInputLayout = GetInputLayout(DEFAULT_INPUTS::POSITION, shader);					
			if (mPositionInputLayout)
				mPositionInputLayout->Bind();
		}
		else{
			mPositionInputLayout->Bind();
		}
	}	

	void SetSystemTextureBindings(SystemTextures::Enum type, const TextureBindings& bindings){
		mSystemTextureBindings[type] = bindings;
	}

	const TextureBindings& GetSystemTextureBindings(SystemTextures::Enum type){
		auto it = mSystemTextureBindings.find(type);
		if (it != mSystemTextureBindings.end())
			return it->second;
		static TextureBindings noBindingInfo;
		return noBindingInfo;
	}

	const Mat44& GetScreenToNDCMatric() {
		return mScreenToNDCMatrix;
	}

	//-------------------------------------------------------------------
	// Device RenderStates
	//-------------------------------------------------------------------
	void RestoreRenderStates(){
		RestoreRasterizerState();
		RestoreBlendState();
		RestoreDepthStencilState();
	}
	void RestoreRasterizerState(){
		auto state = mResourceProvider->GetRasterizerState(ResourceTypes::RasterizerStates::Default);
		if (state)
			state->Bind();
	}
	void RestoreBlendState(){
		auto state = mResourceProvider->GetBlendState(ResourceTypes::BlendStates::Default);
		if (state)
			state->Bind();
	}
	void RestoreDepthStencilState(){
		auto state = mResourceProvider->GetDepthStencilState(ResourceTypes::DepthStencilStates::Default);
		if (state)
			state->Bind();
	}

	// sampler
	void SetSamplerState(int ResourceTypes_SamplerStates, SHADER_TYPE shader, int slot){
		auto sampler = mResourceProvider->GetSamplerState(ResourceTypes_SamplerStates);
		if (!sampler){
			Logger::Log(FB_ERROR_LOG_ARG, FormatString("Cannot find sampler (%d)", ResourceTypes_SamplerStates).c_str());
			return;
		}
		sampler->Bind(shader, slot);
	}


	//-------------------------------------------------------------------
	// GPU constants
	//-------------------------------------------------------------------
	void UpdateObjectConstantsBuffer(const void* pData, bool record){
		if (mRendererOptions->r_noObjectConstants)
			return;
		if (record)
			mFrameProfiler.NumUpdateObjectConst += 1;
		mObjectConstants = *(OBJECT_CONSTANTS*)pData;
		GetPlatformRenderer().UpdateShaderConstants(ShaderConstants::Object, pData, sizeof(OBJECT_CONSTANTS));
	}

	void UpdatePointLightConstantsBuffer(const void* pData){
		GetPlatformRenderer().UpdateShaderConstants(ShaderConstants::PointLight, pData, sizeof(POINT_LIGHT_CONSTANTS));
	}

	void UpdateFrameConstantsBuffer(){
		mFrameConstants.gMousePos.x = (float)mInputInfo.mCurrentMousePos.x;
		mFrameConstants.gMousePos.y = (float)mInputInfo.mCurrentMousePos.y;
		bool lbuttonDown = mInputInfo.mLButtonDown;
		mFrameConstants.gMousePos.z = lbuttonDown ? (float)mInputInfo.mCurrentMousePos.x : 0;
		mFrameConstants.gMousePos.w = lbuttonDown ? (float)mInputInfo.mCurrentMousePos.y : 0;
		mFrameConstants.gTime = (float)gpTimer->GetTime();
		mFrameConstants.gDeltaTime = (float)gpTimer->GetDeltaTime();
		GetPlatformRenderer().UpdateShaderConstants(ShaderConstants::Frame, &mFrameConstants, sizeof(FRAME_CONSTANTS));
	}

	void UpdateMaterialConstantsBuffer(const void* pData){
		GetPlatformRenderer().UpdateShaderConstants(ShaderConstants::MaterialConstant, pData, sizeof(MATERIAL_CONSTANTS));
	}

	void UpdateCameraConstantsBuffer(){
		if (!mCamera)
			return;

		mCameraConstants.gView = mCamera->GetMatrix(ICamera::View);
		
		mCameraConstants.gInvView = mCamera->GetMatrix(ICamera::InverseView);
		mCameraConstants.gViewProj = mCamera->GetMatrix(ICamera::ViewProj);
		mCameraConstants.gInvViewProj = mCamera->GetMatrix(ICamera::InverseViewProj);
		mCamera->GetTransformation().GetHomogeneous(mCameraConstants.gCamTransform);
		mCameraConstants.gProj = mCamera->GetMatrix(ICamera::Proj);
		mCameraConstants.gInvProj = mCamera->GetMatrix(ICamera::InverseProj);
		Real ne, fa;
		mCamera->GetNearFar(ne, fa);
		mCameraConstants.gNearFar.x = (float)ne;
		mCameraConstants.gNearFar.y = (float)fa;
		mCameraConstants.gTangentTheta = (float)tan(mCamera->GetFOV() / 2.0);		
		GetPlatformRenderer().UpdateShaderConstants(ShaderConstants::Camera, &mCameraConstants, sizeof(CAMERA_CONSTANTS));
	}

	void UpdateCameraConstantsBuffer(const void* manualData) {
		GetPlatformRenderer().UpdateShaderConstants(ShaderConstants::Camera, manualData, sizeof(CAMERA_CONSTANTS));
	}

	void UpdateRenderTargetConstantsBuffer(){
		mRenderTargetConstants.gScreenSize.x = (float)mCurrentRTSize.x;
		mRenderTargetConstants.gScreenSize.y = (float)mCurrentRTSize.y;
		mRenderTargetConstants.gScreenRatio = mCurrentRTSize.x / (float)mCurrentRTSize.y;				

		GetPlatformRenderer().UpdateShaderConstants(ShaderConstants::RenderTarget, &mRenderTargetConstants, sizeof(RENDERTARGET_CONSTANTS));
	}

	void UpdateSceneConstantsBuffer(){
		if (!mCurrentRenderTarget){
			Logger::Log(FB_ERROR_LOG_ARG, "No current render target found.");
			return;
		}
		auto scene = mCurrentScene.lock();
		if (!scene){
			return;
		}
		auto pLightCam = mShadowManager->GetLightCamera();
		if (pLightCam)
			mSceneConstants.gLightView = pLightCam->GetMatrix(ICamera::View);

		for (int i = 0; i < 2; i++)
		{
			mSceneConstants.gDirectionalLightDir_Intensity[i] = float4(mDirectionalLight[i].mDirection_Intensiy);
			mSceneConstants.gDirectionalLightDiffuse[i] = float4(mDirectionalLight[i].mDiffuse);
			mSceneConstants.gDirectionalLightSpecular[i] = float4(mDirectionalLight[i].mSpecular);
		}
		mSceneConstants.gFogColor = scene->GetFogColor().GetVec4();
		GetPlatformRenderer().UpdateShaderConstants(ShaderConstants::Scene, &mSceneConstants, sizeof(SCENE_CONSTANTS));
	}
	void UpdateRareConstantsBuffer(){
		RARE_CONSTANTS rare;
		rare.gMiddleGray = mRendererOptions->r_HDRMiddleGray;
		rare.gStarPower = mRendererOptions->r_StarPower;
		rare.gBloomPower = mRendererOptions->r_BloomPower;
		rare.gRareDummy = 0.f;
		GetPlatformRenderer().UpdateShaderConstants(ShaderConstants::RareChange, &rare, sizeof(RARE_CONSTANTS));

	}
	void UpdateRadConstantsBuffer(const void* pData){
		RAD_CONSTANTS constants;
		memcpy(constants.gIrradConstsnts, pData, sizeof(Vec4f) * 9);
		std::vector<Vec2> hammersley;
		GenerateHammersley(ENV_SAMPLES, hammersley);
		memcpy(constants.gHammersley, &hammersley[0], sizeof(Vec2f)* ENV_SAMPLES);
		GetPlatformRenderer().UpdateShaderConstants(ShaderConstants::Radiance, 
			&constants, sizeof(RAD_CONSTANTS));
	}

	void UpdateImmutableConstantsBuffer() {
		IMMUTABLE_CONSTANTS buffer;
		GetPlatformRenderer().UpdateShaderConstants(ShaderConstants::Immutable,
			&buffer, sizeof(IMMUTABLE_CONSTANTS));
	}

	void UpdateShadowConstantsBuffer(const void* pData){
		unsigned size = sizeof(SHADOW_CONSTANTS);
		GetPlatformRenderer().UpdateShaderConstants(ShaderConstants::Shadow,
			pData, sizeof(SHADOW_CONSTANTS));
	}

	void* MapShaderConstantsBuffer(){
		return GetPlatformRenderer().MapShaderConstantsBuffer();
	}

	void UnmapShaderConstantsBuffer(){
		GetPlatformRenderer().UnmapShaderConstantsBuffer();
	}
	void* MapBigBuffer(){
		return GetPlatformRenderer().MapBigBuffer();
	}
	void UnmapBigBuffer(){
		GetPlatformRenderer().UnmapBigBuffer();
	}
	//-------------------------------------------------------------------
	// GPU Manipulation
	//-------------------------------------------------------------------
	void DrawIndexed(unsigned indexCount, unsigned startIndexLocation, unsigned startVertexLocation){
		/*if (mSelf->mCurrentRenderTargetTextureIds.empty() ||
			mSelf->mCurrentRenderTargetTextureIds[0] == -1){
			int a = 0;
			a++;
		}*/
		GetPlatformRenderer().DrawIndexed(indexCount, startIndexLocation, startVertexLocation);
		mFrameProfiler.NumIndexedDrawCall++;
		mFrameProfiler.NumIndexCount += indexCount;
	}

	void Draw(unsigned int vertexCount, unsigned int startVertexLocation){
		/*if (mSelf->mCurrentRenderTargetTextureIds.empty() ||
			mSelf->mCurrentRenderTargetTextureIds[0] == -1){
			int a = 0;
			a++;
		}*/
		GetPlatformRenderer().Draw(vertexCount, startVertexLocation);
		mFrameProfiler.NumDrawCall++;
		mFrameProfiler.NumVertexCount += vertexCount;
	}

	void DrawFullscreenQuad(ShaderPtr pixelShader, bool farside){
		// vertex buffer

		ShaderPtr shader;
		if (farside)
			shader = mResourceProvider->GetShader(ResourceTypes::Shaders::FullscreenQuadFarVS);
		else
			shader = mResourceProvider->GetShader(ResourceTypes::Shaders::FullscreenQuadNearVS);

		if (shader){
			shader->Bind(true);
		}
		else{
			return;
		}

		if (pixelShader)
			pixelShader->Bind(false);

		UnbindInputLayout();		
		// draw
		// using full screen triangle : http://blog.naver.com/jungwan82/220108100698
		SetPrimitiveTopology(PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		Draw(3, 0);
	}

	void DrawTriangle(const Vec3& a, const Vec3& b, const Vec3& c, const Vec4& color, MaterialPtr mat){
		VertexBuffer* pVB = mDynVBs[DEFAULT_INPUTS::POSITION].get();
		assert(pVB);
		MapData mapped = pVB->Map(0, MAP_TYPE_WRITE_DISCARD, MAP_FLAG_NONE);
		DEFAULT_INPUTS::V_P* data = (DEFAULT_INPUTS::V_P*)mapped.pData;
		data[0].p = a;
		data[1].p = b;
		data[2].p = c;
		pVB->Unmap(0);
		pVB->Bind();
		SetPrimitiveTopology(PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		mat->SetShaderParameter(0, color);
		mat->Bind(true);
		Draw(3, 0);
	}

	void DrawQuad(const Vec2I& pos, const Vec2I& size, const Color& color, bool updateRs = true){
		// vertex buffer
		auto rtSize = GetCurrentRenderTargetSize();
		Mat44 screenToProj(2.f / rtSize.x, 0, 0, -1.f,
			0.f, -2.f / rtSize.y, 0, 1.f,
			0, 0, 1.f, 0.f,
			0, 0, 0, 1.f);

		DEFAULT_INPUTS::V_PC data[4] = {
			DEFAULT_INPUTS::V_PC(Vec3((float)pos.x, (float)pos.y, 0.f), color.Get4Byte()),
			DEFAULT_INPUTS::V_PC(Vec3((float)pos.x + size.x, (float)pos.y, 0.f), color.Get4Byte()),
			DEFAULT_INPUTS::V_PC(Vec3((float)pos.x, (float)pos.y + size.y, 0.f), color.Get4Byte()),
			DEFAULT_INPUTS::V_PC(Vec3((float)pos.x + size.x, (float)pos.y + size.y, 0.f), color.Get4Byte()),
		};		
		for (int i = 0; i < 4; i++){
			data[i].p = (screenToProj * Vec4(data[i].p, 1.0)).GetXYZ();
		}
		MapData mapped = mDynVBs[DEFAULT_INPUTS::POSITION_COLOR]->Map(0, MAP_TYPE_WRITE_DISCARD, MAP_FLAG_NONE);
		if (mapped.pData)
		{
			memcpy(mapped.pData, data, sizeof(data));
			mDynVBs[DEFAULT_INPUTS::POSITION_COLOR]->Unmap(0);
		}

		if (updateRs){
			// set primitive topology
			SetPrimitiveTopology(PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
			// set material
			auto quadMateiral = mResourceProvider->GetMaterial(ResourceTypes::Materials::Quad);
			if (quadMateiral){
				quadMateiral->Bind(true);
			}
		}


		// set vertex buffer
		mDynVBs[DEFAULT_INPUTS::POSITION_COLOR]->Bind();
		// draw
		Draw(4, 0);
	}

	void DrawFrustum(const Frustum& frustum)
	{
		auto points  = frustum.ToPoints();
		assert(points.size() == 8);
		DEFAULT_INPUTS::V_PC data[8];
		for (int i = 0; i < 8; ++i) {
			data[i].p = points[i];
			if (i<4)
				data[i].color = Color::Green.Get4Byte();
			else
				data[i].color = Color::Blue.Get4Byte();
		}		
		MapData mapped = mDynVBs[DEFAULT_INPUTS::POSITION_COLOR]->Map(0, MAP_TYPE_WRITE_DISCARD, MAP_FLAG_NONE);
		if (mapped.pData)
		{
			memcpy(mapped.pData, data, sizeof(data));
			mDynVBs[DEFAULT_INPUTS::POSITION_COLOR]->Unmap(0);
		}

		auto indexBuffer = mResourceProvider->GetIndexBuffer(ResourceTypes::IndexBuffer::Frustum);
		if (!indexBuffer) {
			Logger::Log(FB_ERROR_LOG_ARG, "IndexBuffer is null.");
			return;
		}
		indexBuffer->Bind(0);
		mDynVBs[DEFAULT_INPUTS::POSITION_COLOR]->Bind();		
		SetPrimitiveTopology(PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		auto quadMateiral = mResourceProvider->GetMaterial(ResourceTypes::Materials::Frustum);
		if (quadMateiral) {
			quadMateiral->Bind(true);
		}
		DrawIndexed(indexBuffer->GetNumIndices(), 0, 0);		

		/// near (left, bottom), (left, top), (right, bottom), (right, top)
		/// far (left, bottom), (left, top), (right, bottom), (right, top)
		DrawLine(points[0], points[1], Color::Green, Color::Green);
		DrawLine(points[0], points[2], Color::Green, Color::Green);
		DrawLine(points[3], points[1], Color::Green, Color::Green);
		DrawLine(points[3], points[2], Color::Green, Color::Green);

		DrawLine(points[4], points[5], Color::Blue, Color::Blue);
		DrawLine(points[4], points[6], Color::Blue, Color::Blue);
		DrawLine(points[7], points[5], Color::Blue, Color::Blue);
		DrawLine(points[7], points[6], Color::Blue, Color::Blue);

		DrawLine(points[0], points[4], Color::Green, Color::Blue);
		DrawLine(points[1], points[5], Color::Green, Color::Blue);
		DrawLine(points[2], points[6], Color::Green, Color::Blue);
		DrawLine(points[3], points[7], Color::Green, Color::Blue);
	}

	void DrawLine(const Vec3& start, const Vec3& end,
		const Color& color0, const Color& color1)
	{
		mDebugHud->DrawLineNow(start, end, color0, color1);
	}

	void DrawLine(const Vec2I& start, const Vec2I& end,
		const Color& color0, const Color& color1) 
	{
		mDebugHud->DrawLineNow(start, end, color0, color1);
	}

	void DrawBox(const std::vector<Vec3>& corners, const Color& color)
	{
		/// bottom:ll lr ur ul, top:ll lr ur ul
		DrawLine(corners[0], corners[1], color, color);
		DrawLine(corners[0], corners[3], color, color);
		DrawLine(corners[2], corners[1], color, color);
		DrawLine(corners[2], corners[3], color, color);

		DrawLine(corners[4], corners[5], color, color);
		DrawLine(corners[4], corners[7], color, color);
		DrawLine(corners[6], corners[5], color, color);
		DrawLine(corners[6], corners[7], color, color);

		DrawLine(corners[0], corners[4], color, color);
		DrawLine(corners[1], corners[5], color, color);
		DrawLine(corners[2], corners[6], color, color);
		DrawLine(corners[3], corners[7], color, color);
	}

	void DrawQuadLine(const Vec2I& pos, const Vec2I& size, const Color& color) {
		int left = pos.x - 1;
		int top = pos.y - 1;
		int right = pos.x + size.x + 1;
		int bottom = pos.y + size.y + 1;
		mDebugHud->DrawLineNow(Vec2I(left, top), Vec2I(right, top), color, color);
		mDebugHud->DrawLineNow(Vec2I(left, top), Vec2I(left, bottom), color, color);
		mDebugHud->DrawLineNow(Vec2I(right, top), Vec2I(right, bottom), color, color);
		mDebugHud->DrawLineNow(Vec2I(left, bottom), Vec2I(right, bottom), color, color);
	}

	void DrawPoints(const std::vector<Vec3>& points, const Color& color)
	{
		mDebugHud->DrawPointsNow(points, color);		
	}

	void DrawQuadWithTexture(const Vec2I& pos, const Vec2I& size, const Color& color, TexturePtr texture, MaterialPtr materialOverride = 0){
		DrawQuadWithTextureUV(pos, size, Vec2(0, 0), Vec2(1, 1), color, texture, materialOverride);
	}

	void DrawQuadWithTextureUV(const Vec2I& pos, const Vec2I& size, const Vec2& uvStart, const Vec2& uvEnd,
		const Color& color, TexturePtr texture, MaterialPtr materialOverride = 0){
		// vertex buffer
		MapData mapped = mDynVBs[DEFAULT_INPUTS::POSITION_COLOR_TEXCOORD]->Map(0, MAP_TYPE_WRITE_DISCARD, MAP_FLAG_NONE);
		DEFAULT_INPUTS::V_PCT data[4] = {
			DEFAULT_INPUTS::V_PCT(Vec3((float)pos.x, (float)pos.y, 0.f), color.Get4Byte(), Vec2(uvStart.x, uvStart.y)),
			DEFAULT_INPUTS::V_PCT(Vec3((float)pos.x + size.x, (float)pos.y, 0.f), color.Get4Byte(), Vec2(uvEnd.x, uvStart.y)),
			DEFAULT_INPUTS::V_PCT(Vec3((float)pos.x, (float)pos.y + size.y, 0.f), color.Get4Byte(), Vec2(uvStart.x, uvEnd.y)),
			DEFAULT_INPUTS::V_PCT(Vec3((float)pos.x + size.x, (float)pos.y + size.y, 0.f), color.Get4Byte(), Vec2(uvEnd.x, uvEnd.y)),
		};
		auto& rtSize = GetCurrentRenderTargetSize();
		Mat44 screenToProj(2.f / rtSize.x, 0, 0, -1.f,
			0.f, -2.f / rtSize.y, 0, 1.f,
			0, 0, 1.f, 0.f,
			0, 0, 0, 1.f);

		for (int i = 0; i < 4; ++i){
			data[i].p = (screenToProj * Vec4(data[i].p, 1.)).GetXYZ();
		}

		if (mapped.pData)
		{
			memcpy(mapped.pData, data, sizeof(data));
			mDynVBs[DEFAULT_INPUTS::POSITION_COLOR_TEXCOORD]->Unmap(0);
		}

		SetPrimitiveTopology(PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
		if (materialOverride)
			materialOverride->Bind(true);
		else{
			auto quadTextureMat = mResourceProvider->GetMaterial(ResourceTypes::Materials::QuadTextured);
			if (quadTextureMat){
				quadTextureMat->Bind(true);
			}
		}
			
		texture->Bind(SHADER_TYPE_PS, 0);
		mDynVBs[DEFAULT_INPUTS::POSITION_COLOR_TEXCOORD]->Bind();
		Draw(4, 0);
	}

	void DrawBillboardWorldQuad(const Vec3& pos, const Vec2& size, const Vec2& offset,
		DWORD color, MaterialPtr pMat){
		VertexBuffer* pVB = mDynVBs[DEFAULT_INPUTS::POSITION_VEC4_COLOR].get();
		assert(pVB);
		MapData mapped = pVB->Map(0, MAP_TYPE_WRITE_DISCARD, MAP_FLAG_NONE);
		DEFAULT_INPUTS::POSITION_VEC4_COLOR_V* data = (DEFAULT_INPUTS::POSITION_VEC4_COLOR_V*)mapped.pData;
		data->p = pos;
		data->v4 = Vec4(size.x, size.y, offset.x, offset.y);
		data->color = color;
		pVB->Unmap(0);
		pVB->Bind();
		SetPrimitiveTopology(PRIMITIVE_TOPOLOGY_POINTLIST);
		pMat->Bind(true);
		Draw(1, 0);
	}

	void DrawCurrentAxis() {
		if (!mAxisDrawingCamera) {
			mAxisDrawingCamera = Camera::Create();			
			mAxisDrawingCamera->SetOrthogonal(true);
			mAxisDrawingCamera->SetOrthogonalData(-1.5f, 1.5f, 1.5f, -1.5f);
			mAxisDrawingCamera->SetNearFar(-1.5f, 1.5f);
		}
		auto prevCamera = SetCamera(mAxisDrawingCamera);
		auto rtSize = GetCurrentRenderTargetSize();
		float width = 60;
		float gap = 20;
		auto vpBackup = mCurrentViewports;
		Viewport vp = { gap, rtSize.y - gap - width, width, width, 0.f, 1.f };		
		SetViewports(&vp, 1);
		auto camT = prevCamera->GetTransformation();
		struct DrawData {
			Vec3 dir;
			Color color;
		};
		
		DrawData dd[] = {
			{ camT.ApplyInverseDir(Vec3::UNIT_X) * 2.0f, Color::Red},
			{ camT.ApplyInverseDir(Vec3::UNIT_Y) * 2.0f, Color::Green},
			{ camT.ApplyInverseDir(Vec3::UNIT_Z) * 2.0f, Color::Blue} };
		std::sort(dd, dd + 3, [](const DrawData& a, const DrawData&b) {
			return a.dir.y > b.dir.y;
		});
		for (int i = 0; i < 3; ++i) {
			DrawLine(Vec3::ZERO, dd[i].dir, dd[i].color, dd[i].color);
		}
		
		SetViewports(&vpBackup[0], vpBackup.size());
		SetCamera(prevCamera);		
	}

	void Draw3DTextNow(const Vec3& worldpos, const char* text, const Color& color, Real size) {
		if (mDebugHud) {
			mDebugHud->Draw3DTextNow(worldpos, AnsiToWide(text), color, size);
		}
	}

	void QueueDrawText(const Vec2I& pos, WCHAR* text, const Color& color, Real size){
		if (mDebugHud)
			mDebugHud->DrawText(pos, text, color, size);
	}

	void QueueDrawText(const Vec2I& pos, const char* text, const Color& color, Real size){
		QueueDrawText(pos, AnsiToWide(text, strlen(text)), color, size);
	}

	void QueueDraw3DText(const Vec3& worldpos, WCHAR* text, const Color& color, Real size){
		if (mDebugHud)
			mDebugHud->Draw3DText(worldpos, text, color, size);
	}

	void QueueDraw3DText(const Vec3& worldpos, const char* text, const Color& color, Real size){
		QueueDraw3DText(worldpos, AnsiToWide(text), color, size);
	}

	void QueueDrawTextForDuration(Real secs, const Vec2I& pos, WCHAR* text,
		const Color& color, Real size){
		if (mDebugHud)
			mDebugHud->DrawTextForDuration(secs, pos, text, color, size);
	}

	void QueueDrawTextForDuration(Real secs, const Vec2I& pos, const char* text,
		const Color& color, Real size){
		QueueDrawTextForDuration(secs, pos, AnsiToWide(text, strlen(text)), color, size);
	}

	void ClearDurationTexts(){
		if (mDebugHud)
			mDebugHud->ClearDurationTexts();
	}

	void QueueDrawLine(const Vec3& start, const Vec3& end,
		const Color& color0, const Color& color1){
		if (mDebugHud)
			mDebugHud->DrawLine(start, end, color0, color1);
	}

	void QueueDrawLine(const Vec2I& start, const Vec2I& end,
		const Color& color0, const Color& color1){
		if (mDebugHud)
			mDebugHud->DrawLine(start, end, color0, color1);
	}

	void QueueDrawLineBeforeAlphaPass(const Vec3& start, const Vec3& end,
		const Color& color0, const Color& color1){
		if (mDebugHud)
			mDebugHud->DrawLineBeforeAlphaPass(start, end, color0, color1);
	}

	void QueueDrawQuad(const Vec2I& pos, const Vec2I& size, const Color& color){
		if (mDebugHud){
			mDebugHud->DrawQuad(pos, size, color);
		}
	}

	void QueueDrawQuadLine(const Vec2I& pos, const Vec2I& size, const Color& color){
		int left = pos.x - 1;
		int top = pos.y - 1;
		int right = pos.x + size.x + 1;
		int bottom = pos.y + size.y + 1;
		QueueDrawLine(Vec2I(left, top), Vec2I(right, top), color, color);
		QueueDrawLine(Vec2I(left, top), Vec2I(left, bottom), color, color);
		QueueDrawLine(Vec2I(right, top), Vec2I(right, bottom), color, color);
		QueueDrawLine(Vec2I(left, bottom), Vec2I(right, bottom), color, color);
	}

	void QueueDrawAABB(const AABB& aabb, const Transformation& transform, 
		const Color& color){
		Vec3 points[8];
		aabb.GetPoints(points);
		for (int i = 0; i < 8; ++i){
			points[i] = transform.ApplyForward(points[i]);
		}
		QueueDraw3DQuad(points[0], points[1], points[2], points[3], color);
		QueueDraw3DQuad(points[4], points[0], points[6], points[2], color);
		QueueDraw3DQuad(points[5], points[4], points[7], points[6], color);
		QueueDraw3DQuad(points[1], points[5], points[3], points[7], color);
		QueueDraw3DQuad(points[4], points[5], points[0], points[1], color);
		QueueDraw3DQuad(points[4], points[5], points[0], points[1], color);
		QueueDraw3DQuad(points[2], points[3], points[6], points[7], color);
	}

	// p0 - p1
	// p0 - p2
	// p3 - p1
	// p3 - p2
	void QueueDraw3DQuad(const Vec3& p0, const Vec3& p1, const Vec3& p2,
		const Vec3& p3, const Color& color){
		QueueDrawLine(p0, p1, color, color);
		QueueDrawLine(p0, p2, color, color);
		QueueDrawLine(p3, p1, color, color);
		QueueDrawLine(p3, p2, color, color);
	}

	//-------------------------------------------------------------------
	// Internal
	//-------------------------------------------------------------------

	void RenderDebugHud(){
		if (!mDebugHud)
			return;
		RenderEventMarker devent("RenderDebugHud");

		if (mRendererOptions->r_renderAxis) {
			DrawCurrentAxis();
		}

		RestoreRenderStates();
		RenderParam param;
		param.mRenderPass = PASS_NORMAL;
		param.mCamera = mCamera.get();
		mDebugHud->Render(param, 0);		
		//SetWireframe(backup);		
	}

	//-------------------------------------------------------------------
	// GPU Manipulation
	//-------------------------------------------------------------------
	void SetClearColor(HWindowId id, const Color& color){
		auto it = mWindowRenderTargets.find(id);
		if (it == mWindowRenderTargets.end()){
			Logger::Log(FB_ERROR_LOG_ARG, "Cannot find the render target.");
			return;
		}
		it->second->SetClearColor(color);
	}

	void SetClearDepthStencil(HWindowId id, Real z, UINT8 stencil){
		auto it = mWindowRenderTargets.find(id);
		if (it == mWindowRenderTargets.end()){
			Logger::Log(FB_ERROR_LOG_ARG, "Cannot find the render target.");
			return;
		}
		it->second->SetClearDepthStencil(z, stencil);
	}

	void Clear(Real r, Real g, Real b, Real a, Real z, UINT8 stencil){		
		GetPlatformRenderer().Clear(r, g, b, a, z, stencil);
	}

	void Clear(Real r, Real g, Real b, Real a){
		GetPlatformRenderer().Clear(r, g, b, a);
	}

	void ClearDepthStencil(Real z, UINT8 stencil) {
		GetPlatformRenderer().ClearDepthStencil(z, stencil);
	}

	// Avoid to use
	void ClearState(){
		GetPlatformRenderer().ClearState();
	}

	void BeginEvent(const char* name){
		GetPlatformRenderer().BeginEvent(name);
	}

	void EndEvent(){
		GetPlatformRenderer().EndEvent();
	}

	void TakeScreenshot(){
		auto filepath = GetNextScreenshotFile();
		GetPlatformRenderer().TakeScreenshot(filepath.c_str());
	}

	void TakeScreenshot(int width, int height) {
		auto filepath = GetNextScreenshotFile();
		RenderTargetParam rparam;
		rparam.mEveryFrame = false;
		rparam.mSize = Vec2I(width, height);
		rparam.mWillCreateDepth = true;
		auto& renderer = Renderer::GetInstance();
		auto rt = renderer.CreateRenderTarget(rparam);
		rt->SetDepthStencilDesc(width, height, PIXEL_FORMAT_D24_UNORM_S8_UINT, false, false);
		auto mainRT = renderer.GetMainRenderTarget();
		auto cam = mainRT->GetCamera()->Clone();
		cam->SetWidth((Real)width);
		cam->SetHeight((Real)height);
		auto aspectRatio = width / (float)height;
		cam->SetAspectRatio(aspectRatio);
		cam->SetFOV(Radian(45) / aspectRatio);
		rt->SetCamera(cam);
		rt->RegisterScene(mainRT->GetScene());
		rt->Render(0);
		rt->SaveToFile(filepath.c_str());
		
	}

	void ChangeFullscreenMode(int mode){
		Logger::Log(FB_DEFAULT_LOG_ARG, FormatString("Changing fullscreen mode: %d", mode).c_str());
		GetPlatformRenderer().ChangeFullscreenMode(mMainWindowId, mWindowHandles[mMainWindowId], mode);
	}

	void OnWindowSizeChanged(HWindow window, const Vec2I& clientSize){
		if (mWindowSizeInternallyChanging)
			return;
		
		ChangeResolution(GetMainWindowHandle(), clientSize);
	}	

	void ChangeResolution(HWindow window, const Vec2I& resol){
		if (resol == Vec2I::ZERO)
			return;
		auto handleId = GetWindowHandleId(window);
		if (handleId == INVALID_HWND_ID)
			return;
		auto rt = GetRenderTarget(handleId);
		if (!rt)
			return;		

		if (!rt->GetRenderTargetTexture() || rt->GetRenderTargetTexture()->GetSize() == resol)
			return;		
		rt->RemoveTextures();
		mCurrentRTTextures.clear();
		mCurrentDSTexture = 0;
		mCurrentDSViewIndex = 0;
		IPlatformTexturePtr color, depth;
		bool success = GetPlatformRenderer().ChangeResolution(handleId, window, 
			resol, color, depth);
		if (success){			
			auto colorTexture = CreateTexture(color);
			colorTexture->SetType(TEXTURE_TYPE_RENDER_TARGET);
			auto depthTexture = CreateTexture(depth);
			depthTexture->SetType(TEXTURE_TYPE_DEPTH_STENCIL);
			rt->SetColorTexture(colorTexture);
			rt->SetDepthTexture(depthTexture);
		}

		mDepthStencilTextureOverride.clear();
		rt->Bind();
		ReportResolutionChange(handleId, window);
	}

	void ReportResolutionChange(HWindowId handleId, HWindow window)
	{		
		if (window == GetMainWindowHandle()) {
			auto libfullscreen = GetPlatformRenderer().IsFullscreen();
			if (libfullscreen) {
				if (mRendererOptions->r_fullscreen == 0 || mRendererOptions->r_fullscreen == 2) {
					mRendererOptions->r_fullscreen = 1;
				}
			}
			else {
				if (mRendererOptions->r_fullscreen == 1) {
					mRendererOptions->r_fullscreen = 0;
				}
			}

			auto resol = GetRenderTargetSize(window);
			mRendererOptions->r_resolution = resol;
			mScreenToNDCMatrix = MakeOrthogonalMatrix(0, 0,
				(Real)resol.x,
				(Real)resol.y,
				0.f, 1.0f);
		}

		auto& observers = mSelf->mObservers_[IRendererObserver::DefaultRenderEvent];
		for (auto it = observers.begin(); it != observers.end(); /**/) {
			IteratingWeakContainer(observers, it, observer);
			observer->OnResolutionChanged(handleId, window);
		}
	}

	void ChangeWindowSizeAndResolution(HWindow window, const Vec2I& resol){
		mWindowSizeInternallyChanging = true;
		ChangeWindowSize(window, resol);
		mWindowSizeInternallyChanging = false;
		ChangeResolution(window, resol);
	}

	void UnbindTexture(SHADER_TYPE shader, int slot){
		GetPlatformRenderer().UnbindTexture(shader, slot);
	}

	void UnbindInputLayout(){
		GetPlatformRenderer().UnbindInputLayout();
	}

	void UnbindVertexBuffers(){
		GetPlatformRenderer().SetVertexBuffers(0, 0, 0, 0, 0);
	}
	
	void UnbindShader(SHADER_TYPE shader){
		GetPlatformRenderer().UnbindShader(shader);
	}

	std::string GetScreenhotFolder(){
		auto appData = FileSystem::GetMyDocumentGameFolder();
		const char* screenShotFolder = "ScreenShot/";
		auto screenShotFolderFull = FileSystem::ConcatPath(appData.c_str(), screenShotFolder);
		return screenShotFolderFull;
	}
	std::string GetNextScreenshotFile(){
		auto screenShotFolder = GetScreenhotFolder();
		if (!FileSystem::Exists(screenShotFolder.c_str())){
			FileSystem::CreateDirectory(screenShotFolder.c_str());
			if (!FileSystem::Exists(screenShotFolder.c_str())){
				Logger::Log(FB_ERROR_LOG_ARG, FormatString("Failed to create folder %s", screenShotFolder.c_str()).c_str());
				return "";
			}
		}
		auto it = FileSystem::GetDirectoryIterator(screenShotFolder.c_str(), false);
		unsigned n = 0;		
		while (it->HasNext())
		{
			const char* szfilename = it->GetNextFilePath();
			std::regex match(".*screenshot_([0-9]+)\\.jpg");
			std::smatch result;
			std::string filename(szfilename);
			if (std::regex_match(filename, result, match)){
				if (result.size() == 2){
					std::ssub_match subMatch = result[1];
					std::string matchNumber = subMatch.str();
					unsigned thisn = StringConverter::ParseUnsignedInt(matchNumber);
					if (thisn >= n){
						n = thisn + 1;
					}
				}
			}			
		}
		return FormatString("%sscreenshot_%d.jpg", screenShotFolder.c_str(), n);
	}

	//-------------------------------------------------------------------
	// FBRenderer State
	//-------------------------------------------------------------------
	ResourceProviderPtr GetResourceProvider() const{
		return mResourceProvider;
	}

	void SetResourceProvider(ResourceProviderPtr provider){
		if (!provider)
			return;
		mResourceProvider = provider;
	}

	RenderTargetPtr GetMainRenderTarget() const{
		auto it = mWindowRenderTargets.find(mMainWindowId);
		if (it == mWindowRenderTargets.end()){
			Logger::Log(FB_FRAME_TIME, FB_ERROR_LOG_ARG, "No main window render target found.");
			return 0;
		}
		return it->second;
	}

	unsigned GetMainRenderTargetId() const{
		auto it = mWindowRenderTargets.find(mMainWindowId);
		if (it == mWindowRenderTargets.end()){
			Logger::Log(FB_FRAME_TIME, FB_ERROR_LOG_ARG, "No main window render target found.");
			return -1;
		}
		return it->second->GetId();
	}

	IScenePtr GetMainScene() const{
		auto rt = GetMainRenderTarget();
		if (rt){
			return rt->GetScene();
		}
		return 0;
	}
 // move to SceneManager
	const Vec2I& GetMainRenderTargetSize() const{
		auto rt = GetMainRenderTarget();
		if (rt)
		{
			return rt->GetSize();
		}
		return Vec2I::ZERO;
	}

	const Vec2I& GetCurrentRenderTargetSize() const{
		return mCurrentRenderTarget->GetSize();
	}

	void SetCurrentRenderTarget(RenderTargetPtr renderTarget){
		mCurrentRenderTarget = renderTarget;
	}

	RenderTargetPtr GetCurrentRenderTarget() const{
		return mCurrentRenderTarget;
	}

	void SetCurrentScene(IScenePtr scene){
		mCurrentScene = scene;
		UpdateLightFrustum();
		UpdateSceneConstantsBuffer();
	}

	bool IsMainRenderTarget() const{
		return GetMainRenderTarget() == mCurrentRenderTarget;
	}

	const Vec2I& GetRenderTargetSize(HWindowId id = INVALID_HWND_ID) const{
		
		if (id != INVALID_HWND_ID){
			auto rt = GetRenderTarget(id);
			if (rt){
				return rt->GetSize();
			}
		}

		return mCurrentRTSize;
	}

	const Vec2I& GetRenderTargetSize(HWindow hwnd = 0) const{
		RenderTargetPtr rt = GetRenderTarget(hwnd);
		if (rt){
			return rt->GetSize();
		}
		return mCurrentRTSize;
	}

	
	void SetDirectionalLightInfo(int idx, const DirectionalLightInfo& info){
		mDirectionalLight[idx] = info;
	}

	const RENDERER_FRAME_PROFILER& GetFrameProfiler() const{
		return mFrameProfiler;
	}

	void DisplayFrameProfiler(){
		wchar_t msg[255];
		int x = 1000;
		int y = 110;
		int yStep = 20;

		const RENDERER_FRAME_PROFILER& profiler = mFrameProfiler;

		swprintf_s(msg, 255, L"Rendering Takes = %f", profiler.mLastDrawTakes);
		mSelf->QueueDrawText(Vec2I(x, y), msg, Vec3(1, 1, 1));
		y += yStep;

		swprintf_s(msg, 255, L"FrameRate = %.0f", profiler.FrameRateDisplay);
		mSelf->QueueDrawText(Vec2I(x, y), msg, Vec3(1, 1, 1));
		y += yStep;

		swprintf_s(msg, 255, L"Num draw calls = %d", profiler.NumDrawCall);
		mSelf->QueueDrawText(Vec2I(x, y), msg, Vec3(1, 1, 1));
		y += yStep;

		swprintf_s(msg, 255, L"Num vertices = %d", profiler.NumVertexCount);
		mSelf->QueueDrawText(Vec2I(x, y), msg, Vec3(1, 1, 1));
		y += yStep;

		swprintf_s(msg, 255, L"Num indexed draw calls = %d", profiler.NumIndexedDrawCall);
		mSelf->QueueDrawText(Vec2I(x, y), msg, Vec3(1, 1, 1));
		y += yStep;

		swprintf_s(msg, 255, L"Num index count = %d", profiler.NumIndexCount);
		mSelf->QueueDrawText(Vec2I(x, y), msg, Vec3(1, 1, 1));
		y += yStep;

		swprintf_s(msg, 255, L"Num UpdateObjectConstantsBuffer = %u", profiler.NumUpdateObjectConst);
		mSelf->QueueDrawText(Vec2I(x, y), msg, Vec3(1, 1, 1));
		y += yStep * 2;		
	}

	void ReloadFonts(){
		auto fonts = mFonts;
		mFonts.clear();
		for (auto& it : fonts){
			it.second->Reload();
			mFonts[it.second->GetFontSize()] = it.second;
		}
	}

	inline FontPtr GetFont(int fontSize) const{
		if (mFonts.empty()){
			return 0;
		}

		if (mFonts.size() == 1){
			auto it = mFonts.begin();
			it->second->ScaleFontSizeTo(fontSize);
			return it->second;
		}

		int bestMatchSize = mFonts.begin()->first;
		int curGap = std::abs(fontSize - bestMatchSize);
		FontPtr bestFont = mFonts.begin()->second;
		for (auto it : mFonts){
			auto newGap = std::abs(fontSize - it.first);
			if (newGap < curGap){
				bestMatchSize = it.first;
				curGap = newGap;
				bestFont = it.second;
			}
		}
		if (!bestFont){
			Logger::Log(FB_ERROR_LOG_ARG, FormatString("Font not found with size %d", fontSize).c_str());
		}
		else{
			bestFont->ScaleFontSizeTo(fontSize);
		}
		return bestFont;
	}

	FontPtr GetFontWithHeight(Real height) const{
		if (mFonts.empty()){
			return 0;
		}

		if (mFonts.size() == 1){
			auto it = mFonts.begin();
			it->second->ScaleFontHeightTo(height);
			return it->second;
		}

		auto bestMatchHeight = mFonts.begin()->second->GetOriginalHeight();
		auto curGap = std::abs(height - bestMatchHeight);
		FontPtr bestFont = mFonts.begin()->second;
		for (auto it : mFonts){
			auto newGap = std::abs(height - it.second->GetOriginalHeight());
			if (newGap < curGap){
				bestMatchHeight = it.second->GetOriginalHeight();
				curGap = newGap;
				bestFont = it.second;
			}
		}
		if (!bestFont){
			Logger::Log(FB_ERROR_LOG_ARG, FormatString("Font not found with height %f", height).c_str());
		}
		else{
			bestFont->ScaleFontHeightTo(height);
		}
		return bestFont;
	}

	const INPUT_ELEMENT_DESCS& GetInputElementDesc(DEFAULT_INPUTS::Enum e){
		return mInputLayoutDescs[e];
	}

	Vec3 NormalFromCubePixelCoord(int face, int w, int h, float halfWidth)
	{
		Vec3 n;
		switch (face)
		{
		case 0:
			n.x = halfWidth;
			n.y = halfWidth - w;
			n.z = halfWidth - h;
			break;
		case 1:
			n.x = -halfWidth;
			n.y = w - halfWidth;
			n.z = halfWidth - h;
			break;
		case 2: // front
			n.x = w - halfWidth;
			n.y = halfWidth;
			n.z = halfWidth - h;
			break;
		case 3: // back
			n.x = halfWidth - w;
			n.y = -halfWidth;
			n.z = halfWidth - h;
			break;
		case 4: // up
			n.x = w - halfWidth;
			n.y = h - halfWidth;
			n.z = halfWidth;
			break;
		case 5: // down
			n.x = w - halfWidth;
			n.y = halfWidth - h;
			n.z = -halfWidth;
			break;		
		}
		return n.NormalizeCopy();
	}

	void GenerateRadianceCoef(TexturePtr pTex)
	{
		pTex->Unbind();
		mGenerateRadianceCoef = false;
		auto ENV_SIZE = pTex->GetSize().x;
		int maxLod = (int)log2((float)ENV_SIZE);

		const float basisCoef[5] = { 0.282095f,
			0.488603f,
			1.092548f,
			0.315392f,
			0.546274f };

		const int usingLod = 2; // if ENV_SIZE == 1024, we are using 256 size.
		const int width = (int)(ENV_SIZE / pow(2, usingLod));
		float halfWidth = width / 2.f;
		auto& renderer = Renderer::GetInstance();
		TexturePtr pStaging = renderer.CreateTexture(0, width, width, PIXEL_FORMAT_R8G8B8A8_UNORM,
			1, BUFFER_USAGE_STAGING, BUFFER_CPU_ACCESS_READ, TEXTURE_TYPE_CUBE_MAP);
		for (int i = 0; i < 6; i++)
		{
			unsigned subResource = i * (maxLod + 1) + usingLod;
			pTex->CopyToStaging(pStaging, i, 0, 0, 0, subResource, 0);
		}
		//pStaging->SaveToFile("envSub.dds");

		// prefiltering
		float lightCoef[3][9];
		for (int i = 0; i < 3; i++)
		{
			for (int j = 0; j < 9; j++)
			{
				lightCoef[i][j] = 0;
			}
		}

		for (int i = 0; i < 6; i++)
		{
			unsigned subResource = i;
			MapData data = pStaging->Map(subResource, MAP_TYPE_READ, MAP_FLAG_NONE);
			if (data.pData)
			{
				DWORD* colors = (DWORD*)data.pData;
				DWORD* row, *pixel;
				for (int h = 0; h < width; h++)
				{
					row = colors + width * h;
					for (int w = 0; w < width; w++)
					{

						Vec3 n = NormalFromCubePixelCoord(i, w, h, halfWidth);

						pixel = row + w;
						Color::RGBA* rgba256 = (Color::RGBA*)pixel;
						Vec3 rgb(rgba256->r / 255.0f, rgba256->g / 255.0f, rgba256->b / 255.0f);
						// 00
						lightCoef[0][0] += rgb.x * basisCoef[0];
						lightCoef[1][0] += rgb.y * basisCoef[0];
						lightCoef[2][0] += rgb.z * basisCoef[0];

						// 1-1
						float yC = (basisCoef[1] * n.y);
						lightCoef[0][1] += rgb.x * yC;
						lightCoef[1][1] += rgb.y * yC;
						lightCoef[2][1] += rgb.z * yC;

						// 10
						yC = (basisCoef[1] * n.z);
						lightCoef[0][2] += rgb.x * yC;
						lightCoef[1][2] += rgb.y * yC;
						lightCoef[2][2] += rgb.z * yC;

						// 11
						yC = (basisCoef[1] * n.x);
						lightCoef[0][3] += rgb.x * yC;
						lightCoef[1][3] += rgb.y * yC;
						lightCoef[2][3] += rgb.z * yC;

						// 2-2
						yC = (basisCoef[2] * n.x*n.y);
						lightCoef[0][4] += rgb.x * yC;
						lightCoef[1][4] += rgb.y * yC;
						lightCoef[2][4] += rgb.z * yC;

						// 2-1
						yC = (basisCoef[2] * n.y*n.z);
						lightCoef[0][5] += rgb.x * yC;
						lightCoef[1][5] += rgb.y * yC;
						lightCoef[2][5] += rgb.z * yC;

						// 20
						yC = (basisCoef[3] * (3.0f*n.z*n.z - 1.f));
						lightCoef[0][6] += rgb.x * yC;
						lightCoef[1][6] += rgb.y * yC;
						lightCoef[2][6] += rgb.z * yC;

						// 21
						yC = (basisCoef[2] * (n.x*n.z));
						lightCoef[0][7] += rgb.x * yC;
						lightCoef[1][7] += rgb.y * yC;
						lightCoef[2][7] += rgb.z * yC;

						// 22
						yC = (basisCoef[4] * (n.x*n.x - n.y*n.y));
						lightCoef[0][8] += rgb.x * yC;
						lightCoef[1][8] += rgb.y * yC;
						lightCoef[2][8] += rgb.z * yC;
					}
				}
			}
			pStaging->Unmap(subResource);
		}

		float avg = 1.0f / (width*width * 6);
		for (int i = 0; i < 9; i++)
		{
			lightCoef[0][i] *= avg;
			lightCoef[1][i] *= avg;
			lightCoef[2][i] *= avg;
			mIrradCoeff[i] = Vec4f(lightCoef[0][i], lightCoef[1][i], lightCoef[2][i], 1);
		}
		UpdateRadConstantsBuffer(mIrradCoeff);
	}

	void SetEnvironmentTexture(TexturePtr pTexture){
		mEnvironmentTexture = pTexture;
		if (pTexture){
			if (pTexture->IsReady())
				GenerateRadianceCoef(pTexture);
			else
				mGenerateRadianceCoef = true;
		}
		SetSystemTexture(SystemTextures::Environment, mEnvironmentTexture);		
	}

	void SetEnvironmentTextureOverride(TexturePtr texture){
		mEnvironmentTextureOverride = texture;
		if (mEnvironmentTextureOverride)
		{
			SetSystemTexture(SystemTextures::Environment, texture);			
		}
		else
		{
			SetSystemTexture(SystemTextures::Environment, mEnvironmentTexture);			
		}
	}

	void SetDebugRenderTarget(unsigned idx, const char* textureName){
		assert(idx < MaxDebugRenderTargets);
		auto mainRT = GetMainRenderTarget();
		assert(mainRT);
		if (_stricmp(textureName, "Shadow") == 0)
			mDebugRenderTargets[idx].mTexture = mShadowManager->GetShadowMap();
		else
			mDebugRenderTargets[idx].mTexture = 0;
	}

	void SetFadeAlpha(Real alpha){
		mFadeAlpha = alpha;
	}

	bool GetSampleOffsets_Bloom(DWORD dwTexSize,
		float afTexCoordOffset[15],
		Vec4* avColorWeight,
		float fDeviation, float fMultiplier){
		// if deviation is big, samples tend to have more distance among them.
		int i = 0;
		float tu = 1.0f / (float)dwTexSize;

		// Fill the center texel
		float weight = fMultiplier * GaussianDistribution(0, 0, fDeviation);
		avColorWeight[7] = Vec4(weight, weight, weight, weight);

		afTexCoordOffset[7] = 0.0f;

		// Fill one side
		for (i = 1; i < 8; i++)
		{
			weight = fMultiplier * GaussianDistribution((float)i, 0, fDeviation);
			afTexCoordOffset[7 - i] = -i * tu;

			avColorWeight[7 - i] = Vec4(weight, weight, weight, weight);
		}

		// Copy to the other side
		for (i = 8; i < 15; i++)
		{
			avColorWeight[i] = avColorWeight[14 - i];
			afTexCoordOffset[i] = -afTexCoordOffset[14 - i];
		}

		// Debug convolution kernel which doesn't transform input data
		/*ZeroMemory( avColorWeight, sizeof(D3DXVECTOR4)*15 );
		avColorWeight[7] = D3DXVECTOR4( 1, 1, 1, 1 );*/

		return true;
	}

	float GaussianDistribution(float x, float y, float rho)
	{
		//http://en.wikipedia.org/wiki/Gaussian_filter

		float g = 1.0f / sqrtf(2.0f * (float)PI * rho * rho);
		g *= expf(-(x * x + y * y) / (2.0f * rho * rho));

		return g;
	}

	void GetSampleOffsets_GaussianBlur5x5(DWORD texWidth, DWORD texHeight, Vec4f** avTexCoordOffset, Vec4f** avSampleWeight, float fMultiplier){
		assert(avTexCoordOffset && avSampleWeight);
		auto it = mGauss5x5.find(std::make_pair(texWidth, texHeight));
		if (it == mGauss5x5.end())
		{
			float tu = 1.0f / (float)texWidth;
			float tv = 1.0f / (float)texHeight;

			Vec4 vWhite(1.0, 1.0, 1.0, 1.0);
			std::vector<Vec4f> offsets;
			std::vector<Vec4f> weights;

			float totalWeight = 0.0;
			int index = 0;
			for (int x = -2; x <= 2; x++)
			{
				for (int y = -2; y <= 2; y++)
				{
					// Exclude pixels with a block distance greater than 2. This will
					// create a kernel which approximates a 5x5 kernel using only 13
					// sample points instead of 25; this is necessary since 2.0 shaders
					// only support 16 texture grabs.
					if (abs(x) + abs(y) > 2)
						continue;

					// Get the unscaled Gaussian intensity for this offset
					offsets.push_back(Vec4f(x * tu, y * tv, 0, 0));
					weights.push_back(Vec4f(vWhite * GaussianDistribution((float)x, (float)y, 1.0f)));
					totalWeight += weights.back().x;
					++index;
				}
			}
			assert(weights.size() == 13);
			// Divide the current weight by the total weight of all the samples; Gaussian
			// blur kernels add to 1.0f to ensure that the intensity of the image isn't
			// changed when the blur occurs. An optional multiplier variable is used to
			// add or remove image intensity during the blur.
			for (int i = 0; i < index; i++)
			{
				weights[i] /= totalWeight;
				weights[i] *= fMultiplier;
			}
			auto it = mGauss5x5.insert(std::make_pair(std::make_pair(texWidth, texHeight), std::make_pair(offsets, weights)));
			*avTexCoordOffset = &(it->second.first[0]);
			*avSampleWeight = &(it->second.second[0]);
		}
		else
		{
			*avTexCoordOffset = &(it->second.first[0]);
			*avSampleWeight = &(it->second.second[0]);
		}
	}

	void GetSampleOffsets_DownScale2x2(DWORD texWidth, DWORD texHeight, Vec4f* avSampleOffsets){
		if (NULL == avSampleOffsets)
			return;

		float tU = 1.0f / texWidth;
		float tV = 1.0f / texHeight;

		// Sample from the 4 surrounding points. Since the center point will be in
		// the exact center of 4 texels, a 0.5f offset is needed to specify a texel
		// center.
		int index = 0;
		for (int y = 0; y < 2; y++)
		{
			for (int x = 0; x < 2; x++)
			{
				avSampleOffsets[index].x = (x - 0.5f) * tU;
				avSampleOffsets[index].y = (y - 0.5f) * tV;

				index++;
			}
		}
	}

	bool IsLuminanceOnCpu() const{
		return mLuminanceOnCpu;
	}

	void SetLockDepthStencilState(bool lock){
		DepthStencilState::SetLock(lock);
	}

	void SetLockBlendState(bool lock){
		BlendState::SetLock(lock);
	}

	void SetLockRasterizerState(bool lock) {
		RasterizerState::SetLock(lock);
	}

	void SetFontTextureAtlas(const char* path){
		if (!ValidCString(path)){
			Logger::Log(FB_ERROR_LOG_ARG, "Invalid arg.");
			return;
		}
		auto textureAtlas = GetTextureAtlas(path);
		if (!textureAtlas){
			Logger::Log(FB_ERROR_LOG_ARG, FormatString("No texture atlas(%s)", path).c_str());
			return;
		}
		mFontTextureAtlas = textureAtlas;
		for (auto font : mFonts){
			font.second->SetTextureAtlas(textureAtlas);
		}
	}

	void ClearFontScissor(){
		for (auto& it : mFonts){
			it.second->SetRenderStates();
		}
	}

	void UpdateLightFrustum(){		
		auto scene = mCurrentScene.lock();
		if (scene){
			mShadowManager->UpdateFrame(mCamera, scene->GetMainLightDirection(), scene->GetSceneAABB());
		}
	}

	void SetBindShadowMap(bool bind){
		if (bind)
			SetSystemTexture(SystemTextures::ShadowMap, mShadowManager->GetShadowMap());
		else
			SetSystemTexture(SystemTextures::ShadowMap, 0);
	}

	void OnShadowOptionChanged(){
		mShadowManager->CreateShadowMap();
		mShadowManager->CreateViewports();
	}

	TexturePtr  KeepTextureReference(const char* filepath, const TextureCreationOption& option){
		if (!ValidCString(filepath)){
			Logger::Log(FB_ERROR_LOG_ARG, "Invalid arg.");
			return nullptr;
		}
		std::string filepathKey(filepath);
		ToLowerCase(filepathKey);
		auto it = mTexturesReferenceKeeper.find(filepathKey);
		if (it != mTexturesReferenceKeeper.end())
			return it->second;
		auto texture = CreateTexture(filepath, option);
		if (texture){
			mTexturesReferenceKeeper[filepathKey] =  texture;
		}
		return texture;
	}

	std::stack<RenderStatesPtr> mRenderStateSnapshots;
	void PushRenderStates() {
		auto p = RenderStates::Create(
			RasterizerState::GetCurrentState(),
			BlendState::GetCurrentState(),
			DepthStencilState::GetCurrentState());
		mRenderStateSnapshots.push(p);		
	}

	void PopRenderStates()
	{
		mRenderStateSnapshots.top()->Bind();
		mRenderStateSnapshots.pop();
	}

	void BindIncrementalStencilState(int stencilRef) {
		mResourceProvider->BindDepthStencilState(ResourceTypes::DepthStencilStates::IncrementalStencilOpaqueIf, stencilRef);
	}

	void SetBindShadowTarget(bool bind){
		if (bind){
			// empty
		}
		else{
			if (mCurrentRenderTarget){
				mCurrentRenderTarget->BindTargetOnly(false);
				SetCamera(mCurrentRenderTarget->GetCamera());
			}
		}
	}

	void RenderShadows(){		
		RenderEventMarker marker("Shadow Pass");						
		SetBindShadowMap(false);
		SetBindShadowTarget(true);
		mShadowManager->RenderShadows(mCurrentScene.lock(), 
			mCurrentRenderTarget == GetMainRenderTarget());
		SetBindShadowTarget(false);
	}

	//-------------------------------------------------------------------
	// Queries
	//-------------------------------------------------------------------
	unsigned GetMultiSampleCount() const{
		return 1;
	}

	bool GetFilmicToneMapping() const{
		return mUseFilmicToneMapping;
	}

	void SetFilmicToneMapping(bool use){
		mUseFilmicToneMapping = use;
	}

	bool GetLuminanaceOnCPU() const{
		return mLuminanceOnCpu;
	}

	void SetLuminanaceOnCPU(bool oncpu){
		mLuminance = oncpu;
	}

	RenderTargetPtr GetRenderTarget(HWindowId id) const{
		auto it = mWindowRenderTargets.find(id);
		if (it != mWindowRenderTargets.end()){
			return it->second;
		}
		Logger::Log(FB_ERROR_LOG_ARG, FormatString("Failed to find the render target for the window id(%u)", id).c_str());
		return 0;
	}

	RenderTargetPtr GetRenderTarget(HWindow hwnd) const{
		auto it = mWindowIds.find(hwnd);
		if (it == mWindowIds.end()){
			Logger::Log(FB_ERROR_LOG_ARG, "Failed to find window Id");
			return 0;
		}
		return GetRenderTarget(it->second);
	}


	CameraPtr SetCamera(CameraPtr pCamera){
		auto prev = mCamera;
		if (mCamera)
			mCamera->SetCurrent(false);
		mCamera = pCamera;
		if (mCamera){
			mCamera->SetCurrent(true);
			UpdateCameraConstantsBuffer();
		}
		return prev;
	}
	CameraPtr GetCamera() const{
		return mCamera;		
	} 

	CameraPtr GetMainCamera() const{
		auto rt = GetMainRenderTarget();
		if (rt)
			return rt->GetCamera();

		Logger::Log(FB_ERROR_LOG_ARG, "No main camera");
		return 0;
	}

	void SetActiveOverrideCamera(bool active){
		if (!mOverridingCamera){
			mOverridingCamera = Camera::Create();
		}
		if (active){
			*mOverridingCamera = *GetMainCamera();
			GetMainCamera()->SetOverridingCamera(mOverridingCamera);			
		}
		else{
			GetMainCamera()->SetOverridingCamera(0);
		}
	}

	HWindow GetMainWindowHandle(){
		auto it = mWindowHandles.find(mMainWindowId);
		if (it != mWindowHandles.end())
			return it->second;
		Logger::Log(FB_ERROR_LOG_ARG, "Cannot find maint window handle.");
		return INVALID_HWND;
	}

	HWindowId GetMainWindowHandleId() const{
		return mMainWindowId;
	}

	HWindow GetWindowHandle(HWindowId windowId){
		auto it = mWindowHandles.find(windowId);
		if (it != mWindowHandles.end()){
			return it->second;
		}
		return INVALID_HWND;
	}

	HWindowId GetWindowHandleId(HWindow window){
		auto it = mWindowIds.find(window);
		if (it != mWindowIds.end()){
			return it->second;
		}
		return INVALID_HWND_ID;
	}

	Vec2I ToSreenPos(HWindowId id, const Vec3& ndcPos) const{
		auto it = mWindowRenderTargets.find(id);
		if (it == mWindowRenderTargets.end())
		{
			Logger::Log(FB_ERROR_LOG_ARG, FormatString("Window id %u is not found.", id).c_str());			
			return Vec2I(0, 0);
		}
		const auto& size = it->second->GetSize();
		Vec2I ret;
		ret.x = (int)((size.x*.5) * ndcPos.x + size.x*.5);
		ret.y = (int)((-size.y*.5) * ndcPos.y + size.y*.5);
		return ret;
	}

	Vec2 ToNdcPos(HWindowId id, const Vec2I& screenPos) const{
		auto it = mWindowRenderTargets.find(id);
		if (it == mWindowRenderTargets.end())
		{
			Logger::Log(FB_ERROR_LOG_ARG, FormatString("Window id %u is not found.", id).c_str());			
			return Vec2(0, 0);
		}
		const auto& size = it->second->GetSize();
		Vec2 ret;
		ret.x = (Real)screenPos.x / (Real)size.x * 2.0f - 1.0f;
		ret.y = -((Real)screenPos.y / (Real)size.y * 2.0f - 1.0f);
		return ret;
	}

	unsigned GetNumLoadingTexture() const{
		return GetPlatformRenderer().GetNumLoadingTexture();
	}

	Vec2I FindClosestSize(HWindowId id, const Vec2I& input){
		return GetPlatformRenderer().FindClosestSize(id, input);
	}

	bool GetResolutionList(unsigned& outNum, Vec2I* list){
		std::unique_ptr<Vec2ITuple[]> tuples;
		if (list){
			tuples = std::unique_ptr<Vec2ITuple[]>(new Vec2ITuple[outNum]);
		}
		auto ret = GetPlatformRenderer().GetResolutionList(outNum, list ? tuples.get() : 0);
		if (ret && list){
			for (unsigned i = 0; i < outNum; ++i){
				list[i] = tuples[i];
			}
		}
		return ret;
	}

	RendererOptionsPtr GetRendererOptions() const{
		return mRendererOptions;
	}

	void SetMainWindowStyle(unsigned style){
		mMainWindowStyle = style;
	}

	bool IsFullscreen() const {
		return GetPlatformRenderer().IsFullscreen();
	}

	GraphicDeviceInfo GetDeviceInfo() const {
		return GetPlatformRenderer().GetDeviceInfo();
	}

	//-------------------------------------------------------------------
	// ISceneObserver
	//-------------------------------------------------------------------
	void OnAfterMakeVisibleSet(IScene* scene){

	}

	void OnBeforeRenderingOpaques(IScene* scene, const RenderParam& renderParam, RenderParamOut* renderParamOut){
		
	}

	void OnBeforeRenderingTransparents(IScene* scene, const RenderParam& renderParam, RenderParamOut* renderParamOut){
		if (mDebugHud){
			mDebugHud->OnBeforeRenderingTransparents(scene, renderParam, renderParamOut);
		}

		BindDepthTexture(true);
	}
	
	//-------------------------------------------------------------------
	// IInputConsumer
	//-------------------------------------------------------------------
	void ConsumeInput(IInputInjectorPtr injector){
		mInputInfo.mCurrentMousePos = injector->GetMousePos();
		mInputInfo.mLButtonDown = injector->IsLButtonDown();
		for (auto rt : mWindowRenderTargets){
			rt.second->ConsumeInput(injector);
		}		
	}

	//-------------------------------------------------------------------
	// ISceneObserver
	//-------------------------------------------------------------------
	bool OnFileChanged(const char* watchDir, const char* file, const char* combinedPath, const char* ext){
		auto extension = std::string(ext);		
		bool shader = extension == ".hlsl" || extension ==  ".h";
		bool material = extension == ".material";
		bool texture = extension == ".png" || extension == ".dds";
		bool xml = extension == ".xml";
		bool font = extension == ".fnt";		
		if (shader){
			ReloadShader(file);
			return true;
		}
		else if (texture){
			Texture::ReloadTexture(file);
			return true;
		}
		else if (material) {
			ReloadMaterial(file);
			return true;
		}
		else if (xml){			
			auto atlas = GetTextureAtlas(file);
			if (atlas){
				atlas->ReloadTextureAtlas();
				return true;
			}
		}
		else if (font){
			LoadFont(file);
			return true;
		}
		return false;
	}
};

static RendererWeakPtr sRenderer;
static Renderer* sRendererRaw = 0;
RendererPtr Renderer::Create(){
	if (sRenderer.expired()){
		auto renderer = RendererPtr(FB_NEW(Renderer), [](Renderer* obj){ FB_DELETE(obj); });
		renderer->mImpl->mSelfPtr = renderer;
		sRenderer = renderer;
		sRendererRaw = renderer.get();
		Logger::Log(FB_DEFAULT_LOG_ARG,
			FormatString("(info) renderer: 0x%x", renderer.get()).c_str());
		return renderer;
	}
	return sRenderer.lock();
}

RendererPtr Renderer::Create(const char* renderEngineName){
	if (sRenderer.expired()){
		auto renderer = Create();		
		renderer->PrepareRenderEngine(renderEngineName);
		return renderer;
	}
	else{
		Logger::Log(FB_ERROR_LOG_ARG, "You can create only one renderer!");
		return sRenderer.lock();
	}
}

Renderer& Renderer::GetInstance(){
	return *sRendererRaw;
}

bool Renderer::HasInstance(){
	return sRenderer.lock() != 0;
}

RendererPtr Renderer::GetInstancePtr(){
	return sRenderer.lock();
}

//---------------------------------------------------------------------------
Renderer::Renderer()
	: mImpl(new Impl(this))
	, mDebug(false)
{

}

Renderer::~Renderer(){
	Logger::Log(FB_DEFAULT_LOG_ARG, "Renderer deleted.");
}

bool Renderer::PrepareRenderEngine(const char* rendererPlugInName) {
	return mImpl->PrepareRenderEngine(rendererPlugInName);
}

void Renderer::RegisterThreadIdConsideredMainThread(std::thread::id threadId) {
	mImpl->GetPlatformRenderer().RegisterThreadIdConsideredMainThread(threadId);
}

void Renderer::PrepareQuit() {
	mImpl->PrepareQuit();
}

//-------------------------------------------------------------------
// Canvas & System
//-------------------------------------------------------------------
bool Renderer::InitCanvas(HWindowId id, HWindow window, int width, int height) {
	return mImpl->InitCanvas(id, window, width, height);
}

void Renderer::DeinitCanvas(HWindowId id) {
	mImpl->DeinitCanvas(id);
}

void Renderer::Render() {
	mImpl->Render();
}

//-------------------------------------------------------------------
// Resource Creation
//-------------------------------------------------------------------
RenderTargetPtr Renderer::CreateRenderTarget(const RenderTargetParam& param) {
	return mImpl->CreateRenderTarget(param);
}

void Renderer::KeepRenderTargetInPool(RenderTargetPtr rt) {
	mImpl->KeepRenderTargetInPool(rt);
}

TexturePtr Renderer::CreateTexture(const char* file, const TextureCreationOption& options) {
	return mImpl->CreateTexture(file, options);
}

TexturePtr Renderer::CreateTexture(void* data, int width, int height, PIXEL_FORMAT format, int mipLevels, BUFFER_USAGE usage, int  buffer_cpu_access, int texture_type) {
	return mImpl->CreateTexture(data, width, height, format, mipLevels, usage, buffer_cpu_access, texture_type);
}

void Renderer::ReloadTexture(TexturePtr texture, const char* filepath){
	return mImpl->ReloadTexture(texture, filepath);
}

VertexBufferPtr Renderer::CreateVertexBuffer(void* data, unsigned stride, unsigned numVertices, BUFFER_USAGE usage, BUFFER_CPU_ACCESS_FLAG accessFlag) {
	return mImpl->CreateVertexBuffer(data, stride, numVertices, usage, accessFlag);
}

IndexBufferPtr Renderer::CreateIndexBuffer(void* data, unsigned int numIndices, INDEXBUFFER_FORMAT format) {
	return mImpl->CreateIndexBuffer(data, numIndices, format);
}

ShaderPtr Renderer::CreateShader(const char* filepath, int shaders){
	return mImpl->CreateShader(filepath, shaders, SHADER_DEFINES());
}

ShaderPtr Renderer::CreateShader(const char* filepath, int shaders, const SHADER_DEFINES& defines) {
	return mImpl->CreateShader(filepath, shaders, defines);
}

ShaderPtr Renderer::CreateShader(const StringVector& filepaths, const SHADER_DEFINES& defines) {
	return mImpl->CreateShader(filepaths, defines);
}

bool Renderer::CreateShader(const ShaderPtr& integratedShader, const char* filepath, SHADER_TYPE shader) {
	return mImpl->CreateShader(integratedShader, filepath, shader, SHADER_DEFINES());
}

bool Renderer::CreateShader(const ShaderPtr& integratedShader, const char* filepath, SHADER_TYPE shader, const SHADER_DEFINES& defines) {
	return mImpl->CreateShader(integratedShader, filepath, shader, defines);
}

ShaderPtr Renderer::CompileComputeShader(const char* code, const char* entry, const SHADER_DEFINES& defines) {
	return mImpl->CompileComputeShader(code, entry, defines);
}

MaterialPtr Renderer::CreateMaterial(const char* file) {
	return mImpl->CreateMaterial(file);
}

// use this if you are sure there is instance of the descs.
InputLayoutPtr Renderer::CreateInputLayout(const INPUT_ELEMENT_DESCS& descs, ShaderPtr shader) {
	return mImpl->CreateInputLayout(descs, shader);
}

InputLayoutPtr Renderer::GetInputLayout(DEFAULT_INPUTS::Enum e, ShaderPtr shader) {
	return mImpl->GetInputLayout(e, shader);
}

RasterizerStatePtr Renderer::CreateRasterizerState(const RASTERIZER_DESC& desc) {
	return mImpl->CreateRasterizerState(desc);
}

BlendStatePtr Renderer::CreateBlendState(const BLEND_DESC& desc) {
	return mImpl->CreateBlendState(desc);
}

DepthStencilStatePtr Renderer::CreateDepthStencilState(const DEPTH_STENCIL_DESC& desc) {
	return mImpl->CreateDepthStencilState(desc);
}

SamplerStatePtr Renderer::CreateSamplerState(const SAMPLER_DESC& desc) {
	return mImpl->CreateSamplerState(desc);
}

TextureAtlasPtr Renderer::GetTextureAtlas(const char* path) {
	return mImpl->GetTextureAtlas(path);
}

TextureAtlasRegionPtr Renderer::GetTextureAtlasRegion(const char* path, const char* region) {
	return mImpl->GetTextureAtlasRegion(path, region);
}

TexturePtr Renderer::GetTemporalDepthBuffer(const Vec2I& size, const char* key) {
	return mImpl->GetTemporalDepthBuffer(size, key);
}

void Renderer::Cache(VertexBufferPtr buffer) {
	mImpl->Cache(buffer);
}

//-------------------------------------------------------------------
// Hot reloading
//-------------------------------------------------------------------
//bool Renderer::ReloadShader(ShaderPtr shader) {
//	return mImpl->ReloadShader(shader);
//}
//
//bool Renderer::ReloadTexture(ShaderPtr shader) {
//	return mImpl->ReloadTexture(shader);
//}

//-------------------------------------------------------------------
// Resource Bindings
//-------------------------------------------------------------------
void Renderer::SetRenderTarget(TexturePtr pRenderTargets[], size_t rtViewIndex[], int num, TexturePtr pDepthStencil, size_t dsViewIndex) {
	mImpl->SetRenderTarget(pRenderTargets, rtViewIndex,  num, pDepthStencil, dsViewIndex);
}

void Renderer::UnbindRenderTarget(TexturePtr renderTargetTexture) {
	mImpl->UnbindRenderTarget(renderTargetTexture);
}

void Renderer::SetRenderTargetAtSlot(TexturePtr pRenderTarget, size_t viewIndex, size_t slot) {
	mImpl->SetRenderTargetAtSlot(pRenderTarget, viewIndex, slot);
}

void Renderer::SetDepthTarget(TexturePtr pDepthStencil, size_t dsViewIndex) {
	mImpl->SetDepthTarget(pDepthStencil, dsViewIndex);
}

void Renderer::OverrideDepthTarget(bool override) {
	mImpl->OverrideDepthTarget(override);
}

void Renderer::UnbindColorTargetAndKeep() {
	mImpl->UnbindColorTargetAndKeep();
}

void Renderer::RebindKeptColorTarget() {
	mImpl->RebindKeptColorTarget();
}

const std::vector<TexturePtr>& Renderer::_GetCurrentRTTextures() const {
	return mImpl->mCurrentRTTextures;
}

const std::vector<size_t>& Renderer::_GetCurrentViewIndices() const {
	return mImpl->mCurrentViewIndices;
}

TexturePtr Renderer::_GetCurrentDSTexture() const {
	return mImpl->mCurrentDSTexture;
}

size_t Renderer::_GetCurrentDSViewIndex() const {
	return mImpl->mCurrentDSViewIndex;
}

void Renderer::SetViewports(const Viewport viewports[], int num) {
	mImpl->SetViewports(viewports, num);
}

void Renderer::SetScissorRects(Rect rects[], int num) {
	mImpl->SetScissorRects(rects, num);
}

void Renderer::SetVertexBuffers(unsigned int startSlot, unsigned int numBuffers, VertexBufferPtr pVertexBuffers[], unsigned int strides[], unsigned int offsets[]) {
	mImpl->SetVertexBuffers(startSlot, numBuffers, pVertexBuffers, strides, offsets);
}

void Renderer::SetPrimitiveTopology(PRIMITIVE_TOPOLOGY pt) {
	mImpl->SetPrimitiveTopology(pt);
}

void Renderer::SetTextures(TexturePtr pTextures[], int num, SHADER_TYPE shaderType, int startSlot) {
	mImpl->SetTextures(pTextures, num, shaderType, startSlot);
}

void Renderer::SetSystemTexture(SystemTextures::Enum type, TexturePtr texture) {
	mImpl->SetSystemTexture(type, texture, ~SHADER_TYPE_CS);
}

void Renderer::SetSystemTexture(SystemTextures::Enum type, TexturePtr texture, int shader_type_mask) {
	mImpl->SetSystemTexture(type, texture, shader_type_mask);
}

void Renderer::UnbindTexture(SHADER_TYPE shader, int slot) {
	mImpl->UnbindTexture(shader, slot);
}

void Renderer::UnbindShader(SHADER_TYPE shader){
	mImpl->UnbindShader(shader);
}

void Renderer::UnbindInputLayout(){
	mImpl->UnbindInputLayout();
}

void Renderer::UnbindVertexBuffers(){
	mImpl->UnbindVertexBuffers();
}

// pre defined
void Renderer::BindDepthTexture(bool set) {
	mImpl->BindDepthTexture(set);
}

void Renderer::SetDepthWriteShader() {
	mImpl->SetDepthWriteShader();
}

void Renderer::SetDepthWriteShaderCloud() {
	mImpl->SetDepthWriteShaderCloud();
}

void Renderer::SetPositionInputLayout() {
	mImpl->SetPositionInputLayout();
}

void Renderer::BindSystemTexture(SystemTextures::Enum systemTexture) {
	mImpl->BindSystemTexture(systemTexture);
}

void Renderer::SetSystemTextureBindings(SystemTextures::Enum type, const TextureBindings& bindings) {
	mImpl->SetSystemTextureBindings(type, bindings);
}

const TextureBindings& Renderer::GetSystemTextureBindings(SystemTextures::Enum type) const {
	return mImpl->GetSystemTextureBindings(type);
}

const Mat44& Renderer::GetScreenToNDCMatric() {
	return mImpl->GetScreenToNDCMatric();
}

//-------------------------------------------------------------------
// Device RenderStates
//-------------------------------------------------------------------
void Renderer::RestoreRenderStates() {
	mImpl->RestoreRenderStates();
}

void Renderer::RestoreRasterizerState() {
	mImpl->RestoreRasterizerState();
}

void Renderer::RestoreBlendState() {
	mImpl->RestoreBlendState();
}

void Renderer::RestoreDepthStencilState() {
	mImpl->RestoreDepthStencilState();
}

// sampler
void Renderer::SetSamplerState(int ResourceTypes_SamplerStates, SHADER_TYPE shader, int slot) {
	mImpl->SetSamplerState(ResourceTypes_SamplerStates, shader, slot);
}

//-------------------------------------------------------------------
// GPU constants
//-------------------------------------------------------------------
void Renderer::UpdateObjectConstantsBuffer(const void* pData){
	mImpl->UpdateObjectConstantsBuffer(pData, false);
}

void Renderer::UpdateObjectConstantsBuffer(const void* pData, bool record) {
	mImpl->UpdateObjectConstantsBuffer(pData, record);
}

const OBJECT_CONSTANTS* Renderer::GetObjectConstants() const {
	return &mImpl->mObjectConstants;
}

void Renderer::UpdatePointLightConstantsBuffer(const void* pData) {
	mImpl->UpdatePointLightConstantsBuffer(pData);
}

void Renderer::UpdateFrameConstantsBuffer() {
	mImpl->UpdateFrameConstantsBuffer();
}

void Renderer::UpdateMaterialConstantsBuffer(const void* pData) {
	mImpl->UpdateMaterialConstantsBuffer(pData);
}

void Renderer::UpdateCameraConstantsBuffer() {
	mImpl->UpdateCameraConstantsBuffer();
}

void Renderer::UpdateCameraConstantsBuffer(const void* manualData) {
	mImpl->UpdateCameraConstantsBuffer(manualData);
}

void Renderer::UpdateRenderTargetConstantsBuffer() {
	mImpl->UpdateRenderTargetConstantsBuffer();
}

void Renderer::UpdateSceneConstantsBuffer() {
	mImpl->UpdateSceneConstantsBuffer();
}

void Renderer::UpdateRareConstantsBuffer() {
	mImpl->UpdateRareConstantsBuffer();
}

void Renderer::UpdateRadConstantsBuffer(const void* pData) {
	mImpl->UpdateRadConstantsBuffer(pData);
}

void Renderer::UpdateShadowConstantsBuffer(const void* pData){
	mImpl->UpdateShadowConstantsBuffer(pData);
}

void* Renderer::MapShaderConstantsBuffer() {
	return mImpl->MapShaderConstantsBuffer();
}

void Renderer::UnmapShaderConstantsBuffer() {
	mImpl->UnmapShaderConstantsBuffer();
}

void* Renderer::MapBigBuffer() {
	return mImpl->MapBigBuffer();
}

void Renderer::UnmapBigBuffer() {
	mImpl->UnmapBigBuffer();
}

//-------------------------------------------------------------------
// GPU Manipulation
//-------------------------------------------------------------------
void Renderer::SetClearColor(HWindowId id, const Color& color) {
	mImpl->SetClearColor(id, color);
}

void Renderer::SetClearDepthStencil(HWindowId id, Real z, UINT8 stencil) {
	mImpl->SetClearDepthStencil(id, z, stencil);
}

void Renderer::Clear(Real r, Real g, Real b, Real a, Real z, UINT8 stencil) {
	mImpl->Clear(r, g, b, a, z, stencil);
}

void Renderer::Clear(Real r, Real g, Real b, Real a) {
	mImpl->Clear(r, g, b, a);
}

void Renderer::ClearDepthStencil(Real z, UINT8 stencil) {
	mImpl->ClearDepthStencil(z, stencil);
}

// Avoid to use
void Renderer::ClearState() {
	mImpl->ClearState();
}

void Renderer::BeginEvent(const char* name) {
	mImpl->BeginEvent(name);
}

void Renderer::EndEvent() {
	mImpl->EndEvent();
}

void Renderer::TakeScreenshot() {
	mImpl->TakeScreenshot();
}

void Renderer::TakeScreenshot(int width, int height) {
	mImpl->TakeScreenshot(width, height);
}

void Renderer::ChangeFullscreenMode(int mode){
	mImpl->ChangeFullscreenMode(mode);
}

void Renderer::OnWindowSizeChanged(HWindow window, const Vec2I& clientSize){
	mImpl->OnWindowSizeChanged(window, clientSize);
}

void Renderer::ChangeResolution(const Vec2I& resol){
	mImpl->ChangeResolution(GetMainWindowHandle(), resol);
}

void Renderer::ChangeResolution(HWindow window, const Vec2I& resol){
	mImpl->ChangeResolution(window, resol);
}

void Renderer::ChangeWindowSizeAndResolution(const Vec2I& resol){
	mImpl->ChangeWindowSizeAndResolution(GetMainWindowHandle(), resol);
}

void Renderer::ChangeWindowSizeAndResolution(HWindow window, const Vec2I& resol){
	mImpl->ChangeWindowSizeAndResolution(window, resol);
}

//-------------------------------------------------------------------
// FBRenderer State
//-------------------------------------------------------------------
ResourceProviderPtr Renderer::GetResourceProvider() const{
	return mImpl->GetResourceProvider();
}

void Renderer::SetResourceProvider(ResourceProviderPtr provider){
	mImpl->SetResourceProvider(provider);
}

RenderTargetPtr Renderer::GetMainRenderTarget() const {
	return mImpl->GetMainRenderTarget();
}

unsigned Renderer::GetMainRenderTargetId() const{
	return mImpl->GetMainRenderTargetId();
}

IScenePtr Renderer::GetMainScene() const {
	return mImpl->GetMainScene();
}

const Vec2I& Renderer::GetMainRenderTargetSize() const {
	return mImpl->GetMainRenderTargetSize();
}

void Renderer::SetCurrentRenderTarget(RenderTargetPtr renderTarget) {
	mImpl->SetCurrentRenderTarget(renderTarget);
}

RenderTargetPtr Renderer::GetCurrentRenderTarget() const {
	return mImpl->GetCurrentRenderTarget();
}

void Renderer::SetCurrentScene(IScenePtr scene){
	mImpl->SetCurrentScene(scene);
}

bool Renderer::IsMainRenderTarget() const {
	return mImpl->IsMainRenderTarget();
}

const Vec2I& Renderer::GetRenderTargetSize(HWindowId id) const {
	return mImpl->GetRenderTargetSize(id);
}

const Vec2I& Renderer::GetRenderTargetSize(HWindow hwnd) const {
	return mImpl->GetRenderTargetSize(hwnd);
}

void Renderer::SetDirectionalLightInfo(int idx, const DirectionalLightInfo& info){
	mImpl->SetDirectionalLightInfo(idx, info);
}

const RENDERER_FRAME_PROFILER& Renderer::GetFrameProfiler() const {
	return mImpl->GetFrameProfiler();
}

void Renderer::DisplayFrameProfiler(){
	return mImpl->DisplayFrameProfiler();
}

void Renderer::ReloadFonts(){
	mImpl->ReloadFonts();
}

inline FontPtr Renderer::GetFont(int fontSize) const {
	return mImpl->GetFont(fontSize);
}

FontPtr Renderer::GetFontWithHeight(Real height) const{
	return mImpl->GetFontWithHeight(height);
}

const INPUT_ELEMENT_DESCS& Renderer::GetInputElementDesc(DEFAULT_INPUTS::Enum e) {
	return mImpl->GetInputElementDesc(e);
}

void Renderer::SetEnvironmentTexture(TexturePtr pTexture) {
	mImpl->SetEnvironmentTexture(pTexture);
}

void Renderer::SetEnvironmentTextureOverride(TexturePtr texture) {
	mImpl->SetEnvironmentTextureOverride(texture);
}

void Renderer::SetDebugRenderTarget(unsigned idx, const char* textureName) {
	mImpl->SetDebugRenderTarget(idx, textureName);
}

void Renderer::SetFadeAlpha(Real alpha) {
	mImpl->SetFadeAlpha(alpha);
}

bool Renderer::GetSampleOffsets_Bloom(DWORD dwTexSize,
	float afTexCoordOffset[15],
	Vec4* avColorWeight,
	float fDeviation, float fMultiplier){
	return mImpl->GetSampleOffsets_Bloom(dwTexSize, afTexCoordOffset, avColorWeight, fDeviation, fMultiplier);
}

void Renderer::GetSampleOffsets_GaussianBlur5x5(DWORD texWidth, DWORD texHeight, Vec4f** avTexCoordOffset, Vec4f** avSampleWeight, float fMultiplier) {
	mImpl->GetSampleOffsets_GaussianBlur5x5(texWidth, texHeight, avTexCoordOffset, avSampleWeight, fMultiplier);
}

void Renderer::GetSampleOffsets_DownScale2x2(DWORD texWidth, DWORD texHeight, Vec4f* avSampleOffsets) {
	mImpl->GetSampleOffsets_DownScale2x2(texWidth, texHeight, avSampleOffsets);
}

bool Renderer::IsLuminanceOnCpu() const {
	return mImpl->IsLuminanceOnCpu();
}

void Renderer::SetLockDepthStencilState(bool lock){
	mImpl->SetLockDepthStencilState(lock);
}

void Renderer::SetLockBlendState(bool lock){
	mImpl->SetLockBlendState(lock);
}

void Renderer::SetLockRasterizerState(bool lock) {
	mImpl->SetLockRasterizerState(lock);
}

void Renderer::SetFontTextureAtlas(const char* path){
	mImpl->SetFontTextureAtlas(path);
}

void Renderer::ClearFontScissor(){
	mImpl->ClearFontScissor();
}

void Renderer::UpdateLightFrustum(){
	mImpl->UpdateLightFrustum();
}

void Renderer::RenderShadows(){
	mImpl->RenderShadows();
}

void Renderer::SetBindShadowMap(bool bind){
	mImpl->SetBindShadowMap(bind);
}

void Renderer::OnShadowOptionChanged(){
	mImpl->OnShadowOptionChanged();
}

TexturePtr Renderer::KeepTextureReference(const char* filepath, 
	const TextureCreationOption& option)
{
	return mImpl->KeepTextureReference(filepath, option);
}

void Renderer::PushRenderStates()
{
	mImpl->PushRenderStates();
}
void Renderer::PopRenderStates()
{
	mImpl->PopRenderStates();
}

void Renderer::BindIncrementalStencilState(int stencilRef) {
	mImpl->BindIncrementalStencilState(stencilRef);
}

void Renderer::ReportResolutionChange(HWindowId handleId, HWindow window) {
	mImpl->ReportResolutionChange(handleId, window);
}

//-------------------------------------------------------------------
// Queries
//-------------------------------------------------------------------
unsigned Renderer::GetMultiSampleCount() const {
	return mImpl->GetMultiSampleCount();
}

bool Renderer::GetFilmicToneMapping() const{
	return mImpl->GetFilmicToneMapping();
}

void Renderer::SetFilmicToneMapping(bool use){
	mImpl->SetFilmicToneMapping(use);
}

bool Renderer::GetLuminanaceOnCPU() const{
	return mImpl->GetLuminanaceOnCPU();
}

void Renderer::SetLuminanaceOnCPU(bool oncpu){
	mImpl->SetLuminanaceOnCPU(oncpu);
}

RenderTargetPtr Renderer::GetRenderTarget(HWindowId id) const {
	return mImpl->GetRenderTarget(id);
}

CameraPtr Renderer::SetCamera(CameraPtr pCamera) {
	return mImpl->SetCamera(pCamera);
}

CameraPtr Renderer::GetCamera() const {
	return mImpl->GetCamera();
}

CameraPtr Renderer::GetMainCamera() const {
	return mImpl->GetMainCamera();
}

ICameraPtr Renderer::GetICamera() const {
	return mImpl->GetCamera();
}
ICameraPtr Renderer::GetMainICamera() const {
	return mImpl->GetMainCamera();
}

void Renderer::SetActiveOverrideCamera(bool active){
	mImpl->SetActiveOverrideCamera(active);
}

HWindow Renderer::GetMainWindowHandle() const {
	return mImpl->GetMainWindowHandle();
}

HWindowId Renderer::GetMainWindowHandleId(){
	return mImpl->GetMainWindowHandleId();
}

HWindow Renderer::GetWindowHandle(HWindowId windowId){
	return mImpl->GetWindowHandle(windowId);
}

HWindowId Renderer::GetWindowHandleId(HWindow window){
	return mImpl->GetWindowHandleId(window);
}

Vec2I Renderer::ToSreenPos(HWindowId id, const Vec3& ndcPos) const {
	return mImpl->ToSreenPos(id, ndcPos);
}

Vec2 Renderer::ToNdcPos(HWindowId id, const Vec2I& screenPos) const {
	return mImpl->ToNdcPos(id, screenPos);
}

unsigned Renderer::GetNumLoadingTexture() const {
	return mImpl->GetNumLoadingTexture();
}

Vec2I Renderer::FindClosestSize(HWindowId id, const Vec2I& input) {
	return mImpl->FindClosestSize(id, input);
}

bool Renderer::GetResolutionList(unsigned& outNum, Vec2I* list) {
	return mImpl->GetResolutionList(outNum, list);
}

RendererOptionsPtr Renderer::GetRendererOptions() const {
	return mImpl->GetRendererOptions();
}

void Renderer::SetMainWindowStyle(unsigned style){
	mImpl->SetMainWindowStyle(style);
}

bool Renderer::IsFullscreen() const {
	return mImpl->IsFullscreen();
}

GraphicDeviceInfo Renderer::GetDeviceInfo() const {
	return mImpl->GetDeviceInfo();
}

std::string Renderer::DeviceInfoToString(const GraphicDeviceInfo& info) {

	return FormatString("VendorId: 0x%x\n\
DeviceId: 0x%x\n\
SubSysId: 0x%x\n\
Revision: %u\n\
DedicatedVideoMemory: %u mb\n\
DedicatedSystemMemory: %u mb\n\
SharedSystemMemory: %u mb\n", 
info.VendorId, info.DeviceId, info.SubSysId, info.Revision, info.DedicatedVideoMemory/ 1048576, info.DedicatedSystemMemory / 1048576, info.SharedSystemMemory / 1048576);

}

//-------------------------------------------------------------------
// Drawing
//-------------------------------------------------------------------
void Renderer::DrawIndexed(unsigned indexCount, unsigned startIndexLocation, unsigned startVertexLocation) {
	mImpl->DrawIndexed(indexCount, startIndexLocation, startVertexLocation);
}

void Renderer::Draw(unsigned int vertexCount, unsigned int startVertexLocation) {
	mImpl->Draw(vertexCount, startVertexLocation);
}

void Renderer::DrawFullscreenQuad(ShaderPtr pixelShader, bool farside) {
	mImpl->DrawFullscreenQuad(pixelShader, farside);
}

void Renderer::DrawTriangle(const Vec3& a, const Vec3& b, const Vec3& c, const Vec4& color, MaterialPtr mat) {
	mImpl->DrawTriangle(a, b, c, color, mat);
}

void Renderer::DrawQuad(const Vec2I& pos, const Vec2I& size, const Color& color){
	mImpl->DrawQuad(pos, size, color, true);
}

void Renderer::DrawQuad(const Vec2I& pos, const Vec2I& size, const Color& color, bool updateRs) {
	mImpl->DrawQuad(pos, size, color, updateRs);
}

void Renderer::DrawFrustum(const Frustum& frustum) {
	mImpl->DrawFrustum(frustum);
}

void Renderer::DrawLine(const Vec3& start, const Vec3& end,
	const Color& color0, const Color& color1)
{
	mImpl->DrawLine(start, end, color0, color1);
}

void Renderer::DrawLine(const Vec2I& start, const Vec2I& end,
	const Color& color0, const Color& color1) 
{
	mImpl->DrawLine(start, end, color0, color1);
}

void Renderer::DrawBox(const Vec3::Array& corners, const Color& color) {
	mImpl->DrawBox(corners, color);
}

void Renderer::DrawQuadLine(const Vec2I& pos, const Vec2I& size, const Color& color) {
	mImpl->DrawQuadLine(pos, size, color);
}

void Renderer::DrawPoints(const Vec3::Array& points, const Color& color){
	mImpl->DrawPoints(points, color);
}

void Renderer::DrawQuadWithTexture(const Vec2I& pos, const Vec2I& size, const Color& color, TexturePtr texture, MaterialPtr materialOverride) {
	mImpl->DrawQuadWithTexture(pos, size, color, texture, materialOverride);
}

void Renderer::DrawQuadWithTextureUV(const Vec2I& pos, const Vec2I& size, const Vec2& uvStart, const Vec2& uvEnd, const Color& color, TexturePtr texture, MaterialPtr materialOverride) {
	mImpl->DrawQuadWithTextureUV(pos, size, uvStart, uvEnd, color, texture, materialOverride);
}

void Renderer::DrawBillboardWorldQuad(const Vec3& pos, const Vec2& size, const Vec2& offset, DWORD color, MaterialPtr pMat) {
	mImpl->DrawBillboardWorldQuad(pos, size, offset, color, pMat);
}

void Renderer::DrawCurrentAxis() {
	mImpl->DrawCurrentAxis();
}

void Renderer::Draw3DTextNow(const Vec3& worldpos, const char* text, const Color& color, Real size) {
	mImpl->Draw3DTextNow(worldpos, text, color, size);
}

void Renderer::QueueDrawText(const Vec2I& pos, WCHAR* text, const Color& color){
	mImpl->QueueDrawText(pos, text, color, defaultFontSize);
}

void Renderer::QueueDrawText(const Vec2I& pos, WCHAR* text, const Color& color, Real size) {
	mImpl->QueueDrawText(pos, text, color, size);
}

void Renderer::QueueDrawText(const Vec2I& pos, const char* text, const Color& color){
	mImpl->QueueDrawText(pos, text, color, defaultFontSize);
}

void Renderer::QueueDrawText(const Vec2I& pos, const char* text, const Color& color, Real size) {
	mImpl->QueueDrawText(pos, text, color, size);
}

void Renderer::QueueDraw3DText(const Vec3& worldpos, WCHAR* text, const Color& color){
	mImpl->QueueDraw3DText(worldpos, text, color, defaultFontSize);
}

void Renderer::QueueDraw3DText(const Vec3& worldpos, WCHAR* text, const Color& color, Real size) {
	mImpl->QueueDraw3DText(worldpos, text, color, size);
}

void Renderer::QueueDraw3DText(const Vec3& worldpos, const char* text, const Color& color){
	mImpl->QueueDraw3DText(worldpos, text, color, defaultFontSize);
}

void Renderer::QueueDraw3DText(const Vec3& worldpos, const char* text, const Color& color, Real size) {
	mImpl->QueueDraw3DText(worldpos, text, color, size);
}

void Renderer::QueueDrawTextForDuration(Real secs, const Vec2I& pos, WCHAR* text, const Color& color){
	mImpl->QueueDrawTextForDuration(secs, pos, text, color, defaultFontSize);
}

void Renderer::QueueDrawTextForDuration(Real secs, const Vec2I& pos, WCHAR* text, const Color& color, Real size) {
	mImpl->QueueDrawTextForDuration(secs, pos, text, color, size);
}

void Renderer::QueueDrawTextForDuration(Real secs, const Vec2I& pos, const char* text, const Color& color){
	mImpl->QueueDrawTextForDuration(secs, pos, text, color, defaultFontSize);
}

void Renderer::QueueDrawTextForDuration(Real secs, const Vec2I& pos, const char* text, const Color& color, Real size) {
	mImpl->QueueDrawTextForDuration(secs, pos, text, color, size);
}

void Renderer::ClearDurationTexts() {
	mImpl->ClearDurationTexts();
}

void Renderer::QueueDrawLine(const Vec3& start, const Vec3& end, const Color& color0, const Color& color1) {
	mImpl->QueueDrawLine(start, end, color0, color1);
}

void Renderer::QueueDrawLine(const Vec2I& start, const Vec2I& end, const Color& color0, const Color& color1) {
	mImpl->QueueDrawLine(start, end, color0, color1);
}

void Renderer::QueueDrawLineBeforeAlphaPass(const Vec3& start, const Vec3& end, const Color& color0, const Color& color1) {
	mImpl->QueueDrawLineBeforeAlphaPass(start, end, color0, color1);
}

void Renderer::QueueDrawQuad(const Vec2I& pos, const Vec2I& size, const Color& color) {
	mImpl->QueueDrawQuad(pos, size, color);
}

void Renderer::QueueDrawQuadLine(const Vec2I& pos, const Vec2I& size, const Color& color) {
	mImpl->QueueDrawQuadLine(pos, size, color);
}

void Renderer::QueueDrawAABB(const AABB& aabb, const Transformation& transform, 
	const Color& color){
	mImpl->QueueDrawAABB(aabb, transform, color);
}

//-------------------------------------------------------------------
// Internal
//-------------------------------------------------------------------
void Renderer::RenderDebugHud() {
	mImpl->RenderDebugHud();
}

//-------------------------------------------------------------------
// ISceneObserver
//-------------------------------------------------------------------
void Renderer::OnAfterMakeVisibleSet(IScene* scene) {
	mImpl->OnAfterMakeVisibleSet(scene);
}

void Renderer::OnBeforeRenderingOpaques(IScene* scene, const RenderParam& renderParam, RenderParamOut* renderParamOut) {
	mImpl->OnBeforeRenderingOpaques(scene, renderParam, renderParamOut);
}

void Renderer::OnBeforeRenderingOpaquesRenderStates(IScene* scene, const RenderParam& renderParam, RenderParamOut* renderParamOut) {

}
void Renderer::OnAfterRenderingOpaquesRenderStates(IScene* scene, const RenderParam& renderParam, RenderParamOut* renderParamOut) {

}

void Renderer::OnBeforeRenderingTransparents(IScene* scene, const RenderParam& renderParam, RenderParamOut* renderParamOut) {
	mImpl->OnBeforeRenderingTransparents(scene, renderParam, renderParamOut);
}

//-------------------------------------------------------------------
// ISceneObserver
//-------------------------------------------------------------------
void Renderer::ConsumeInput(IInputInjectorPtr injector) {
	mImpl->ConsumeInput(injector);
}

void Renderer::OnChangeDetected(){

}

bool Renderer::OnFileChanged(const char* watchDir, const char* file, const char* combinedPath, const char* ext){
	return mImpl->OnFileChanged(watchDir, file, combinedPath, ext);
}
