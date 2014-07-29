#pragma once

#include <Engine/Foundation/Object.h>
#include <CommonLib/Math/Vec2I.h>
#include <CommonLib/Math/Vec3.h>
#include <CommonLib/Color.h>
#include <Engine/Renderer/Shaders/Constants.h>
#undef DrawText

namespace fastbird
{

class IShader;
class DebugHud : public Object
{
public:
	DebugHud();
	virtual ~DebugHud();

	struct TextData
	{
		TextData(const Vec2I& pos, WCHAR* text, const Color& color, float secs)
			: mPos(pos), mText(text), mColor(color), mSecs(secs)
		{
		}

		Vec2I mPos;
		std::wstring mText;
		Color mColor;
		float mSecs;
	};

	struct Line
	{
		Vec3 mStart;
		unsigned mColor;
			
		Vec3 mEnd;
		unsigned mColore;
	};

	//--------------------------------------------------------------------
	// IObject
	//--------------------------------------------------------------------
	virtual void Render();
	virtual void PreRender();
	virtual void PostRender();

	//--------------------------------------------------------------------
	// Own
	//--------------------------------------------------------------------
	void DrawTextForDuration(float secs, const Vec2I& pos, WCHAR* text, 
		const Color& color);
	void DrawText(const Vec2I& pos, WCHAR* text, const Color& color);
	// if wolrdspace is false, it's in the screenspace 0~width, 0~height
	void DrawLine(const Vec3& start, const Vec3& end, const Color& color0,
		const Color& color1);
	void DrawLine(const Vec2I& start, const Vec2I& end, const Color& color0, 
		const Color& color1);

private:

	struct LINE_VERTEX
	{
		LINE_VERTEX(const Vec3& pos, unsigned c)
			: v(pos), color(c)
		{
		}
		Vec3 v;
		unsigned color;
	};
	static const unsigned LINE_STRIDE;
	static const unsigned MAX_LINE_VERTEX;
	typedef std::queue<TextData> MessageQueue;
	MessageQueue mTexts;
	typedef std::list<TextData> MessageBuffer;
	MessageBuffer mTextsForDur;
	std::vector<Line> mScreenLines;
	std::vector<Line> mWorldLines;
	OBJECT_CONSTANTS mObjectConstants;
	OBJECT_CONSTANTS mObjectConstants_WorldLine;

	SmartPtr<IShader> mLineShader;
	SmartPtr<IInputLayout> mInputLayout;
	SmartPtr<IVertexBuffer> mVertexBuffer;
};

}