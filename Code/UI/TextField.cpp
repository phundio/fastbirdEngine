#include <UI/StdAfx.h>
#include "TextField.h"
#include <UI/KeyboardCursor.h>
#include <Engine/GlobalEnv.h>
#include <UI/IUIManager.h>

namespace fastbird
{
const float TextField::LEFT_GAP = 0.001f;
const float TextField::BOTTOM_GAP = 0.004f;

TextField::TextField()
	: WinBase()
	, mCursorPos(0)
	, mPasswd(false)
{
	mUIObject = IUIObject::CreateUIObject();
	mUIObject->SetMaterial("es/Materials/UITextField.material");
	mUIObject->mOwnerUI = this;
	mUIObject->mTypeString = ToString(GetType());
	mUIObject->SetTextColor(mTextColor);
}

TextField::~TextField()
{
}

void TextField::GatherVisit(std::vector<IUIObject*>& v)
{
	v.push_back(mUIObject);	
	if (GetFocus())
		v.push_back(KeyboardCursor::GetKeyboardCursor().GetUIObject());
}

void TextField::OnInputFromHandler(IMouse* mouse, IKeyboard* keyboard)
{
	__super::OnInputFromHandler(mouse, keyboard);

	if (!mVisible || !GetFocus(false))
		return;

	if (keyboard->IsValid())
	{
		char c = (char)keyboard->GetChar();
		keyboard->Invalidate();

		if (c!=0)
		{
			switch(c)
			{
			case VK_BACK:
				{
					if (!mTextw.empty())
					{
						mTextw.pop_back();
						MoveCursor(-1);
					}
				}
				break;
			case VK_TAB:
				{
					if (keyboard->IsKeyDown(VK_SHIFT))
					{
						if (mPrev)
							IUIManager::GetUIManager().SetFocusUI(mPrev);
					}
					else
					{
						if (mNext)
							IUIManager::GetUIManager().SetFocusUI(mNext);
					}
				}
				break;
			default:
				{
					mTextw.push_back(AnsiToWide(&c, 1)[0]);
					MoveCursor(1);
				}
				break;
			}
			if (!mPasswd)
			{
				mUIObject->SetText(mTextw.c_str());
			}
			else
			{
				std::wstring asterisks(mTextw.size(), L'*');
				mUIObject->SetText(asterisks.c_str());
			}
		}
	}

	if (mouse->IsValid() && mMouseIn)
	{
		mouse->Invalidate();
	}
}

void TextField::SetText(const wchar_t* szText)
{
	mTextw = szText;
	if (mUIObject)
	{
		if (!mPasswd)
		{
			mUIObject->SetText(szText);
		}
		else
		{
			std::wstring asterisks(mTextw.size(), L'*');
			mUIObject->SetText(asterisks.c_str());
		}
	}
}

void TextField::OnPosChanged()
{
	__super::OnPosChanged();
	mUIObject->SetTextStartNPos(Vec2(mWNPos.x, mWNPos.y + mWNSize.y - BOTTOM_GAP));
}

void TextField::OnSizeChanged()
{
	__super::OnSizeChanged();
	mUIObject->SetTextStartNPos(Vec2(mWNPos.x, mWNPos.y + mWNSize.y - BOTTOM_GAP));
}


void TextField::OnFocusLost()
{
}

void TextField::OnFocusGain()
{
	gEnv->pRenderer->GetFont()->SetHeight(mTextSize);
	float width;
	width = gEnv->pRenderer->GetFont()->GetTextWidth((const char*)AnsiToWide("A", 1), 2);
	gEnv->pRenderer->GetFont()->SetBackToOrigHeight();

	Vec2 size = ConvertToNormalized(Vec2I((int)width, 2));
	KeyboardCursor::GetKeyboardCursor().SetNSize(size);

	int chrs = mTextw.size();
	MoveCursor(chrs - mCursorPos);	
}

void TextField::MoveCursor(int move)
{
	if (!mUIObject)
		return;
	mCursorPos += move;
	if (mCursorPos<0)
		mCursorPos = 0;

	float xpos = 0.f;
	if (!mTextw.empty())
	{
		gEnv->pRenderer->GetFont()->SetHeight(mTextSize);
		float width;
		if (mPasswd)
		{
			std::wstring asterisks(mTextw.size(), L'*');
			width = gEnv->pRenderer->GetFont()->GetTextWidth(
				(const char*)asterisks.c_str(), asterisks.size()*2);
		}
		else
		{
			width = gEnv->pRenderer->GetFont()->GetTextWidth(
				(const char*)mTextw.c_str(), mTextw.size()*2);
		}
		xpos = ConvertToNormalized(Vec2I((int)width, (int)width)).x;
		gEnv->pRenderer->GetFont()->SetBackToOrigHeight();
	}

	KeyboardCursor::GetKeyboardCursor().SetNPos(
		Vec2(mWNPos.x + LEFT_GAP + xpos,
		mWNPos.y + mWNSize.y - BOTTOM_GAP));
	
}

void TextField::SetPasswd(bool passwd)
{
	mPasswd = passwd;
}

}