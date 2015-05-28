#pragma once
namespace fastbird{
	struct RegionTestParam{
		RegionTestParam()
			: mOnlyContainer(false)
			, mIgnoreScissor(false)
			, mTestChildren(false)
			, mHwndId(1)
		{}
		RegionTestParam(bool onlyContainer, bool ignoreScissor, bool testChildren, HWND_ID hwndId)
			: mOnlyContainer(onlyContainer)
			, mIgnoreScissor(ignoreScissor)
			, mTestChildren(testChildren)
			, mHwndId(hwndId)
		{}
		bool mOnlyContainer;
		bool mIgnoreScissor;
		bool mTestChildren;
		HWND_ID mHwndId;
	};
}