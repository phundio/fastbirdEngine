#include <UI/StdAfx.h>
#include <UI/TextField.h>
#include <UI/KeyboardCursor.h>
#include <UI/IUIManager.h>
#include <UI/ImageBox.h>
#include <UI/PropertyList.h>
#include <UI/IUIEditor.h>
#include <Engine/TextManipulator.h>


namespace fastbird
{
const float TextField::LEFT_GAP = 0.001f;

TextField::TextField()
	: WinBase()
	, mPasswd(false)
{
	mUIObject = gFBEnv->pEngine->CreateUIObject(false, GetRenderTargetSize());
	mUIObject->SetMaterial("es/Materials/UITextField.material");
	mUIObject->mOwnerUI = this;
	mUIObject->mTypeString = ComponentType::ConvertToString(GetType());
	mUIObject->SetTextColor(mTextColor);
}

TextField::~TextField()
{
}

void TextField::GatherVisit(std::vector<IUIObject*>& v)
{
	v.push_back(mUIObject);	
	if (gFBUIManager->GetKeyboardFocusUI() == this)
		v.push_back(KeyboardCursor::GetKeyboardCursor().GetUIObject());

	__super::GatherVisit(v);
}

bool TextField::OnInputFromHandler(IMouse* mouse, IKeyboard* keyboard)
{
	if (!mEnable)
		return false;

	bool mouseIn = __super::OnInputFromHandler(mouse, keyboard);

	if (!mVisibility.IsVisible() || !GetFocus(false))
		return mouseIn;

	if (keyboard->IsValid()) {
		if (keyboard->GetChar() == VK_TAB)	{
			if (gFBUIManager->GetKeyboardFocusUI() == this)
			{
				keyboard->PopChar();
				auto propertyList = IsInPropertyList();
				if (propertyList)
				{
					propertyList->MoveToNextLine();
				}
				else
				{
					auto root = GetRootWnd();
					if (root)
					{
						root->TabPressed();
					}
				}
			}
		}
		else if (keyboard->GetChar() == VK_RETURN)
		{
			bool succ = false;
			if (OnEvent(EVENT_ENTER))
			{
				succ = true;
			}
			else
			{
				auto parent = GetParent();
				int count = 2;
				while (count-- && parent)
				{
					auto eventHandler = dynamic_cast<IEventHandler*>(parent);
					if (eventHandler)
					{
						if (eventHandler->OnEvent(EVENT_ENTER)){
							succ = true;
							break;
						}
					}
					parent = parent->GetParent();
				}
			}
			if (succ){
				keyboard->PopChar();
				TriggerRedraw();
				keyboard->Invalidate();
			}
		}
		else{
			gFBUIManager->GetTextManipulator()->OnInput(mouse, keyboard);
		}
	}
	
	return mouseIn;
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
	CalcTextWidth();

	AlignText();
	TriggerRedraw();
}

void TextField::OnFocusGain()
{
	KeyboardCursor::GetKeyboardCursor().SetHwndId(GetHwndId());
	gFBUIManager->DirtyRenderList(mHwndId);

	auto mani = gFBUIManager->GetTextManipulator();
	mani->AddListener(this);
	mani->SetText(&mTextw);

	auto mouse = gFBEnv->pEngine->GetMouse();
	long x, y;
	mouse->GetPos(x, y);
	Vec2I cursorPos(x, y);
	PixelToLocalPixel(cursorPos);
	cursorPos.x -= mTextGap.x;

	auto font = gFBEnv->pRenderer->GetFont();
	if (font)
	{
		font->SetHeight(mTextSize);
		float length = 0.f;
		for (int i = 0; i < (int)mTextw.size(); i++)
		{
			float halfLength = font->GetTextWidth((const char*)&mTextw[i], 2) *.5f;
			length += halfLength;
			if (cursorPos.x < length)
			{
				mani->SetCursorPos(i);
				break;
			}
			length += halfLength;
		}
		font->SetBackToOrigHeight();
	}

	auto propertyList = IsInPropertyList();
	if (propertyList)
	{
		ListItem* listItem = (ListItem*)mParent;
		propertyList->SetFocusRow(listItem->GetRowIndex());
	}

	KeyboardCursor::GetKeyboardCursor().SetScissorRegion(GetScissorRegion());
}

void TextField::OnFocusLost()
{
	auto mani = gFBUIManager->GetTextManipulator();
	auto propertyList = IsInPropertyList();
	if (propertyList)
	{
		assert(mParent && mParent->GetType() == ComponentType::ListItem);
		ListItem* valueItem = (ListItem*)mParent;
		auto index = valueItem->GetRowIndex();
		auto uiEditor = gFBUIManager->GetUIEditor();
		IWinBase* editingUI = uiEditor->GetCurSelected();
		std::string key, value;
		propertyList->GetCurKeyValue(key, value);
		if (editingUI)
		{
			char buf[255] = { 0 };
			editingUI->GetProperty(UIProperty::ConverToEnum(key.c_str()), buf, false);
			SetText(AnsiToWide());
		}
	}
	mani->SetText(0);
	mani->RemoveListener(this);
}

void TextField::SetPasswd(bool passwd)
{
	mPasswd = passwd;
}

void TextField::OnCursorPosChanged(TextManipulator* mani)
{	
	TriggerRedraw();

	auto finalPos = GetFinalPos();
	int cursorPos = mani->GetCursorPos();	
	if (mani->IsHighlighting())
	{
		auto start = mani->GetHighlightStart();
		auto end = cursorPos;
		if (start > end)
		{
			std::swap(start, end);
		}
		gFBEnv->pRenderer->GetFont()->SetHeight(mTextSize);
			float width = gFBEnv->pRenderer->GetFont()->GetTextWidth(
				((const char*)mTextw.c_str()) + (start*2),
				(end-start) * 2);
			float leftGap = gFBEnv->pRenderer->GetFont()->GetTextWidth(
				(const char*)mTextw.c_str(), start * 2);
		gFBEnv->pRenderer->GetFont()->SetBackToOrigHeight();
		float xpos = ConvertToNormalized(Vec2I((int)leftGap, (int)leftGap)).x;

		Vec2 size = ConvertToNormalized(Vec2I((int)width, (int)mTextSize));
		KeyboardCursor::GetKeyboardCursor().SetNSize(size);

		KeyboardCursor::GetKeyboardCursor().SetNPos(
			Vec2(mUIObject->GetTextStarNPos().x + xpos, finalPos.y)
			);
	}
	else
	{
		gFBEnv->pRenderer->GetFont()->SetHeight(mTextSize);
			float aWidth = gFBEnv->pRenderer->GetFont()->GetTextWidth((const char*)AnsiToWide("A", 1), 2);
			Vec2 size = ConvertToNormalized(Vec2I((int)aWidth, 2));
			KeyboardCursor::GetKeyboardCursor().SetNSize(size);
			float width = gFBEnv->pRenderer->GetFont()->GetTextWidth(
				(const char*)mTextw.c_str(), cursorPos * 2);
		gFBEnv->pRenderer->GetFont()->SetBackToOrigHeight();
		float xpos = ConvertToNormalized(Vec2I((int)width, (int)width)).x;
		KeyboardCursor::GetKeyboardCursor().SetNPos(
			Vec2(mUIObject->GetTextStarNPos().x + xpos,
			finalPos.y + mWNSize.y - GetTextBottomGap() - (2.f / GetRenderTargetSize().y)));
	}
}
void TextField::OnTextChanged(TextManipulator* mani)
{
	TriggerRedraw();
	mUIObject->SetText(mTextw.c_str());
}

void TextField::SetUseBorder(bool use)
{
	if (use && mBorders.empty())
	{
		ImageBox* T = (ImageBox*)gFBEnv->pUIManager->CreateComponent(ComponentType::ImageBox);
		T->SetHwndId(GetHwndId());
		mBorders.push_back(T);
		T->SetRender3D(mRender3D, GetRenderTargetSize());
		T->SetManualParent(this);
		T->SetSizeY(1);
		T->SetTextureAtlasRegion("es/textures/ui.xml", "ThinBorder");


		ImageBox* L = (ImageBox*)gFBEnv->pUIManager->CreateComponent(ComponentType::ImageBox);
		L->SetHwndId(GetHwndId());
		mBorders.push_back(L);
		L->SetRender3D(mRender3D, GetRenderTargetSize());
		L->SetManualParent(this);
		L->SetSizeX(1);
		L->SetTextureAtlasRegion("es/textures/ui.xml", "ThinBorder");

		ImageBox* R = (ImageBox*)gFBEnv->pUIManager->CreateComponent(ComponentType::ImageBox);
		R->SetHwndId(GetHwndId());
		mBorders.push_back(R);
		R->SetRender3D(mRender3D, GetRenderTargetSize());
		R->SetManualParent(this);
		R->SetAlign(ALIGNH::RIGHT, ALIGNV::TOP);
		R->SetSizeX(1);
		R->SetTextureAtlasRegion("es/textures/ui.xml", "ThinBorder");


		ImageBox* B = (ImageBox*)gFBEnv->pUIManager->CreateComponent(ComponentType::ImageBox);
		B->SetHwndId(GetHwndId());
		mBorders.push_back(B);
		B->SetRender3D(mRender3D, GetRenderTargetSize());
		B->SetManualParent(this);
		B->SetAlign(ALIGNH::LEFT, ALIGNV::BOTTOM);
		B->SetSizeY(1);
		B->SetTextureAtlasRegion("es/textures/ui.xml", "ThinBorder");

		ImageBox* LT = (ImageBox*)gFBEnv->pUIManager->CreateComponent(ComponentType::ImageBox);
		LT->SetHwndId(GetHwndId());
		mBorders.push_back(LT);
		LT->SetRender3D(mRender3D, GetRenderTargetSize());
		LT->SetManualParent(this);
		LT->SetSize(Vec2I(1, 1));
		LT->SetTextureAtlasRegion("es/textures/ui.xml", "ThinBorder");

		ImageBox* RT = (ImageBox*)gFBEnv->pUIManager->CreateComponent(ComponentType::ImageBox);
		RT->SetHwndId(GetHwndId());
		mBorders.push_back(RT);
		RT->SetRender3D(mRender3D, GetRenderTargetSize());
		RT->SetManualParent(this);
		RT->SetSize(Vec2I(1, 1));
		RT->SetAlign(ALIGNH::RIGHT, ALIGNV::TOP);
		RT->SetTextureAtlasRegion("es/textures/ui.xml", "ThinBorder");

		ImageBox* LB = (ImageBox*)gFBEnv->pUIManager->CreateComponent(ComponentType::ImageBox);;
		LB->SetHwndId(GetHwndId());
		mBorders.push_back(LB);
		LB->SetRender3D(mRender3D, GetRenderTargetSize());
		LB->SetManualParent(this);
		LB->SetSize(Vec2I(1, 1));
		LB->SetAlign(ALIGNH::LEFT, ALIGNV::BOTTOM);
		LB->SetTextureAtlasRegion("es/textures/ui.xml", "ThinBorder");

		ImageBox* RB = (ImageBox*)gFBEnv->pUIManager->CreateComponent(ComponentType::ImageBox);
		RB->SetHwndId(GetHwndId());
		mBorders.push_back(RB);
		RB->SetRender3D(mRender3D, GetRenderTargetSize());
		RB->SetManualParent(this);
		RB->SetSize(Vec2I(1, 1));
		RB->SetAlign(ALIGNH::RIGHT, ALIGNV::BOTTOM);
		RB->SetTextureAtlasRegion("es/textures/ui.xml", "ThinBorder");

		RefreshBorder();
		RefreshScissorRects();
		gFBEnv->pUIManager->DirtyRenderList(GetHwndId());
	}
	else if (!use && !mBorders.empty())
	{
		for (auto ib : mBorders)
		{
			gFBEnv->pUIManager->DeleteComponent(ib);
		}
		mBorders.clear();
		gFBEnv->pUIManager->DirtyRenderList(GetHwndId());
	}
}

void TextField::OnPosChanged()
{
	__super::OnPosChanged();
	if (gFBUIManager->GetKeyboardFocusUI() == this)
	{
		OnCursorPosChanged(gFBUIManager->GetTextManipulator());
		KeyboardCursor::GetKeyboardCursor().SetScissorRegion(GetScissorRegion());
	}
}

PropertyList* TextField::IsInPropertyList() const
{
	if (mParent && mParent->GetType() == ComponentType::ListItem)
	{
		auto pp = mParent->GetParent();
		if (pp && pp->GetType() == ComponentType::PropertyList)
		{
			return (PropertyList*)pp;
		}
	}
	return 0;
}
void TextField::SelectAllAfterGetFocused()
{
	gFBUIManager->GetTextManipulator()->SelectAll();
	TriggerRedraw();
}
}