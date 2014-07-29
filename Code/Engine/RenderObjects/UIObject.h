#pragma once
#include <Engine/IUIObject.h>
#include <Engine/Renderer/Shaders/Constants.h>
namespace fastbird
{
	class UIObject : public IUIObject
	{
	public:
		UIObject();
		virtual ~UIObject();

		//-------------------------------------------------------------------------
		// IUIObject interfaces
		//-------------------------------------------------------------------------
		virtual void SetVertices(const Vec3* ndcPoints, int num,
			const DWORD* colors = 0, const Vec2* texcoords = 0);
		virtual void SetTexCoord(Vec2 coord[], DWORD num);
		virtual void SetColors(DWORD colors[], DWORD num);

		virtual void SetNSize(const Vec2& nsize); // in normalized space (0.0f ~ 1.0f)
		virtual void SetNPos(const Vec2& npos);
		virtual void SetNPosOffset(const Vec2& nposOffset);
		virtual void SetAlpha(float alpha);
		virtual void SetText(const wchar_t* s);
		virtual void SetTextStartNPos(const Vec2& npos);
		virtual void SetTextColor(const Color& c);
		virtual void SetTextSize(float size);
		virtual const RECT& GetRegion() const;
		virtual void SetDebugString(const char* string);
		virtual void SetNoDrawBackground(bool flag);
		virtual void SetUseScissor(bool use, const RECT& rect);

		//-------------------------------------------------------------------------
		// IObject interfaces
		//-------------------------------------------------------------------------
		virtual void PreRender();
		virtual void Render();		
		virtual void PostRender();

		virtual void SetMaterial(const char* name);
		virtual IMaterial* GetMaterial() const { return mMaterial; }

		//-------------------------------------------------------------------------
		// Own
		//-------------------------------------------------------------------------
		void UpdateRegion();

	private:
		void PrepareVBs();

	private:
		static SmartPtr<IRasterizerState> mRasterizerStateShared;
		SmartPtr<IMaterial> mMaterial;
		SmartPtr<IVertexBuffer> mVertexBuffer;
		OBJECT_CONSTANTS mObjectConstants; // for ndc space x, y;
		
		std::wstring mText;
		Vec2 mNDCPos; // ndc pos
		Vec2 mNDCOffset;
		Vec2 mNOffset;
		Vec2 mTextNPos;
		Color mTextColor;
		float mTextSize;
		float mAlpha;
		RECT mRegion;
		Vec2 mNSize; // normalized (0~1)
		Vec2 mNPos;
		std::string mDebugString;
		bool mNoDrawBackground;
		std::vector<Vec3> mPositions;
		std::vector<DWORD> mColors;
		std::vector<Vec2> mTexcoords;
		SmartPtr<IVertexBuffer> mVBColor;
		SmartPtr<IVertexBuffer> mVBTexCoord;
		bool mDirty;
		bool mScissor;
		RECT mScissorRect;
		bool mOut;
	};
}